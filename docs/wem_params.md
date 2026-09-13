# Параметры WEM (`vgmstream` / `sound2wem`)

Заполняется в Фазе 0 (п.5) и уточняется в Фазе 3.

## Что снять с инструментов

1. `vgmstream -m <GUID>_en.wem` на одном реальном `.wem` → вписать сюда `codec`, `sample_rate`, `channels`, `vorbis_quality`.
2. `sound2wem --help` → вписать сюда точные `args` кодирования (`Vorbis HQ`, см. [`PLAN.md`](PLAN.md:395)).

## Шаблон `wem_probe_json` (хранится в `lines.wem_probe_json`)

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

## Валидация encode (Фаза 3)

```text
WAV -> WEM(sound2wem) -> WAV(vgmstream) -> |T_new - T_ref| < 20ms
```

Пока не сняты выводы инструментов — секция пуста, это нормально.
