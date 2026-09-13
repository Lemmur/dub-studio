# RuDub Studio

Профессиональная DAW для сольного дубляжа игр EN -> RU. Полная аналитика и архитектура — [PLAN.md](PLAN.md).

## Стек

- C++20, Qt 6.5 LTS Widgets, CMake + Ninja, SQLite WAL
- Аудио: RtAudio + ASIO SDK, libsndfile, SoXR, RubberBand, Eigen
- Python sidecar (gRPC): DeepFilterNet3, LLM-прокси
- Внешние CLI: vgmstream (decode), sound2wem (encode)

## Среда разработки (Windows 11 x64)

| Инструмент | Расположение |
|---|---|
| MSVC (VS "18" Build Tools) | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` |
| Qt 6.5.3 LTS | `D:\Qt\6.5.3\msvc2019_64` |
| vcpkg | `D:\dev\vcpkg` |
| Python | 3.14 (системный) |

## Сборка

```bat
scripts\build.cmd
```

Скрипт вызывает `scripts\env.cmd` (vcvarsall + PATH Qt + CMake/Ninja), конфигурирует
пресет `release` из [CMakePresets.json](CMakePresets.json) и собирает `build\release\app\rudub.exe`.

Ручная сборка в терминале:

```bat
call scripts\env.cmd
cmake --preset release
cmake --build --preset release
```

## Структура репозитория

```text
app/              GUI (Qt Widgets, тёмная тема, русский интерфейс)
core/audio/       аудио-ядро (RtAudio/ASIO, Фаза 1)
core/project/     проект/БД/импортёр combined.json (Фаза 0)
core/text/        текст, слоги, липсинк (Фаза 5)
include/rudub/    публичные интерфейсы модулей
integrations/     ElevenLabs, OpenRouter (Фазы 5,7)
plugins/          офлайн-эффекты по manifest.json (Фаза 6)
workers/python/   Python sidecar (Фаза 6)
third_party/      rtaudio, sound2wem; asio_sdk — вручную, не коммитить
docs/             планы модулей, параметры WEM, лицензии
tests/            Catch2
schema.sql        схема lines.db (PLAN.md, раздел 5)
```

## Фазы

0 → 9 строго по порядку (PLAN.md, раздел 14). Текущая: **Фаза 0 — Скелет**.

## Лицензионные правила

Только open-source в линковке. ASIO SDK и sound2wem — внешние SDK/исходники,
vgmstream — внешний CLI. Детали: [docs/licenses.md](docs/licenses.md).
