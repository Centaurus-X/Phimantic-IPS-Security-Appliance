#!/usr/bin/env bash
set -Eeuo pipefail

SURICATA_YAML="${SURICATA_YAML:-/etc/suricata/suricata.yaml}"
RULE_FILE="${RULE_FILE:-/etc/suricata/rules/phimantec-local.rules}"
SURICATA_LOG_DIR="${SURICATA_LOG_DIR:-/var/log/suricata}"
PROXY_IP="${PROXY_IP:-192.168.0.232}"

trap 'rc=$?; printf "\n[PHIMANTEC-HS-CHECK] Fehler in Zeile %s, Exit-Code %s\n" "$LINENO" "$rc" >&2; exit "$rc"' ERR

LANE_IF="$(ip -o route get "$PROXY_IP" | awk '{for (i=1;i<=NF;i++) if ($i=="dev") {print $(i+1); exit}}')"
if [[ -z "$LANE_IF" ]]; then
  printf '[PHIMANTEC-HS-CHECK] FEHLER: Konnte Interface Richtung Proxy-IP %s nicht ermitteln.\n' "$PROXY_IP" >&2
  exit 1
fi

printf '[PHIMANTEC-HS-CHECK] Suricata Build Features:\n'
suricata --build-info | grep -iE 'Hyperscan|AF_PACKET|NFQ|PCRE|JIT' || true

printf '\n[PHIMANTEC-HS-CHECK] Aktive YAML Pattern-Matcher:\n'
grep -nE '^[[:space:]]*(mpm-algo|spm-algo):' "$SURICATA_YAML" || true

printf '\n[PHIMANTEC-HS-CHECK] Suricata-Konfigurationstest mit Interface %s:\n' "$LANE_IF"
sudo suricata -T \
  -c "$SURICATA_YAML" \
  --af-packet="$LANE_IF" \
  -S "$RULE_FILE" \
  -l "$SURICATA_LOG_DIR" \
  -k none

printf '\n[PHIMANTEC-HS-CHECK] OK.\n'
