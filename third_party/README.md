# third_party/

Внешние зависимости, не входящие в систему сборки ядра.

| Каталог | Что | Когда | Как |
|---|---|---|---|
| `asio_sdk/` | Steinberg ASIO SDK | Фаза 1 | **скачать вручную с сайта Steinberg**, НЕ коммитить (в .gitignore) |
| `rtaudio/` | RtAudio (MIT) | Фаза 1 | `git clone https://github.com/thestk/rtaudio` |
| `sound2wem/` | кодер WEM (EternalLeo) | Фаза 0 | `git clone https://github.com/EternalLeo/sound2wem`, сборка exe, лицензию — в docs/licenses.md |

vgmstream — внешний CLI, в репо не входит (положить рядом при Фазе 3 и прописать путь в настройках).
