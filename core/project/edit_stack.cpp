// Фаза 2: EditStack — команды правок + undo/redo через undo_log (PLAN.md 6.5).
// Снапшоты сэмплов: WAV PCM 24-bit в <myDub>/undo/<seq><тег>.wav.
#include "dubstudio/edit_stack.h"

#include "dubstudio/audio_engine.h" // rmsDb/peakDb
#include "dubstudio/audio_ops.h"
#include "dubstudio/wav_writer.h"

#include <nlohmann/json.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace dubstudio {
namespace {

using Json = nlohmann::json;

Json clipToJson(const Clip& c, const std::string& wav) {
    Json j;
    j["id"] = c.id;
    j["title"] = c.title;
    j["wem"] = c.wemHash;
    j["sr"] = c.sampleRate;
    j["color"] = c.colorRgb;
    j["rms"] = c.rmsDb;
    j["peak"] = c.peakDb;
    j["start"] = c.startSample;
    j["gainDb"] = c.gainDb;
    j["fadeIn"] = c.fadeInSamples;
    j["fadeOut"] = c.fadeOutSamples;
    j["len"] = c.samples.size();
    j["wav"] = wav;
    return j;
}

Clip clipFromJson(const Json& j) {
    Clip c;
    c.id = j.value("id", std::string());
    c.title = j.value("title", std::string());
    c.wemHash = j.value("wem", std::string());
    c.sampleRate = j.value("sr", std::uint64_t{48000});
    c.colorRgb = j.value("color", std::uint32_t{0x4f7cb0});
    c.rmsDb = j.value("rms", -99.0);
    c.peakDb = j.value("peak", -99.0);
    c.startSample = j.value("start", std::uint64_t{0});
    c.gainDb = j.value("gainDb", 0.0);
    c.fadeInSamples = j.value("fadeIn", std::uint64_t{0});
    c.fadeOutSamples = j.value("fadeOut", std::uint64_t{0});
    return c;
}

void clampFades(Clip& c) {
    const std::uint64_t n = c.samples.size();
    c.fadeInSamples = std::min<std::uint64_t>(c.fadeInSamples, n);
    c.fadeOutSamples = std::min<std::uint64_t>(c.fadeOutSamples, n);
}

// "take_7_AB12CD34" -> "TAKE-07", "take_7_AB12CD34.1" -> "TAKE-07.1"
std::string titleFromId(const std::string& id) {
    if (!id.starts_with("take_")) return id;
    const std::string rest = id.substr(5);
    const std::size_t us = rest.find('_');
    if (us == std::string::npos || us == 0) return id;
    std::string num = rest.substr(0, us);
    const std::string tail = rest.substr(us + 1);
    const std::size_t dot = tail.find('.');
    const std::string suffix = dot == std::string::npos ? std::string() : tail.substr(dot);
    if (num.size() > 6 || num.empty() ||
        !std::all_of(num.begin(), num.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        return id;
    while (num.size() < 2) num.insert(num.begin(), '0');
    return "TAKE-" + num + suffix;
}

int takeNumFromId(const std::string& id) {
    if (!id.starts_with("take_")) return 0;
    const std::string rest = id.substr(5);
    std::string num;
    for (char ch : rest) {
        if (ch >= '0' && ch <= '9') {
            num += ch;
        } else {
            break;
        }
    }
    return num.empty() ? 0 : std::atoi(num.c_str());
}

std::string joinPath(const std::string& base, const std::string& name) {
    return (fs::path(base) / name).generic_string();
}

// Загрузить сэмплы клипа из WAV (снапшота/оригинала). Кидаёт при неудаче.
void loadSamples(const std::string& wav, Clip& c) {
    std::uint32_t sr = static_cast<std::uint32_t>(c.sampleRate);
    std::vector<float> s;
    if (!WavWriter::readMono(wav, s, sr) || s.empty())
        throw std::runtime_error("Не удалось прочитать WAV: " + wav);
    c.samples = std::move(s);
    c.sampleRate = sr;
}

} // namespace

const char* EditStack::typeName(EditType t) {
    switch (t) {
    case EditType::TrimSilence: return "Трим тишины";
    case EditType::TrimRange: return "Обрезка диапазона";
    case EditType::Split: return "Разделение";
    case EditType::Move: return "Перемещение";
    case EditType::Align: return "Выравнивание";
    case EditType::Gain: return "Усиление";
    case EditType::Normalize: return "Нормализация";
    case EditType::Silence: return "Тишина";
    case EditType::Reverse: return "Реверс";
    case EditType::FadeIn: return "Фейд-ин";
    case EditType::FadeOut: return "Фейд-аут";
    case EditType::Crossfade: return "Кроссфейд";
    case EditType::FitToRef: return "Fit to Ref";
    }
    return "?";
}

EditStack::EditStack(Database& db, ClipStore& store, std::string myDubDir)
    : db_(db), store_(store), myDubDir_(std::move(myDubDir)) {}

// --- Вспомогательные SQL ------------------------------------------------------

namespace {

// scalar по параметру: SELECT ... WHERE take_id=?1
std::string scalarText1(sqlite3* h, const std::string& sql, const std::string& p1) {
    sqlite3_stmt* st = nullptr;
    std::string out;
    if (sqlite3_prepare_v2(h, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
        if (!p1.empty()) sqlite3_bind_text(st, 1, p1.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(st, 0);
            if (t) out.assign(reinterpret_cast<const char*>(t));
        }
        sqlite3_finalize(st);
    }
    return out;
}

} // namespace

// Собственные wav-файлы шагов redo-ветки (удаляются после COMMIT).
// Удаляем ТОЛЬКО файлы вида <seq>_<тег>.wav с seq самого удаляемого шага:
// property-команды наследуют wav предыдущих шагов/оригинал записи —
// такие пути трогать нельзя (иначе сломаем живые шаги и исходники).
std::vector<fs::path> collectRedoFiles(sqlite3* h) {
    std::vector<fs::path> files;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h,
                           "SELECT seq, state_json FROM undo_log"
                           " WHERE scope='edit' AND undone=1;",
                           -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const sqlite3_int64 seq = sqlite3_column_int64(st, 0);
            const unsigned char* t = sqlite3_column_text(st, 1);
            if (!t) continue;
            try {
                const Json j = Json::parse(reinterpret_cast<const char*>(t));
                const std::string prefix = std::to_string(seq) + "_";
                for (const char* key : {"clip", "created", "removed"}) {
                    if (!j.contains(key)) continue;
                    const auto& wav = j[key].value("wav", std::string());
                    if (wav.empty()) continue;
                    const fs::path p(wav);
                    if (p.parent_path().filename() == "undo" &&
                        p.filename().string().rfind(prefix, 0) == 0)
                        files.emplace_back(p);
                }
            } catch (...) { /* битый json ветки — файлы не трогаем */ }
        }
        sqlite3_finalize(st);
    }
    return files;
}

void EditStack::dropRedoBranch() {
    db_.exec("DELETE FROM undo_log WHERE scope='edit' AND undone=1;");
}

std::int64_t EditStack::apply(const EditCommand& cmd) {
    const int idx = store_.takeIndexById(cmd.takeId);
    if (idx < 0) throw std::runtime_error("Тейк не найден: " + cmd.takeId);
    const Clip before = store_.takes()[static_cast<std::size_t>(idx)];

    Clip after = before;
    Clip created, removed;
    bool hasCreated = false, hasRemoved = false;
    bool samplesChanged = true; // property-команды не перезаписывают WAV
    std::string action;

    const std::size_t n = after.samples.size();
    auto rangeIn = [&](std::uint64_t from, std::uint64_t to, std::size_t& f,
                       std::size_t& t) {
        f = static_cast<std::size_t>(std::min<std::uint64_t>(from, n));
        t = static_cast<std::size_t>(std::min<std::uint64_t>(to == 0 ? n : to, n));
        if (t < f) std::swap(f, t);
    };

    switch (cmd.type) {
    case EditType::TrimSilence: {
        action = "edit.trim_silence";
        const SoundRange r = findSoundRange(after.samples);
        if (r.empty)
            throw std::runtime_error("Клип тише порога -50 dBFS: обрезать нечего");
        after.samples.assign(after.samples.begin() + static_cast<std::ptrdiff_t>(r.first),
                             after.samples.begin() + static_cast<std::ptrdiff_t>(r.last));
        after.startSample += r.first;
        clampFades(after);
        break;
    }
    case EditType::TrimRange: {
        action = "edit.trim_range";
        std::size_t f, t;
        rangeIn(cmd.from, cmd.to, f, t);
        if (t - f < 2) throw std::runtime_error("Неверный диапазон обрезки");
        after.samples.assign(after.samples.begin() + static_cast<std::ptrdiff_t>(f),
                             after.samples.begin() + static_cast<std::ptrdiff_t>(t));
        after.startSample += f;
        clampFades(after);
        break;
    }
    case EditType::Split: {
        action = "edit.split";
        if (n < 4) throw std::runtime_error("Клип слишком короткий для разделения");
        std::uint64_t pos = snapToZeroCrossing(
            after.samples, std::clamp<std::uint64_t>(cmd.from, 1, n - 1));
        if (pos == 0 || pos >= n) pos = std::clamp<std::uint64_t>(cmd.from, 1, n - 1);
        // уникальный id правого куска: <id>.k
        int k = 1;
        std::string rightId = after.id + ".1";
        while (store_.takeIndexById(rightId) >= 0) rightId = after.id + '.' + std::to_string(++k);
        created = after;
        created.id = rightId;
        created.title = titleFromId(rightId);
        created.samples.assign(after.samples.begin() + static_cast<std::ptrdiff_t>(pos),
                               after.samples.end());
        created.startSample = after.startSample + pos;
        created.fadeInSamples = 0;
        clampFades(created);
        created.rmsDb = rmsDb(created.samples);
        created.peakDb = peakDb(created.samples);
        after.samples.resize(static_cast<std::size_t>(pos));
        after.fadeOutSamples = 0;
        hasCreated = true;
        break;
    }
    case EditType::Move:
        action = "edit.move";
        after.startSample = cmd.uValue;
        samplesChanged = false;
        break;
    case EditType::Align:
        action = "edit.align";
        after.startSample = 0; // начало референса (Track 0 стартует с 0)
        samplesChanged = false;
        break;
    case EditType::Gain:
        action = "edit.gain";
        after.gainDb = std::clamp(cmd.dValue, -60.0, 24.0);
        samplesChanged = false;
        break;
    case EditType::Normalize: {
        action = "edit.normalize";
        normalizePeakToDb(after.samples, std::clamp(cmd.dValue, -24.0, 0.0));
        break;
    }
    case EditType::Silence: {
        action = "edit.silence";
        std::size_t f, t;
        rangeIn(cmd.from, cmd.to, f, t);
        silenceRange(after.samples, f, t);
        break;
    }
    case EditType::Reverse: {
        action = "edit.reverse";
        std::size_t f, t;
        rangeIn(cmd.from, cmd.to, f, t);
        reverseSamples(after.samples, f, t);
        break;
    }
    case EditType::FadeIn:
        action = "edit.fade_in";
        after.fadeInSamples = std::min<std::uint64_t>(cmd.from, n);
        samplesChanged = false;
        break;
    case EditType::FadeOut:
        action = "edit.fade_out";
        after.fadeOutSamples =
            n > std::min<std::uint64_t>(cmd.from, n) ? n - std::min<std::uint64_t>(cmd.from, n) : 0;
        samplesChanged = false;
        break;
    case EditType::Crossfade: {
        action = "edit.crossfade";
        const int idx2 = store_.takeIndexById(cmd.takeId2);
        if (idx2 < 0) throw std::runtime_error("Второй тейк не найден: " + cmd.takeId2);
        const Clip& b = store_.takes()[static_cast<std::size_t>(idx2)];
        const MergedClip m = crossfadeMerge(after, b);
        if (m.samples.empty()) throw std::runtime_error("Нечего сливать: оба клипа пусты");
        removed = b;
        hasRemoved = true;
        after.samples = m.samples;
        after.startSample = m.startSample;
        after.fadeInSamples = 0;
        after.fadeOutSamples = 0; // кроссфейд сам управляет амплитудой
        break;
    }
    case EditType::FitToRef: {
        action = "edit.fit_ref";
        sqlite3* h = db_.handle();
        sqlite3_int64 refMs = 0;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h, "SELECT ref_duration_ms FROM lines WHERE wem_hash=?1;", -1,
                               &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, after.wemHash.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) refMs = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
        }
        if (refMs <= 0)
            throw std::runtime_error("У реплики нет длительности референса (ref_duration_ms)");
        if (n == 0) throw std::runtime_error("Клип пуст");
        const double target =
            static_cast<double>(refMs) * static_cast<double>(after.sampleRate) / 1000.0;
        after.samples = timeStretchWsola(after.samples, target / static_cast<double>(n),
                                         static_cast<std::uint32_t>(after.sampleRate));
        clampFades(after);
        break;
    }
    }

    if (samplesChanged) {
        after.rmsDb = rmsDb(after.samples);
        after.peakDb = peakDb(after.samples);
    }
    clampFades(after);

    // --- Снапшоты и транзакция --------------------------------------------------
    std::error_code ec;
    const fs::path undoDir = fs::path(myDubDir_) / "undo";
    fs::create_directories(undoDir, ec);

    // wav для property-команд наследуем из последнего состояния тейка.
    std::string wavA;
    if (!samplesChanged) {
        const std::string prev = scalarText1(
            db_.handle(),
            "SELECT state_json FROM undo_log"
            " WHERE scope='edit' AND take_id=?1 ORDER BY seq DESC LIMIT 1;",
            cmd.takeId);
        if (!prev.empty()) {
            try {
                wavA = Json::parse(prev)["clip"].value("wav", std::string());
            } catch (...) {}
        }
        if (wavA.empty()) wavA = before.filePath; // оригинал записи
    }

    const std::vector<fs::path> redoFiles = collectRedoFiles(db_.handle());

    db_.exec("BEGIN");
    try {
        dropRedoBranch();
        // Вставляем шаг без state_json, чтобы получить seq для имён файлов.
        sqlite3_int64 seqReal = 0;
        {
            sqlite3* h = db_.handle();
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "INSERT INTO undo_log(action, scope, take_id)"
                                   " VALUES(?1,'edit',?2);",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            sqlite3_bind_text(st, 1, action.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, cmd.takeId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            }
            sqlite3_finalize(st);
            seqReal = sqlite3_last_insert_rowid(h);
        }

        const std::string seqStr = std::to_string(seqReal);
        if (samplesChanged) {
            wavA = joinPath(undoDir.generic_string(), seqStr + "_a.wav");
            WavWriter::writePcm24(wavA, after.samples,
                                  static_cast<std::uint32_t>(after.sampleRate));
        }
        std::string wavB, wavR;
        if (hasCreated) {
            wavB = joinPath(undoDir.generic_string(), seqStr + "_b.wav");
            WavWriter::writePcm24(wavB, created.samples,
                                  static_cast<std::uint32_t>(created.sampleRate));
        }
        if (hasRemoved) {
            wavR = joinPath(undoDir.generic_string(), seqStr + "_r.wav");
            WavWriter::writePcm24(wavR, removed.samples,
                                  static_cast<std::uint32_t>(removed.sampleRate));
        }

        Json state;
        state["clip"] = clipToJson(after, wavA);
        if (hasCreated) state["created"] = clipToJson(created, wavB);
        if (hasRemoved) state["removed"] = clipToJson(removed, wavR);
        {
            sqlite3* h = db_.handle();
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "UPDATE undo_log SET state_json=?1 WHERE seq=?2;", -1,
                                   &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            const std::string dump = state.dump();
            sqlite3_bind_text(st, 1, dump.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, seqReal);
            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            }
            sqlite3_finalize(st);
        }

        // Строка takes основного клипа.
        {
            sqlite3* h = db_.handle();
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "UPDATE takes SET duration_ms=?1, rms_db=?2, peak_db=?3"
                                   " WHERE take_id=?4;",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            const auto durMs = static_cast<int>(after.samples.size() * 1000.0 /
                                                static_cast<double>(after.sampleRate));
            sqlite3_bind_int(st, 1, durMs);
            sqlite3_bind_double(st, 2, after.rmsDb);
            sqlite3_bind_double(st, 3, after.peakDb);
            sqlite3_bind_text(st, 4, cmd.takeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (hasCreated) {
            sqlite3* h = db_.handle();
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "INSERT INTO takes(take_id, wem_hash, file_cas, duration_ms,"
                                   " quality, rms_db, peak_db, is_master_candidate, comment)"
                                   " VALUES(?1,?2,?3,?4,?5,?6,?7,0,?8);",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            const auto durMs = static_cast<int>(created.samples.size() * 1000.0 /
                                                static_cast<double>(created.sampleRate));
            const char* quality =
                created.peakDb > -1.0 || created.rmsDb < -50.0 ? "red" : "green";
            sqlite3_bind_text(st, 1, created.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, created.wemHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, wavB.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 4, durMs);
            sqlite3_bind_text(st, 5, quality, -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(st, 6, created.rmsDb);
            sqlite3_bind_double(st, 7, created.peakDb);
            sqlite3_bind_text(st, 8, created.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (hasRemoved) {
            sqlite3* h = db_.handle();
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "DELETE FROM takes WHERE take_id=?1;", -1, &st,
                                   nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("apply: ") + sqlite3_errmsg(h));
            sqlite3_bind_text(st, 1, removed.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        db_.exec("COMMIT");

        // Мусор redo-ветки (после успешного коммита).
        for (const auto& f : redoFiles) {
            std::error_code del;
            fs::remove(f, del);
        }

        // Мутации in-memory (только после COMMIT).
        store_.takes()[static_cast<std::size_t>(idx)] = after;
        ClipStore::rebuildPeaks(store_.takes()[static_cast<std::size_t>(idx)]);
        if (hasCreated) store_.addTake(created);
        if (hasRemoved) store_.removeTakeById(removed.id);
        return seqReal;
    } catch (...) {
        try { db_.exec("ROLLBACK"); } catch (...) {}
        throw;
    }
}

bool EditStack::canUndo() const {
    return db_.scalarInt(
               "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=0;") > 0;
}

bool EditStack::canRedo() const {
    return db_.scalarInt(
               "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=1;") > 0;
}

std::string EditStack::nextUndoAction() const {
    return scalarText1(db_.handle(),
                       "SELECT action FROM undo_log WHERE scope='edit' AND undone=0"
                       " ORDER BY seq DESC LIMIT 1;",
                       "");
}

std::string EditStack::nextRedoAction() const {
    return scalarText1(db_.handle(),
                       "SELECT action FROM undo_log WHERE scope='edit' AND undone=1"
                       " ORDER BY seq ASC LIMIT 1;",
                       "");
}

bool EditStack::undo() {
    sqlite3* h = db_.handle();
    // Последний применённый шаг.
    struct Step {
        sqlite3_int64 seq = 0;
        std::string takeId;
        std::string stateJson;
    };
    Step step;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h,
                               "SELECT seq, take_id, state_json FROM undo_log"
                               " WHERE scope='edit' AND undone=0 ORDER BY seq DESC LIMIT 1;",
                               -1, &st, nullptr) != SQLITE_OK)
            return false;
        if (sqlite3_step(st) == SQLITE_ROW) {
            step.seq = sqlite3_column_int64(st, 0);
            const unsigned char* t1 = sqlite3_column_text(st, 1);
            const unsigned char* t2 = sqlite3_column_text(st, 2);
            if (t1) step.takeId.assign(reinterpret_cast<const char*>(t1));
            if (t2) step.stateJson.assign(reinterpret_cast<const char*>(t2));
        }
        sqlite3_finalize(st);
    }
    if (step.seq == 0 || step.stateJson.empty()) return false;

    Json state;
    try {
        state = Json::parse(step.stateJson);
    } catch (...) {
        return false;
    }

    // Предыдущее состояние этого тейка (или оригинал записи).
    Clip restored;
    std::string restoredWav;
    const std::string prev = scalarText1(
        h, "SELECT state_json FROM undo_log"
           " WHERE scope='edit' AND take_id=?1 AND undone=0 AND seq<?2"
           " ORDER BY seq DESC LIMIT 1;",
        step.takeId);
    bool havePrev = false;
    if (!prev.empty()) {
        try {
            const Json pj = Json::parse(prev);
            restored = clipFromJson(pj["clip"]);
            restoredWav = pj["clip"].value("wav", std::string());
            havePrev = true;
        } catch (...) {}
    }
    const int curIdx = store_.takeIndexById(step.takeId);
    if (!havePrev) {
        // Оригинал: строка takes + WAV записи.
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h,
                               "SELECT wem_hash, file_cas FROM takes WHERE take_id=?1;",
                               -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, step.takeId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* w = sqlite3_column_text(st, 0);
                const unsigned char* f = sqlite3_column_text(st, 1);
                if (w) restored.wemHash.assign(reinterpret_cast<const char*>(w));
                if (f) restoredWav.assign(reinterpret_cast<const char*>(f));
            }
            sqlite3_finalize(st);
        }
        restored.id = step.takeId;
        restored.title = titleFromId(step.takeId);
        // Цвет команды не меняют — берём текущий.
        if (curIdx >= 0) restored.colorRgb = store_.takes()[static_cast<std::size_t>(curIdx)].colorRgb;
        restored.startSample = 0;
        restored.gainDb = 0.0;
        restored.fadeInSamples = restored.fadeOutSamples = 0;
    }
    if (restoredWav.empty()) return false;
    loadSamples(restoredWav, restored);
    restored.rmsDb = rmsDb(restored.samples);
    restored.peakDb = peakDb(restored.samples);

    // created удалить, removed восстановить (данные в state).
    Json created = state.contains("created") ? state["created"] : Json();
    Json removed = state.contains("removed") ? state["removed"] : Json();

    db_.exec("BEGIN");
    try {
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "UPDATE undo_log SET undone=1 WHERE seq=?1;", -1, &st,
                                   nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("undo: ") + sqlite3_errmsg(h));
            sqlite3_bind_int64(st, 1, step.seq);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (!created.is_null()) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "DELETE FROM takes WHERE take_id=?1;", -1, &st,
                                   nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("undo: ") + sqlite3_errmsg(h));
            const std::string cid = created.value("id", std::string());
            sqlite3_bind_text(st, 1, cid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (!removed.is_null()) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "INSERT OR REPLACE INTO takes(take_id, wem_hash, file_cas,"
                                   " duration_ms, quality, rms_db, peak_db,"
                                   " is_master_candidate, comment)"
                                   " VALUES(?1,?2,?3,?4,'yellow',?5,?6,0,?7);",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("undo: ") + sqlite3_errmsg(h));
            const std::string rid = removed.value("id", std::string());
            const std::string rwav = removed.value("wav", std::string());
            const std::string rwem = removed.value("wem", std::string());
            const std::string rtitle = removed.value("title", std::string());
            const std::uint64_t rlen = removed.value("len", std::uint64_t{0});
            const std::uint64_t rsr = removed.value("sr", std::uint64_t{48000});
            sqlite3_bind_text(st, 1, rid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, rwem.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, rwav.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 4, static_cast<int>(rlen * 1000.0 / static_cast<double>(rsr)));
            sqlite3_bind_double(st, 5, removed.value("rms", -99.0));
            sqlite3_bind_double(st, 6, removed.value("peak", -99.0));
            sqlite3_bind_text(st, 7, rtitle.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        // Основной тейк -> предыдущие метрики.
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "UPDATE takes SET duration_ms=?1, rms_db=?2, peak_db=?3"
                                   " WHERE take_id=?4;",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("undo: ") + sqlite3_errmsg(h));
            const auto durMs = static_cast<int>(restored.samples.size() * 1000.0 /
                                                static_cast<double>(restored.sampleRate));
            sqlite3_bind_int(st, 1, durMs);
            sqlite3_bind_double(st, 2, restored.rmsDb);
            sqlite3_bind_double(st, 3, restored.peakDb);
            sqlite3_bind_text(st, 4, step.takeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        db_.exec("COMMIT");
    } catch (...) {
        try { db_.exec("ROLLBACK"); } catch (...) {}
        throw;
    }

    // In-memory.
    if (curIdx >= 0) {
        store_.takes()[static_cast<std::size_t>(curIdx)] = restored;
        ClipStore::rebuildPeaks(store_.takes()[static_cast<std::size_t>(curIdx)]);
    }
    if (!created.is_null()) store_.removeTakeById(created.value("id", std::string()));
    if (!removed.is_null()) {
        Clip rc = clipFromJson(removed);
        loadSamples(removed.value("wav", std::string()), rc);
        store_.addTake(std::move(rc));
    }
    return true;
}

