"""
SuricataMonitor · Flask Application Bootstrap.

Vorgehensweise gemaess Repository-Stilrichtlinien:
- kein OOP
- functools.partial fuer gebundene Route-Handler
- keine Decorators (Flask-Routen werden ueber app.add_url_rule gebunden)
"""

import os
import sys
from functools import partial
from pathlib import Path

from flask import Flask, send_from_directory

PROJECT_ROOT = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(PROJECT_ROOT / "backend"))

from config import load_config
from db.connection import init_db_pool, get_db_path
from routes.auth_routes import register_auth_routes
from routes.dashboard_routes import register_dashboard_routes
from routes.events_routes import register_events_routes
from routes.rules_routes import register_rules_routes
from routes.settings_routes import register_settings_routes
from routes.system_routes import register_system_routes
from routes.users_routes import register_users_routes


def _serve_frontend_index(frontend_dir):
    return send_from_directory(frontend_dir, "index.html")


def _serve_frontend_login(frontend_dir):
    return send_from_directory(frontend_dir, "login.html")


def _serve_frontend_static(frontend_dir, subpath):
    return send_from_directory(frontend_dir, subpath)


def create_app(config_path=None):
    """Application Factory.

    Eingangspunkt fuer alle Deploys (Dev, systemd, Docker).
    Frontend liegt unter ``frontend/`` und wird statisch ausgeliefert.
    """
    app = Flask(__name__, static_folder=None, template_folder=None)

    cfg = load_config(config_path)
    app.config["SURIGUARD_CFG"] = cfg
    app.config["SECRET_KEY"] = cfg["auth"]["secret_key"]

    init_db_pool(get_db_path(cfg))

    api_prefix = "/api/v1"
    register_auth_routes(app, api_prefix, cfg)
    register_dashboard_routes(app, api_prefix, cfg)
    register_events_routes(app, api_prefix, cfg)
    register_rules_routes(app, api_prefix, cfg)
    register_settings_routes(app, api_prefix, cfg)
    register_system_routes(app, api_prefix, cfg)
    register_users_routes(app, api_prefix, cfg)

    frontend_dir = str(PROJECT_ROOT / "frontend")
    app.add_url_rule("/", "index",
                     partial(_serve_frontend_index, frontend_dir))
    app.add_url_rule("/login", "login",
                     partial(_serve_frontend_login, frontend_dir))
    app.add_url_rule("/<path:subpath>", "static_frontend",
                     partial(_serve_frontend_static, frontend_dir))

    return app


def main():
    """CLI-Entrypoint."""
    config_path = os.environ.get("SURIMON_CONFIG")
    app = create_app(config_path)
    cfg = app.config["SURIGUARD_CFG"]
    host = cfg["server"]["bind_host"]
    port = int(cfg["server"]["bind_port"])
    debug = bool(cfg["server"].get("debug", False))
    app.run(host=host, port=port, debug=debug, threaded=True)


if __name__ == "__main__":
    main()
