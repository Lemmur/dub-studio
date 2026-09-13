#include "dubstudio/importer.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dubstudio {
namespace {

using Json = nlohmann::ordered_json;

// Минималистичный RAII для sqlite3_stmt.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("prepare: ") + sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    sqlite3_stmt* get() const noexcept { return stmt_; }

    void bindText(int idx, const std::string& v) {
        if (sqlite3_bind_text(stmt_, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("bind_text failed");
    }
    void bindInt(int idx, std::int64_t v) {
        if (sqlite3_bind_int64(stmt_, idx, v) != SQLITE_OK)
            throw std::runtime_error("bind_int failed");
    }
    void bindDouble(int idx, double v) {
        if (sqlite3_bind_double(stmt_, idx, v) != SQLITE_OK)
            throw std::runtime_error("bind_double failed");
    }
    void step() {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
            throw std::runtime_error(std::string("step: ") + sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Не удалось открыть combined.json: " + path);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad()) throw std::runtime_error("Ошибка чтения combined.json: " + path);
    return data;
}

} // namespace

ImportStats Importer::importFile(const std::string& jsonPath) {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Парсинг с сохранением порядка ключей (порядок = порядок в сцене).
    Json root;
    try {
        root = Json::parse(readFile(jsonPath));
    } catch (const std::exception& e) {
        // nlohmann кидает свои типы (не runtime_error) — приводим к единому контракту.
        throw std::runtime_error(std::string("combined.json: ошибка парсинга: ") + e.what());
    }
    if (!root.is_object())
        throw std::runtime_error("combined.json: корень должен быть объектом {file_id: {...}}");

    sqlite3* db = db_.handle();

    Stmt insFile(db, "INSERT OR IGNORE INTO game_files(file_id, order_index) VALUES(?1, ?2)");
    Stmt insQuest(db, "INSERT OR IGNORE INTO quests(quest_id, file_id, order_index) VALUES(?1, ?2, ?3)");
    Stmt insLine(db,
        "INSERT OR IGNORE INTO lines("
        "file_id, quest_id, order_index, wem_hash,"
        "speaker_name, speaker_internal, speaker_confidence, ref_duration_ms)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
    Stmt insText(db,
        "INSERT OR REPLACE INTO texts(wem_hash, en_original, ru_base, ru_adapted)"
        " VALUES(?1,?2,?3,?4)");

    int inBatch = 0; // строк с момента BEGIN

    auto beginTx = [&] {
        db_.exec("BEGIN IMMEDIATE;");
        inBatch = 0;
    };
    auto commitTx = [&] {
        if (inBatch > 0) db_.exec("COMMIT;");
        inBatch = 0;
    };

    // Итераторы объекта ordered_json не поддерживают вычитание (offsets),
    // поэтому порядковые номера ведём вручную.
    std::int64_t fileOrder = 0;
    std::int64_t questOrder = 0;
    std::int64_t lineOrder = 0;

    beginTx();
    for (auto fileIt = root.begin(); fileIt != root.end(); ++fileIt, ++fileOrder) {
        const std::string fileId = fileIt.key();
        if (!fileIt.value().is_object())
            throw std::runtime_error("combined.json: уровень L2 у '" + fileId + "' должен быть объектом");

        insFile.bindText(1, fileId);
        insFile.bindInt(2, fileOrder);
        insFile.step();
        insFile.reset();

        const auto scenes = fileIt.value();
        questOrder = 0;
        for (auto questIt = scenes.begin(); questIt != scenes.end(); ++questIt, ++questOrder) {
            const std::string questId = questIt.key();
            if (!questIt.value().is_object())
                throw std::runtime_error("combined.json: уровень L3 у '" + questId + "' должен быть объектом");

            insQuest.bindText(1, questId);
            insQuest.bindText(2, fileId);
            insQuest.bindInt(3, questOrder);
            insQuest.step();
            insQuest.reset();

            const auto lines = questIt.value();
            lineOrder = 0;
            for (auto lineIt = lines.begin(); lineIt != lines.end(); ++lineIt, ++lineOrder) {
                const std::string guid = lineIt.key();
                const Json& r = lineIt.value();

                // dur (сек, float) -> ref_duration_ms
                double dur = 0.0;
                if (r.contains("dur") && r["dur"].is_number()) dur = r["dur"].get<double>();
                const auto durMs = static_cast<std::int64_t>(std::llround(dur * 1000.0));

                // Пустой speaker_name -> UNKNOWN + confidence 0.0 (PLAN.md п.7).
                // Суффиксы вида "Ambrus (whisper)" храним целиком (PLAN.md п.8).
                std::string speaker = "UNKNOWN";
                if (r.contains("speaker_name") && r["speaker_name"].is_string()) {
                    speaker = r["speaker_name"].get<std::string>();
                    if (speaker.empty()) speaker = "UNKNOWN";
                }
                const double confidence = (speaker == "UNKNOWN") ? 0.0 : 1.0;

                std::string speakerInternal;
                if (r.contains("speaker_internal") && r["speaker_internal"].is_string())
                    speakerInternal = r["speaker_internal"].get<std::string>();

                std::string en, ru;
                if (r.contains("en") && r["en"].is_string()) en = r["en"].get<std::string>();
                if (r.contains("ru") && r["ru"].is_string()) ru = r["ru"].get<std::string>();

                if (++inBatch > kBatchSize) commitTx(), beginTx();

                insLine.bindText(1, fileId);
                insLine.bindText(2, questId);
                insLine.bindInt(3, lineOrder);
                insLine.bindText(4, guid);
                insLine.bindText(5, speaker);
                insLine.bindText(6, speakerInternal);
                insLine.bindDouble(7, confidence);
                insLine.bindInt(8, durMs);
                insLine.step();
                insLine.reset();

                insText.bindText(1, guid);
                insText.bindText(2, en);
                insText.bindText(3, ru); // ru_base
                insText.bindText(4, ru); // ru_adapted (PLAN.md 5.2)
                insText.step();
                insText.reset();
            }
        }
    }
    commitTx();

    // 2. Пересборка FTS: всегда консистентен, повторный импорт не дублирует.
    //    ru-колонка индексируется по текущему тексту: ru_final, иначе ru_adapted.
    db_.exec("DELETE FROM lines_fts;");
    db_.exec(
        "INSERT INTO lines_fts(wem_hash, speaker_name, en_original, ru_final)"
        " SELECT l.wem_hash, l.speaker_name, t.en_original,"
        " COALESCE(NULLIF(t.ru_final,''), t.ru_adapted)"
        " FROM lines l JOIN texts t ON t.wem_hash = l.wem_hash;");

    ImportStats stats;
    stats.files = db_.scalarInt("SELECT COUNT(*) FROM game_files;");
    stats.quests = db_.scalarInt("SELECT COUNT(*) FROM quests;");
    stats.lines = db_.scalarInt("SELECT COUNT(*) FROM lines;");
    stats.ftsRows = db_.scalarInt("SELECT COUNT(*) FROM lines_fts;");

    const auto t1 = std::chrono::steady_clock::now();
    stats.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return stats;
}

} // namespace dubstudio
