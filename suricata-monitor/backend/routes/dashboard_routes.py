"""Dashboard-API: System-Status, Top-Statistiken."""

from collections import Counter
from functools import partial

from flask import jsonify

from auth import require_auth
from db.connection import get_connection
from services import suricata_ctl, eve_reader
from routes.events_routes import _sync_from_eve


def _dashboard_overview(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx

    status = suricata_ctl.service_status(cfg)
    version = suricata_ctl.get_version(cfg)
    uptime = suricata_ctl.get_uptime(cfg)
    capture = suricata_ctl.get_capture_mode(cfg)
    runmode = suricata_ctl.get_running_mode(cfg)
    _sync_from_eve(cfg)
    eve_stats = eve_reader.get_eve_stats(cfg)

    # Top-N Statistik aus persistierten Events
    conn = get_connection()
    try:
        total_events = conn.execute(
            "SELECT COUNT(*) AS c FROM events").fetchone()["c"]
        by_stage = conn.execute(
            "SELECT stage, COUNT(*) AS c FROM events GROUP BY stage"
        ).fetchall()
        by_severity = conn.execute(
            "SELECT severity, COUNT(*) AS c FROM events GROUP BY severity"
        ).fetchall()
        top_rules = conn.execute(
            "SELECT rule_id, rule_msg, COUNT(*) AS hits "
            "FROM events WHERE rule_id IS NOT NULL "
            "GROUP BY rule_id ORDER BY hits DESC LIMIT 10"
        ).fetchall()
    finally:
        conn.close()

    # Wenn DB leer, ergaenze mit live tail aus eve.json
    live_alerts = []
    if total_events == 0:
        live_alerts = eve_reader.tail_alerts(cfg, limit=50)
        if live_alerts:
            total_events = len(live_alerts)
            by_severity = [{"severity": a.get("severity") or 3,
                            "c": 0} for a in live_alerts]
            cnt = Counter(a.get("severity") or 3 for a in live_alerts)
            by_severity = [{"severity": k, "c": v} for k, v in cnt.items()]
            cnt_rules = Counter()
            for a in live_alerts:
                if a.get("rule_id"):
                    cnt_rules[(a["rule_id"], a.get("rule_msg") or "")] += 1
            top_rules = [{"rule_id": k[0], "rule_msg": k[1], "hits": v}
                         for k, v in cnt_rules.most_common(10)]

    return jsonify({
        "ok": True,
        "service": {
            "status": status.get("status", "unknown"),
            "version": version.get("message"),
            "uptime_seconds": uptime.get("message"),
            "capture_mode": capture.get("message"),
            "run_mode": runmode.get("message"),
        },
        "eve": eve_stats,
        "stats": {
            "total_events": total_events,
            "by_stage": [dict(r) for r in by_stage],
            "by_severity": [dict(r) for r in by_severity],
            "top_rules": [dict(r) for r in top_rules],
        },
    })


def _iface_stats(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    from flask import request
    iface = request.args.get("iface")
    res = suricata_ctl.get_iface_stats(cfg, iface)
    return jsonify(res)


def register_dashboard_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/dashboard/overview", "dashboard_overview",
                     partial(_dashboard_overview, cfg), methods=["GET"])
    app.add_url_rule(prefix + "/dashboard/iface-stats", "dashboard_iface",
                     partial(_iface_stats, cfg), methods=["GET"])
