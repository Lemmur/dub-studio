// Тесты Фазы 2 (редактура): DSP-операции, EditStack (apply/undo/redo),
// готовность фазы — «откат до старта после рестарта» (PLAN.md 14):
// новый EditStack на той же БД = симуляция перезапуска приложения.
#include "catch_amalgamated.hpp"
#include "test_helpers.h"

#include "dubstudio/audio_engine.h"
#include "dubstudio/audio_ops.h"
#include "dubstudio/clip_store.h"
#include "dubstudio/database.h"
#include "dubstudio/edit_stack.h"
#include "dubstudio/wav_writer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Catch::Approx;
using dubstudio::Clip;
using dubstudio::ClipStore;
using dubstudio::Database;
using dubstudio::EditCommand;
using dubstudio::EditStack;
using dubstudio::EditType;
using dubstudio::WavWriter;

namespace {

int countZeroCrossings(const std::vector<float>& s) {
    int z = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if ((s[i - 1] < 0.0f && s[i] >= 0.0f) || (s[i - 1] >= 0.0f && s[i] < 0.0f)) ++z;
    }
    return z;
}

// Мини-проект: БД + MyDub + один «записанный» тейк (как после Фазы 1).
// Сигнал: синус 440 Гц амплитуды 0.5 с тишиной по 100 мс по краям
// (чтобы был что тримать), ref_duration_ms реплики = 1500.
struct TestProject {
    TempDir tmp;
    fs::path dbPath;
    fs::path myDub;
    std::unique_ptr<Database> db;
    std::unique_ptr<ClipStore> store;
    std::unique_ptr<EditStack> edits;
    std::string takeId = "HASH0001_take_1"; // формат: <wem_hash>_take_<N>

    static constexpr std::uint32_t kSr = 48000;

    TestProject() {
        dbPath = tmp.path() / "lines.db";
        myDub = tmp.path() / "MyDub";
        fs::create_directories(myDub / "takes");
        db = std::make_unique<Database>(dbPath.string());
        db->exec("INSERT INTO game_files(file_id) VALUES('q000');");
        db->exec("INSERT INTO quests(quest_id, file_id) VALUES('cs1','q000');");
        db->exec("INSERT INTO lines(file_id, wem_hash, quest_id, ref_duration_ms)"
                 " VALUES('q000','HASH0001','cs1', 1500);");
        store = std::make_unique<ClipStore>();
        edits = std::make_unique<EditStack>(*db, *store, myDub.string());
    }

    std::string takeWav() const { return (myDub / "takes" / (takeId + ".wav")).string(); }

    // Пишет WAV + строку takes + клип в store (эквивалент записи Фазы 1).
    void addRecordedTake() {
        Clip c;
        c.id = takeId;
        c.title = "TAKE-01";
        c.wemHash = "HASH0001";
        c.sampleRate = kSr;
        c.colorRgb = 0x4f9dff;
        c.samples.assign(kSr, 0.0f); // 1 сек
        const std::size_t edge = kSr / 10; // 100 мс тишины по краям
        for (std::size_t i = edge; i < kSr - edge; ++i) {
            c.samples[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 440.0 *
                                           static_cast<double>(i - edge) / kSr);
        }
        c.rmsDb = dubstudio::rmsDb(c.samples);
        c.peakDb = dubstudio::peakDb(c.samples);
        WavWriter::writePcm24(takeWav(), c.samples, kSr);
        c.filePath = takeWav();
        db->exec("INSERT INTO takes(take_id, wem_hash, file_cas, duration_ms, quality,"
                 " rms_db, peak_db, is_master_candidate, comment) VALUES('" +
                 takeId + "','HASH0001','" + takeWav() + "',1000,'green'," +
                 std::to_string(c.rmsDb) + "," + std::to_string(c.peakDb) + ",0,'Фаза 1: запись');");
        store->addTake(std::move(c));
    }

    EditCommand cmd(EditType t) const {
        EditCommand c;
        c.type = t;
        c.takeId = takeId;
        return c;
    }
};

} // namespace

// --- audio_ops: чистые операции --------------------------------------------------

