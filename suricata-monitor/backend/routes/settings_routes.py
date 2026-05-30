"""Settings-API: Suricata YAML lesen/schreiben + Service-Control."""

from functools import partial

from flask import jsonify, request

from auth import require_auth
from db.connection import get_connection
from services import config_manager, suricata_ctl


def _audit(conn, user_id, action, target, summary, success=True):
    conn.execute(
        "INSERT INTO audit_log (user_id, action, target, diff_summary, success) "
        "VALUES (?, ?, ?, ?, ?)",
        (user_id, action, target, summary, 1 if success else 0),
    )


def _read_config(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify(config_manager.read_config(cfg))


def _write_config(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    content = body.get("content")
    if not isinstance(content, str) or not content.strip():
        return jsonify({"error": "content_required"}), 400
    res = config_manager.write_config(cfg, content, run_validate=True)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "config.write",
               cfg["suricata"]["config_path"], res.get("error") or "ok",
               success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _validate_config(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    return jsonify(config_manager.validate(cfg))


def _config_summary(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify(config_manager.get_summary(cfg))


def _service_control(cfg):
    """POST {action: 'reload'|'restart'} - Reload bevorzugt."""
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    action = body.get("action", "reload")
    if action == "restart":
        res = suricata_ctl.restart_service(cfg)
    else:
        res = suricata_ctl.reload_rules(cfg)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "service." + action, "suricata", "",
               success=(res.get("return") == "OK" or res.get("ok", False)))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _service_status(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify(suricata_ctl.service_status(cfg))


def _read_structured(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify(config_manager.read_structured(cfg))


def _write_structured(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    updates = body.get("updates")
    if not isinstance(updates, dict) or not updates:
        return jsonify({"error": "updates_dict_required"}), 400
    res = config_manager.write_structured(cfg, updates, run_validate=True)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "config.write_structured",
               cfg["suricata"]["config_path"],
               "{0} updates applied".format(res.get("applied", 0)),
               success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def register_settings_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/settings/suricata", "settings_read",
                     partial(_read_config, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/settings/suricata", "settings_write",
                     partial(_write_config, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/settings/suricata/validate",
                     "settings_validate",
                     partial(_validate_config, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/settings/suricata/summary",
                     "settings_summary",
                     partial(_config_summary, cfg), methods=["GET"])
    # NEU: schema-driven structured Endpoints
    app.add_url_rule(prefix + "/settings/suricata/structured",
                     "settings_structured_read",
                     partial(_read_structured, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/settings/suricata/structured",
                     "settings_structured_write",
                     partial(_write_structured, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/settings/service", "settings_service",
                     partial(_service_control, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/settings/service/status",
                     "settings_service_status",
                     partial(_service_status, cfg), methods=["GET"])
