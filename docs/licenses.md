# Лицензии сторонних компонентов

Фаза 0 — зафиксировано по фактически подключённым зависимостям.
Правило (AGENTS.md): в линковку ядра — только open-source без GPL-заражения.

| Компонент | Версия | Лицензия | Тип интеграции | Статус |
|---|---|---|---|---|
| `Qt` | 6.5.3 LTS (`C:\Qt\6.5.3\msvc2019_64`) | `LGPLv3` (open-source edition) | линковка ядра (Widgets) | подключено |
| `SQLite` | 3.49.1 (amalgamation, [`third_party/sqlite`](../third_party/sqlite/sqlite3.h)) | Public Domain | линковка ядра (`SQLITE_ENABLE_FTS5`) | подключено |
| `nlohmann::json` | 3.11.3 (single header, [`third_party/nlohmann`](../third_party/nlohmann/nlohmann/json.hpp)) | `MIT` | header-only в ядре | подключено |
| `Catch2` | 3.7.1 (amalgamated, [`tests/third_party/catch2`](../tests/third_party/catch2/catch_amalgamated.hpp)) | `BSL-1.0` | только тесты | подключено |
| `sound2wem` | v6 (2026-01-02, [`third_party/sound2wem/zSound2wem.cmd`](../third_party/sound2wem/zSound2wem.cmd)) | `MPL-2.0` (см. [`LICENSE`](../third_party/sound2wem/LICENSE)) | внешний `cmd`-скрипт, вызов как `subprocess`; НЕ линкуется | подключено |
| `Wwise Authoring` | 2026.1.3.9276 (`C:\Audiokinetic\Wwise_2026.1.3.9276`) | Proprietary (Audiokinetic) | требуется для работы `sound2wem` (`WwiseConsole.exe`); в репо не входит | установлено локально |
| `FFmpeg` | — (ставится `zSound2wem.cmd` автоматически при первом запуске) | `LGPL/GPL` в зависимости от сборки | внешний `CLI` внутри скрипта `sound2wem`, НЕ линкуется в ядро | ещё не устанавливался |
| `RtAudio` | 6.0.1 (vendored, [`third_party/rtaudio/rtaudio`](../third_party/rtaudio/rtaudio/RtAudio.h)) | `MIT` (см. [`LICENSE`](../third_party/rtaudio/rtaudio/LICENSE)) | статическая линковка ядра (`__WINDOWS_ASIO__` + `__WINDOWS_WASAPI__`) | подключено (Фаза 1) |
| `ASIO headers` | 2.3 в составе `RtAudio` (redistribute by RtAudio) | Steinberg ASIO Licence (dual: Proprietary / `GPLv3`), вендорятся апстрим-репозиторием `RtAudio` | компиляция `RtApiAsio` | подключено (Фаза 1) |
| `ASIO SDK` | 2.3.4 (полный) | Dual: Proprietary / `GPLv3` | не требуется: RtAudio вендорит заголовки ASIO 2.3; локальная копия `ASIO-SDK_2.3.4_2025-10-15/` удалена после Фазы 1 (при необходимости — скачать у Steinberg, не коммитить) | не используется |
| `dr_wav` | master 2026-01 (single header, [`third_party/dr_wav`](../third_party/dr_wav/dr_wav.h)) | `MIT-0` | реализация `WavWriter` (PCM 24-bit) в ядре | подключено (Фаза 1) |
| `vgmstream` | — | `GPL` | внешний `CLI` через `subprocess`, в ядро НЕ линковать | запланировано (Фаза 3) |
| `libsndfile` / `dr_wav` / `SoXR` / `RubberBand` / `Eigen` | — | `LGPL`/`MIT`/`GPL`(см. ниже) | линковка ядра — проверить каждую перед Фазой 1–2 | запланировано |

Примечания:

- `RubberBand` — `GPL` + коммерческая; для линковки в ядро потребуется коммерческая лицензия
  либо замена (решение — перед Фазой 2, зафиксировать сюда).
- `sound2wem` (вопреки ожиданию PLAN.md п.7 «собирается из исходников») — это готовый
  `cmd`-скрипт-обёртка над `WwiseConsole.exe` + `FFmpeg`, а не компилируемый кодер:
  собирать `sound2wem.exe` не из чего, см. [`docs/wem_params.md`](wem_params.md).
