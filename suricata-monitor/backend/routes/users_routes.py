"""User-Management-API.

Endpoints:
    GET    /users                  list
    POST   /users                  create
    GET    /users/<id>             detail
    PATCH  /users/<id>             update (role, full_name, active)
    POST   /users/<id>/password    set new password
    DELETE /users/<id>             delete
    POST   /users/me/password      eigenes Passwort aendern
"""

from functools import partial

from flask import g, jsonify, request

from auth import hash_password, require_auth, verify_password
from db.connection import get_connection


_ALLOWED_ROLES = ("admin", "analyst", "viewer")


def _row_to_dict(row):
    return {
        "id": row["id"],
        "username": row["username"],
        "role": row["role"],
        "full_name": row["full_name"],
        "email": row["email"],
        "active": bool(row["active"]),
        "created_at": row["created_at"],
        "last_login_at": row["last_login_at"],
    }


def _list_users(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    conn = get_connection()
    try:
        rows = conn.execute(
            "SELECT id, username, role, full_name, email, active, "
            "created_at, last_login_at FROM users ORDER BY username"
        ).fetchall()
    finally:
        conn.close()
    return jsonify({"ok": True, "users": [_row_to_dict(r) for r in rows]})


def _create_user(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    body = request.get_json(silent=True) or {}
    username = (body.get("username") or "").strip()
    password = body.get("password") or ""
    role = body.get("role") or "viewer"
    full_name = (body.get("full_name") or "").strip() or None
    email = (body.get("email") or "").strip() or None

    if not username or len(username) < 2:
        return jsonify({"error": "invalid_username"}), 400
    if not password or len(password) < 8:
        return jsonify({"error": "password_too_short",
                        "message": "min 8 chars"}), 400
    if role not in _ALLOWED_ROLES:
        return jsonify({"error": "invalid_role"}), 400

    conn = get_connection()
    try:
        existing = conn.execute(
            "SELECT id FROM users WHERE username = ?", (username,)).fetchone()
        if existing is not None:
            return jsonify({"error": "username_taken"}), 409
        cur = conn.execute(
            "INSERT INTO users (username, password_hash, role, full_name, email) "
            "VALUES (?, ?, ?, ?, ?)",
            (username, hash_password(password), role, full_name, email),
        )
        conn.commit()
        new_id = cur.lastrowid
        row = conn.execute(
            "SELECT id, username, role, full_name, email, active, "
            "created_at, last_login_at FROM users WHERE id = ?",
            (new_id,)).fetchone()
    finally:
        conn.close()
    return jsonify({"ok": True, "user": _row_to_dict(row)})


def _get_user(cfg, user_id):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT id, username, role, full_name, email, active, "
            "created_at, last_login_at FROM users WHERE id = ?",
            (user_id,)).fetchone()
    finally:
        conn.close()
    if row is None:
        return jsonify({"error": "not_found"}), 404
    return jsonify({"ok": True, "user": _row_to_dict(row)})


def _update_user(cfg, user_id):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    body = request.get_json(silent=True) or {}
    updates = []
    params = []
    if "role" in body:
        if body["role"] not in _ALLOWED_ROLES:
            return jsonify({"error": "invalid_role"}), 400
        updates.append("role = ?")
        params.append(body["role"])
    if "full_name" in body:
        updates.append("full_name = ?")
        params.append((body["full_name"] or "").strip() or None)
    if "email" in body:
        updates.append("email = ?")
        params.append((body["email"] or "").strip() or None)
    if "active" in body:
        updates.append("active = ?")
        params.append(1 if body["active"] else 0)

    if not updates:
        return jsonify({"error": "no_updates"}), 400

    conn = get_connection()
    try:
        params.append(user_id)
        conn.execute("UPDATE users SET " + ", ".join(updates) +
                     " WHERE id = ?", params)
        conn.commit()
        row = conn.execute(
            "SELECT id, username, role, full_name, email, active, "
            "created_at, last_login_at FROM users WHERE id = ?",
            (user_id,)).fetchone()
    finally:
        conn.close()
    if row is None:
        return jsonify({"error": "not_found"}), 404
    return jsonify({"ok": True, "user": _row_to_dict(row)})


def _set_password(cfg, user_id):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    body = request.get_json(silent=True) or {}
    password = body.get("password") or ""
    if len(password) < 8:
        return jsonify({"error": "password_too_short"}), 400
    conn = get_connection()
    try:
        cur = conn.execute(
            "UPDATE users SET password_hash = ? WHERE id = ?",
            (hash_password(password), user_id))
        if cur.rowcount == 0:
            return jsonify({"error": "not_found"}), 404
        # Alle Sessions des Users revoken
        conn.execute("UPDATE sessions SET revoked = 1 WHERE user_id = ?",
                     (user_id,))
        conn.commit()
    finally:
        conn.close()
    return jsonify({"ok": True})


def _delete_user(cfg, user_id):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    if user["id"] == int(user_id):
        return jsonify({"error": "cannot_delete_self"}), 400
    conn = get_connection()
    try:
        cur = conn.execute("DELETE FROM users WHERE id = ?", (user_id,))
        if cur.rowcount == 0:
            return jsonify({"error": "not_found"}), 404
        conn.commit()
    finally:
        conn.close()
    return jsonify({"ok": True})


def _change_my_password(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    old = body.get("old_password") or ""
    new = body.get("new_password") or ""
    if len(new) < 8:
        return jsonify({"error": "password_too_short"}), 400
    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT password_hash FROM users WHERE id = ?",
            (user["id"],)).fetchone()
        if row is None or not verify_password(old, row["password_hash"]):
            return jsonify({"error": "invalid_old_password"}), 400
        conn.execute("UPDATE users SET password_hash = ? WHERE id = ?",
                     (hash_password(new), user["id"]))
        conn.execute("UPDATE sessions SET revoked = 1 WHERE user_id = ?",
                     (user["id"],))
        conn.commit()
    finally:
        conn.close()
    return jsonify({"ok": True, "note": "all_sessions_revoked"})


def register_users_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/users", "users_list",
                     partial(_list_users, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/users", "users_create",
                     partial(_create_user, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/users/<int:user_id>", "users_get",
                     partial(_get_user, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/users/<int:user_id>", "users_update",
                     partial(_update_user, cfg), methods=["PATCH"])
    app.add_url_rule(prefix + "/users/<int:user_id>/password",
                     "users_set_password",
                     partial(_set_password, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/users/<int:user_id>", "users_delete",
                     partial(_delete_user, cfg), methods=["DELETE"])
    app.add_url_rule(prefix + "/users/me/password", "users_my_password",
                     partial(_change_my_password, cfg), methods=["POST"])
