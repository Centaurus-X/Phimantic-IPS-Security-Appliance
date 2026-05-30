"""Konfiguration aus YAML laden mit sauberem Default-Fallback."""

import os
from functools import partial
from pathlib import Path

try:
    import yaml
    _YAML_AVAILABLE = True
except ImportError:
    _YAML_AVAILABLE = False

PROJECT_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_CONFIG = {
    "server": {
        "bind_host": "0.0.0.0",
        "bind_port": 5050,
        "debug": False,
    },
    "auth": {
        "secret_key": "change-me-in-production",
        "token_ttl_minutes": 480,
        "cookie_name": "surimon_token",
        "cookie_secure": False,
    },
    "database": {
        "path": str(PROJECT_ROOT / "data" / "suricata-monitor.db"),
    },
    "suricata": {
        # Pfad zur Suricata Unix-Socket fuer suricatasc-Kommandos
        "socket_path": "/var/run/suricata/suricata-command.socket",
        # Pfad zur eve.json
        "eve_path": "/var/log/suricata/eve.json",
        # Pfad zur suricata.yaml (read/write)
        "config_path": "/etc/suricata/suricata.yaml",
        # Hauptverzeichnis fuer aktive Regeldateien
        "rules_dir": "/var/lib/suricata/rules",
        # suricata-update binary
        "update_bin": "/usr/bin/suricata-update",
        # systemctl unit name
        "service_unit": "suricata.service",
        # control_mode: auto | systemctl | socket | command
        # - auto: systemctl falls verfuegbar, sonst Socket-Status
        # - socket: fuer Container ohne systemd
        # - command: optionale reload/status/restart shell commands
        "control_mode": "auto",
        "reload_command": "",
        "restart_command": "",
        "status_command": "",
        # Mock-Modus: wenn True, werden Demo-Daten ausgeliefert
        "mock_mode": True,
    },
    "logging": {
        # Vorbereitete Felder fuer kuenftige Log-Persistenz - aktuell nur Stub
        "persistence_enabled": False,
        "retention_days": 30,
    },
}


def _deep_merge(base, override):
    """Rekursives Mergen, ohne Overrides zu verlieren."""
    result = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def load_config(config_path=None):
    """YAML laden, sonst Defaults. Niemals crashen.

    Pfad-Aufloesung:
    1. Argument ``config_path``
    2. Umgebungsvariable ``SURIMON_CONFIG``
    3. ``config/suricata-monitor.yaml`` im Projekt-Root
    """
    cfg = dict(DEFAULT_CONFIG)

    candidate = config_path or os.environ.get("SURIMON_CONFIG") \
        or str(PROJECT_ROOT / "config" / "suricata-monitor.yaml")

    if _YAML_AVAILABLE and os.path.isfile(candidate):
        try:
            with open(candidate, "r", encoding="utf-8") as fh:
                loaded = yaml.safe_load(fh) or {}
            cfg = _deep_merge(cfg, loaded)
        except (OSError, ValueError):
            # Fallback bleibt der Default - kein Crash bei fehlerhaftem YAML
            pass

    # Sicherstellen, dass das Datenverzeichnis existiert
    db_path = Path(cfg["database"]["path"])
    db_path.parent.mkdir(parents=True, exist_ok=True)

    return cfg


load_config_default = partial(load_config, None)
