#!/usr/bin/env bash
set -Eeuo pipefail

PROXY_IP="${PROXY_IP:-192.168.0.232}"
SURICATA_IP="${SURICATA_IP:-192.168.0.236}"
WIN11_IP="${WIN11_IP:-192.168.0.233}"
TOKEN="${TOKEN:-phimantec-lab-token-v020-change-me}"
PROJECT_DIR="${PROJECT_DIR:-/home/philo/phimantec-c11-appliance}"
ZIP_DIR="${ZIP_DIR:-/home/philo}"
REGENERATE_CA="${REGENERATE_CA:-0}"
START_TEST_UPSTREAM="${START_TEST_UPSTREAM:-1}"
RUN_SMOKE="${RUN_SMOKE:-1}"

trap 'rc=$?; printf "\n[PHIMANTEC-PROXY] Fehler in Zeile %s, Exit-Code %s\n" "$LINENO" "$rc" >&2; exit "$rc"' ERR

export DEBIAN_FRONTEND=noninteractive
export SYSTEMD_PAGER=cat
export SYSTEMD_COLORS=0

log() {
  printf '[PHIMANTEC-PROXY] %s\n' "$*"
}

find_zip() {
  local candidate
  for candidate in \
    "$ZIP_DIR/phimantec-v0.3.18-final.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.3.18-monitor-offline-vendor.zip" \
    "$ZIP_DIR/phimantec-v0.3.9-release.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.3.3-monitor-offline-vendor.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.3.2-monitor-offline-vendor.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.2.0-lab.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.9-lab.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.7-lab-patch7-hyperscan.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.7-lab-patch7.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.6-lab-patch6.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.3-lab-patch3.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.2-lab-patch2.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.1-lab-patch1.zip" \
    "$ZIP_DIR/phimantec-c11-appliance-v0.1.0-lab.zip"
  do
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  candidate="$(find "$ZIP_DIR" -maxdepth 1 -type f -name 'phimantec-c11-appliance*.zip' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1 {print $2}')"
  if [[ -n "$candidate" && -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  return 1
}

log "Installiere Build- und Laufzeitpakete."
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  gcc \
  g++ \
  make \
  cmake \
  pkg-config \
  libssl-dev \
  openssl \
  ca-certificates \
  unzip \
  curl \
  jq \
  netcat-openbsd \
  lsof

log "Toolchain: $(gcc --version | head -n 1)"
log "CMake: $(cmake --version | head -n 1)"
log "OpenSSL: $(openssl version)"

if [[ ! -f "$PROJECT_DIR/CMakeLists.txt" ]]; then
  ZIP_FILE="$(find_zip)" || {
    printf '[PHIMANTEC-PROXY] Kein Projekt-ZIP in %s gefunden.\n' "$ZIP_DIR" >&2
    printf '[PHIMANTEC-PROXY] Kopiere zuerst eines dieser ZIPs nach %s:\n' "$ZIP_DIR" >&2
    printf '  phimantec-v0.3.18-final.zip\n' >&2
    printf '  phimantec-c11-appliance-v0.3.18-monitor-offline-vendor.zip\n' >&2
    printf '  phimantec-v0.3.9-release.zip\n' >&2
    printf '  phimantec-c11-appliance-v0.3.3-monitor-offline-vendor.zip\n' >&2
    printf '  phimantec-c11-appliance-v0.3.2-monitor-offline-vendor.zip\n' >&2
    printf '  phimantec-c11-appliance-v0.2.0-lab.zip\n' >&2
    printf '  phimantec-c11-appliance-v0.1.9-lab.zip\n' >&2
    exit 20
  }
  log "Nutze ZIP: $ZIP_FILE"
  sha256sum "$ZIP_FILE" || true
  TMP_EXTRACT_DIR="$(mktemp -d)"
  rm -rf "$PROJECT_DIR"
  unzip -q "$ZIP_FILE" -d "$TMP_EXTRACT_DIR"
  EXTRACTED_PROJECT="$(find "$TMP_EXTRACT_DIR" -mindepth 1 -maxdepth 2 -type f -name CMakeLists.txt -printf '%h\n' | head -n 1)"
  if [[ -z "$EXTRACTED_PROJECT" || ! -f "$EXTRACTED_PROJECT/CMakeLists.txt" ]]; then
    printf '[PHIMANTEC-PROXY] ZIP enthaelt kein gueltiges Projekt: %s\n' "$ZIP_FILE" >&2
    rm -rf "$TMP_EXTRACT_DIR"
    exit 21
  fi
  mkdir -p "$(dirname "$PROJECT_DIR")"
  mv "$EXTRACTED_PROJECT" "$PROJECT_DIR"
  rm -rf "$TMP_EXTRACT_DIR"
fi
if [[ ! -f "$PROJECT_DIR/CMakeLists.txt" ]]; then
  printf '[PHIMANTEC-PROXY] Projektordner ist nach dem Entpacken nicht vorhanden: %s\n' "$PROJECT_DIR" >&2
  exit 21
fi

cd "$PROJECT_DIR"

log "Baue Projekt."
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPHIMANTEC_ASAN=OFF
cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
ctest --test-dir build --output-on-failure

log "Installiere phimantec-proxyd."
sudo groupadd --system phimantec 2>/dev/null || true
if ! id -u phimantec >/dev/null 2>&1; then
  sudo useradd --system --no-create-home --shell /usr/sbin/nologin --gid phimantec phimantec
fi

sudo install -d -o root -g phimantec -m 0751 /etc/phimantec
sudo install -d -o root -g phimantec -m 0751 /etc/phimantec/ca
sudo install -d -o phimantec -g phimantec -m 0750 /var/cache/phimantec/certs
sudo install -d -o phimantec -g phimantec -m 0750 /var/log/phimantec
sudo install -o root -g root -m 0755 build/phimantec-proxyd /usr/local/bin/phimantec-proxyd

ca_has_required_extensions() {
  local text
  [[ -s /etc/phimantec/ca/phimantec-bump-ca.crt ]] || return 1
  text="$(sudo openssl x509 -in /etc/phimantec/ca/phimantec-bump-ca.crt -noout -text 2>/dev/null || true)"
  [[ -n "$text" ]] || return 1
  printf '%s\n' "$text" | grep -q 'CA:TRUE' || return 1
  printf '%s\n' "$text" | grep -q 'Certificate Sign' || return 1
  printf '%s\n' "$text" | grep -q 'CRL Sign' || return 1
}

ca_key_matches() {
  local cert_pub key_pub rc
  [[ -s /etc/phimantec/ca/phimantec-bump-ca.crt && -s /etc/phimantec/ca/phimantec-bump-ca.key ]] || return 1
  cert_pub="$(mktemp)"
  key_pub="$(mktemp)"
  sudo openssl x509 -in /etc/phimantec/ca/phimantec-bump-ca.crt -noout -pubkey 2>/dev/null | openssl pkey -pubin -outform der 2>/dev/null | openssl sha256 > "$cert_pub" || { rm -f "$cert_pub" "$key_pub"; return 1; }
  sudo openssl pkey -in /etc/phimantec/ca/phimantec-bump-ca.key -pubout -outform der 2>/dev/null | openssl sha256 > "$key_pub" || { rm -f "$cert_pub" "$key_pub"; return 1; }
  if cmp -s "$cert_pub" "$key_pub"; then rc=0; else rc=1; fi
  rm -f "$cert_pub" "$key_pub"
  return "$rc"
}

ca_needs_regeneration() {
  [[ -s /etc/phimantec/ca/phimantec-bump-ca.crt && -s /etc/phimantec/ca/phimantec-bump-ca.key ]] || return 0
  ca_has_required_extensions || return 0
  ca_key_matches || return 0
  sudo openssl x509 -checkend 604800 -noout -in /etc/phimantec/ca/phimantec-bump-ca.crt >/dev/null 2>&1 || return 0
  return 1
}

if [[ "$REGENERATE_CA" = "1" ]] || ca_needs_regeneration; then
  log "Erzeuge browserfaehige Lab-CA."
  sudo rm -f /etc/phimantec/ca/phimantec-bump-ca.crt /etc/phimantec/ca/phimantec-bump-ca.key
  sudo env \
    PHIMANTEC_CA_DIR=/etc/phimantec/ca \
    PHIMANTEC_CA_CN="PHIMANTEC C11 Docker Lab Root CA" \
    PHIMANTEC_CA_ORG="PHIMANTEC" \
    PHIMANTEC_CA_OU="C11 Appliance" \
    PHIMANTEC_CA_DAYS=3650 \
    bash scripts/gen_ca.sh
else
  log "Bestehende browserfaehige Lab-CA bleibt erhalten."
fi

sudo chown root:phimantec /etc/phimantec/ca/phimantec-bump-ca.key
sudo chown root:phimantec /etc/phimantec/ca/phimantec-bump-ca.crt
sudo chmod 0640 /etc/phimantec/ca/phimantec-bump-ca.key
sudo chmod 0751 /etc/phimantec /etc/phimantec/ca
sudo chmod 0644 /etc/phimantec/ca/phimantec-bump-ca.crt
sudo install -m 0644 /etc/phimantec/ca/phimantec-bump-ca.crt /home/philo/phimantec-bump-ca.crt
sudo chown philo:philo /home/philo/phimantec-bump-ca.crt

log "Pruefe CA-Lesbarkeit fuer Service und lokalen Smoke-Test."
sudo -u phimantec test -r /etc/phimantec/ca/phimantec-bump-ca.crt
sudo -u phimantec test -r /etc/phimantec/ca/phimantec-bump-ca.key
test -r /home/philo/phimantec-bump-ca.crt
openssl x509 -in /home/philo/phimantec-bump-ca.crt -noout -subject >/dev/null
CA_TEXT="$(openssl x509 -in /home/philo/phimantec-bump-ca.crt -noout -text)"
printf '%s\n' "$CA_TEXT" | grep -q 'CA:TRUE'
printf '%s\n' "$CA_TEXT" | grep -q 'Certificate Sign'
printf '%s\n' "$CA_TEXT" | grep -q 'CRL Sign'

log "Schreibe Proxy-Konfiguration."
sudo tee /etc/phimantec/phimantec-proxyd.conf >/dev/null <<EOF_CONF
[listener]
bind = "0.0.0.0:3128"

[tls]
ca_cert = "/etc/phimantec/ca/phimantec-bump-ca.crt"
ca_key = "/etc/phimantec/ca/phimantec-bump-ca.key"
cert_cache_dir = "/var/cache/phimantec/certs"
cert_cache_entries = 1024
upstream_verify = false
max_body_bytes = 1048576

[http]
mode = "h1_only"
downstream_alpn = "http/1.1"
upstream_alpn = "http/1.1"
strip_alt_svc = true
strip_upgrade_headers = true
preconnect_grace_ms = 250
first_request_timeout_ms = 1500
idle_tunnel_timeout_ms = 30000

[inspection]
sample_target_bytes = 524288
sample_max_bytes = 1048576
stream_chunk_min_bytes = 524288
queue_max = 1024
queue_wait_ms = 3000
max_inflight = 128
max_inflight_per_host = 32
max_inflight_per_client = 96

[lane]
enabled = true
addr = "$SURICATA_IP:18080"
pool_size = 16

[flowbus]
enabled = true
addr = "$SURICATA_IP:18090"
pool_size = 16
token = "$TOKEN"
timeout_ms = 3000
connect_timeout_ms = 500
read_timeout_ms = 3000
keepalive = false

[network]
connect_timeout_ms = 1200

[verdict]
fail_policy = "closed"
timeout_ms = 3000
timeout_under_load_ms = 8000
EOF_CONF

sudo chown root:phimantec /etc/phimantec/phimantec-proxyd.conf
sudo chmod 0640 /etc/phimantec/phimantec-proxyd.conf

sudo install -o root -g root -m 0644 packaging/systemd/phimantec-proxyd.service /etc/systemd/system/phimantec-proxyd.service

if command -v ufw >/dev/null 2>&1 && sudo ufw status | grep -q active; then
  sudo ufw allow from "$WIN11_IP" to any port 3128 proto tcp
fi

log "Pruefe Suricata-Node Ports."
nc -vz "$SURICATA_IP" 18080
nc -vz "$SURICATA_IP" 18090

if [[ "$START_TEST_UPSTREAM" = "1" ]]; then
  log "Installiere lokalen HTTPS-Test-Upstream auf 127.0.0.1:8443."
  install -d -m 0755 /home/philo/phimantec-upstream
  if [[ ! -f /home/philo/phimantec-upstream/upstream.crt || ! -f /home/philo/phimantec-upstream/upstream.key ]]; then
    openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 30 \
      -keyout /home/philo/phimantec-upstream/upstream.key \
      -out /home/philo/phimantec-upstream/upstream.crt \
      -subj "/CN=127.0.0.1" \
      -addext "subjectAltName=IP:127.0.0.1,DNS:localhost"
  fi

  sudo tee /etc/systemd/system/phimantec-test-upstream.service >/dev/null <<'EOF_UPSTREAM'
[Unit]
Description=PHIMANTEC local HTTPS test upstream
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=philo
Group=philo
ExecStart=/usr/bin/openssl s_server -accept 127.0.0.1:8443 -cert /home/philo/phimantec-upstream/upstream.crt -key /home/philo/phimantec-upstream/upstream.key -www
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF_UPSTREAM
fi

log "Starte Services mit Restart, damit CA/Config sicher neu geladen werden."
sudo systemctl daemon-reload
if [[ "$START_TEST_UPSTREAM" = "1" ]]; then
  sudo systemctl enable phimantec-test-upstream >/dev/null
  sudo systemctl restart phimantec-test-upstream
fi
sudo systemctl enable phimantec-proxyd >/dev/null
sudo systemctl restart phimantec-proxyd

sleep 2

log "Status."
sudo systemctl --no-pager --full status phimantec-proxyd || true
if [[ "$START_TEST_UPSTREAM" = "1" ]]; then
  sudo systemctl --no-pager --full status phimantec-test-upstream || true
fi

log "Listener."
sudo ss -ltnp | grep -E ':(3128|8443)\b' || true

if [[ "$RUN_SMOKE" = "1" ]]; then
  log "Smoke-Test allow/block."
  # The private CA directory under /etc/phimantec/ca is intentionally restricted
  # because it also contains the CA private key. Use the public CA export for
  # user-level curl smoke tests.
  CA="/home/philo/phimantec-bump-ca.crt"
  if [[ ! -r "$CA" ]]; then
    printf '[PHIMANTEC-PROXY] Smoke-CA ist nicht lesbar: %s\n' "$CA" >&2
    exit 77
  fi
  openssl x509 -in "$CA" -noout -subject >/dev/null
  ALLOW_CODE="$(curl --http1.1 -sS \
    --noproxy "" \
    --proxy http://127.0.0.1:3128 \
    --cacert "$CA" \
    -o /tmp/phimantec-allow.html \
    -w "%{http_code}" \
    https://127.0.0.1:8443/)"
  printf 'allow_http_code=%s\n' "$ALLOW_CODE"
  if [[ "$ALLOW_CODE" != "200" ]]; then
    printf '[PHIMANTEC-PROXY] FEHLER: Allow-Smoke-Test erwartet 200, bekam %s.\n' "$ALLOW_CODE" >&2
    sudo journalctl -u phimantec-proxyd --no-pager -n 120 || true
    exit 80
  fi

  BLOCK_CODE="$(curl --http1.1 -sS \
    --noproxy "" \
    --proxy http://127.0.0.1:3128 \
    --cacert "$CA" \
    -o /tmp/phimantec-suricata-block.html \
    -w "%{http_code}" \
    https://127.0.0.1:8443/suricata-block || true)"
  printf 'suricata_block_http_code=%s\n' "$BLOCK_CODE"
  if [[ "$BLOCK_CODE" != "403" ]]; then
    printf '[PHIMANTEC-PROXY] FEHLER: Block-Smoke-Test erwartet 403, bekam %s. Bitte Suricata-Node mit scripts/repair_suricata_rules_vm.sh aus v0.1.6 aktualisieren.\n' "$BLOCK_CODE" >&2
    sudo journalctl -u phimantec-proxyd --no-pager -n 160 || true
    exit 81
  fi
fi

log "Fertig. CA fuer Win11: /home/philo/phimantec-bump-ca.crt"
