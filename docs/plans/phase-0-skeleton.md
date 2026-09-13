# План Фаза 0 — Скелет (детали — в PLAN.md раздел 14)

Статус: **ЗАВЕРШЕНО** (см. «Результаты» внизу).

Готовность фазы (PLAN.md раздел 14): импорт `39481` строк за `<3` сек, скролл дерева
`файл -> сцена -> реплики` без лагов.

## Шаги (по первому промпту из PLAN.md 15.4)

1. [x] Структура папок + `CMakeLists.txt` (корень + `core/` + `app/` + `tests/`,
       сборка `scripts/{configure,build,test}.bat` под VS BuildTools 18 + Qt 6.5.3).
2. [x] `Database` ([`include/dubstudio/database.h`](../../include/dubstudio/database.h)):
       открывает `MyDub/lines.db`, применяет [`schema.sql`](../../schema.sql) (PLAN.md 5.1,
       идемпотентно, встраивается в бинарник через `cmake/schema.h.in`).
3. [x] `Importer` ([`include/dubstudio/importer.h`](../../include/dubstudio/importer.h))
       `combined.json`, 4 уровня, `ordered_json`, `order_index` на каждом уровне,
       `dur*1000` -> `ref_duration_ms`, пусто -> `UNKNOWN` + `speaker_confidence = 0.0`,
       батч `1000` строк в транзакции, `WAL`, пересборка `lines_fts` после импорта.
4. [x] `MainWindow` ([`app/mainwindow.cpp`](../../app/mainwindow.cpp)): тёмная тема
       (Fusion + палитра), слева док-дерево `файл -> сцена` со счётчиками, справа
       `QTableView` (`Статус | Спикер | EN | RU | Длит`) с ленивой подгрузкой по 500 строк
       ([`app/linesmodel.cpp`](../../app/linesmodel.cpp)), `FTS`-поиск, фильтры по сцене/файлу.
5. [x] `third_party/sound2wem`: склонирован (v6, `MPL-2.0`). Уточнение: это
       `cmd`-скрипт-обёртка над `WwiseConsole.exe`+`FFmpeg`, а не исходники кодера —
       «сборка `sound2wem.exe`» неприменима. Лицензия в
       [`docs/licenses.md`](../licenses.md), параметры в [`docs/wem_params.md`](../wem_params.md).
       `Wwise 2026.1.3` на машине установлен (`WWISEROOT` проверен).
6. [x] Тест `Catch2` ([`tests/`](../../tests/test_importer.cpp)): фейковый `combined.json`
       на `42` файла / `2181` сцену / `39481` реплику + малый фикстурный набор
       (UNKNOWN, `(whisper)`, порядок, `dur`, идемпотентность, FTS).

Коммит: `feat(project): скелет + 4-уровневый импорт + sound2wem`.

## Результаты (замеры на i9-14900K, Release)

- Импорт `39481` реплики (парсинг + вставка + FTS): **~0.8 сек** (цель < 3 сек).
- Виртуализация таблицы: первая страница 500 строк, докачка страниц до 39481 —
  скролл без лагов (`QTableView` + фиксированная высота строк).
- Все тесты `Catch2` зелёные (`ctest`: 1/1).
- Отклонения от плана: см. [`docs/wem_params.md`](../wem_params.md) — `sound2wem`
  оказался скриптом (Wwise обязателен), «сборка exe» заменена вендорингом скрипта.
