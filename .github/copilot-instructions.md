# Copilot instructions — DubStudio (кратко, полный текст в AGENTS.md)

- Перед генерацией кода учитывай [`AGENTS.md`](AGENTS.md:1): `C++20`, `Qt 6.5 LTS Widgets`, `CMake + Ninja`, `SQLite WAL`.
- Полная аналитика — только в [`PLAN.md`](PLAN.md:1), не дублируй решения оттуда.
- `UI`-текст только на русском, тёмная тема по умолчанию.
- Все мутации состояния — через `Command` + запись в `undo_log`.
- В `ASIO callback` только `ring-buffer`; запрещены `malloc`, `I/O`, `SQL`, `mutex`.
- Лицензии: в линковку ядра — только open-source без `GPL`; `GPL` только как внешний `CLI` через `subprocess`.
- Один `PR` = один модуль, ветки `feat/*`, коммиты `conventional commits` на русском.
- Перед кодом модуля — план в [`docs/plans/`](docs/plans/phase-0-skeleton.md:1); фазы строго `0 -> 1 -> ... -> 9`.
- `combined.json` — 4 уровня `{file_id: {quest_id: {guid: {...}}}}`; пустой `speaker_name` → `UNKNOWN`.
