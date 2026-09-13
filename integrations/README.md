# integrations/

Внешние REST/gRPC-интеграции (L1 Infra, PLAN.md раздел 3).

- `openrouter/` — ILlmProvider -> OpenRouter (Фаза 5); настройка BaseURL + API Key + Model ID
- `ollama/` — ILlmProvider -> локальный Ollama http://localhost:11434 (Фаза 5)
- `elevenlabs/` — STS/TTS, voice_map, кэш sha256, кредиты GET /v1/user (Фаза 7)
