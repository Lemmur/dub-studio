# `DubStudio` — полный файл аналитики и проекта системы дубляжа

> Единый консолидированный документ. Содержит все согласованные решения.
> Масштаб: `1 проект = 1 игра`, формат `combined.json`, `42 файла / 2181 сцена / 39481 реплика`, референсы `<hash>_en.wav` (конвертация `WEM->WAV` вручную), экспорт `WEM Vorbis HQ` через `sound2wem`, `ASIO Focusrite`, `Windows`, `open-source`, `агентная разработка`.

---

## 1. Паспорт проекта

### 1.1 Назначение

Профессиональная `DAW` для сольного дубляжа игр `EN -> RU` в домашних условиях. Референс UX — `Cubase`, частично `Audacity`. Только русский интерфейс. Тёмная тема по умолчанию.

### 1.2 Зафиксированные требования

| # | Область | Решение |
|---|---|---|
| 1 | Платформа | Только `Windows 10/11 x64` |
| 2 | Масштаб | $N = 39481$ реплика, $2181$ сцена, $42$ файла, $T_{total} \approx 30\text{-}36$ часов |
| 3 | Входной формат | `combined.json`: `{file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}` + файлы `<hash>_en.wav` (конвертация из `.wem` вручную вне приложения) |
| 4 | Запись | $1$ микрофон, `Focusrite USB ASIO`, частота проекта настраиваемая, `Direct Monitoring` есть, по умолчанию `OFF` |
| 5 | Треки | `Track 0 REF-EN` locked + `Track 1 MASTER-RU` + `Track 2..N` тейки, цикличная запись, автоцвет |
| 6 | Редактирование | Волноформа до сэмплов, `trim/split/move/fade/crossfade/gain/normalize/silence/reverse/time-stretch`, `Fit to Ref`, `Comp to Master`, `Undo/Redo` с открытия сессии |
| 7 | `WEM` | `decode` не нужен: импорт готовых `<hash>_en.wav`; `encode` через `sound2wem`, прямая подгрузка `.wav` в таймлайн, `batch` |
| 8 | Текст | Интерактивная `LLM`-адаптация, слоги + смычные + виземы, история `base / adapted / final` |
| 9 | Очистка | `Offline` цепочка, `DeepFilterNet3` по умолчанию, пресеты под `8GB / 16GB VRAM` |
| 10 | `ElevenLabs` | `STS` + `TTS`, клон голоса персонажа, обязательный кэш `sha256`, виджет кредитов |
| 11 | Проект | Рабочая директория `MyDub/` + экспорт `.dubpack`, `FLAC + ZSTD-12 + AES-GCM`, `CAS`, инкрементальность |
| 12 | Режимы | Только `online`, без фолбэков. `Batch` обязателен. Автосейв настраиваемый |
| 13 | Лицензии | Только `open-source` в линковке. `ASIO SDK 2.3.4` уже скачан локально в `ASIO-SDK_2.3.4_2025-10-15/ASIOSDK` (dual license: Proprietary / GPLv3, не коммитить), `sound2wem` как внешний исходник |
| 14 | Разработка | Агентные ИИ, `C++20 + Qt6 + Python sidecar`, модульность через плагины, фазы `0-9` строго по порядку |

### 1.3 Железо

- Сейчас: `RTX 4060 Laptop 8GB VRAM`.
- Позже: `RTX 4080 Super 16GB VRAM`.
- `CPU i9-14900K + 64GB RAM`.

Все локальные модели имеют два пресета: `fast-8GB` и `quality-16GB`.

---

## 2. Технологический стек

### 2.1 Ядро `C++20`

```cmake
C++20 + Qt 6.5 LTS Widgets + CMake + Ninja + SQLite WAL
Аудио I/O: RtAudio (MIT) + Steinberg ASIO SDK 2.3.4 (внешний, уже скачан в ASIO-SDK_2.3.4_2025-10-15/ASIOSDK)
Файлы: libsndfile + dr_wav
Ресемплинг: SoXR
Time-stretch: RubberBand
Математика: Eigen
Тесты: Catch2
JSON: nlohmann::json (ordered_json для сохранения порядка)
Сеть: Qt Network (REST к OpenRouter / ElevenLabs)
```