TEST_CASE("audio_ops: нормализация по пику", "[edit]") {
    std::vector<float> s = {0.1f, -0.5f, 0.25f, 0.3f};
    const double applied = dubstudio::normalizePeakToDb(s, -3.0);
    const float expect = static_cast<float>(std::pow(10.0, -3.0 / 20.0));
    CHECK(std::fabs(s[1]) == Approx(expect).epsilon(1e-4));
    CHECK(s[0] == Approx(0.1f / 0.5f * expect).epsilon(1e-4));
    CHECK(applied == Approx(20.0 * std::log10(expect / 0.5)).epsilon(0.001));
    // Тишина не ломается.
    std::vector<float> silence(10, 0.0f);
    CHECK(dubstudio::normalizePeakToDb(silence, -3.0) == 0.0);
}

TEST_CASE("audio_ops: тишина и реверс диапазона", "[edit]") {
    std::vector<float> s = {1, 2, 3, 4, 5, 6};
    dubstudio::silenceRange(s, 1, 4);
    CHECK(s == std::vector<float>{1, 0, 0, 0, 5, 6});
    std::vector<float> r = {1, 2, 3, 4, 5};
    dubstudio::reverseSamples(r, 1, 4);
    CHECK(r == std::vector<float>{1, 4, 3, 2, 5});
}

TEST_CASE("audio_ops: поиск звука с порогом -50 dBFS", "[edit]") {
    std::vector<float> s(1000, 0.0f);
    for (std::size_t i = 300; i < 700; ++i) s[i] = 0.5f;
    const auto r = dubstudio::findSoundRange(s, -50.0, 100); // окно 100 сэмплов
    CHECK_FALSE(r.empty);
    CHECK(r.first == 300);
    CHECK(r.last == 700);
    // Полная тишина.
    const auto q = dubstudio::findSoundRange(std::vector<float>(1000, 0.0f));
    CHECK(q.empty);
}

TEST_CASE("audio_ops: прилипание к zero-crossing", "[edit]") {
    // Синус, первый переход нуля на сэмпле 54 (440 Гц @ 48 кГц).
    std::vector<float> s(480);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<float>(std::sin(2 * 3.14159265 * 440.0 * i / 48000.0));
    const auto p = dubstudio::snapToZeroCrossing(s, 40);
    CHECK(p >= 53);
    CHECK(p <= 55);
    const float atP = s[p];
    CHECK(std::fabs(atP) < 0.2f); // у пересечения нуля амплитуда мала
}

TEST_CASE("audio_ops: renderMix — позиция, гейн, фейды", "[edit]") {
    Clip a;
    a.samples.assign(10, 1.0f);
    a.startSample = 100; // клип занимает [100,110)
    a.gainDb = 0.0;
    a.fadeInSamples = 10;
    const std::vector<float> mix = dubstudio::renderMix({a});
    REQUIRE(mix.size() == 110);
    CHECK(mix[99] == 0.0f);                         // до клипа тишина
    CHECK(mix[100] == Approx(1.0f / 10.0f).epsilon(1e-5)); // линейный фейд-ин
    CHECK(mix[109] == Approx(1.0f).epsilon(1e-5));

    a.gainDb = -6.0;
    const auto mix2 = dubstudio::renderMix({a});
    CHECK(mix2[109] == Approx(std::pow(10.0, -6.0 / 20.0)).epsilon(0.01));

    // Два клипа складываются в наложении.
    Clip b;
    b.samples.assign(10, 0.25f);
    b.startSample = 105; // [105,115)
    const auto mix3 = dubstudio::renderMix({a, b});
    REQUIRE(mix3.size() == 115);
    // 105: a-фейд (6/10 * -6dB) + b целиком.
    CHECK(mix3[105] == Approx(0.6 * std::pow(10.0, -6.0 / 20.0) + 0.25f).epsilon(0.01));
    CHECK(mix3[114] == Approx(0.25f).epsilon(0.01)); // после конца a — только b
}

