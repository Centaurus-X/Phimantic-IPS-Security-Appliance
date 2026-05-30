"""SQLite-Verbindung pro Request, plus Schema-Init."""

import os
import sqlite3
import threading
from pathlib import Path

_DB_PATH = None
_INIT_LOCK = threading.Lock()


def get_db_path(cfg=None):
    if cfg is not None:
        return cfg["database"]["path"]
    return _DB_PATH


def init_db_pool(db_path):
    """Globalen Pfad setzen + Schema sicherstellen.

    'Pool' ist hier ueberzeichnet: SQLite hat keinen echten Pool, wir oeffnen
    pro Request eine kurze Verbindung. Die Funktion existiert, damit das
    Application-Bootstrap-Modell konsistent bleibt.
    """
    global _DB_PATH
    _DB_PATH = db_path

    with _INIT_LOCK:
        Path(db_path).parent.mkdir(parents=True, exist_ok=True)
        schema_path = Path(__file__).parent / "schema.sql"
        if not schema_path.is_file():
            return

        conn = sqlite3.connect(db_path)
        try:
            conn.executescript(schema_path.read_text(encoding="utf-8"))
            conn.commit()
            _ensure_default_admin(conn)
        finally:
            conn.close()


def _ensure_default_admin(conn):
    """Falls noch kein Admin-User existiert, anlegen.

    Default-Login: admin / changeme  - bitte sofort nach Erst-Start aendern!
    """
    cur = conn.execute("SELECT COUNT(*) FROM users")
    if cur.fetchone()[0] > 0:
        return

    from auth import hash_password
    pwd_hash = hash_password("changeme")
    conn.execute(
        "INSERT INTO users (username, password_hash, role, full_name, active) "
        "VALUES (?, ?, ?, ?, 1)",
        ("admin", pwd_hash, "admin", "Default Administrator"),
    )
    conn.commit()


def get_connection():
    """Neue SQLite-Verbindung mit Row-Factory."""
    if _DB_PATH is None:
        raise RuntimeError("init_db_pool was not called")
    conn = sqlite3.connect(_DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn
