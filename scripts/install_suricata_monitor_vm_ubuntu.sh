#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_DIR="${PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
INSTALL_DIR="${INSTALL_DIR:-/opt/phimantec/suricata-monitor}"
CONFIG_DIR="${CONFIG_DIR:-/etc/phimantec}"
SERVICE_USER="${SERVICE_USER:-phimantec}"
PHIMANTEC_SKIP_APT="${PHIMANTEC_SKIP_APT:-0}"

log() {
  printf '[PHIMANTEC-MONITOR-INSTALL] %s\n' "$*"
}

ensure_python_deps() {
  if python3 - <<'PY' >/dev/null 2>&1
import flask
import yaml
PY
  then
    log "Python-Systempakete fuer Flask/PyYAML sind vorhanden."
    return 0
  fi

  if [[ "$PHIMANTEC_SKIP_APT" == "1" ]]; then
    log "PHIMANTEC_SKIP_APT=1, installiere keine Systempakete. Flask/PyYAML muessen vorhanden sein."
    return 0
  fi

  log "Installiere minimale Ubuntu-Python-Pakete: python3, python3-flask, python3-yaml"
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends python3 python3-flask python3-yaml
}

ensure_python_deps

sudo groupadd --system phimantec 2>/dev/null || true
if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
  sudo useradd --system --no-create-home --shell /usr/sbin/nologin --gid phimantec "$SERVICE_USER"
fi
sudo usermod -aG adm "$SERVICE_USER" || true

sudo install -d -o root -g phimantec -m 0750 "$CONFIG_DIR"
sudo install -d -o "$SERVICE_USER" -g phimantec -m 0750 /var/lib/suricata-monitor /var/log/suricata-monitor
sudo rm -rf "$INSTALL_DIR"
sudo install -d -o root -g root -m 0755 "$INSTALL_DIR"
sudo cp -a "$PROJECT_DIR/suricata-monitor/." "$INSTALL_DIR/"

sudo install -o root -g phimantec -m 0640 \
  "$PROJECT_DIR/config/suricata-monitor/phimantec-suricata-monitor.yaml" \
  "$CONFIG_DIR/suricata-monitor.yaml"

sudo install -o root -g root -m 0644 \
  "$PROJECT_DIR/packaging/systemd/suricata-monitor.service" \
  /etc/systemd/system/suricata-monitor.service

sudo env SURIMON_CONFIG="$CONFIG_DIR/suricata-monitor.yaml" /usr/bin/python3 "$INSTALL_DIR/scripts/init_db.py"
sudo chown -R "$SERVICE_USER":phimantec /var/lib/suricata-monitor /var/log/suricata-monitor
sudo systemctl daemon-reload
sudo systemctl enable --now suricata-monitor.service
sudo systemctl --no-pager --full status suricata-monitor.service || true
