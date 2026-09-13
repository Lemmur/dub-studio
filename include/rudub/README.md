# include/rudub/

Публичные интерфейсы модулей (AGENTS.md: «include/ интерфейсы, core/ реализация»).

Сюда кладутся заголовки вида `include/rudub/<модуль>/<файл>.h`,
например `include/rudub/plugin.h` (PLAN.md, раздел 3), `include/rudub/audio_engine.h`
(PLAN.md, раздел 6.1), `include/rudub/llm_provider.h` (раздел 8.2).

Реализации — в `core/<модуль>/`, потребители линкуются к целям
`rudub_project`, `rudub_audio`, `rudub_text` (include уже прописан в target_include_directories).
