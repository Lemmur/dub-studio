// Тесты Importer: корректность 4-уровневого импорта + производительность
// (готовность Фазы 0: 39481 реплика < 3 сек, PLAN.md раздел 14).
#include "test_helpers.h"

#include "dubstudio/database.h"
#include "dubstudio/importer.h"

#include <catch_amalgamated.hpp>

#include <fstream>

namespace {

void writeSmallJson(const fs::path& p) {
    std::ofstream f(p, std::ios::binary);
    f << smallCombinedJson();
}

} // namespace

TEST_CASE("Importer: малый combined.json, все правила PLAN.md", "[importer]") {
    TempDir dir;
    const fs::path jsonPath = dir.path() / "combined.json";
    writeSmallJson(jsonPath);

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer importer(db);
    const auto stats = importer.importFile(jsonPath.string());

    REQUIRE(stats.files == 2);
    REQUIRE(stats.quests == 3);
    REQUIRE(stats.lines == 6);
    REQUIRE(stats.ftsRows == 6);
    REQUIRE(stats.elapsedMs > 0.0);

    SECTION("порядок строк = порядок ключей JSON (ordered_json)") {
        // line_pk возрастает в порядке импорта: guid-ы идут ровно в порядке файла.
        REQUIRE(db.scalarText(
                    "SELECT wem_hash FROM lines ORDER BY line_pk LIMIT 1;") ==
                "6046256F4DF7E505F0906FBE58C09951");
        REQUIRE(db.scalarText(
                    "SELECT wem_hash FROM lines ORDER BY line_pk DESC LIMIT 1;") ==
                "A0000000000000000000000000000005");
        // order_index внутри сцены: 0,1,2 в первой сцене.
        REQUIRE(db.scalarInt(
                    "SELECT order_index FROM lines WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") == 0);
        REQUIRE(db.scalarInt(
                    "SELECT order_index FROM lines WHERE wem_hash='A0000000000000000000000000000002';") == 2);
        // order_index сцен и файлов.
        REQUIRE(db.scalarInt(
                    "SELECT order_index FROM quests WHERE quest_id='cs_q000_2_gate';") == 1);
        REQUIRE(db.scalarInt(
                    "SELECT order_index FROM quests WHERE quest_id='ambrus_whispers';") == 0);
        REQUIRE(db.scalarInt(
                    "SELECT order_index FROM game_files WHERE file_id='sq708_ambrus';") == 1);
    }

    SECTION("пустой speaker_name -> UNKNOWN, confidence 0.0") {
        REQUIRE(db.scalarText(
                    "SELECT speaker_name FROM lines WHERE wem_hash='A0000000000000000000000000000001';") ==
                "UNKNOWN");
        REQUIRE(db.scalarInt(
                    "SELECT speaker_confidence == 0.0 FROM lines WHERE wem_hash='A0000000000000000000000000000001';") == 1);
        // Обычный спикер — confidence 1.0.
        REQUIRE(db.scalarInt(
                    "SELECT speaker_confidence == 1.0 FROM lines WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") == 1);
    }

    SECTION("суффикс (whisper) хранится целиком") {
        REQUIRE(db.scalarText(
                    "SELECT speaker_name FROM lines WHERE wem_hash='A0000000000000000000000000000002';") ==
                "Ambrus (whisper)");
        REQUIRE(db.scalarText(
                    "SELECT speaker_internal FROM lines WHERE wem_hash='A0000000000000000000000000000002';") ==
                "Character.Main.Ambrus");
    }

    SECTION("dur сек -> ref_duration_ms") {
        REQUIRE(db.scalarInt(
                    "SELECT ref_duration_ms FROM lines WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") == 1861);
        REQUIRE(db.scalarInt(
                    "SELECT ref_duration_ms FROM lines WHERE wem_hash='A0000000000000000000000000000005';") == 3142);
    }

    SECTION("texts: en_original + ru_base = ru_adapted") {
        REQUIRE(db.scalarText(
                    "SELECT en_original FROM texts WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") ==
                "Oh! Coen, look!");
        REQUIRE(db.scalarText(
                    "SELECT ru_base FROM texts WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") ==
                "О! Коэн, смотри!");
        REQUIRE(db.scalarText(
                    "SELECT ru_adapted FROM texts WHERE wem_hash='6046256F4DF7E505F0906FBE58C09951';") ==
                "О! Коэн, смотри!");
    }

    SECTION("FTS-поиск: RU, EN, спикер") {
        REQUIRE(db.scalarInt(
                    "SELECT COUNT(*) FROM lines_fts WHERE lines_fts MATCH 'Коэн';") == 1);
        REQUIRE(db.scalarInt(
                    "SELECT COUNT(*) FROM lines_fts WHERE lines_fts MATCH 'gate';") == 1);
        // Колоночный фильтр по спикеру (для фильтра UNKNOWN в LineList).
        REQUIRE(db.scalarInt(
                    "SELECT COUNT(*) FROM lines_fts WHERE lines_fts MATCH 'speaker_name:UNKNOWN';") == 1);
        REQUIRE(db.scalarInt(
                    "SELECT COUNT(*) FROM lines_fts WHERE lines_fts MATCH 'speaker_name:Ambrus';") == 2);
    }

    SECTION("повторный импорт идемпотентен (OR IGNORE / FTS пересборка)") {
        const auto again = importer.importFile(jsonPath.string());
        REQUIRE(again.lines == 6);
        REQUIRE(again.ftsRows == 6);
        REQUIRE(db.scalarInt("SELECT COUNT(*) FROM text_history;") == 0);
    }
}