bool EditStack::redo() {
    sqlite3* h = db_.handle();
    sqlite3_int64 seq = 0;
    std::string takeId, stateJson;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h,
                               "SELECT seq, take_id, state_json FROM undo_log"
                               " WHERE scope='edit' AND undone=1 ORDER BY seq ASC LIMIT 1;",
                               -1, &st, nullptr) != SQLITE_OK)
            return false;
        if (sqlite3_step(st) == SQLITE_ROW) {
            seq = sqlite3_column_int64(st, 0);
            const unsigned char* t1 = sqlite3_column_text(st, 1);
            const unsigned char* t2 = sqlite3_column_text(st, 2);
            if (t1) takeId.assign(reinterpret_cast<const char*>(t1));
            if (t2) stateJson.assign(reinterpret_cast<const char*>(t2));
        }
        sqlite3_finalize(st);
    }
    if (seq == 0 || stateJson.empty()) return false;

    Json state;
    try {
        state = Json::parse(stateJson);
    } catch (...) {
        return false;
    }

    Clip after = clipFromJson(state["clip"]);
    loadSamples(state["clip"].value("wav", std::string()), after);
    after.rmsDb = rmsDb(after.samples);
    after.peakDb = peakDb(after.samples);
    Json created = state.contains("created") ? state["created"] : Json();
    Json removed = state.contains("removed") ? state["removed"] : Json();

    db_.exec("BEGIN");
    try {
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "UPDATE undo_log SET undone=0 WHERE seq=?1;", -1, &st,
                                   nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("redo: ") + sqlite3_errmsg(h));
            sqlite3_bind_int64(st, 1, seq);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (!created.is_null()) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "INSERT OR REPLACE INTO takes(take_id, wem_hash, file_cas,"
                                   " duration_ms, quality, rms_db, peak_db,"
                                   " is_master_candidate, comment)"
                                   " VALUES(?1,?2,?3,?4,'yellow',?5,?6,0,?7);",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("redo: ") + sqlite3_errmsg(h));
            const std::string cid = created.value("id", std::string());
            const std::uint64_t clen = created.value("len", std::uint64_t{0});
            const std::uint64_t csr = created.value("sr", std::uint64_t{48000});
            sqlite3_bind_text(st, 1, cid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, created.value("wem", std::string()).c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, created.value("wav", std::string()).c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 4, static_cast<int>(clen * 1000.0 / static_cast<double>(csr)));
            sqlite3_bind_double(st, 5, created.value("rms", -99.0));
            sqlite3_bind_double(st, 6, created.value("peak", -99.0));
            sqlite3_bind_text(st, 7, created.value("title", std::string()).c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (!removed.is_null()) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, "DELETE FROM takes WHERE take_id=?1;", -1, &st,
                                   nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("redo: ") + sqlite3_errmsg(h));
            const std::string rid = removed.value("id", std::string());
            sqlite3_bind_text(st, 1, rid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h,
                                   "UPDATE takes SET duration_ms=?1, rms_db=?2, peak_db=?3"
                                   " WHERE take_id=?4;",
                                   -1, &st, nullptr) != SQLITE_OK)
                throw std::runtime_error(std::string("redo: ") + sqlite3_errmsg(h));
            const auto durMs = static_cast<int>(after.samples.size() * 1000.0 /
                                                static_cast<double>(after.sampleRate));
            sqlite3_bind_int(st, 1, durMs);
            sqlite3_bind_double(st, 2, after.rmsDb);
            sqlite3_bind_double(st, 3, after.peakDb);
            sqlite3_bind_text(st, 4, takeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        db_.exec("COMMIT");
    } catch (...) {
        try { db_.exec("ROLLBACK"); } catch (...) {}
        throw;
    }

    // In-memory.
    const int idx = store_.takeIndexById(takeId);
    if (idx >= 0) {
        store_.takes()[static_cast<std::size_t>(idx)] = after;
        ClipStore::rebuildPeaks(store_.takes()[static_cast<std::size_t>(idx)]);
    } else {
        store_.addTake(after); // тейк был удалён иначе — восстанавливаем
    }
    if (!created.is_null()) {
        Clip cc = clipFromJson(created);
        loadSamples(created.value("wav", std::string()), cc);
        store_.addTake(std::move(cc));
    }
    if (!removed.is_null()) store_.removeTakeById(removed.value("id", std::string()));
    return true;
}