Почему `RtAudio`, а не `JUCE`:

- `JUCE 8` втягивает `GPLv3` без коммерческой лицензии, конфликт с требованием open-source и командной работы.
- `RtAudio` под флагом `__WINDOWS_ASIO__` даёт `ASIO` из коробки, лицензия `MIT`.
- Волноформы, `Thumbnail`-кэш и `SampleView` пишутся на `Qt + libsndfile` за $1$-$2$ итерации агента.

### 2.2 `Python 3.11 sidecar`

```text
C++ ядро <-- gRPC / ZeroMQ + JSON --> workers/python/
  denoise_server.py   # DeepFilterNet3, UVR-MDX
  llm_proxy.py        # унификация OpenRouter / Ollama (опционально)
  rvc_server.py       # опциональный локальный voice conversion
```

Изоляция обязательна чтобы падение `CUDA / GIL / OOM` не роняло `ASIO callback` во время записи.

### 2.3 Внешние инструменты

| Задача | Инструмент | Тип интеграции |
|---|---|---|
| `WEM -> WAV` | вручную, вне приложения | в ядро не интегрируется |
| `WAV -> WEM` | `sound2wem` (`https://github.com/EternalLeo/sound2wem`) | сборка из исходников в `third_party/sound2wem`, вызов как `subprocess` |
| `LLM cloud` | `OpenRouter API` | `REST`, `BaseURL + Key + Model` |
| `LLM local` | `Ollama` | тот же интерфейс, `http://localhost:11434` |
| `Voice` | `ElevenLabs API` | `REST`: `STS + TTS + GET /v1/user` |

<details>
<summary>Почему не Python / Electron / C# для ядра</summary>

- `Python`: джиттер `GC`, невозможен детерминированный `ASIO` callback $<10ms$.
- `Electron`: задержки UI, тормоза волноформы на $36$ часах аудио.
- `C# + NAudio`: всё равно нужен native `ASIO` и `DSP`, зрелых либ меньше чем под `C++`.
- `Qt Widgets + C++`: нативный докинг в стиле `Cubase`, виртуальная таблица на $39481$ строк, `QSS`-темы, `QDockWidget`.
</details>

---

## 3. Архитектура системы

```text
+----------------------------------------------------------+
| L4 UI (Qt): LineList | Timeline | ClipEditor | LLM | Jobs |
|             Transport | Hotkeys | Settings | Credits        |
+--------------------+---------------------------------+
| L3 App: ProjectManager | TakeManager | UndoStack | JobQueue |
|         Search FTS | Autosave | ExportPack | VoiceMap       |
+--------------------+---------------------------------+
| L2 Domain: AudioEngine(RtAudio) | ClipStore | TextEngine |
|            LipSyncAnalyzer | QualityScorer | WemBridge  |
+--------------------+---------------------------------+
| L1 Infra: PythonBridge | ElevenLabsClient | OpenRouter |
|           FileStore CAS | Crypto AES-GCM + Argon2id       |
+----------------------------------------------------------+
| Python sidecar: DeepFilterNet3 | UVR | llm_proxy | RVC opt |
+----------------------------------------------------------+
| External CLI: sound2wem (encode only)                        |
+----------------------------------------------------------+
```

Поток одной реплики:

```text
combined.json + <hash>_en.wav -> lines.db -> импорт WAV Job -> FLAC CAS
 -> REF-EN Track0 -> запись Take -> автоцвет RMS/ZCR/delta
 -> denoise offline -> STS/TTS ElevenLabs -> MASTER-RU
 -> Fit to Ref -> sound2wem encode -> валидация
```

Принцип плагина для ИИ-эффектов:

```cpp
// include/dubstudio/plugin.h
struct ProcessorDesc {
  std::string id;       // "denoise.deepfilternet3"
  std::string kind;     // "offline"
  std::string manifest; // "plugins/denoise/manifest.json"
};

class IAudioProcessor {
public:
  virtual ~IAudioProcessor() = default;
  virtual Job submit(const Job& req) = 0;
};
```

Новый эффект = папка `plugins/<name>/` + `manifest.json` + `python`-скрипт. Ядро не пересобирается.

---

## 4. Входной формат `combined.json`

Фактическая структура:

```json
{
  "q000_intro": {
    "cs_q000_1_opening": {
      "6046256F4DF7E505F0906FBE58C09951": {
        "en": "Oh! Coen, look!",
        "ru": "О! Коэн, смотри!",
        "speaker_name": "Lunka",
        "speaker_internal": "Character.Secondary.Lunka",
        "dur": 1.861
      }
    }
  }
}
```

Уровни:

- `L1 file_id`: $42$ ключа, например `q000_intro`, `sq708_ambrus`, `_uncovered_whispers`. Единица поставки и `batch`.
- `L2 quest_id`: $2181$ сцена, например `cs_q000_1_opening`, `ambrus_whispers`.
- `L3 GUID`: $39481$ реплика, `32-hex` (он же `hash`), имя файла референса — `<hash>_en.wav` (получен ручной конвертацией из `<hash>_en.wem`).
- `L4` поля: `en`, `ru`, `speaker_name`, `speaker_internal`, `dur` float в секундах.

Особые кейсы:

- Пустой `speaker_name` → `UNKNOWN`, `speaker_confidence = 0.0`.
- `speaker_name` вида `"Ambrus (whisper)"` хранить целиком для отображения. Для `voice-map` чистить суффикс в скобках, а сам суффикс маппить в пресет параметров.
- Порядок ключей `JSON` = порядок в сцене. Требовать `ordered_json` при парсинге.

---

## 5. База проекта `lines.db`

