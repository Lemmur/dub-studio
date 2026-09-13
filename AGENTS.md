# AGENTS.md — RuDubStudio

Стек: C++20, Qt6 Widgets, CMake+Ninja, SQLite WAL.
Аудио: RtAudio + ASIO SDK (third_party/asio_sdk, не коммитить).
DSP: libsndfile, SoXR, RubberBand, Eigen.
Python 3.11 sidecar по gRPC только для ИИ.
GPL только как CLI (vgmstream), sound2wem по его лицензии, не линковать GPL в ядро.
Структура: include/ интерфейсы, core/ реализация, tests/ Catch2.
Один PR = один модуль. Перед кодом план в docs/plans/<модуль>.md.
UI-текст только на русском.
Все мутации через Command + undo_log.
В callback ASIO: только ring-buffer, без malloc/I/O/SQL/mutex.
Фазы: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9. Не перескакивать.
Коммиты: conventional commits, описание на русском.

## Среда разработки (эта машина, Windows 11 x64)

| Инструмент | Версия / путь |
|---|---|
| Компилятор | MSVC, VS "18" Build Tools: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` |
| CMake + Ninja | в составе Build Tools; добавляются в PATH через `scripts\env.cmd` |
| Qt 6.5.3 LTS | `D:\Qt\6.5.3\msvc2019_64` (Widgets, Sql + плагин qsqlite, Network, Svg, Tools) |
| vcpkg | `D:\dev\vcpkg` (пакеты ставить по мере необходимости фаз) |
| Python | системный 3.14 (утилиты); для sidecar — отдельный Python 3.11 (Фаза 6) |
| git | 2.53 |

Правила работы со средой:

- Перед любой ручной командой сборки в терминале: `call scripts\env.cmd`.
- Полная сборка: `scripts\build.cmd` (CMakePresets → Ninja, `build\release\`).
- Qt передавать через `CMAKE_PREFIX_PATH=D:/Qt/6.5.3/msvc2019_64` (уже зашито в пресеты).
- ASIO SDK не коммитить: скачать вручную с сайта Steinberg в `third_party/asio_sdk/`.
- `qtdeclarative` не устанавливался (не нужен для Widgets); при необходимости:
  `python -m aqt install-qt windows desktop 6.5.3 win64_msvc2019_64 --archives qtdeclarative -O D:\Qt`.
- Свободное место на C: ограничено (~6 GB) — тяжёлые сборки и кэши держать на D:.

## Карта фаз (полный план — PLAN.md, раздел 14)

| Фаза | Содержимое |
|---|---|
| 0 | Скелет: CMake+Qt, schema.sql, импорт combined.json, дерево файл->сцена->реплики, sound2wem |
| 1 | Аудио: RtAudio+ASIO, запись, волноформа |
| 2 | Редактура: trim/split/..., Undo+автосейв |
| 3 | WEM: decode/encode, валидация |
| 4 | Тейки: циклозапись, автоцвет |
| 5 | LLM: ILlmProvider, OpenRouter/Ollama |
| 6 | Denoise: sidecar, DeepFilterNet3 |
| 7 | ElevenLabs: STS/TTS, voice_map, кэш |
| 8 | Pack: .dubpack, CAS |
| 9 | Полировка |
