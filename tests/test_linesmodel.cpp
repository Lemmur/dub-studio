// Тесты LinesSqlModel: виртуализация (fetchMore), фильтры файл/сцена/FTS.
#include "test_helpers.h"

#include "linesmodel.h"

#include "dubstudio/database.h"
#include "dubstudio/importer.h"

#include <QCoreApplication>
#include <QtGlobal>

#include <catch_amalgamated.hpp>

#include <fstream>

namespace {

// Моделям Qt нужен QCoreApplication (события не гоняем, только сигналы).
QCoreApplication* qtApp() {
    static int argc = 1;
    static char name[] = "dubstudio_tests";
    static char* argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    return &app;
}

void writeSmallJson(const fs::path& p) {
    std::ofstream f(p, std::ios::binary);
    f << smallCombinedJson();
}

} // namespace

TEST_CASE("LinesSqlModel: колонки, статусы, формат длительности", "[model]") {
    qtApp();
    TempDir dir;
    const fs::path jsonPath = dir.path() / "combined.json";
    writeSmallJson(jsonPath);

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer(db).importFile(jsonPath.string());

    dubstudio::LinesSqlModel model(db);
    model.setFilter({}, {}, {});

    REQUIRE(model.rowCount() == 6);
    REQUIRE(model.columnCount() == 5);
    REQUIRE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString()
                .toStdString() == "Статус");

    const QModelIndex first = model.index(0, 0);
    REQUIRE(first.data(Qt::DisplayRole).toString().toStdString() == "К работе"); // todo
    REQUIRE(model.index(0, dubstudio::LinesSqlModel::ColSpeaker)
                .data(Qt::DisplayRole).toString().toStdString() == "Lunka");
    // 1.861 с -> "1.86 с"
    REQUIRE(model.index(0, dubstudio::LinesSqlModel::ColDur)
                .data(Qt::DisplayRole).toString().toStdString() == "1.86 с");

    // Пустой спикер отображается как UNKNOWN (из БД).
    REQUIRE(model.index(1, dubstudio::LinesSqlModel::ColSpeaker)
                .data(Qt::DisplayRole).toString().toStdString() == "UNKNOWN");

    REQUIRE(model.wemHashAt(0).toStdString() == "6046256F4DF7E505F0906FBE58C09951");
    REQUIRE(model.wemHashAt(-1).isEmpty());
}

TEST_CASE("LinesSqlModel: фильтры сцена/файл/FTS", "[model]") {
    qtApp();
    TempDir dir;
    const fs::path jsonPath = dir.path() / "combined.json";
    writeSmallJson(jsonPath);

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer(db).importFile(jsonPath.string());

    dubstudio::LinesSqlModel model(db);

    SECTION("фильтр по сцене") {
        model.setFilter({}, "cs_q000_1_opening", {});
        REQUIRE(model.rowCount() == 3);
        model.setFilter({}, "ambrus_whispers", {});
        REQUIRE(model.rowCount() == 2);
    }

    SECTION("фильтр по файлу") {
        model.setFilter("sq708_ambrus", {}, {});
        REQUIRE(model.rowCount() == 2);
    }

    SECTION("FTS-поиск по русскому тексту") {
        model.setFilter({}, {}, "Коэн");
        REQUIRE(model.rowCount() == 1);
        REQUIRE(model.index(0, dubstudio::LinesSqlModel::ColSpeaker)
                    .data(Qt::DisplayRole).toString().toStdString() == "Lunka");
    }

    SECTION("FTS-поиск без результатов") {
        model.setFilter({}, {}, "такоготекстанет");
        REQUIRE(model.rowCount() == 0);
    }

    SECTION("сцена + FTS вместе") {
        model.setFilter({}, "cs_q000_1_opening", "Ambrus");
        REQUIRE(model.rowCount() == 1);
    }
}

TEST_CASE("LinesSqlModel: ленивая подгрузка 39481 строки", "[model][perf]") {
    qtApp();
    TempDir dir;
    const fs::path jsonPath = dir.path() / "combined_big.json";
    generateBigCombinedJson(jsonPath);

    dubstudio::Database db((dir.path() / "lines.db").string());
    dubstudio::Importer(db).importFile(jsonPath.string());

    dubstudio::LinesSqlModel model(db);
    model.setFilter({}, {}, {}); // всё

    // Первый груз — ровно страница (500), не все 39481.
    REQUIRE(model.rowCount() == 500);
    REQUIRE(model.canFetchMore({}));

    // Прокрутка до конца страницами.
    int guard = 0;
    while (model.canFetchMore({}) && guard++ < 100) {
        model.fetchMore({});
    }
    REQUIRE(model.rowCount() == 39481);
    REQUIRE(!model.canFetchMore({}));
}
