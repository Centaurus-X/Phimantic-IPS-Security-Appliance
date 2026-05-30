#!/usr/bin/env bash
set -Eeuo pipefail

PROXY_IP="${PROXY_IP:-192.168.0.232}"
RULE_FILE="${RULE_FILE:-/etc/suricata/rules/phimantec-local.rules}"
SURICATA_YAML="${SURICATA_YAML:-/etc/suricata/suricata.yaml}"
SURICATA_LOG_DIR="${SURICATA_LOG_DIR:-/var/log/suricata}"
TOKEN="${TOKEN:-phimantec-lab-token-2026-05-24-change-me}"
PROJECT_DIR="${PROJECT_DIR:-/home/philo/phimantec-c11-appliance}"

trap 'rc=$?; printf "\n[PHIMANTEC-PATCH] Fehler in Zeile %s, Exit-Code %s\n" "$LINENO" "$rc" >&2; exit "$rc"' ERR
export SYSTEMD_PAGER=cat
export SYSTEMD_COLORS=0

printf '[PHIMANTEC-PATCH] Proxy-IP: %s\n' "$PROXY_IP"
LANE_IF="$(ip -o route get "$PROXY_IP" | awk '{for (i=1;i<=NF;i++) if ($i=="dev") {print $(i+1); exit}}')"
if [[ -z "$LANE_IF" ]]; then
  printf '[PHIMANTEC-PATCH] Konnte Interface Richtung Proxy nicht ermitteln.\n' >&2
  exit 1
fi
printf '[PHIMANTEC-PATCH] Lane Interface: %s\n' "$LANE_IF"

sudo groupadd --system phimantec 2>/dev/null || true
if ! id -u phimantec >/dev/null 2>&1; then
  sudo useradd --system --no-create-home --shell /usr/sbin/nologin --gid phimantec phimantec
fi
sudo usermod -aG adm phimantec || true

sudo install -d -o root -g phimantec -m 0750 /etc/phimantec
sudo install -d -o phimantec -g phimantec -m 0750 /var/log/phimantec
sudo install -d -o root -g root -m 0755 /etc/suricata/rules
sudo install -d -o root -g root -m 0755 "$SURICATA_LOG_DIR"

if [[ -x "$PROJECT_DIR/build/phimantec-lane-sink" ]]; then
  sudo install -o root -g root -m 0755 "$PROJECT_DIR/build/phimantec-lane-sink" /usr/local/bin/phimantec-lane-sink
fi
if [[ -x "$PROJECT_DIR/build/phimantec-inspectord" ]]; then
  sudo install -o root -g root -m 0755 "$PROJECT_DIR/build/phimantec-inspectord" /usr/local/bin/phimantec-inspectord
fi

sudo tee /etc/phimantec/phimantec-suricata-node.conf >/dev/null <<EOF_CONF
[lane]
enabled = true
addr = "0.0.0.0:18080"
log_path = "/var/log/phimantec/lane.jsonl"

[flowbus]
enabled = true
addr = "0.0.0.0:18090"
pool_size = 16
token = "$TOKEN"
timeout_ms = 3000
connect_timeout_ms = 500
read_timeout_ms = 3000
keepalive = false

[verdict]
fail_policy = "closed"
timeout_ms = 900
no_alert_grace_ms = 100

[suricata]
eve_path = "/var/log/suricata/eve.json"
EOF_CONF
sudo chown root:phimantec /etc/phimantec/phimantec-suricata-node.conf
sudo chmod 0640 /etc/phimantec/phimantec-suricata-node.conf

sudo tee "$RULE_FILE" >/dev/null <<'RULES_EOF'
# PHIMANTEC lab rule set for Suricata 8.
# Suricata is the only blocking decision source. The proxy and inspectord never
# block on local content heuristics. A block verdict is emitted only when
# inspectord correlates one of these HTTP-layer EVE alerts to the PHIMANTEC
# Flow-ID transported by the lane URL/header.
#
# The clear lane is an HTTP POST to phimantec-lane-sink on TCP/18080:
#   POST /phimantec/lane/<direction>/<flow_id> HTTP/1.1
# The original request/response sample is carried as HTTP request body.

alert http any any -> any 18080 (msg:"PHIMANTEC BODY BLOCK"; flow:established,to_server; http.uri; content:"/phimantec/lane/"; http.request_body; content:"PHIMANTEC_TEST_BLOCK"; sid:900001; rev:4;)
alert http any any -> any 18080 (msg:"PHIMANTEC EICAR TEST FILE"; flow:established,to_server; http.uri; content:"/phimantec/lane/"; http.request_body; content:"EICAR-STANDARD-ANTIVIRUS-TEST-FILE"; nocase; sid:900002; rev:2;)
alert http any any -> any 18080 (msg:"PHIMANTEC EICAR TEST PATH"; flow:established,to_server; http.uri; content:"/phimantec/lane/"; http.header; content:"X-Phim-Path|3a|"; nocase; content:"eicar"; nocase; distance:0; within:256; sid:900004; rev:1;)
alert http any any -> any 18080 (msg:"PHIMANTEC SURICATA PATH BLOCK"; flow:established,to_server; http.uri; content:"/phimantec/lane/"; http.header; content:"X-Phim-Path|3a|"; nocase; content:"/suricata-block"; nocase; sid:900003; rev:4;)
RULES_EOF
sudo chmod 0644 "$RULE_FILE"


