#!/usr/bin/env bash
# Dev-Helper: virtualenv anlegen, Konfig sicherstellen, App im Debug-Modus starten.
#
# Verwendung aus dem Projekt-Root:
#   bash scripts/run_dev.sh

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

if [[ ! -d venv ]]; then
    echo "Lege venv an…"
    python3 -m venv venv
    ./venv/bin/pip install --upgrade pip wheel
    ./venv/bin/pip install -r backend/requirements.txt
fi

if [[ ! -f config/suricata-monitor.yaml ]]; then
    echo "Kopiere Beispiel-Konfiguration…"
    cp config/suricata-monitor.example.yaml config/suricata-monitor.yaml
    echo "Dev-Modus: debug=true setzen…"
    sed -i 's/^  debug: false/  debug: true/' config/suricata-monitor.yaml
fi

mkdir -p data
echo "Starte SuricataMonitor (Dev) auf http://127.0.0.1:5050"
SURIMON_CONFIG="${PROJECT_ROOT}/config/suricata-monitor.yaml" \
    ./venv/bin/python backend/app.py
