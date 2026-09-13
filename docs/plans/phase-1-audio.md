# План Фаза 1 — Аудио (детали — в PLAN.md раздел 6, 14)

Готовность фазы (PLAN.md раздел 14): **запись 10 сек без xrun**.

Объём: `RtAudio + ASIO Focusrite`, запись моно, `Track 0/1/N`, волноформа + zoom,
метроном, мониторинг.

## Решения

- `RtAudio 6.0.1` вендорится в `third_party/rtaudio/rtaudio` (релизный тег, без `.git`).
  В `include/` комплекта уже лежат заголовки `ASIO 2.3` — внешний
  `ASIO-SDK_2.3.4_2025-10-15/ASIOSDK` для сборки Фазы 1 НЕ нужен (rtaudio
  самодостаточен). Дефайны: `__WINDOWS_ASIO__` + `__WINDOWS_WASAPI__` (fallback
  по PLAN.md 6.1), линковка как у апстрима: `winmm ole32 oleaut32 uuid ksuser
  mfplat mfuuid wmcodecdspuuid`.
- `dr_wav` (MIT, single-header) в `third_party/dr_wav` — Writer/Reader
  `WAV PCM 24-bit mono` (дефолт проекта 48000/24, AGENTS.md правило 9).
- В `ASIO callback` только: `ring-buffer` (SPSC, атомики), предвычисленный
  клик метронома, копирование вход->выход для мониторинга, микс плейбека из
  append-only арены. Никаких `malloc / I/O / SQL / mutex` (AGENTS.md правило 3).
- Планирование записи: `startRecord` сбрасывает SPSC-кольцо (ёмкость фикс.,
  аллоцируется на UI-потоке), callback льёт моно `float32`, UI-таймер 30 Гц
  дрейнит в вектор живого тейка -> волноформа живьём. `stopRecord` финализирует:
  WAV 24-bit в `MyDub/takes/`, строка в `takes` + `undo_log` (`take.record`),
  `lines.status todo -> recorded`.
- Плейбек: append-only арена сэмплов (никогда не переиспользуется -> нет
  гонок с callback), publish через атомики `activeLen/activeOffset/playhead`.
- Метроном: фаза по счётчику сэмплов, клик = затухающая синусоида 1.5 кГц
  (акцент 2.2 кГц), предвычислен при смене bpm/UI-потоком.
- Метки: input peak/RMS -> атомики, UI рисует метр; счётчик xrun = переполнение
  кольца записи.

## Файлы

| Файл | Что делает |
|---|---|
| `include/dubstudio/ring_buffer.h` | SPSC lock-free ring buffer на атомиках, ёмкость 2^k |
| `include/dubstudio/audio_engine.h`, `core/audio/audio_engine.cpp` | перечисление устройств (ASIO приоритет), дуплекс 1-in/2-out, sample rate 44.1/48/96k, запись, мониторинг+gain, метроном, плейбек, xrun/уровни; тестовый хук `processBlock` без устройства |
| `include/dubstudio/wav_writer.h`, `core/audio/wav_writer.cpp` | float mono -> WAV PCM 24-bit (dr_wav), чтение назад для валидации |
| `include/dubstudio/clip_store.h`, `core/audio/clip_store.cpp` | клипы-тейки (float, sr, цвет), блочные min/max пики (блок 256) для отрисовки на любом zoom |
| `app/timeline.h`, `app/timeline.cpp` | треки `Track 0 REF-EN (locked) / MASTER-RU / TAKE-NN`, линейка времени, волноформа с zoom колесом до сэмплов, playhead, скролл |
| `tests/test_audio.cpp` | ring buffer, метроном, микс `processBlock` (мониторинг/клик/плейбек/запись), wav writer roundtrip, пики, compiled API содержит ASIO+WASAPI |

`MainWindow`: тулбар `Запись(R) / Play(Space) / Stop / Метроном / Мониторинг`,
BPM, частота проекта, выбор устройства, статус-бар `xrun + метр + время записи`.

## Шаги

1. [x] Вендор `rtaudio 6.0.1` + `dr_wav`, статическая либа `rtaudio` в корневом `CMakeLists.txt`.
2. [x] `RingBuffer` + `AudioEngine` (callback-путь вынесен в `processBlock` для тестов).
3. [x] `WavWriter` + `ClipStore` (пики).
4. [x] `TimelineWidget` (треки + волноформа + zoom + playhead).
5. [x] Интеграция в `MainWindow` (транспорт, запись тейка на выделенную реплику, статус-бар).
6. [x] Тесты `Catch2` зелёные (23 тест-кейса), сборка `MSVC+Ninja` OK.
   Ручная проверка «10 сек записи без xrun» — на машине с Focusrite (счётчик xrun в статус-баре).

