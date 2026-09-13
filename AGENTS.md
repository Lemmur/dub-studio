# AGENTS.md — DubStudio

> Единый файл правил для всех агентных ИИ (Roo, Cline, Copilot, Claude, Cursor).
> Полная аналитика проекта — в [`PLAN.md`](PLAN.md:1). Дублировать решения из него сюда запрещено,
> здесь только операционные правила для агента.

## Стек

- `C++20`, `Qt 6.5 LTS Widgets`, `CMake + Ninja`, `SQLite WAL`.
- Аудио: `RtAudio (MIT)` + `ASIO SDK 2.3.4` (`third_party/asio_sdk`, НЕ коммитить;
  исходник уже лежит в `ASIO-SDK_2.3.4_2025-10-15/ASIOSDK`, при сборке брать `common/` + `host/` + `host/pc/` оттуда).
- `DSP`: `libsndfile`, `dr_wav`, `SoXR`, `RubberBand`, `Eigen`.
- `JSON`: `nlohmann::json` (`ordered_json` — порядок ключей = порядок реплик в сцене).
- Тесты: `Catch2`. Сеть: `Qt Network` (`REST` к `OpenRouter` / `ElevenLabs`).
- `Python 3.11 sidecar` по `gRPC / ZeroMQ + JSON` ТОЛЬКО для ИИ (`denoise`, `llm_proxy`, `RVC` опционально).
  Падение `CUDA / GIL / OOM` в sidecar не должно ронять `ASIO callback`.

## Лицензии (жёстко)

- В линковку ядра — только `open-source` без `GPL`-заражения.
- `GPL` допускается только как внешний `CLI` (`vgmstream`), вызывать через `subprocess`.
- `sound2wem` — внешний исходник в `third_party/sound2wem`, вызывать как `subprocess`, лицензию зафиксировать в [`docs/licenses.md`](docs/licenses.md:1).
- `ASIO SDK` — dual license (Proprietary / GPLv3), НЕ коммитить (см. [`.gitignore`](.gitignore:1)).

## Структура репо

```text
app/                 # MainWindow, QDockWidget-панели
include/dubstudio/   # публичные интерфейсы (*.h)
core/audio/          # AudioEngine (RtAudio), ClipStore
core/project/        # ProjectManager, importer, TakeManager, UndoStack, JobQueue, ExportPack
core/text/           # TextEngine, LipSyncAnalyzer
workers/python/      # denoise_server.py, llm_proxy.py, rvc_server.py (опц.)
integrations/        # ElevenLabsClient, OpenRouterProvider, WemBridge
plugins/             # <name>/manifest.json + python-скрипт, ядро не пересобирается
tests/               # Catch2
third_party/rtaudio  # сабмодуль/вендор
third_party/sound2wem# исходники кодера, сборка sound2wem.exe
third_party/asio_sdk # в .gitignore, НЕ коммитить
docs/plans/          # план на каждый модуль ПЕРЕД кодом
```

## Железные правила

1. `UI`-текст только на русском. Тёмная тема по умолчанию.
2. Все мутации состояния — через паттерн `Command` + запись в таблицу `undo_log`. Откат — с открытия сессии.
3. В `ASIO callback` только `ring-buffer`. Запрещены `malloc`, `I/O`, `SQL`, `mutex`.
4. Один `PR` = один модуль. Ветки `feat/*`. Коммиты `conventional commits`, описание на русском.
5. Перед кодом модуля — план в `docs/plans/<модуль>.md`.
6. Фазы строго по порядку `0 -> 1 -> ... -> 9` (см. [`PLAN.md`](PLAN.md:565)). Не перескакивать.
   Каждая фаза заканчивается тестом из колонки «Готовность» и коммитом.
7. Формат `combined.json` — 4 уровня `{file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}`
   (см. [`PLAN.md`](PLAN.md:149)). Пустой `speaker_name` → `UNKNOWN`, `speaker_confidence = 0.0`.
8. Суффикс вида `"Ambrus (whisper)"` хранить целиком для отображения; для `voice-map` чистить скобки,
   сам суффикс маппить в пресет параметров.
9. Проект по умолчанию `48000 Гц / 24-bit`. `Direct Monitoring OFF` по умолчанию.
10. Кэш `ElevenLabs` обязателен: `cache/elevenlabs/{sha256(audio+voice_id+params)}.flac + meta.json`.

## Сборка (Windows 10/11 x64, MSVC)

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`Qt 6.5 LTS` ставится через `aqtinstall` или официальный инсталлер, путь — через `CMAKE_PREFIX_PATH`.
Без `Qt`/`MSVC` агент выполняет только фазы, не требующие сборки (доки, `Python`-утилиты, схемы).

## MCP

Доступные серверы (см. [`.vscode/mcp.json`](.vscode/mcp.json:1), [`.roo/mcp.json`](.roo/mcp.json:1)):
`filesystem` (только корень репо), `git`, `github` (ветки `feat/*`, `PR`), `sqlite`
(только `SELECT` по реальной БД `MyDub/lines.db`), `fetch` (доки `RtAudio` / `OpenRouter` /
`ElevenLabs` / `sound2wem`), `memory` (фиксация решений).