TEST_CASE("audio_ops: кроссфейд equal-power сохраняет мощность", "[edit]") {
    Clip a, b;
    a.samples.assign(1000, 0.5f);
    a.startSample = 0;
    b.samples.assign(1000, 0.5f);
    b.startSample = 500; // перекрытие 500
    const auto m = dubstudio::crossfadeMerge(a, b);
    REQUIRE(m.samples.size() == 1500);
    CHECK(m.overlapSamples == 500);
    CHECK(m.startSample == 0);
    // Вне перекрытия — сигнал без изменений.
    CHECK(m.samples[10] == Approx(0.5f).epsilon(1e-4));
    CHECK(m.samples[1490] == Approx(0.5f).epsilon(1e-4));
    // Середина перекрытия: 0.5*cos45 + 0.5*sin45 = 0.707 -> мощность 0.5.
    CHECK(m.samples[750] == Approx(0.7071f).epsilon(0.01));
}

TEST_CASE("audio_ops: WSOLA — длина точная, питч сохраняется", "[edit]") {
    std::vector<float> sine(48000);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * 440.0 * i / 48000.0));

    SECTION("растяжение x1.25") {
        const auto out = dubstudio::timeStretchWsola(sine, 1.25, 48000);
        REQUIRE(out.size() == 60000); // ровно round(48000*1.25)
        // Частота пересечений нуля сохраняется (питч не уехал).
        const double zcIn = static_cast<double>(countZeroCrossings(sine)) / sine.size();
        const double zcOut = static_cast<double>(countZeroCrossings(out)) / out.size();
        CHECK(zcOut == Approx(zcIn).epsilon(0.05));
        // Громкость не схлопнулась.
        CHECK(dubstudio::rmsDb(out) > -15.0);
    }
    SECTION("сжатие x0.8") {
        const auto out = dubstudio::timeStretchWsola(sine, 0.8, 48000);
        REQUIRE(out.size() == 38400);
        const double zcIn = static_cast<double>(countZeroCrossings(sine)) / sine.size();
        const double zcOut = static_cast<double>(countZeroCrossings(out)) / out.size();
        CHECK(zcOut == Approx(zcIn).epsilon(0.05));
        CHECK(dubstudio::rmsDb(out) > -15.0);
    }
    SECTION("ratio 1.0 — копия") {
        const auto out = dubstudio::timeStretchWsola(sine, 1.0, 48000);
        CHECK(out == sine);
    }
    SECTION("кламп коэффициента") {
        const auto out = dubstudio::timeStretchWsola(sine, 100.0, 48000);
        CHECK(out.size() == 48000 * 4);
    }
}

// --- EditStack --------------------------------------------------------------------

TEST_CASE("EditStack: apply/undo/redo в одной сессии", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    std::vector<float> samples0;
    std::uint32_t sr = 0;
    REQUIRE(WavWriter::readMono(p.takeWav(), samples0, sr));

    // 1) трим тишины по краям
    EditCommand c1 = p.cmd(EditType::TrimSilence);
    p.edits->apply(c1);
    CHECK(p.store->takes()[0].samples.size() < samples0.size());
    CHECK(p.store->takes()[0].samples.size() > samples0.size() - 2 * 4800 - 1000);

    // 2) гейн +6 dB
    EditCommand c2 = p.cmd(EditType::Gain);
    c2.dValue = 6.0;
    p.edits->apply(c2);
    CHECK(p.store->takes()[0].gainDb == 6.0);

    // 3) перемещение
    EditCommand c3 = p.cmd(EditType::Move);
    c3.uValue = 12345;
    p.edits->apply(c3);
    CHECK(p.store->takes()[0].startSample == 12345);

    CHECK(p.edits->canUndo());
    CHECK_FALSE(p.edits->canRedo());

    // Откат всех трёх: точные исходные сэмплы (тот же WAV) и свойства.
    REQUIRE(p.edits->undo());
    CHECK(p.store->takes()[0].startSample == 0);
    REQUIRE(p.edits->undo());
    CHECK(p.store->takes()[0].gainDb == 0.0);
    REQUIRE(p.edits->undo());
    CHECK(p.store->takes()[0].samples == samples0);
    CHECK(p.store->takes()[0].startSample == 0);
    CHECK_FALSE(p.edits->canUndo());
    CHECK(p.edits->canRedo());

    // Повтор всех трёх.
    REQUIRE(p.edits->redo());
    REQUIRE(p.edits->redo());
    REQUIRE(p.edits->redo());
    CHECK(p.store->takes()[0].startSample == 12345);
    CHECK(p.store->takes()[0].gainDb == 6.0);
    CHECK(p.store->takes()[0].samples.size() < samples0.size());
    CHECK_FALSE(p.edits->canRedo());

    // Счётчики undo_log соответствуют.
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=0;") ==
          3);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=1;") ==
          0);
}

