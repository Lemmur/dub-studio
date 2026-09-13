# plugins/

Офлайн ИИ-эффекты как плагины: новый эффект = папка `plugins/<name>/` +
`manifest.json` + python-скрипт, ядро не пересобирается (PLAN.md, раздел 3).

Манифест:

```json
{
  "id": "denoise.deepfilternet3",
  "kind": "offline",
  "entry": "denoise_server.py"
}
```

Первый плагин — DeepFilterNet3 (Фаза 6).