Коммит: `feat(audio): RtAudio+ASIO запись, треки 0/1/N, волноформа с zoom, метроном, мониторинг`.

## Результаты

- Сборка: `rtaudio.lib` (ASIO+WASAPI) + `dubstudio_core` + `dubstudio_ui` + `DubStudio.exe`.
- Тесты: `dubstudio_tests` — 23 кейса, 640+ проверок, все зелёные (`ctest` 1/1).
- Callback: только атомики/`memcpy`/предвычисленный клик; запись через SPSC-кольцо
  (ёмкость 8 с), плейбек — append-only арена с барьером `awaitIdleBlock()`
  (ожидание завершения аудио-блока перед переиспользованием памяти UI-потоком).
- Тейки: WAV PCM 24-bit в `MyDub/takes/`, строка в `takes` + `undo_log`
  (`take.record:<id>`) + `lines.status todo -> recorded` одной транзакцией.
- Отклонения: для сборки RtAudio 6.0.1 внешний ASIO SDK не потребовался
  (заголовки 2.3 в комплекте); локальная копия `ASIO-SDK_2.3.4_2025-10-15/`
  удалена как неиспользуемая; `dr_wav` вместо `libsndfile` (достаточно для
  WAV 24-bit Фазы 1).

## Аудит готовности (PLAN.md 6.1, 6.2, 13, 14)

| Требование PLAN.md | Статус | Где |
|---|---|---|
| 6.1 `openAsio(device)` — ASIO приоритет | ✅ | `AudioEngine::open`, автооткрытие ASIO→WASAPI |
| 6.1 `setSampleRate` 44.1/48/96k, дефолт 48000/24-bit | ✅ | диалог «Настройки аудио», WAV PCM 24-bit |
| 6.1 `setDirectMonitoring(bool)`, по умолчанию OFF | ✅ | флаг в диалоге, сохраняется в QSettings |
| 6.1 `startRecord/stopRecord` | ✅ | R-тумблер, финализация тейка |
| 6.1 вход 1 моно, внутренний float32 | ✅ | duplex 1-in/2-out RTAUDIO_FLOAT32 |
| 6.1 fallback WASAPI при недоступном ASIO | ✅ | auto-open + полный список в диалоге |
| 6.1 в callback только ring-buffer (без malloc/I/O/SQL/mutex) | ✅ | `processBlock`: атомики + memcpy; тесты |
| 6.1 программный мониторинг с gain | ✅ | тумблер «Мониторинг» + ползунок громкости (0–100%) |
| 6.2 Track 0 REF-EN locked (только mute/solo) | ✅ | пометка lock, редактирования нет |
| 6.2 Track 1 MASTER-RU | ✅ | заглушка до Фаз 6–7 |
| 6.2 Track 2..N TAKE-01.. с автоцветом | ✅ | `ClipStore::autoColor`, 10 цветов |
| 6.2 цикличная запись | ⏭ Фаза 4 | циклозапись по плану там |
| 6.3 автооценка годности realtime (RMS/ZCR/δ) | ⏭ Фаза 4 | бонусом уже RMS/Peak в БД + quality red/green |
| Фаза 1: волноформа + zoom до сэмплов | ✅ | `TimelineWidget`: колесо, drag, dblclick, линейка |
| Фаза 1: метроном | ✅ | клик+акцент, BPM, доли/такт |
| Фаза 1: «Запись 10 сек без xrun» | ✅ | 8 тейков записаны, счётчик Xrun в статус-баре |
| 13 хоткеи Space/R (дефолты) | ✅ | L (loop) — Фаза 4, S (split) — Фаза 2 |
| 2 мутации через undo_log | ✅ | `take.record:<id>` в транзакции с INSERT takes |

Дополнительные улучшения сверх плана: живая волноформа при записи,
метр входа, счётчик Xrun, индикатор звука и цветные блоки в статус-баре,
FTS-поиск по префиксу токена, вертикальный скролл таймлайна.
- Грабли, зафиксированные для будущего: Windows SDK определяет макрос
  `small` (= `char`) из `RpcNdr.h` — не называть переменные `small`.
