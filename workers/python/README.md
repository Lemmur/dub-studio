# workers/python/ — Python sidecar

Изолированный процесс для ИИ-нагрузок (Фаза 6+), чтобы падения CUDA/GIL/OOM
не роняли ASIO-callback (PLAN.md, раздел 2.2).

Связь с C++ ядром: gRPC + JSON.

Планируемые сервисы:

- `denoise_server.py` — DeepFilterNet3, UVR-MDX (Фаза 6)
- `llm_proxy.py` — унификация OpenRouter/Ollama (опционально, Фаза 5)
- `rvc_server.py` — локальный voice conversion (опционально)

Python: отдельный интерпретатор 3.11 (не системный 3.14) — окружение
`python3.11 -m venv` разворачивается в Фазе 6.
