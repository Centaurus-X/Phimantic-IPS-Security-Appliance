"""Event-Management-API.

Liefert persistierte Events aus SQLite. Neue Suricata-EVE-Alerts werden vor
Listen-/Dashboard-Abfragen inkrementell aus eve.json importiert. Dadurch bleibt
Stage-Tracking stabil und Alerts verschwinden nicht, wenn spaeter viele HTTP-,
DNS- oder flow-EVE-Zeilen geschrieben werden.
"""

import json
from functools import partial

from flask import jsonify, request

from auth import require_auth
from db.connection import get_connection
from services import eve_reader


_ALLOWED_STAGES = ("pending", "investigation", "completed")
_CURSOR_KEY = "eve_reader.cursor.v1"


def _load_cursor(conn):
    row = conn.execute(
        "SELECT value FROM settings_kv WHERE key = ?",
        (_CURSOR_KEY,),
    ).fetchone()
    if row is None or not row["value"]:
        return {}
    try:
        data = json.loads(row["value"])
    except (TypeError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _save_cursor(conn, cursor):
    conn.execute(
        "INSERT INTO settings_kv (key, value, updated_at) "
        "VALUES (?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(key) DO UPDATE SET "
        "value = excluded.value, updated_at = CURRENT_TIMESTAMP",
        (_CURSOR_KEY, json.dumps(cursor, separators=(",", ":"))),
    )


def _same_identity(cursor, identity):
    if not cursor or not identity:
        return False
    return (cursor.get("path") == identity.get("path")
            and cursor.get("dev") == identity.get("dev")
            and cursor.get("ino") == identity.get("ino"))


def _event_exists(conn, event):
    row = conn.execute(
        "SELECT id FROM events "
        "WHERE COALESCE(flow_id, '') = COALESCE(?, '') "
        "AND COALESCE(timestamp, '') = COALESCE(?, '') "
        "AND COALESCE(rule_id, 0) = COALESCE(?, 0) "
        "AND COALESCE(rule_msg, '') = COALESCE(?, '') "
        "LIMIT 1",
        (event.get("flow_id"), event.get("timestamp"),
         event.get("rule_id"), event.get("rule_msg")),
    ).fetchone()
    return row is not None


def _insert_event(conn, event):
    conn.execute(
        "INSERT INTO events (flow_id, timestamp, src_ip, src_port, "
        "dest_ip, dest_port, proto, rule_id, rule_msg, severity, "
        "category, stage, raw_alert) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?)",
        (event.get("flow_id"), event.get("timestamp"),
         event.get("src_ip"), event.get("src_port"),
         event.get("dest_ip"), event.get("dest_port"),
         event.get("proto"), event.get("rule_id"), event.get("rule_msg"),
         event.get("severity"), event.get("category"),
         json.dumps(event.get("raw") or {}, separators=(",", ":"))),
    )


def _sync_from_eve(cfg):
    """Importiert neue Alerts anhand eines persistierten Byte-Cursors."""
    conn = get_connection()
    try:
        cursor = _load_cursor(conn)
        offset = int(cursor.get("offset") or 0)
        alerts, new_offset, identity = eve_reader.read_alerts_from_offset(
            cfg, offset=offset)
        if not _same_identity(cursor, identity):
            alerts, new_offset, identity = eve_reader.read_alerts_from_offset(
                cfg, offset=0)
        inserted = 0
        for event in alerts:
            if not event.get("timestamp"):
                continue
            if _event_exists(conn, event):
                continue
            _insert_event(conn, event)
            inserted += 1
        cursor = dict(identity)
        cursor["offset"] = int(new_offset)
        _save_cursor(conn, cursor)
        conn.commit()
        return inserted
    except OSError:
        return 0
    finally:
        conn.close()


def _list_events(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx

    _sync_from_eve(cfg)

    stage = request.args.get("stage")
    severity = request.args.get("severity")
    search = (request.args.get("q") or "").strip()
    limit = min(int(request.args.get("limit", 100)), 1000)
    offset = max(int(request.args.get("offset", 0)), 0)

    where = []
    params = []
    if stage and stage in _ALLOWED_STAGES:
        where.append("e.stage = ?")
        params.append(stage)
    if severity in ("1", "2", "3"):
        where.append("e.severity = ?")
        params.append(int(severity))
    if search:
        where.append("(e.rule_msg LIKE ? OR e.src_ip LIKE ? OR e.dest_ip LIKE ? "
                     "OR CAST(e.rule_id AS TEXT) = ?)")
        like = "%" + search + "%"
        params.extend([like, like, like, search])

    where_sql = (" WHERE " + " AND ".join(where)) if where else ""

    conn = get_connection()
    try:
        total = conn.execute(
            "SELECT COUNT(*) AS c FROM events e" + where_sql,
            params,
        ).fetchone()["c"]

        rows = conn.execute(
            "SELECT e.id, e.flow_id, e.timestamp, e.src_ip, e.src_port, "
            "e.dest_ip, e.dest_port, e.proto, e.rule_id, e.rule_msg, "
            "e.severity, e.category, e.stage, "
            "u.username AS handler_username "
            "FROM events e LEFT JOIN users u ON u.id = e.handler_id"
            + where_sql + " ORDER BY e.timestamp DESC LIMIT ? OFFSET ?",
            params + [limit, offset],
        ).fetchall()
    finally:
        conn.close()

    return jsonify({
        "ok": True,
        "total": total,
        "limit": limit,
        "offset": offset,
        "events": [dict(r) for r in rows],
    })


def _get_event(cfg, event_id):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    _sync_from_eve(cfg)
    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT e.*, u.username AS handler_username "
            "FROM events e LEFT JOIN users u ON u.id = e.handler_id "
            "WHERE e.id = ?",
            (event_id,),
        ).fetchone()
        if row is None:
            return jsonify({"error": "not_found"}), 404
        history = conn.execute(
            "SELECT h.action, h.note, h.created_at, u.username "
            "FROM event_history h LEFT JOIN users u ON u.id = h.user_id "
            "WHERE h.event_id = ? ORDER BY h.created_at DESC",
            (event_id,),
        ).fetchall()
    finally:
        conn.close()
    event = dict(row)
    if event.get("raw_alert"):
        try:
            event["raw_alert"] = json.loads(event["raw_alert"])
        except (TypeError, ValueError):
            pass
    event["history"] = [dict(r) for r in history]
    return jsonify({"ok": True, "event": event})


def _process_event(cfg, event_id):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    stage = body.get("stage")
    note = body.get("note") or ""
    take_ownership = bool(body.get("take_ownership"))

    if stage and stage not in _ALLOWED_STAGES:
        return jsonify({"error": "invalid_stage"}), 400

    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT id FROM events WHERE id = ?",
            (event_id,)).fetchone()
        if row is None:
            return jsonify({"error": "not_found"}), 404

        updates = ["updated_at = CURRENT_TIMESTAMP"]
        params = []
        if stage:
            updates.append("stage = ?")
            params.append(stage)
        if take_ownership:
            updates.append("handler_id = ?")
            params.append(user["id"])
        params.append(event_id)
        conn.execute("UPDATE events SET " + ", ".join(updates) + " WHERE id = ?",
                     params)
        action = "stage:" + stage if stage else "note"
        conn.execute(
            "INSERT INTO event_history (event_id, user_id, action, note) "
            "VALUES (?, ?, ?, ?)",
            (event_id, user["id"], action, note),
        )
        conn.commit()
    finally:
        conn.close()
    return jsonify({"ok": True})


def register_events_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/events", "events_list",
                     partial(_list_events, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/events/<int:event_id>", "events_get",
                     partial(_get_event, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/events/<int:event_id>/process",
                     "events_process",
                     partial(_process_event, cfg), methods=["POST"])
