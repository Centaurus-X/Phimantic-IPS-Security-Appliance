"""Rules- und Ruleset-API.

/rules/files              - rule files listing
/rules/files/<name>       - rules in file
/rules/files/<name>/toggle - aktivieren/deaktivieren
/rules/files/<name>/rule  - upsert/delete
/rules/reload             - Suricata ruleset-reload-rules

/rulesets/sources         - suricata-update sources listing
/rulesets/sources/<name>/enable
/rulesets/sources/<name>/disable
/rulesets/update          - alle aktiven Quellen pullen
"""

from functools import partial

from flask import jsonify, request

from auth import require_auth
from db.connection import get_connection
from services import rules_manager, ruleset_manager, suricata_ctl


def _audit(conn, user_id, action, target, summary, success=True):
    conn.execute(
        "INSERT INTO audit_log (user_id, action, target, diff_summary, success) "
        "VALUES (?, ?, ?, ?, ?)",
        (user_id, action, target, summary, 1 if success else 0),
    )


# ---------------------------------------------------------------------------
# Rule-Dateien
# ---------------------------------------------------------------------------

def _list_files(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify({"ok": True, "files": rules_manager.list_rule_files(cfg)})


def _read_file(cfg, filename):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    limit = request.args.get("limit", type=int) or 200
    offset = request.args.get("offset", type=int) or 0
    query = request.args.get("q") or None
    data = rules_manager.read_rules(cfg, filename, limit=limit,
                                    offset=offset, query=query)
    return jsonify({"ok": "error" not in data, **data})


def _toggle_rule(cfg, filename):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    sid = body.get("sid")
    enabled = body.get("enabled")
    if sid is None or enabled is None:
        return jsonify({"error": "sid_and_enabled_required"}), 400
    res = rules_manager.toggle_rule(cfg, filename, sid, enabled)

    conn = get_connection()
    try:
        _audit(conn, user["id"],
               "rules.toggle" + (".on" if enabled else ".off"),
               filename, "sid=" + str(sid), success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _upsert_rule(cfg, filename):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    user = ctx
    body = request.get_json(silent=True) or {}
    raw = body.get("raw") or ""
    res = rules_manager.upsert_rule(cfg, filename, raw)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "rules.upsert", filename,
               "sid=" + str(res.get("sid", "")), success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _delete_rule(cfg, filename, sid):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    res = rules_manager.delete_rule(cfg, filename, sid)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "rules.delete", filename,
               "sid=" + str(sid), success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _reload_rules(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    user = ctx
    res = suricata_ctl.reload_rules(cfg)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "rules.reload", "suricata",
               (res.get("stage", "") + ": " + res.get("message", ""))[:200],
               success=res.get("return") == "OK")
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _test_rules(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    return jsonify(suricata_ctl.test_suricata_config(cfg))


def _reload_status(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin", "analyst"))
    if not ok:
        return ctx
    stats = suricata_ctl.send_command(cfg, "ruleset-stats")
    reload_time = suricata_ctl.send_command(cfg, "ruleset-reload-time")
    failed = suricata_ctl.send_command(cfg, "ruleset-failed-rules")
    return jsonify({"ok": True, "stats": stats,
                    "reload_time": reload_time,
                    "failed_rules": failed})


# ---------------------------------------------------------------------------
# Rulesets (Pattern-Quellen)
# ---------------------------------------------------------------------------

def _list_sources(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify(ruleset_manager.list_sources(cfg))


def _enable_source(cfg, name):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    res = ruleset_manager.enable_source(cfg, name)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "ruleset.source.enable",
               name, "", success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _disable_source(cfg, name):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    res = ruleset_manager.disable_source(cfg, name)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "ruleset.source.disable",
               name, "", success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def _update_sources_index(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    return jsonify(ruleset_manager.update_sources_index(cfg))


def _update_rules(cfg):
    ok, ctx = require_auth(cfg, allowed_roles=("admin",))
    if not ok:
        return ctx
    user = ctx
    res = ruleset_manager.update_rules(cfg)
    conn = get_connection()
    try:
        _audit(conn, user["id"], "ruleset.update", "all",
               res.get("message", "")[:200], success=res.get("ok", False))
        conn.commit()
    finally:
        conn.close()
    return jsonify(res)


def register_rules_routes(app, prefix, cfg):
    # Rule files
    app.add_url_rule(prefix + "/rules/files", "rules_files",
                     partial(_list_files, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/rules/files/<path:filename>", "rules_file_read",
                     partial(_read_file, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/rules/files/<path:filename>/toggle",
                     "rules_file_toggle",
                     partial(_toggle_rule, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rules/files/<path:filename>/rule",
                     "rules_file_upsert",
                     partial(_upsert_rule, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rules/files/<path:filename>/rule/<int:sid>",
                     "rules_file_delete",
                     partial(_delete_rule, cfg), methods=["DELETE"])
    app.add_url_rule(prefix + "/rules/reload", "rules_reload",
                     partial(_reload_rules, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rules/test", "rules_test",
                     partial(_test_rules, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rules/reload/status", "rules_reload_status",
                     partial(_reload_status, cfg), methods=["GET"])

    # Rulesets
    app.add_url_rule(prefix + "/rulesets/sources", "rulesets_sources",
                     partial(_list_sources, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/rulesets/sources/<path:name>/enable",
                     "rulesets_enable",
                     partial(_enable_source, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rulesets/sources/<path:name>/disable",
                     "rulesets_disable",
                     partial(_disable_source, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rulesets/sources/update",
                     "rulesets_update_index",
                     partial(_update_sources_index, cfg), methods=["POST"])
    app.add_url_rule(prefix + "/rulesets/update", "rulesets_update_rules",
                     partial(_update_rules, cfg), methods=["POST"])