patch_http_body_limits() {
  local file="$1"
  local body_limit="${PHIMANTEC_SURICATA_BODY_LIMIT:-1048576}"
  sudo python3 - "$file" "$body_limit" <<'PYCONF'
import sys
try:
    import yaml
except ImportError:
    raise SystemExit("python3-yaml is required to patch Suricata HTTP body limits")
path = sys.argv[1]
body_limit = int(sys.argv[2])
with open(path, "r", encoding="utf-8", errors="replace") as handle:
    data = yaml.safe_load(handle) or {}
app_layer = data.setdefault("app-layer", {})
protocols = app_layer.setdefault("protocols", {})
http = protocols.setdefault("http", {})
http["enabled"] = True
libhtp = http.setdefault("libhtp", {})
default_config = libhtp.setdefault("default-config", {})
for key in (
    "request-body-limit",
    "response-body-limit",
    "request-body-minimal-inspect-size",
    "request-body-inspect-window",
    "response-body-minimal-inspect-size",
    "response-body-inspect-window",
):
    default_config[key] = body_limit
with open(path, "w", encoding="utf-8") as handle:
    yaml.safe_dump(data, handle, sort_keys=False)
PYCONF
}

set_yaml_scalar() {
  local key="$1"
  local value="$2"
  local file="$3"

  if sudo grep -qE "^[[:space:]]*#?[[:space:]]*${key}:" "$file"; then
    sudo sed -i -E "0,/^[[:space:]]*#?[[:space:]]*${key}:.*/s//${key}: ${value}/" "$file"
  else
    printf '\n%s: %s\n' "$key" "$value" | sudo tee -a "$file" >/dev/null
  fi
}

printf '[PHIMANTEC-PATCH] Setze HTTP-Body-Inspektionstiefe fuer PHIMANTEC-Lane.\n'
patch_http_body_limits "$SURICATA_YAML"

printf '[PHIMANTEC-PATCH] EVE-Payload-Optionen aktivieren, falls kommentiert.\n'
sudo sed -i -E 's/^([[:space:]]*)# payload: yes/\1payload: yes/' "$SURICATA_YAML" || true
sudo sed -i -E 's/^([[:space:]]*)# payload-printable: yes/\1payload-printable: yes/' "$SURICATA_YAML" || true
sudo sed -i -E 's/^([[:space:]]*)# http-body: yes/\1http-body: yes/' "$SURICATA_YAML" || true
sudo sed -i -E 's/^([[:space:]]*)# http-body-printable: yes/\1http-body-printable: yes/' "$SURICATA_YAML" || true

PHIMANTEC_SURICATA_MPM="${PHIMANTEC_SURICATA_MPM:-hs}"
PHIMANTEC_SURICATA_SPM="${PHIMANTEC_SURICATA_SPM:-hs}"
PHIMANTEC_REQUIRE_HYPERSCAN="${PHIMANTEC_REQUIRE_HYPERSCAN:-0}"
PHIMANTEC_SURICATA_ALLOW_AC_FALLBACK="${PHIMANTEC_SURICATA_ALLOW_AC_FALLBACK:-1}"

if [[ "$PHIMANTEC_SURICATA_MPM" = "hs" || "$PHIMANTEC_SURICATA_SPM" = "hs" ]]; then
  if suricata --build-info 2>/dev/null | grep -qiE 'Hyperscan support:[[:space:]]*yes'; then
    printf '%s\n' '[PHIMANTEC-PATCH] Hyperscan erkannt. Explizite hs-Konfiguration bleibt aktiv.'
  elif [[ "$PHIMANTEC_REQUIRE_HYPERSCAN" = "1" ]]; then
    printf '%s\n' '[PHIMANTEC-PATCH] FEHLER: Hyperscan wurde angefordert, aber Suricata meldet keine Hyperscan-Unterstuetzung.' >&2
    exit 12
  elif [[ "$PHIMANTEC_SURICATA_ALLOW_AC_FALLBACK" = "1" ]]; then
    printf '%s\n' '[PHIMANTEC-PATCH] WARNUNG: Hyperscan nicht verfuegbar. Einmaliger Fallback auf mpm=ac-ks, spm=auto.' >&2
    PHIMANTEC_SURICATA_MPM="ac-ks"
    PHIMANTEC_SURICATA_SPM="auto"
  else
    printf '%s\n' '[PHIMANTEC-PATCH] FEHLER: Hyperscan nicht verfuegbar und Fallback deaktiviert.' >&2
    exit 12
  fi
fi

