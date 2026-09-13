// Тесты Database: схема раздела 5.1 + режим WAL.
#include "test_helpers.h"

#include "dubstudio/database.h"

#include <catch_amalgamated.hpp>

TEST_CASE("Database применяет схему и включает WAL", "[database]") {
    TempDir dir;
    const std::string dbPath = (dir.path() / "lines.db").string();

    {
        dubstudio::Database db(dbPath);

        // Режим журнала — WAL (PLAN.md, раздел 5.1).
        REQUIRE(db.journalMode() == "wal");

        // Все таблицы и индексы схемы на месте.
        const auto objects = db.scalarInt(
            "SELECT COUNT(*) FROM sqlite_master WHERE name IN ("
            "'game_files','quests','lines','texts','text_history','lines_fts',"
            "'takes','voice_map','jobs','undo_log',"
            "'idx_lines_file','idx_lines_quest','idx_lines_speaker','idx_lines_status',"
            "'idx_jobs_status');");
        REQUIRE(objects == 15);

        // FTS5 работает: виртуальная таблица отвечает на запросы.
        db.exec("INSERT INTO lines_fts(wem_hash, speaker_name, en_original, ru_final)"
                " VALUES('TESTHASH','Lunka','hello','привет');");
        REQUIRE(db.scalarInt(
                    "SELECT COUNT(*) FROM lines_fts WHERE lines_fts MATCH 'привет';") == 1);
    }

    // Повторное открытие той же базы — схема идемпотентна.
    REQUIRE_NOTHROW([&] { dubstudio::Database db2(dbPath); }());
}

TEST_CASE("Database: scalar-хелперы", "[database]") {
    TempDir dir;
    dubstudio::Database db((dir.path() / "lines.db").string());

    db.exec("INSERT INTO game_files(file_id, order_index) VALUES('f1', 0);");
    REQUIRE(db.scalarInt("SELECT COUNT(*) FROM game_files;") == 1);
    REQUIRE(db.scalarText("SELECT file_id FROM game_files;") == "f1");
}
