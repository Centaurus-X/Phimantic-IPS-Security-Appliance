"""Login, Logout, Whoami."""

from functools import partial

from flask import jsonify, make_response, request

from auth import (hash_password, issue_jwt, require_auth, verify_password)
from db.connection import get_connection


def _do_login(cfg):
    body = request.get_json(silent=True) or {}
    username = (body.get("username") or "").strip()
    password = body.get("password") or ""
    if not username or not password:
        return jsonify({"error": "missing_credentials"}), 400

    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT id, username, password_hash, role, full_name, active "
            "FROM users WHERE username = ?",
            (username,),
        ).fetchone()
        if row is None or not row["active"]:
            return jsonify({"error": "invalid_credentials"}), 401
        if not verify_password(password, row["password_hash"]):
            return jsonify({"error": "invalid_credentials"}), 401

        token, jti, exp_iso = issue_jwt(
            cfg["auth"]["secret_key"], row["id"], row["role"],
            cfg["auth"]["token_ttl_minutes"],
        )
        conn.execute(
            "INSERT INTO sessions (id, user_id, expires_at, user_agent, remote_addr) "
            "VALUES (?, ?, ?, ?, ?)",
            (jti, row["id"], exp_iso,
             request.headers.get("User-Agent", "")[:200],
             request.remote_addr or ""),
        )
        conn.execute(
            "UPDATE users SET last_login_at = CURRENT_TIMESTAMP WHERE id = ?",
            (row["id"],),
        )
        conn.commit()
    finally:
        conn.close()

    resp = make_response(jsonify({
        "ok": True,
        "user": {"id": row["id"], "username": row["username"],
                 "role": row["role"], "full_name": row["full_name"]},
    }))
    resp.set_cookie(
        cfg["auth"]["cookie_name"], token,
        max_age=int(cfg["auth"]["token_ttl_minutes"]) * 60,
        httponly=True, samesite="Lax",
        secure=bool(cfg["auth"]["cookie_secure"]),
        path="/",
    )
    return resp


def _do_logout(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return jsonify({"ok": True, "note": "no_session"})
    # Session revoken
    cookie_name = cfg["auth"]["cookie_name"]
    token = request.cookies.get(cookie_name) or ""
    if token.count(".") == 2:
        try:
            import base64
            import json as _json
            payload = _json.loads(
                base64.urlsafe_b64decode(
                    token.split(".")[1] + "=" * (-len(token.split(".")[1]) % 4)
                ).decode("utf-8"))
            jti = payload.get("jti")
            if jti:
                conn = get_connection()
                try:
                    conn.execute(
                        "UPDATE sessions SET revoked = 1 WHERE id = ?",
                        (jti,))
                    conn.commit()
                finally:
                    conn.close()
        except (ValueError, KeyError):
            pass

    resp = make_response(jsonify({"ok": True}))
    resp.delete_cookie(cookie_name, path="/")
    return resp


def _do_whoami(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify({"ok": True, "user": ctx})


def register_auth_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/auth/login", "auth_login",
                     partial(_do_login, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/auth/logout", "auth_logout",
                     partial(_do_logout, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/auth/whoami", "auth_whoami",
                     partial(_do_whoami, cfg), methods=["GET"])
