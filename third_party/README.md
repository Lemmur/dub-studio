# third_party/

Внешние зависимости, не входящие в систему сборки ядра.

| Каталог | Что | Когда | Как |
|---|---|---|---|
| `asio_sdk/` | Steinberg ASIO SDK | — | не нужен: RtAudio вендорит заголовки ASIO 2.3; в `.gitignore` оставлен на случай возврата |
| `rtaudio/` | RtAudio 6.0.1 (MIT, vendored) | Фаза 1 | клон тега `6.0.1`, ASIO-заголовки в комплекте |
| `dr_wav/` | dr_wav (MIT-0, single header) | Фаза 1 | `dr_wav.h` |
| `sound2wem/` | кодер WEM (EternalLeo) | Фаза 0 | `git clone https://github.com/EternalLeo/sound2wem`, сборка exe, лицензию — в docs/licenses.md |

vgmstream — внешний CLI, в репо не входит (положить рядом при Фазе 3 и прописать путь в настройках).
