#!/usr/bin/env bash
#
# SuricataMonitor Installer
#
# Verwendet werden:
#   - Ubuntu-Systempakete python3-flask + python3-yaml
#   - optionaler sudoers-Eintrag fuer Suricata-Service-Control
#
# Tested on: Ubuntu 22.04, Ubuntu 24.04, Debian 12.

set -euo pipefail

INSTALL_DIR="${INSTALL_DIR:-/opt/suricata-monitor}"
CONFIG_DIR="${CONFIG_DIR:-/etc/suricata-monitor}"
DATA_DIR="${DATA_DIR:-/var/lib/suricata-monitor}"
LOG_DIR="${LOG_DIR:-/var/log/suricata-monitor}"
SERVICE_USER="${SERVICE_USER:-suricata-monitor}"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mXX\033[0m %s\n' "$*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || die "Bitte als root ausfuehren (sudo)."

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
log "Source: ${SRC_DIR}"
log "Install: ${INSTALL_DIR}"

# --------------------------------------------------------------------------
# 1. Systemvoraussetzungen
# --------------------------------------------------------------------------
log "Systempakete installieren …"
apt-get update -qq
apt-get install -y --no-install-recommends \
    python3 python3-flask python3-yaml \
    >/dev/null

# --------------------------------------------------------------------------
# 2. User + Verzeichnisse
# --------------------------------------------------------------------------
if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
    log "Benutzer ${SERVICE_USER} anlegen …"
    useradd --system --no-create-home --shell /usr/sbin/nologin \
        --home-dir "$DATA_DIR" "$SERVICE_USER"
fi

mkdir -p "$INSTALL_DIR" "$CONFIG_DIR" "$DATA_DIR" "$LOG_DIR"

# --------------------------------------------------------------------------
# 3. Code kopieren
# --------------------------------------------------------------------------
log "Code nach ${INSTALL_DIR} kopieren …"
cp -r "${SRC_DIR}/backend"  "${INSTALL_DIR}/"
cp -r "${SRC_DIR}/frontend" "${INSTALL_DIR}/"
cp -r "${SRC_DIR}/scripts"  "${INSTALL_DIR}/"

# --------------------------------------------------------------------------
# 4. Python-Abhaengigkeiten
# --------------------------------------------------------------------------
log "Python-Abhaengigkeiten pruefen …"
python3 - <<'PY_CHECK'
import flask
import yaml
PY_CHECK
# --------------------------------------------------------------------------
# 5. Konfiguration
# --------------------------------------------------------------------------
if [[ ! -f "${CONFIG_DIR}/suricata-monitor.yaml" ]]; then
    log "Konfiguration nach ${CONFIG_DIR}/suricata-monitor.yaml kopieren …"
    cp "${SRC_DIR}/config/suricata-monitor.example.yaml" \
       "${CONFIG_DIR}/suricata-monitor.yaml"
    SECRET=$(python3 -c 'import secrets; print(secrets.token_hex(32))')
    sed -i "s/CHANGE-ME-PLEASE-USE-LONG-RANDOM-STRING/${SECRET}/" \
        "${CONFIG_DIR}/suricata-monitor.yaml"
    sed -i "s|path: \"/var/lib/suricata-monitor/suricata-monitor.db\"|path: \"${DATA_DIR}/suricata-monitor.db\"|" \
        "${CONFIG_DIR}/suricata-monitor.yaml"
    chmod 640 "${CONFIG_DIR}/suricata-monitor.yaml"
else
    log "Vorhandene Konfiguration bleibt: ${CONFIG_DIR}/suricata-monitor.yaml"
fi

# --------------------------------------------------------------------------
# 6. Berechtigungen
# --------------------------------------------------------------------------
chown -R "$SERVICE_USER:$SERVICE_USER" \
    "$INSTALL_DIR" "$DATA_DIR" "$LOG_DIR" "$CONFIG_DIR"

# --------------------------------------------------------------------------
# 7. DB initialisieren (App tut das selbst beim ersten Start, aber wir
#     wollen direkt nach Install den Admin sehen koennen).
# --------------------------------------------------------------------------
log "Datenbank initialisieren …"
SURIMON_CONFIG="${CONFIG_DIR}/suricata-monitor.yaml" \
    python3 "${INSTALL_DIR}/scripts/init_db.py"
chown -R "$SERVICE_USER:$SERVICE_USER" "$DATA_DIR" "$LOG_DIR"

# --------------------------------------------------------------------------
# 8. Sudoers fuer Service-Control (optional)
# --------------------------------------------------------------------------
SUDOERS_FILE="/etc/sudoers.d/suricata-monitor"
if [[ ! -f "$SUDOERS_FILE" ]]; then
    log "Sudoers-Eintrag fuer Suricata-Restart anlegen …"
    cat > "$SUDOERS_FILE" <<EOF
# SuricataMonitor darf Suricata neu starten und Status abfragen
${SERVICE_USER} ALL=(root) NOPASSWD: /bin/systemctl restart suricata.service
${SERVICE_USER} ALL=(root) NOPASSWD: /bin/systemctl reload suricata.service
${SERVICE_USER} ALL=(root) NOPASSWD: /bin/systemctl is-active suricata.service
EOF
    chmod 440 "$SUDOERS_FILE"
fi

# --------------------------------------------------------------------------
# 9. systemd Unit
# --------------------------------------------------------------------------
log "systemd Unit installieren …"
sed -e "s|@INSTALL_DIR@|${INSTALL_DIR}|g" \
    -e "s|@CONFIG_DIR@|${CONFIG_DIR}|g" \
    -e "s|@SERVICE_USER@|${SERVICE_USER}|g" \
    "${SRC_DIR}/systemd/suricata-monitor.service" \
    > /etc/systemd/system/suricata-monitor.service
systemctl daemon-reload

cat <<EOF

================================================================
SuricataMonitor ist installiert.

Naechste Schritte:
  1. Konfiguration pruefen:
       sudo \$EDITOR ${CONFIG_DIR}/suricata-monitor.yaml
     - mock_mode: false setzen, wenn echte Suricata existiert
     - bind_host/bind_port anpassen falls hinter Reverse-Proxy
  2. Service starten:
       sudo systemctl enable --now suricata-monitor
  3. Im Browser oeffnen (Default Port 5050):
       http://localhost:5050/
  4. Erstes Login:  admin / changeme
       -> SOFORT Passwort aendern in Settings -> Benutzer

Konfiguration: ${CONFIG_DIR}/suricata-monitor.yaml
Datenbank:     ${DATA_DIR}/suricata-monitor.db
Logs:          journalctl -u suricata-monitor -f
================================================================
EOF
