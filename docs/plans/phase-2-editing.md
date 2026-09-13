# План Фаза 2 — Редактура (детали — в PLAN.md разделы 6.4, 6.5, 14)

Готовность фазы (PLAN.md раздел 14): **откат до старта после рестарта** —
все правки клипа отменяются до исходного состояния тейка даже после
перезапуска приложения (undo-история живёт в БД, PLAN.md 6.5).

Объём: `trim/split/move/fade/crossfade/gain/normalize/silence/reverse`,
`Fit to Ref` (time-stretch с сохранением питча), `Align to Ref`,
`Undo/Redo` + автосейв.

## Решения

- **Time-stretch: свой WSOLA вместо RubberBand.** RubberBand Library —
  GPLv2+/commercial (dual), линковка в ядро запрещена правилом лицензий
  AGENTS.md («GPL только как внешний CLI»). Реализуем in-house WSOLA
  (окно Ханна ~2048, поиск best-offset по нормированной кросс-корреляции,
  overlap-add 50%) — для моно-речи 0.5x–2.0x достаточно. При необходимости
  качественного апгрейда — RubberBand CLI как subprocess (план Фазы 3+,
  по аналогии с vgmstream). Функционально закрывает «RubberBand Fit»:
  растяжение под `T_ref` с сохранением питча.
- **Undo живёт в БД, а не в памяти.** Каждая команда правки пишет в
  `undo_log` снапшот-состояние: `scope='edit'`, `take_id`,
  `state_json` (полные свойства клипа ПОСЛЕ команды + путь к WAV-снапшоту).
  Сэмплы ПОСЛЕ команды — WAV PCM 24-bit в `MyDub/undo/<seq>_<tag>.wav`
  (для property-команд (move/fade/gain) WAV не перезаписывается — путь
  наследуется из предыдущего шага). Откат = восстановление ПРЕДЫДУЩЕГО
  снапшота этого же тейка (или оригинала `takes.file_cas`), переживает
  рестарт. Инвариант: `undone=1` шаги — всегда суффикс последовательности.
- **Модель клипа**: `Clip` получает `startSample` (позиция на таймлайне,
  move/align), `gainDb` (clip-gain, применяется при рендере — не деструктивно),
  `fadeInSamples/fadeOutSamples` (линейные фейды, при рендере). Деструктивные
  операции (trim/silence/reverse/normalize/fit/split) меняют сэмплы —
  всегда через `EditStack`, никогда напрямую.
- **Crossfade** = операция «слить с перекрытием»: два клипа с перекрытием
  по времени сливаются в один с equal-power кривой на зоне перекрытия
  (cos/sin). Также фейды/gain учитываются в `renderMix` (микс всех клипов
  для плейбека). Кроссфейд между дорожками не нужен (треки смешиваются
  суммированием, как в DAW).
- **Trim** двух видов: «обрезать вне диапазона» (курсор+Shift-клик задаёт
  диапазон на выбранном клипе) и «трим тишины по краям» (порог −50 dBFS,
  окно 10 мс — согласован с эвристикой PLAN.md 6.3 «RMS < −50 dB = тишина»).
- **Split** по клику на линейку (курсор) + хоткей `S`, с прилипанием к
  zero-crossing (PLAN.md 6.4). Правый кусок — новый тейк `<id>.<n>`
  (строка в `takes`), undo удаляет его, redo — восстанавливает.
- **Fit to Ref**: цель — `lines.ref_duration_ms` реплики тейка; WSOLA-растяжение
  к целевой длине (кламп коэффициента 0.25–4.0). **Align to Ref**:
  `startSample = 0` (начало референса; REF-EN дорожка появляется в Фазе 3).
