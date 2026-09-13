# CLAUDE.md — DubStudio (кратко, полный текст в AGENTS.md)

Перед работой прочитай [`AGENTS.md`](AGENTS.md:1) целиком и следуй ему.
Полная аналитика — только в [`PLAN.md`](PLAN.md:1), не дублируй решения оттуда.

- Стек: `C++20`, `Qt 6.5 LTS Widgets`, `CMake + Ninja`, `SQLite WAL`, `RtAudio (MIT)` + `ASIO SDK 2.3.4` (не коммитить).
- `UI`-текст только на русском, тёмная тема по умолчанию.
- Все мутации состояния — через `Command` + `undo_log`; откат с открытия сессии.
- В `ASIO callback` только `ring-buffer` (без `malloc`/`I/O`/`SQL`/`mutex`).
- Один `PR` = один модуль, ветки `feat/*`, `conventional commits` на русском.
- Перед кодом модуля — план в [`docs/plans/`](docs/plans/phase-0-skeleton.md:1).
- Фазы строго `0 -> 1 -> ... -> 9`, каждая заканчивается тестом «Готовность» и коммитом.
- MCP: `filesystem` только корень репо, `sqlite` только `SELECT` по `MyDub/lines.db`, `memory` для решений.
