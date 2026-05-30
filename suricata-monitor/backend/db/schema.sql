-- SuricataMonitor SQLite Schema
-- Aufbau:
--   users         - lokale Benutzer mit Rolle
--   sessions      - aktive JWT-Tokens (Server-Side Revoke)
--   events        - persistierte Security-Events (von eve.json abgeleitet)
--   event_history - Bearbeitungsverlauf pro Event
--   audit_log     - Audit-Trail fuer Konfigurations-Aenderungen
--   settings_kv   - System-Settings ausserhalb von suricata.yaml

CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,
    role            TEXT NOT NULL DEFAULT 'viewer',  -- admin | analyst | viewer
    full_name       TEXT,
    email           TEXT,
    active          INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login_at   TEXT
);

CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);

CREATE TABLE IF NOT EXISTS sessions (
    id              TEXT PRIMARY KEY,             -- jti claim
    user_id         INTEGER NOT NULL,
    issued_at       TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at      TEXT NOT NULL,
    revoked         INTEGER NOT NULL DEFAULT 0,
    user_agent      TEXT,
    remote_addr     TEXT,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);

CREATE TABLE IF NOT EXISTS events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    flow_id         TEXT,
    timestamp       TEXT NOT NULL,
    src_ip          TEXT,
    src_port        INTEGER,
    dest_ip         TEXT,
    dest_port       INTEGER,
    proto           TEXT,
    rule_id         INTEGER,
    rule_msg        TEXT,
    severity        INTEGER,                       -- 1 (high) bis 3 (low)
    category        TEXT,
    stage           TEXT NOT NULL DEFAULT 'pending', -- pending | investigation | completed
    handler_id      INTEGER,
    raw_alert       TEXT,                          -- vollstaendiges EVE-Snippet JSON
    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (handler_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_events_stage ON events(stage);
CREATE INDEX IF NOT EXISTS idx_events_severity ON events(severity);
CREATE INDEX IF NOT EXISTS idx_events_flow ON events(flow_id);

CREATE TABLE IF NOT EXISTS event_history (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id        INTEGER NOT NULL,
    user_id         INTEGER,
    action          TEXT NOT NULL,
    note            TEXT,
    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (event_id) REFERENCES events(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_event_history_event ON event_history(event_id);

CREATE TABLE IF NOT EXISTS audit_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER,
    action          TEXT NOT NULL,                 -- e.g. "rules.write" | "config.update"
    target          TEXT,                          -- e.g. file path
    diff_summary    TEXT,
    success         INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_audit_action ON audit_log(action);
CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_log(created_at DESC);

CREATE TABLE IF NOT EXISTS settings_kv (
    key             TEXT PRIMARY KEY,
    value           TEXT,
    updated_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
