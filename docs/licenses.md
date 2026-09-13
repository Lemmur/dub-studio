# Лицензии зависимостей RuDub Studio

Правило (PLAN.md, раздел 1.2 №13): в линковке ядра — только open-source.
GPL в ядро не линковать; GPL-инструменты — только внешний CLI (subprocess).

| Компонент | Лицензия | Тип интеграции | Статус |
|---|---|---|---|
| Qt 6.5.3 LTS (Widgets, Sql, Network, Svg, Tools) | LGPL-3.0 / GPL-3.0 (dual) | линковка (LGPL) | установлен D:\Qt |
| RtAudio | MIT | линковка (Фаза 1) | клонировать в third_party |
| Steinberg ASIO SDK | проприетарная (Steinberg) | внешний SDK, **не коммитить** | скачать вручную |
| libsndfile | LGPL-2.1+ | линковка (vcpkg, Фаза 1) | — |
| SoXR | LGPL-2.1+ | линковка (vcpkg, Фаза 2) | — |
| RubberBand | GPL-2.0+/**коммерческая** | ⚠ проверить совместимость (Фаза 2) | — |
| Eigen | MPL-2 | header-only (vcpkg) | — |
| Catch2 | BSL-1.0 | тесты | — |
| nlohmann::json | MIT | header-only | — |
| dr_wav | MIT-0 | header-only (Фаза 1) | — |
| vgmstream | LGPL-2.1+ (CLI) | **внешний CLI**, не линковать | — |
| sound2wem | по лицензии репозитория EternalLeo/sound2wem | сборка из исходников, subprocess | клонировать в Фазе 0, текст лицензии добавить сюда |
| DeepFilterNet3 | по лицензии проекта | Python sidecar (Фаза 6) | — |
| Python 3.11 + gRPC | PSF / Apache-2.0 | sidecar-процесс | — |

> ⚠ RubberBand: GPL — допускаем только вызов как внешний CLI/отдельный процесс,
> либо коммерческая лицензия. Решение зафиксировать до Фазы 2 в docs/plans/.
