# Параметры WEM (`vgmstream` / `sound2wem`)

Фаза 0: зафиксировано фактическое состояние инструментов. Уточнение — в Фазе 3.

## Важное открытие Фазы 0: `sound2wem` — это скрипт, а не кодер

[`third_party/sound2wem`](../third_party/sound2wem) (репо `EternalLeo/sound2wem`, v6 от
2026-01-02, лицензия `MPL-2.0`) содержит единственный инструмент —
[`zSound2wem.cmd`](../third_party/sound2wem/zSound2wem.cmd), батник-обёртку:

```text
вход (.wav/.mp3/.ogg/...)
  -> FFmpeg          (нормализация в .wav; ставится скриптом автоматически)
  -> WwiseConsole.exe (Собственно кодирование в .wem, проприетарный Wwise Authoring)
  -> .wem
```

Следствия (уточняют PLAN.md п.7 и раздел 16):

1. «Собрать `sound2wem.exe`» невозможно — нет исходников для компиляции; скрипт и есть
   инструмент. Вендорим его как есть.
2. Кодирование всё равно зависит от установленного `Wwise Authoring`. На этой машине
   он есть: `WWISEROOT=C:\Audiokinetic\Wwise_2026.1.3.9276`,
   `WwiseConsole.exe = %WWISEROOT%\Authoring\x64\Release\bin\WwiseConsole.exe` (проверено).
3. У `zSound2wem.cmd` НЕТ ключа `--help`: параметры перечислены в `README.md` репо и в
   комментариях внутри скрипта. Справка снята из них (ниже).
4. Первый запуск скрипта интерактивен (установка FFmpeg/7-zip) — для Фазы 3 нужен
   headless-прогон один раз, чтобы зависимости установились.

## Параметры `zSound2wem.cmd` (из README v6)

Синтаксис не `--flag value`, а `--flag:value` (значения с пробелами — в кавычки):

```text
--ffmpeg       | путь к ffmpeg.exe
--wwise        | путь к WwiseConsole.exe
--samplerate   | частота, напр. 48000 (Гц)
--channels     | каналы: 1, 2
--volume       | громкость: 1.5 / 0.5 или 10dB / -5dB
--extra        | доп. флаги ffmpeg
--conversion   | конверсия Wwise, напр. "Vorbis Quality High" | "Vorbis Quality Medium" | "Vorbis Quality Low"
--out          | выходная папка
--audioformats | расширения при передаче папки, напр. ".wav .mp3"
```

Конфиг по умолчанию внутри скрипта: `conversion=Vorbis Quality High`, `samplerate=(как
в источнике)`, `channels=(как в источнике)`.

Пример: `zSound2wem.cmd --volume:6dB "--conversion:Vorbis Quality High" "take.wav"`

## Планируемый вызов из DubStudio (Фаза 3)

```text
MASTER-RU FLAC -> SoXR resample 48000/mono
  -> temp WAV float32
  -> zSound2wem.cmd --wwise:%WWISEROOT%\Authoring\x64\Release\bin\WwiseConsole.exe
                    --samplerate:48000 --channels:1 "--conversion:Vorbis Quality High"
                    --out:<tmp> <wav>
  -> валидация: vgmstream decode -> |T_new - T_ref| < 20ms
```

Ожидаемый `wem_probe_json` (проверить в Фазе 3 на реальном файле):

```json
{
  "codec": "vorbis",
  "quality": "high",
  "sample_rate": 48000,
  "channels": 1,
  "encoder": "sound2wem",
  "args": ["--samplerate:48000", "--channels:1", "--conversion:Vorbis Quality High"]
}
```

## Что ещё снять в Фазе 3

1. `vgmstream -m <GUID>_en.wem` на одном реальном `.wem` → `codec`, `sample_rate`,
   `channels`, `vorbis_quality` — записать сюда.
2. Реальный прогон `zSound2wem.cmd` (после автоустановки FFmpeg) и сравнение
   `probe` входа/выхода.
3. Решить риск: зависимость кодирования от проприетарного Wwise — альтернативы
   (`ww2ogg` обратный путь невозможен; самописный кодер отклонён в PLAN.md).

## Валидация encode (Фаза 3)

```text
WAV -> WEM(zSound2wem.cmd) -> WAV(vgmstream) -> |T_new - T_ref| < 20ms
```
