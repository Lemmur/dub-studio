-- DubStudio — схема БД проекта (PLAN.md, раздел 5.1).
-- Применяется классом dubstudio::Database при каждом открытии (идемпотентно).

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
CREATE INDEX IF NOT EXISTS idx_lines_file ON lines(file_id);
CREATE INDEX IF NOT EXISTS idx_lines_quest ON lines(quest_id);
CREATE INDEX IF NOT EXISTS idx_lines_speaker ON lines(speaker_name);
CREATE INDEX IF NOT EXISTS idx_lines_status ON lines(status);

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
CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status);

CREATE TABLE IF NOT EXISTS undo_log (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  created_at INTEGER DEFAULT (strftime('%s','now')),
  action TEXT NOT NULL,
  undone INTEGER DEFAULT 0
);