TEST_CASE("EditStack: ГОТОВНОСТЬ Фазы 2 — откат до старта после рестарта", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    std::vector<float> samples0;
    std::uint32_t sr = 0;
    REQUIRE(WavWriter::readMono(p.takeWav(), samples0, sr));
    const std::size_t len0 = samples0.size();

    // Цепочка правок: трим -> гейн -> нормализация -> тишина -> реверс -> fit.
    REQUIRE(p.edits->apply(p.cmd(EditType::TrimSilence)) > 0);
    EditCommand g = p.cmd(EditType::Gain);
    g.dValue = 3.0;
    REQUIRE(p.edits->apply(g) > 0);
    EditCommand n = p.cmd(EditType::Normalize);
    n.dValue = -3.0;
    REQUIRE(p.edits->apply(n) > 0);
    EditCommand s = p.cmd(EditType::Silence);
    s.from = 100;
    s.to = 200;
    REQUIRE(p.edits->apply(s) > 0);
    EditCommand r = p.cmd(EditType::Reverse);
    REQUIRE(p.edits->apply(r) > 0);
    EditCommand mv = p.cmd(EditType::Move);
    mv.uValue = 777;
    REQUIRE(p.edits->apply(mv) > 0);
    REQUIRE(p.edits->apply(p.cmd(EditType::FitToRef)) > 0);
    CHECK(p.store->takes()[0].samples.size() == 72000); // 1500 мс * 48 кГц

    // --- «Рестарт»: все объекты пересоздаются на тех же файлах/БД. ---
    p.edits.reset();
    p.store.reset();
    p.db.reset();
    p.db = std::make_unique<Database>(p.dbPath.string());
    p.store = std::make_unique<ClipStore>();
    p.edits = std::make_unique<EditStack>(*p.db, *p.store, p.myDub.string());
    const auto session = p.edits->loadSession();
    CHECK(session.takes == 1);
    CHECK(session.maxTakeNum == 1);
    // Состояние после рестарта = состояние на момент закрытия.
    REQUIRE(p.store->takes().size() == 1);
    CHECK(p.store->takes()[0].samples.size() == 72000);
    CHECK(p.store->takes()[0].startSample == 777);
    CHECK(p.edits->canUndo());

    // Полный откат до старта сессии: ровно исходный тейк.
    int undone = 0;
    while (p.edits->canUndo()) {
        REQUIRE(p.edits->undo());
        ++undone;
    }
    CHECK(undone == 7);
    REQUIRE(p.store->takes().size() == 1);
    CHECK(p.store->takes()[0].samples == samples0); // побитово исходный WAV
    CHECK(p.store->takes()[0].samples.size() == len0);
    CHECK(p.store->takes()[0].startSample == 0);
    CHECK(p.store->takes()[0].gainDb == 0.0);
    CHECK(p.store->takes()[0].fadeInSamples == 0);
    CHECK(p.store->takes()[0].fadeOutSamples == 0);

    // И redo тоже пережил рестарт: повтор первого шага (трим) -> 38400.
    CHECK(p.edits->canRedo());
    REQUIRE(p.edits->redo());
    CHECK(p.store->takes()[0].samples.size() == 48000 - 2 * 4800);
    CHECK(p.edits->canRedo());
}