- **Автосейв** (PLAN.md 6.5, быстрый уровень): команды и так персистятся
  транзакционно, поэтому снапшот = `PRAGMA wal_checkpoint(TRUNCATE)` +
  `MyDub/manifest.json` (время, счётчики) атомарной записью. Настройка
  «Проект…»: интервал N минут (дефолт 5), флаг «только при изменениях»
  (дефолт вкл). Индикация последнего автосейва в статус-баре. Полный
  чекпоинт CAS — Фаза 8, crash-диалог — Фаза 9.
- **Загрузка сессии**: на старте тейки восстанавливаются из `takes` +
  последний неотменённый `state_json` каждого тейка (это и даёт состояние
  на момент закрытия). Хоткеи PLAN.md 13: `S` split, `Ctrl+Z/Ctrl+Y`
  undo/redo (Space/R из Фазы 1; `L` — Фаза 4).
- Отмена записи тейка (`take.record` Фазы 1) — вне объёма Фазы 2,
  тейк-менеджмент по плану Фазы 4; undo Фазы 2 откатывает правки редактуры
  до состояния сразу после записи.

## Схема БД (миграция)

`undo_log` расширяется колонками (schema.sql для новых БД + `ALTER TABLE`
для существующих, проверка через `pragma table_info`):

```sql
scope      TEXT DEFAULT '',   -- 'edit' для правок Фазы 2
take_id    TEXT DEFAULT '',   -- затронутый тейк
state_json TEXT DEFAULT ''    -- снапшот для undo/redo
```

`state_json`: `{ "clip": {props, "wav": "..."}, "created": {…} }` —
`created` только для split (новый тейк + его строка `takes` для redo).

## Файлы

| Файл | Что делает |
|---|---|
| `include/dubstudio/audio_ops.h`, `core/audio/audio_ops.cpp` | чистые DSP-операции: normalizePeak, silenceRange, reverse, trim, trimSilenceEdges (−50 dBFS), zero-crossing snap, renderClip (gain+фейды), renderMix, crossfadeMerge (equal-power), WSOLA timeStretch |
| `include/dubstudio/edit_stack.h`, `core/project/edit_stack.cpp` | Command-стек правок: apply/undo/redo, снапшоты в `MyDub/undo/`, транзакции `undo_log`+`takes`, loadSession (рестарт), autosaveSnapshot (manifest+checkpoint) |
| `include/dubstudio/clip_store.h` | `Clip`: `startSample`, `gainDb`, `fadeIn/fadeOut`; `removeTakeById`, `takeIndexById` |
| `app/timeline.h/.cpp` | клип рисуется по `startSample` (не от 0), выбор клипа кликом, drag-move, курсор (клик по линейке) + диапазон (Shift-клик), отрисовка фейдов/выделения |
| `app/mainwindow.h/.cpp` | меню «Правка» + хоткеи (S, Ctrl+Z/Y), диалоги Нормализация/Усиление, Fit to Ref / Align (тулбар), автосейв-таймер + «Настройки → Проект…», загрузка сессии, плей через `renderMix` |
| `tests/test_editing.cpp` | DSP-операции; EditStack: apply/undo/redo, split, fit; готовность — «рестарт» (новый EditStack на той же БД) и откат до старта; миграция undo_log; manifest автосейва |

## Шаги

1. [x] Схема: колонки `undo_log` + миграция старых БД (индекс не добавляли —
   `CREATE INDEX` по новой колонке в `schema.sql` ломал бы открытие старых баз
   до миграции).
2. [x] `audio_ops` (чистые функции) + юнит-тесты.
3. [x] `Clip`-поля + `EditStack` (apply/undo/redo/loadSession/autosave).
4. [x] Timeline: позиция клипа, выбор/курсор/диапазон/move-drag.
5. [x] MainWindow: меню «Правка», Fit/Align, автосейв, сессия, микс-плейбек.
6. [x] Тесты зелёные, сборка MSVC+Ninja.

Коммит: `feat(editing): редактура клипов, Fit/Align, undo через рестарт, автосейв`.

## Результаты