printf '[PHIMANTEC-PATCH] Setze Suricata Pattern-Matcher: mpm-algo=%s, spm-algo=%s\n' "$PHIMANTEC_SURICATA_MPM" "$PHIMANTEC_SURICATA_SPM"
set_yaml_scalar 'mpm-algo' "$PHIMANTEC_SURICATA_MPM" "$SURICATA_YAML"
set_yaml_scalar 'spm-algo' "$PHIMANTEC_SURICATA_SPM" "$SURICATA_YAML"

echo '[PHIMANTEC-PATCH] Aktive Pattern-Matcher-Konfiguration:'
sudo grep -nE '^[[:space:]]*(mpm-algo|spm-algo):' "$SURICATA_YAML" || true

sudo systemctl stop suricata-phimantec phimantec-inspectord phimantec-lane-sink suricata 2>/dev/null || true
sudo rm -f "$SURICATA_LOG_DIR/eve.json"
sudo touch "$SURICATA_LOG_DIR/eve.json"
sudo chmod 0644 "$SURICATA_LOG_DIR/eve.json"

if ! sudo suricata -T -c "$SURICATA_YAML" --af-packet="$LANE_IF" -S "$RULE_FILE" -l "$SURICATA_LOG_DIR" -k none; then
  if [[ "$PHIMANTEC_SURICATA_ALLOW_AC_FALLBACK" = "1" && ( "$PHIMANTEC_SURICATA_MPM" = "hs" || "$PHIMANTEC_SURICATA_SPM" = "hs" ) ]]; then
    if [[ "$PHIMANTEC_REQUIRE_HYPERSCAN" = "1" ]]; then
      printf '%s\n' '[PHIMANTEC-PATCH] FEHLER: Suricata-Konfiguration mit Hyperscan ist fehlgeschlagen und PHIMANTEC_REQUIRE_HYPERSCAN=1.' >&2
      exit 13
    fi
    printf '%s\n' '[PHIMANTEC-PATCH] WARNUNG: Suricata-Test mit Hyperscan fehlgeschlagen. Einmaliger Fallback auf mpm=ac-ks, spm=auto.' >&2
    PHIMANTEC_SURICATA_MPM="ac-ks"
    PHIMANTEC_SURICATA_SPM="auto"
    set_yaml_scalar 'mpm-algo' "$PHIMANTEC_SURICATA_MPM" "$SURICATA_YAML"
    set_yaml_scalar 'spm-algo' "$PHIMANTEC_SURICATA_SPM" "$SURICATA_YAML"
    sudo suricata -T -c "$SURICATA_YAML" --af-packet="$LANE_IF" -S "$RULE_FILE" -l "$SURICATA_LOG_DIR" -k none
  else
    printf '%s\n' '[PHIMANTEC-PATCH] FEHLER: Suricata-Konfigurationstest fehlgeschlagen. Kein Reparaturloop.' >&2
    exit 14
  fi
fi
sudo tee /etc/systemd/system/suricata-phimantec.service >/dev/null <<SERVICE_EOF
[Unit]
Description=Suricata PHIMANTEC AF_PACKET Lab
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=60
StartLimitBurst=3

[Service]
Type=simple
ExecStart=/usr/bin/suricata -c $SURICATA_YAML --af-packet=$LANE_IF -S $RULE_FILE -l $SURICATA_LOG_DIR -k none
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
SERVICE_EOF

if [[ -f "$PROJECT_DIR/packaging/systemd/phimantec-lane-sink.service" ]]; then
  sudo install -o root -g root -m 0644 "$PROJECT_DIR/packaging/systemd/phimantec-lane-sink.service" /etc/systemd/system/phimantec-lane-sink.service
fi
if [[ -f "$PROJECT_DIR/packaging/systemd/phimantec-inspectord.service" ]]; then
  sudo install -o root -g root -m 0644 "$PROJECT_DIR/packaging/systemd/phimantec-inspectord.service" /etc/systemd/system/phimantec-inspectord.service
fi

if command -v ufw >/dev/null 2>&1; then
  sudo ufw allow from "$PROXY_IP" to any port 18080 proto tcp || true
  sudo ufw allow from "$PROXY_IP" to any port 18090 proto tcp || true
fi
if command -v iptables >/dev/null 2>&1; then
  sudo iptables -C INPUT -p tcp -s "$PROXY_IP" --dport 18080 -j ACCEPT 2>/dev/null || sudo iptables -I INPUT 1 -p tcp -s "$PROXY_IP" --dport 18080 -j ACCEPT
  sudo iptables -C INPUT -p tcp -s "$PROXY_IP" --dport 18090 -j ACCEPT 2>/dev/null || sudo iptables -I INPUT 1 -p tcp -s "$PROXY_IP" --dport 18090 -j ACCEPT
fi

sudo systemctl daemon-reload
sudo systemctl enable --now suricata-phimantec
sudo systemctl enable --now phimantec-lane-sink
sudo systemctl enable --now phimantec-inspectord
sleep 3

sudo systemctl --no-pager --full status suricata-phimantec phimantec-lane-sink phimantec-inspectord || true
sudo ss -ltnp | grep -E ':(18080|18090)\b' || true
sudo cat "$RULE_FILE"
printf '[PHIMANTEC-PATCH] Fertig.\n'
