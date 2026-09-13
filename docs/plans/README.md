# docs/plans/

Правило (AGENTS.md): перед кодом любого модуля — план `docs/plans/<модуль>.md`.

План модуля содержит:

1. Цель и фаза (0-9 из PLAN.md, раздел 14).
2. Интерфейсы (include/rudub/) и точки интеграции.
3. Схема БД/миграции, если меняют schema.sql.
4. Тесты приёмки (Catch2, критерии готовности из PLAN.md).
5. Риски.

Примеры имён: `phase0_skeleton.md`, `phase1_audio_engine.md`, `phase3_wem.md`.
