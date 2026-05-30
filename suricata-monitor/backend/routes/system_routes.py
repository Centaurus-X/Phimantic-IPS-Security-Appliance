"""System-Info-API: Interfaces des Hosts auflisten."""

from functools import partial

from flask import jsonify

from auth import require_auth
from services import system_info


def _list_interfaces(cfg):
    ok, ctx = require_auth(cfg)
    if not ok:
        return ctx
    return jsonify({"ok": True,
                    "interfaces": system_info.list_interfaces(cfg)})


def register_system_routes(app, prefix, cfg):
    app.add_url_rule(prefix + "/system/interfaces",
                     "system_interfaces",
                     partial(_list_interfaces, cfg), methods=["GET"])