TEST_CASE("EditStack: split создаёт тейк, undo/redo и рестарт", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    const std::size_t n = p.store->takes()[0].samples.size();

    EditCommand c = p.cmd(EditType::Split);
    c.from = n / 2;
    REQUIRE(p.edits->apply(c) > 0);
    REQUIRE(p.store->takes().size() == 2);
    const std::size_t left = p.store->takes()[0].samples.size();
    const std::size_t right = p.store->takes()[1].samples.size();
    CHECK(left + right == n);
    CHECK(p.store->takes()[1].id == p.takeId + ".1");
    CHECK(p.store->takes()[1].startSample == p.store->takes()[0].startSample + left);
    // Строка takes для созданного куска появилась.
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM takes;") == 2);

    // Undo: второй кусок исчез, длина исходная.
    REQUIRE(p.edits->undo());
    REQUIRE(p.store->takes().size() == 1);
    CHECK(p.store->takes()[0].samples.size() == n);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM takes;") == 1);

    // Redo: снова два.
    REQUIRE(p.edits->redo());
    REQUIRE(p.store->takes().size() == 2);
    CHECK(p.store->takes()[1].samples.size() == right);

    // Рестарт: split-тейк загружается со своим title.
    const std::string rightId = p.takeId + ".1";
    p.edits.reset();
    p.store.reset();
    p.db.reset();
    p.db = std::make_unique<Database>(p.dbPath.string());
    p.store = std::make_unique<ClipStore>();
    p.edits = std::make_unique<EditStack>(*p.db, *p.store, p.myDub.string());
    const auto session = p.edits->loadSession();
    CHECK(session.takes == 2);
    REQUIRE(p.store->takes().size() == 2);
    bool sawRight = false;
    for (const auto& t : p.store->takes()) {
        if (t.id == rightId) {
            sawRight = true;
            CHECK(t.samples.size() == right);
            CHECK(t.title == "TAKE-01.1");
        }
    }
    CHECK(sawRight);
}

TEST_CASE("EditStack: новая команда отбрасывает redo-ветку", "[edit]") {
    TestProject p;
    p.addRecordedTake();

    EditCommand g = p.cmd(EditType::Gain);
    g.dValue = 6.0;
    REQUIRE(p.edits->apply(g) > 0);
    REQUIRE(p.edits->undo());
    CHECK(p.edits->canRedo());

    EditCommand g2 = p.cmd(EditType::Gain);
    g2.dValue = -2.0;
    REQUIRE(p.edits->apply(g2) > 0);
    CHECK_FALSE(p.edits->canRedo()); // ветка сожжена
    CHECK(p.store->takes()[0].gainDb == -2.0);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM undo_log WHERE scope='edit';") == 1);
}

TEST_CASE("EditStack: кроссфейд сливает клипы, undo возвращает оба", "[edit]") {
    TestProject p;
    p.addRecordedTake();

    // Второй тейк: перекрывает первый наполовину.
    Clip b;
    b.id = "take_2_DEADBEEF";
    b.title = "TAKE-02";
    b.wemHash = "HASH0001";
    b.sampleRate = 48000;
    b.startSample = 24000; // перекрытие с первым
    b.samples.assign(24000, 0.25f);
    WavWriter::writePcm24((p.myDub / "takes" / "take_2_DEADBEEF.wav").string(), b.samples, 48000);
    b.filePath = (p.myDub / "takes" / "take_2_DEADBEEF.wav").string();
    p.db->exec("INSERT INTO takes(take_id, wem_hash, file_cas, duration_ms, comment)"
               " VALUES('take_2_DEADBEEF','HASH0001','" + b.filePath + "',500,'TAKE-02');");
    p.store->addTake(b);

    EditCommand c = p.cmd(EditType::Crossfade);
    c.takeId2 = "take_2_DEADBEEF";
    REQUIRE(p.edits->apply(c) > 0);
    REQUIRE(p.store->takes().size() == 1); // второй слит
    // a [0..48000) + b [24000..48000) -> итог 48000, перекрытие 24000.
    CHECK(p.store->takes()[0].samples.size() == 48000);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM takes;") == 1);

    // Undo: оба клипа на месте.
    REQUIRE(p.edits->undo());
    REQUIRE(p.store->takes().size() == 2);
    int seenSecond = 0;
    for (const auto& t : p.store->takes())
        if (t.id == "take_2_DEADBEEF") ++seenSecond;
    CHECK(seenSecond == 1);
    CHECK(p.store->takeIndexById("take_2_DEADBEEF") >= 0);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM takes;") == 2);
}