TEST_CASE("Importer: некорректный JSON даёт понятную ошибку", "[importer]") {
    TempDir dir;
    const fs::path jsonPath = dir.path() / "broken.json";
    { std::ofstream f(jsonPath, std::ios::binary); f << "{ not json ]"; }

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer importer(db);
    REQUIRE_THROWS_AS(importer.importFile(jsonPath.string()), std::runtime_error);
    REQUIRE_THROWS_AS(importer.importFile((dir.path() / "missing.json").string()),
                      std::runtime_error);
}

TEST_CASE("Importer: 39481 реплика за < 3 сек (готовность Фазы 0)",
          "[importer][perf]") {
    TempDir dir;
    const fs::path jsonPath = dir.path() / "combined_big.json";
    const BigJson big = generateBigCombinedJson(jsonPath);

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer importer(db);

    const auto stats = importer.importFile(jsonPath.string());

    // Точные объёмы проекта (PLAN.md 1.2: 42 / 2181 / 39481).
    REQUIRE(stats.files == big.files);
    REQUIRE(stats.quests == big.quests);
    REQUIRE(stats.lines == big.lines);
    REQUIRE(stats.ftsRows == big.lines);

    // Первая и последняя реплика на своих местах (порядок сохранён).
    REQUIRE(db.scalarText("SELECT wem_hash FROM lines ORDER BY line_pk LIMIT 1;") ==
            big.firstGuid);
    REQUIRE(db.scalarText("SELECT wem_hash FROM lines ORDER BY line_pk DESC LIMIT 1;") ==
            big.lastGuid);

    // Цель готовности: импорт < 3 сек (PLAN.md 5.2 и раздел 14).
    INFO("Время импорта: " << stats.elapsedMs << " мс");
    REQUIRE(stats.elapsedMs < 3000.0);

    // Доля UNKNOWN-спикеров сошлась с ожидаемой раскладкой (5% пустых).
    const auto unknown = db.scalarInt(
        "SELECT COUNT(*) FROM lines WHERE speaker_name='UNKNOWN';");
    REQUIRE(unknown > 0);
    REQUIRE(unknown < big.lines);
}
