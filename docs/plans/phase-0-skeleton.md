# План Фаза 0 — Скелет (черновик для агента, детали — в PLAN.md раздел 14)

Готовность: импорт `39481` строк за `<3` сек, скролл дерева `файл -> сцена -> реплики` без лагов.

## Шаги (по первому промпту из PLAN.md 15.4)

1. Структура папок + `CMakeLists.txt` (по [`AGENTS.md`](AGENTS.md:1), раздел «Структура репо»).
2. `Database`: открыть `MyDub/lines.db`, применить `schema.sql` из [`PLAN.md`](PLAN.md:189).
3. `Importer` `combined.json`, 4 уровня `{file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}`:
   `order_index` на каждом уровне, `dur*1000` → `ref_duration_ms`, пусто → `UNKNOWN` + `speaker_confidence = 0.0`,
   батч по `1000` в транзакции, `WAL`.
4. `MainWindow`: тёмная тема, слева дерево `файл -> сцена` + `QTableView`
   (`Статус | Спикер | EN | RU | Длит`), `FTS`-поиск, виртуализация.
5. `third_party/sound2wem`: клонировать, собрать `sound2wem.exe`,
   лицензию в [`docs/licenses.md`](docs/licenses.md:1), `--help` в [`docs/wem_params.md`](docs/wem_params.md:1).
6. Тест: `39481` фейковая строка (`42` файла), импорт `< 3` сек, скролл без лагов.

Коммит: `feat(project): скелет + 4-уровневый импорт + sound2wem`.