TEST_CASE("EditStack: ошибки не роняют состояние", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    const auto before = p.store->takes()[0].samples;

    EditCommand bad = p.cmd(EditType::TrimSilence);
    bad.takeId = "no_such_take";
    CHECK_THROWS(p.edits->apply(bad));
    // Клип не тронут.
    CHECK(p.store->takes()[0].samples == before);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM undo_log WHERE scope='edit';") == 0);

    // FitToRef у реплики без длительности.
    EditCommand noRef = p.cmd(EditType::FitToRef);
    p.db->exec("UPDATE lines SET ref_duration_ms=0;");
    CHECK_THROWS(p.edits->apply(noRef));
    CHECK(p.store->takes()[0].samples == before);
}

TEST_CASE("Схема: миграция undo_log старой базы (Фаза 0/1)", "[edit]") {
    TestProject p;
    // «Старая» база: undo_log без колонок Фазы 2.
    p.edits.reset();
    p.db->exec("DROP TABLE undo_log;");
    p.db->exec("CREATE TABLE undo_log(seq INTEGER PRIMARY KEY AUTOINCREMENT,"
               " created_at INTEGER DEFAULT (strftime('%s','now')),"
               " action TEXT NOT NULL, undone INTEGER DEFAULT 0);");
    p.db->exec("INSERT INTO undo_log(action) VALUES('take.record:take_1_AB');");
    p.db.reset();

    // Повторное открытие накатывает миграцию.
    p.db = std::make_unique<Database>(p.dbPath.string());
    CHECK(p.db->scalarInt(
              "SELECT COUNT(*) FROM pragma_table_info('undo_log') WHERE name='state_json';") ==
          1);
    CHECK(p.db->scalarInt(
              "SELECT COUNT(*) FROM pragma_table_info('undo_log') WHERE name='take_id';") == 1);
    CHECK(p.db->scalarInt("SELECT COUNT(*) FROM undo_log;") == 1); // история не потеряна

    // EditStack работает поверх мигрировавшей таблицы.
    p.store = std::make_unique<ClipStore>();
    p.edits = std::make_unique<EditStack>(*p.db, *p.store, p.myDub.string());
    p.addRecordedTake();
    REQUIRE(p.edits->apply(p.cmd(EditType::Align)) > 0);
    REQUIRE(p.edits->undo());
}

TEST_CASE("EditStack: maxTakeNum учитывает тейки без WAV (нет коллизии take_id)", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    // Сценарий из чек-листа: удалён MyDub, база осталась. Клип не загрузится,
    // но номер строки takes обязан учесться в maxTakeNum — иначе после рестарта
    // новый тейк сгенерирует занятый take_id -> UNIQUE constraint failed.
    p.edits.reset();
    p.store.reset();
    std::error_code ec;
    fs::remove_all(p.myDub, ec);
    p.store = std::make_unique<ClipStore>();
    p.edits = std::make_unique<EditStack>(*p.db, *p.store, p.myDub.string());
    const auto s = p.edits->loadSession();
    CHECK(s.takes == 0);      // WAV нет — клип пропущен, сессия не упала
    CHECK(s.maxTakeNum == 1); // номер строки учтён -> следующий тейк будет №2
}

TEST_CASE("EditStack: старый формат id take_N_hash из баз Фазы 1", "[edit]") {
    TestProject p;
    p.takeId = "take_9_OLD12345";
    p.addRecordedTake();
    const auto s = p.edits->loadSession();
    REQUIRE(s.takes == 1);
    CHECK(s.maxTakeNum == 9);                          // номер распознан
    CHECK(p.store->takes()[0].title == "TAKE-09");     // тайтл восстановлен
}

TEST_CASE("Автосейв: manifest.json и wal checkpoint", "[edit]") {
    TestProject p;
    p.addRecordedTake();
    REQUIRE(p.edits->apply(p.cmd(EditType::Align)) > 0);

    const std::int64_t t = p.edits->autosaveSnapshot();
    CHECK(t > 0);
    const fs::path manifest = p.myDub / "manifest.json";
    REQUIRE(fs::exists(manifest));
    std::ifstream f(manifest, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(content.find("\"saved_at\"") != std::string::npos);
    CHECK(content.find("\"takes\"") != std::string::npos);
    CHECK(content.find("\"edits\"") != std::string::npos);
    CHECK_FALSE(fs::exists(p.myDub / "manifest.json.tmp")); // атомарная запись
}
