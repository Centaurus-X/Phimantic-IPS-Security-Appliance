#!/usr/bin/env bash
set -Eeuo pipefail

log(){ printf '[phimantec-stop-legacy] %s\n' "$*"; }
run_priv(){
  if [[ "$(id -u)" == 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo -n "$@"
  else
    return 1
  fi
}

units=(phimantec-proxyd.service phimantec-inspectord.service phimantec-lane-sink.service suricata-monitor.service)
for unit in "${units[@]}"; do
  if systemctl list-unit-files "$unit" >/dev/null 2>&1; then
    run_priv systemctl disable --now "$unit" >/dev/null 2>&1 || true
    log "gestoppt/deaktiviert, falls vorhanden: $unit"
  fi
done