- Сборка: `MSVC + Ninja` ок; тесты `dubstudio_tests` — 39 кейсов, все зелёные.
- Готовность фазы подтверждена тестом «ГОТОВНОСТЬ Фазы 2»: 7 команд правок ->
  полное пересоздание объектов (симуляция рестарта) -> `loadSession` -> 7 undo ->
  побитовое равенство исходному тейку; redo после рестарта работает.
- Отклонения от PLAN.md: RubberBand заменён на собственный WSOLA
  (GPL-библиотеку в ядро линковать нельзя, см. «Решения» и
  [`docs/licenses.md`](licenses.md)); `pitch-nudge`, `cut/copy/paste`,
  `DeEss/DePlosive` — вне объёма строки Фазы 2 таблицы PLAN.md 14
  (Comp to Master — Фаза 4, DeEss — Фаза 6).
- Undo записи тейка (`take.record`) — Фаза 4; undo Фазы 2 откатывает правки
  до состояния сразу после записи.
- По итогам ручного тестирования (чек-лист [`phase-2-checklist.md`](phase-2-checklist.md))
  добавлено сверх плана: удаление тейка целиком (кнопка «✕», команда DeleteTake,
  undo-восстановление, синхронизация `lines.status` с наличием тейков),
  фильтр таймлайна по выбранной реплике, rubber-band выделение на дорожке
  с авто-выбором тейка, контекстное меню правок (ПКМ в диапазоне), курсор
  у волны (мини-линейки, тянущийся курсор), `Ctrl+ЛКМ`-move клипа,
  сохранение выделения строки таблицы и раскладки интерфейса между
  запусками, id тейков `<wem_hash>_take_<N>`.

## Аудит готовности (PLAN.md 6.4, 6.5, 13, 14)

| Требование PLAN.md | Статус | Где |
|---|---|---|
| 6.4 `trim` (диапазон + тишина по краям) | ✅ | `EditType::TrimRange/TrimSilence` (−50 dBFS) |
| 6.4 `split at cursor` + snap zero-crossing | ✅ | `EditType::Split`, `snapToZeroCrossing`, хоткей `S` |
| 6.4 `move` | ✅ | drag клипа в таймлайне + `EditType::Move` |
| 6.4 `fade in/out`, `crossfade` | ✅ | фейды до/от курсора; equal-power слияние клипов |
| 6.4 `gain`, `normalize peak` | ✅ | clip-gain (при рендере), нормализация по пику (диалог) |
| 6.4 `silence`, `reverse` | ✅ | диапазон или весь клип |
| 6.4 time-stretch с сохранением питча | ✅ | WSOLA (RubberBand GPL — не линкуем) |
| 6.4 `Fit to Ref` | ✅ | цель `lines.ref_duration_ms`, кламп 0.25–4.0 |
| 6.4 `Align to Ref` | ✅ | `startSample = 0` (начало референса) |
| 6.5 Command + `undo_log`, откат до старта, переживает рестарт | ✅ | `EditStack`, снапшоты `state_json` + `MyDub/undo/*.wav` |
| 6.5 автосейв каждые N мин, «только при изменениях» | ✅ | «Настройки → Проект…», checkpoint + `manifest.json` |
| 13 хоткеи `S`, `Ctrl+Z/Ctrl+Y` | ✅ | дефолты PLAN.md (`L` — Фаза 4) |
| 14 «Откат до старта после рестарта» | ✅ | тест `EditStack: ГОТОВНОСТЬ Фазы 2` |
| 2 мутации через Command + undo_log | ✅ | все правки + `take.record` Фазы 1 в логе |
| Удаление тейка (сверх плана, по итогам ручного теста) | ✅ | `DeleteTake` + кнопка «✕», undo восстанавливает |
| 13 компоновка панелей сохраняемая | ✅ | `QMainWindow::saveState/restoreState` в QSettings |
| Удаление тейка (сверх плана) | ✅ | DeleteTake + кнопка «✕», undo восстанавливает |
| 13 компоновка панелей сохраняемая | ✅ | saveState/restoreState в QSettings |
