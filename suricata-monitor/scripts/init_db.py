#!/usr/bin/env python3
"""DB initialisieren (Schema anlegen + Default-Admin erstellen).

Standalone-Script. Wird vom install.sh aufgerufen, kann aber auch manuell
ausgefuehrt werden:

    SURIMON_CONFIG=/etc/suricata-monitor/suricata-monitor.yaml python3 scripts/init_db.py
"""

import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "backend"))

from config import load_config
from db.connection import init_db_pool


def main():
    config_path = os.environ.get("SURIMON_CONFIG")
    cfg = load_config(config_path)
    db_path = cfg["database"]["path"]
    print("Initialisiere DB unter: " + db_path)
    init_db_pool(db_path)
    print("Fertig. Default-Admin: admin / changeme (bitte aendern!).")


if __name__ == "__main__":
    main()