### 5.1 Схема

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS game_files (
  file_id TEXT PRIMARY KEY,
  order_index INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS quests (
  quest_id TEXT PRIMARY KEY,
  file_id TEXT NOT NULL REFERENCES game_files(file_id),
  order_index INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS lines (
  line_pk INTEGER PRIMARY KEY AUTOINCREMENT,
  file_id TEXT NOT NULL,
  quest_id TEXT NOT NULL REFERENCES quests(quest_id),
  order_index INTEGER DEFAULT 0,
  wem_hash TEXT NOT NULL UNIQUE,
  speaker_name TEXT DEFAULT 'UNKNOWN',
  speaker_internal TEXT DEFAULT '',
  speaker_confidence REAL DEFAULT 1.0,
  ref_duration_ms INTEGER DEFAULT 0,
  ref_audio_cas TEXT DEFAULT '',
  wem_probe_json TEXT DEFAULT '{}',
  status TEXT DEFAULT 'todo'
    CHECK(status IN ('todo','recorded','in_review','done')),
  created_at INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX idx_lines_file ON lines(file_id);
CREATE INDEX idx_lines_quest ON lines(quest_id);
CREATE INDEX idx_lines_speaker ON lines(speaker_name);
CREATE INDEX idx_lines_status ON lines(status);

CREATE TABLE IF NOT EXISTS texts (
  wem_hash TEXT PRIMARY KEY REFERENCES lines(wem_hash),
  en_original TEXT DEFAULT '',
  ru_base TEXT DEFAULT '',
  ru_adapted TEXT DEFAULT '',
  ru_final TEXT DEFAULT '',
  en_syllables INTEGER DEFAULT 0,
  ru_syllables INTEGER DEFAULT 0,
  fit_score REAL DEFAULT 0.0
);

CREATE TABLE IF NOT EXISTS text_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  wem_hash TEXT NOT NULL,
  ver INTEGER NOT NULL,
  who TEXT NOT NULL,
  text TEXT NOT NULL,
  created_at INTEGER DEFAULT (strftime('%s','now'))
);

CREATE VIRTUAL TABLE IF NOT EXISTS lines_fts USING fts5(
  wem_hash, speaker_name, en_original, ru_final
);

CREATE TABLE IF NOT EXISTS takes (
  take_id TEXT PRIMARY KEY,
  wem_hash TEXT NOT NULL REFERENCES lines(wem_hash),
  file_cas TEXT NOT NULL,
  duration_ms INTEGER DEFAULT 0,
  quality TEXT DEFAULT 'yellow',
  rms_db REAL DEFAULT -99.0,
  peak_db REAL DEFAULT -99.0,
  is_master_candidate INTEGER DEFAULT 0,
  comment TEXT DEFAULT ''
);

CREATE TABLE IF NOT EXISTS voice_map (
  speaker_internal TEXT PRIMARY KEY,
  voice_id TEXT NOT NULL,
  preset_json TEXT DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS jobs (
  job_id TEXT PRIMARY KEY,
  type TEXT NOT NULL,
  wem_hash TEXT NOT NULL,
  params_json TEXT DEFAULT '{}',
  cache_key TEXT DEFAULT '',
  status TEXT DEFAULT 'queued',
  progress_pct INTEGER DEFAULT 0,
  log TEXT DEFAULT ''
);
CREATE INDEX idx_jobs_status ON jobs(status);

CREATE TABLE IF NOT EXISTS undo_log (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  created_at INTEGER DEFAULT (strftime('%s','now')),
  action TEXT NOT NULL,
  undone INTEGER DEFAULT 0
);
```

### 5.2 Импортёр

```python
# ТЗ для агента: core/project/importer.cpp
import json
data = json.load(open('combined.json'))
for fi, (file_id, scenes) in enumerate(data.items()):
  db.execute("INSERT OR IGNORE INTO game_files VALUES(?,?)", (file_id, fi))
  for qi, (quest_id, repliki) in enumerate(scenes.items()):
    db.execute("INSERT OR IGNORE INTO quests VALUES(?,?,?)", (quest_id, file_id, qi))
    batch_lines, batch_texts = [], []
    for oi, (guid, r) in enumerate(repliki.items()):
      dur_ms = int(float(r.get('dur', 0)) * 1000)
      speaker = r.get('speaker_name') or 'UNKNOWN'
      conf = 0.0 if speaker == 'UNKNOWN' else 1.0
      batch_lines.append((file_id, quest_id, oi, guid, speaker,
                          r.get('speaker_internal',''), conf, dur_ms))
      batch_texts.append((guid, r.get('en',''), r.get('ru',''), r.get('ru','')))
    db.executemany("INSERT OR IGNORE INTO lines ...", batch_lines)
    db.executemany("INSERT OR REPLACE INTO texts ...", batch_texts)
```

Требования: батч по $1000$ в транзакции, `WAL`, target импорт $39481$ строк за $<3$ сек.

---

## 6. Аудио-ядро и запись

### 6.1 Движок

```cpp
// include/dubstudio/audio_engine.h
class AudioEngine {
public:
  bool openAsio(const std::string& device);
  void setSampleRate(int sr);
  void setDirectMonitoring(bool on);
  void startRecord(TrackId id);
  void stopRecord();
};
```

- Вход $1$ моно, внутренний формат `float32`.
- Частота проекта настраиваемая: `44100 | 48000 | 96000`, по умолчанию `48000 / 24-bit`.
- `Direct Monitoring`: флаг есть, по умолчанию `OFF`. При `OFF` — программный мониторинг с `gain`, без эффектов на входе.
- `Fallback`: `WASAPI exclusive` если `ASIO` недоступен.
- Железное правило для агента: в `ASIO callback` только `ring-buffer`, никакого `malloc`, `I/O`, `SQL`, `mutex`.

### 6.2 Треки и тейки

```text
Track 0 [REF-EN]    locked, только mute/solo, не редактируется
Track 1 [MASTER-RU] финал после cleanup + STS/TTS, сюда Comp
Track 2..N [TAKE-01..] цикличная запись, каждый цикл = новый трек
```

Цикличная запись: `Loop + Record`, каждый проход пишет новый `TAKE-N` синхронно с `REF-EN`. Все тейки хранятся в проекте. Удаление только вручную или через меню `Удалить точно бракованные`.

### 6.3 Автооценка годности в реальном времени

Окно $512$ сэмплов, только эвристики:

- $RMS < -50dBFS$ — пропуск / тишина.
- $peak > -1dBFS$ — клиппинг.
- Всплеск $ZCR$ + просадка $SNR$ — задел микрофон, взрывная `п/б`.
- $\delta = |T_{take} - T_{ref}| / T_{ref}$: $\delta > 0.25$ жёлтый, $\delta > 0.40$ красный.

Цвет: зелёный годен, жёлтый проверить, красный брак. Это подсказка, вердикт за актёром.

### 6.4 Редактирование

Обязательный `MVP`:

```text
cut / copy / paste, trim, split at cursor, move,
fade in/out, crossfade, gain, normalize peak/LUFS,
silence, reverse, time-stretch RubberBand, pitch-nudge
```

Специально для дубляжа:

```text
Fit to Ref       авто time-stretch под T_ref с сохранением питча
Align to Ref     привязка начала клипа к началу референса по хоткею
DeEss / DePlosive DSP-пресеты 7-8kHz + HPF 80Hz
Comp to Master   отправить выделение в MASTER-RU с кроссфейдом
```

Волноформа с `zoom` до сэмплов как в `Audacity`, `snap to zero-crossing`, кастомный `SampleView` на `Qt`.

### 6.5 `Undo/Redo` + автосейв

- Паттерн `Command`, запись в `undo_log` с открытия сессии. Откат до старта, переживает рестарт.
- Автосейв: `Настройки -> Проект -> каждые N минут`, дефолт $5$ мин, флаг `только если были изменения`.
- Два уровня: быстрый снапшот `WAL + manifest.json + tmp/` за $1$-$2$ сек (только между тейками, никогда внутри callback) и полный чекпоинт `CAS`.
- При краше: `Найден снапшот от 14:32. Восстановить?` с diff по числу реплик.

---

## 7. Импорт `<hash>_en.wav` и экспорт `WEM` через `sound2wem`

`WEM -> WAV` ядро НЕ выполняет: конвертация делается вручную вне приложения (любым проверенным декодером), один раз до старта работы. Ядро импортирует только готовые `<hash>_en.wav`.

```text
<hash>_en.wav -> libsndfile / dr_wav -> float32 -> FLAC CAS
probe WAV (libsndfile) -> wem_probe_json {codec, sample_rate, channels, bits}
drop .wav в таймлайн -> фоновый import Job -> клип
MASTER-RU FLAC -> SoXR resample -> sound2wem encode -> валидация
Batch окно: папка -> очередь jobs -> лог
```

Пример `wem_probe_json` (probe импортированного WAV, имя колонки в БД сохранено):

```json
{
  "source": "wav",
  "codec": "pcm_s24le",
  "sample_rate": 48000,
  "channels": 1,
  "duration_ms": 1861
}
```

Точные `args` кодера агент снимает из `sound2wem --help` и фиксирует в `docs/wem_params.md`.

Валидация encode без внешнего декодера: читаем длительность из заголовка полученного `WEM` (`fmt`-чанк), полный decode не нужен:

```text
WAV -> WEM(sound2wem) -> parse WEM header -> |T_new - T_ref| < 20ms
```

<details>
<summary>Почему именно эта связка</summary>

`WEM -> WAV` вынесен из ядра: конвертация нужна один раз и делается вручную, ядро не тащит `GPL`-декодер и лишний `subprocess`. Самописный `Vorbis -> WEM` даст файл который движок отвергнет. `Wwise CLI` проприетарный и неудобен для агентов. `sound2wem` — открытый кодер, собирается из исходников, параметры задаются явно. Для проверки результата достаточно длительности из заголовка `WEM`.
</details>

---

## 8. Текст, слоги, липсинк, `LLM`

### 8.1 Подсчёт

- `EN`: `syllapy / espeak` + `CMU phonemes -> visemes`.
- `RU`: `pyphen-ru + словарь ударений` + `RU phonemes -> visemes`.
- Метрика:

$$fit = w_1 \cdot syllableMatch + w_2 \cdot visemeMatch + w_3 \cdot durationMatch$$

где

$$durationMatch = 1 - min(1, |T_{ru\_est} - T_{ref}| / T_{ref})$$

Проверка первого и последнего смычного: `б/п/м` против `b/p/m`, `д/т/н` против `d/t/n`.

### 8.2 `LLM`-адаптация

```cpp
class ILlmProvider {
public:
  virtual ~ILlmProvider() = default;
  virtual Completion complete(const Prompt& p) = 0;
};
class OpenRouterProvider : public ILlmProvider {};
class OllamaLocalProvider : public ILlmProvider {};
```

Настройки: `BaseURL + API Key + Model ID`. Дефолт `https://openrouter.ai/api/v1`. Переключение на `http://localhost:11434` без пересборки.

Промпт включает: `EN текст, dur мс, число слогов EN, первый/последний звук EN, требование 3 варианта + объяснение`.

`UI`: слева `EN + RU base`, справа $3$ варианта с `слоги / оценка длительности / viseme-match`, кнопка `Принять как ru_adapted`. История `import -> llm:* -> actor` в `text_history`.

---

## 9. Очистка голоса

`Offline Render Effect`, каждый шаг со слайдером и `A/B Preview`.

| Шаг | `fast-8GB` | `quality-16GB` | Назначение |
|---|---|---|---|
| `Denoise + лёгкий dereverb` | `DeepFilterNet3` | `DeepFilterNet3 large` | Шум квартиры |
| Сильный гул | `UVR MDX Reverb` | `Resemble Enhance` | Гулкая комната |
| Сибилянты/плозивы | `DSP DeEsser + HPF 80Hz` | то же | Свист `с`, взрывные `п/б` |
| Картавость/шепелявость | Только `STS` | `STS` + опц. `RVC` | Дефекты речи |

Дефекты речи не чинятся шумодавом, перекрываются `STS` клоном персонажа. Локальный `RVC / so-vits-svc` — опциональный бесплатный плагин.

Цепочка по умолчанию:

```text
Take -> Denoise -> DeReverb -> DeEss/HPF -> Gain -> MASTER-RU candidate
```

Каждый слой отключаемый.

---

## 10. `ElevenLabs`

Оба режима:

```text
(a) STS: Take WAV -> Speech-to-Speech (voice_id персонажа,
    stability, similarity_boost) -> MASTER-RU candidate
(b) TTS: ru_final -> TTS -> candidate
```

- `voice_map`: `speaker_internal -> voice_id`, например `Character.Main.Coen -> voice_xxx`. Суффикс `(whisper)` → пресет параметров.
- Кэш: `cache/elevenlabs/{sha256(audio+voice_id+params)}.flac + meta.json`. Повтор не тратит кредиты.
- Кредиты: `GET /v1/user` каждые $5$ мин + кнопка, виджет в статус-баре и настройках. При нуле блокировка отправки. Прогноз стоимости `batch` по символам до запуска.

---

## 11. `Batch` для целой игры

```cpp
struct Job {
  std::string job_id;
  std::string type;
  std::string wem_hash;
  Json params;
  std::string status;
  int progress_pct;
  std::string cache_key;
};
```

- Очередь в `SQLite`, переживает перезапуск.
- Воркеры: `N = CPU_threads` для локальных, $M = 4$ для `ElevenLabs` против `rate limit`.
- Дедупликация по `cache_key`.
- Скоуп: `file_id / quest_id / speaker / выделенные`.
- Цепочка: `[import_wav] -> [denoise] -> [STS voice] -> [Fit to Ref] -> [encode_wem]`.
- Оценка: `denoise 0.3x realtime` → $30$ часов $\approx 9$-$10$ часов `GPU` по файлам за ночь. `ElevenLabs 1000 реплик` $\approx 1$ час. Целиком $39481$ за ночь через `API` нереально ($\approx 33$ часа), гнать по одному `file_id` ($\approx 900$ реплик).

---

## 12. Проект и архив

Оценка веса:

$$V_{wav24} \approx 36 \times 3600 \times 48000 \times 3 \approx 18.6GB$$

```text
MyDub/                  рабочая директория
  project.db
  manifest.json
  audio_flac/           CAS <sha>.flac level 5
  cache/elevenlabs/
  tmp/

MyDub.dubpack           экспорт для переноса
  ZIP + ZSTD-12 + FLAC + AES-256-GCM при пароле
  ключ: Argon2id(password)
```

- `FLAC` $\approx 50$-$60\%$ от `WAV` без потерь. Итог $\approx 6$-$7GB$ рабочая + $\approx 6GB$ `.dubpack`. `БД` $\approx 100MB$.
- `ZSTD-12`, не `LZMA`: `7z` легче на $5$-$8\%$, но в $4$-$5$ раз медленнее на $18GB`.
- Инкрементальность через `CAS`: файл = `hash.flac`, сейв пишет только новые `blobs` + `manifest.json`.
- Без пароля — только `ZSTD` для скорости. С паролем — `AES-GCM`.

---

## 13. `UI` в стиле `Cubase`

```text
[Меню][Тулбар: Record Loop Play Fit Ref Comp]
[LineList слева | Timeline центр | Inspector LLM/Jobs справа]
[Транспорт снизу: метроном, мониторинг, частота, кредиты EL, autosave]
```

- Все панели `QDockWidget`, компоновка сохраняемая.
- `LineList`: дерево `Файл (42) -> Сцена -> таблица`, колонки `Статус | Спикер | Квест | EN | RU финал | Длит.EN | Delta | Score`, фильтры `UNKNOWN / todo / >300ms`, `FTS`-поиск, бейджи `RU +4 слога`, `lip B...B`.
- Темы `QSS`, настройка цветов треков и статусов.
- Хоткеи обязательны: `Действие | Шорткат | Контекст`, `JSON` импорт/экспорт. Дефолты: `Space` play, `R` record, `L` loop, `S` split, `Ctrl+Z / Ctrl+Y`.

---

## 14. `Roadmap` по фазам

| Фаза | Объём | Готовность |
|---|---|---|
| **0. Скелет** | `CMake + Qt` докинг, `schema.sql`, импорт `combined.json`, дерево `файл->сцена->реплики`, `sound2wem` сборка + `docs/wem_params.md` | Импорт $39481$ $<3$ сек, скролл без лагов |
| **1. Аудио** | `RtAudio + ASIO Focusrite`, запись моно, `Track 0/1/N`, волноформа + zoom, метроном, мониторинг | Запись $10$ сек без `xrun` |
| **2. Редактура** | `trim/split/move/fade/crossfade/gain/normalize/silence/reverse`, `RubberBand Fit`, `Align`, `Undo` + автосейв | Откат до старта после рестарта |
| **3. `WAV/WEM`** | импорт `<hash>_en.wav`, прямая подгрузка `.wav`, `probe` в БД, `encode sound2wem batch`, валидация по заголовку `WEM` | `WAV->WEM`, $\Delta T < 20ms$ |
| **4. Тейки** | Циклозапись, автоцвет $RMS/ZCR/\delta$, маркировка, `Comp to Master` | Цвета в realtime |
| **5. `LLM`** | `ILlmProvider + OpenRouter`, $3$ варианта, слоги/виземы, история | Переключение на `Ollama` без пересборки |
| **6. `Denoise`** | `sidecar gRPC + DeepFilterNet3`, цепочка, `batch` | Ночной прогон $200$+ |
| **7. `ElevenLabs`** | `STS + TTS`, `voice_map`, кэш, кредиты, `batch` с прогнозом | Повтор без кредитов |
| **8. `Pack`** | `Working Dir + .dubpack`, `CAS`, инкремент, перенос на другой ПК | $\approx 6GB$, открытие на чистом ПК |
| **9. Полировка** | Темы, хоткеи, локализация, краш-восстановление, мануал | Бета на одной главе |

Правило: фазы строго по порядку, каждая заканчивается тестом и коммитом.

---

## 15. Агентная разработка

### 15.1 Структура репо

```text
DubStudio/
  AGENTS.md
  docs/plans/
  docs/wem_params.md
  docs/licenses.md
  app/
  core/audio/
  core/project/
  core/text/
  workers/python/
  integrations/
  plugins/
  tests/
  third_party/rtaudio
  third_party/sound2wem
  third_party/asio_sdk   # в .gitignore, НЕ коммитить; уже скачан локально в ASIO-SDK_2.3.4_2025-10-15/ASIOSDK (ASIO 2.3.4), при сборке скопировать/прилинковать оттуда: common/ + host/ + host/pc/
```

### 15.2 `MCP` минимум

`filesystem` только `DubStudio/`, `git + github` ветки `feat/*`, `sqlite` для `SELECT` на реальных $39481$, `fetch` для доков `RtAudio / OpenRouter / ElevenLabs / sound2wem`, `memory` для решений.

### 15.3 `AGENTS.md`

```markdown
# AGENTS.md — DubStudio
Стек: C++20, Qt6 Widgets, CMake+Ninja, SQLite WAL.
Аудио: RtAudio + ASIO SDK 2.3.4 (third_party/asio_sdk, не коммитить; исходник уже лежит в ASIO-SDK_2.3.4_2025-10-15/ASIOSDK).
DSP: libsndfile, SoXR, RubberBand, Eigen.
Python 3.11 sidecar по gRPC только для ИИ.
sound2wem по его лицензии, не линковать GPL в ядро.
Структура: include/ интерфейсы, core/ реализация, tests/ Catch2.
Один PR = один модуль. Перед кодом план в docs/plans/<модуль>.md.
UI-текст только на русском.
Все мутации через Command + undo_log.
В callback ASIO: только ring-buffer, без malloc/I/O/SQL/mutex.
Фазы: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9. Не перескакивать.
Коммиты: conventional commits, описание на русском.
```

### 15.4 Первый промпт агенту

```text
Инициализируй проект по AGENTS.md.
Фаза 0:
1. Структура папок и CMakeLists.txt.
2. Database: открыть lines.db, применить schema.sql из раздела 5.
3. Importer combined.json 4 уровня:
   {file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}.
   order_index на каждом уровне, dur*1000, пусто -> UNKNOWN.
4. MainWindow: тёмная тема, слева дерево файл->сцена + QTableView
   (Статус | Спикер | EN | RU | Длит), FTS-поиск, виртуализация.
5. third_party/sound2wem: клонировать, собрать sound2wem.exe,
   лицензию в docs/licenses.md, --help в docs/wem_params.md.
6. Тест: 39481 фейковая строка (42 файла), импорт < 3 сек, скролл без лагов.
Коммит feat(project): скелет + 4-уровневый импорт + sound2wem.
```

---

## 16. Что нужно дальше

1. Вывод `sound2wem --help` — для точного ТЗ Фазы 3. Плюс вручную сконвертировать пробную партию `WEM -> WAV` и проверить, что имена `<hash>_en.wav` совпадают с ключами `combined.json`.
2. Подтверждение дефолта `48000 / 24-bit`.
3. Маппинг $2$-$3$ персонажей в `voice_id` — для Фазы 7.

После этого — запуск Фазы 0 по промпту из раздела `15.4`.
