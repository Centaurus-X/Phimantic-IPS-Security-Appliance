#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "$0")/.."

export SURIMON_CONFIG="${SURIMON_CONFIG:-config/suricata-monitor/phimantec-suricata-monitor.yaml}"

if python3 - <<'PY' >/dev/null 2>&1
import flask
import yaml
PY
then
  python3 suricata-monitor/scripts/init_db.py
  exec python3 suricata-monitor/backend/app.py
fi

python3 -m venv suricata-monitor/.venv
suricata-monitor/.venv/bin/pip install --no-cache-dir -r suricata-monitor/backend/requirements.txt
suricata-monitor/.venv/bin/python suricata-monitor/scripts/init_db.py
exec suricata-monitor/.venv/bin/python suricata-monitor/backend/app.py