EditStack::SessionInfo EditStack::loadSession() {
    SessionInfo info;
    store_.takes().clear();
    sqlite3* h = db_.handle();

    struct Row {
        std::string id, wem, file, comment;
    };
    std::vector<Row> rows;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h,
                           "SELECT take_id, wem_hash, file_cas, comment"
                           " FROM takes ORDER BY rowid;",
                           -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Row r;
            const unsigned char* c[4] = {sqlite3_column_text(st, 0), sqlite3_column_text(st, 1),
                                         sqlite3_column_text(st, 2), sqlite3_column_text(st, 3)};
            if (c[0]) r.id.assign(reinterpret_cast<const char*>(c[0]));
            if (c[1]) r.wem.assign(reinterpret_cast<const char*>(c[1]));
            if (c[2]) r.file.assign(reinterpret_cast<const char*>(c[2]));
            if (c[3]) r.comment.assign(reinterpret_cast<const char*>(c[3]));
            rows.push_back(std::move(r));
        }
        sqlite3_finalize(st);
    }

    int loaded = 0;
    for (const auto& r : rows) {
        // Номер тейка считаем по СТРОКЕ БД, а не по загруженному клипу:
        // если WAV недоступен (MyDub удалён/другая папка), клип пропускаем,
        // но счётчик обязан учесть его номер — иначе новый тейк сгенерирует
        // коллизию take_id (UNIQUE constraint при записи).
        info.maxTakeNum = std::max(info.maxTakeNum, takeNumFromId(r.id));
        const std::string stateJson = scalarText1(
            h, "SELECT state_json FROM undo_log"
               " WHERE scope='edit' AND take_id=?1 AND undone=0"
               " ORDER BY seq DESC LIMIT 1;",
            r.id);
        Clip c;
        std::string wav;
        if (!stateJson.empty()) {
            try {
                const Json j = Json::parse(stateJson);
                c = clipFromJson(j["clip"]);
                wav = j["clip"].value("wav", std::string());
            } catch (...) { continue; }
        } else {
            c.id = r.id;
            c.wemHash = r.wem;
            c.filePath = r.file;
            wav = r.file;
            c.title = r.comment.rfind("TAKE-", 0) == 0 ? r.comment : titleFromId(r.id);
            c.colorRgb = ClipStore::autoColor(loaded);
        }
        try {
            loadSamples(wav, c);
        } catch (...) {
            continue; // WAV недоступен — тейк пропускаем, сессия не падает
        }
        if (stateJson.empty()) {
            c.rmsDb = rmsDb(c.samples);
            c.peakDb = peakDb(c.samples);
        }
        ClipStore::rebuildPeaks(c);
        store_.takes().push_back(std::move(c));
        ++loaded;
    }
    info.takes = loaded;
    return info;
}

std::int64_t EditStack::autosaveSnapshot() {
    std::error_code ec;
    fs::create_directories(myDubDir_, ec);

    // Быстрый снапшот: содержимое команд уже в БД, добиваем WAL в основной файл.
    db_.exec("PRAGMA wal_checkpoint(TRUNCATE);");

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    Json m;
    m["saved_at"] = now;
    m["takes"] = db_.scalarInt("SELECT COUNT(*) FROM takes;");
    m["edits"] = db_.scalarInt(
        "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=0;");
    m["redo_available"] = db_.scalarInt(
        "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=1;");

    const fs::path manifest = fs::path(myDubDir_) / "manifest.json";
    const fs::path tmp = fs::path(myDubDir_) / "manifest.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("Автосейв: не открыть " + tmp.string());
        f << m.dump(2);
        f.close();
        if (!f.good()) throw std::runtime_error("Автосейв: ошибка записи manifest.json");
    }
    std::error_code ren;
    fs::rename(tmp, manifest, ren);
    if (ren) throw std::runtime_error("Автосейв: не переименовать manifest.json");
    return now;
}

} // namespace dubstudio
