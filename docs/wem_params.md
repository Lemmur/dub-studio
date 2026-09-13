# Параметры WEM-кодирования

> Черновик. Заполняется в Фазе 0 (сборка sound2wem) и Фазе 3 (валидация цепочки),
> точные аргументы снимаются с `sound2wem --help` и `vgmstream -m` (PLAN.md, разделы 7, 16).

## decode (vgmstream CLI)

```text
TODO (Фаза 0/3): вывод `vgmstream-cli -m <GUID>_en.wem`
```

Целевой `wem_probe_json` в lines.wem_probe_json:

```json
{
  "codec": "vorbis",
  "quality": "high",
  "sample_rate": 44100,
  "channels": 1,
  "encoder": "sound2wem",
  "args": ["--vorbis-quality", "6"]
}
```

## encode (sound2wem)

```text
TODO (Фаза 0): вывод `sound2wem --help`
```

## Валидация encode (Фаза 3)

```text
WAV -> WEM(sound2wem) -> WAV(vgmstream) -> |T_new - T_ref| < 20ms
```
