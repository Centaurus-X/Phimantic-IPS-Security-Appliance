#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="${PHIMANTEC_ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ENV_FILE="${PHIMANTEC_ENV_FILE:-${ROOT_DIR}/.env}"
COMMENT="phimantec-appliance-ingress"
SERVICE_NAME="phimantec-appliance-ingress.service"

log(){ printf '[phimantec-ingress] %s\n' "$*"; }
warn(){ printf '[phimantec-ingress] WARN: %s\n' "$*" >&2; }
fail(){ printf '[phimantec-ingress] ERROR: %s\n' "$*" >&2; exit 67; }

trim_text(){
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s\n' "$value"
}

strip_outer_quotes(){
  local value="$1"
  if [[ ${#value} -ge 2 ]]; then
    if [[ "${value:0:1}" == '"' && "${value: -1}" == '"' ]]; then
      value="${value:1:${#value}-2}"
    elif [[ "${value:0:1}" == "'" && "${value: -1}" == "'" ]]; then
      value="${value:1:${#value}-2}"
    fi
  fi
  printf '%s\n' "$value"
}

read_env_value(){
  local key="$1" file="$2" line env_key value
  [[ -f "$file" ]] || return 0
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%$'\r'}"
    [[ -n "$(trim_text "$line")" ]] || continue
    [[ "$(trim_text "$line")" != \#* ]] || continue
    [[ "$line" == *=* ]] || continue
    env_key="$(trim_text "${line%%=*}")"
    [[ "$env_key" == "$key" ]] || continue
    value="$(trim_text "${line#*=}")"
    strip_outer_quotes "$value"
    return 0
  done < "$file"
}

bind_port(){
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value##*:}" || printf '%s\n' "$value"
}

run_priv(){
  if [[ "$(id -u)" == 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    return 1
  fi
}

bool_env(){
  local key="$1" default_value="$2" value
  value="${!key:-}"
  [[ -n "$value" ]] || value="$(read_env_value "$key" "$ENV_FILE")"
  [[ -n "$value" ]] || value="$default_value"
  [[ "$value" == 1 || "$value" == true || "$value" == yes ]]
}

resolve_iface(){
  local value dev
  value="${PHIMANTEC_APPLIANCE_INGRESS_IFACE:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_APPLIANCE_INGRESS_IFACE "$ENV_FILE")"
  [[ -n "$value" ]] || value=auto
  if [[ "$value" != auto ]]; then
    printf '%s\n' "$value"
    return 0
  fi
  dev="$(ip -o route show default 2>/dev/null | awk '{for (i=1; i<=NF; i++) if ($i=="dev") {print $(i+1); exit}}' | head -n1)"
  [[ -n "$dev" ]] || dev="$(ip -o route get 1.1.1.1 2>/dev/null | awk '{for (i=1; i<=NF; i++) if ($i=="dev") {print $(i+1); exit}}' | head -n1)"
  [[ -n "$dev" ]] || dev="$(ip -o link show up 2>/dev/null | awk -F': ' '$2 != "lo" {print $2; exit}' | cut -d@ -f1)"
  [[ -n "$dev" ]] || fail "kein Ingress-Interface ermittelbar; setze PHIMANTEC_APPLIANCE_INGRESS_IFACE=ens18 oder passendes Interface"
  printf '%s\n' "$dev"
}

runtime_ports(){
  local proxy monitor
  proxy="${PHIMANTEC_PROXY_PORT:-}"
  monitor="${PHIMANTEC_MONITOR_PORT:-}"
  [[ -n "$proxy" ]] || proxy="$(read_env_value PHIMANTEC_PROXY_PORT "$ENV_FILE")"
  [[ -n "$monitor" ]] || monitor="$(read_env_value PHIMANTEC_MONITOR_PORT "$ENV_FILE")"
  [[ -n "$proxy" ]] || proxy=3128
  [[ -n "$monitor" ]] || monitor=5050
  printf '%s\n' "$(bind_port "$proxy")" "$(bind_port "$monitor")" | awk 'NF && !seen[$0]++'
}

ingress_source(){
  local value
  value="${PHIMANTEC_APPLIANCE_INGRESS_SOURCE:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_APPLIANCE_INGRESS_SOURCE "$ENV_FILE")"
  printf '%s\n' "$value"
}

list_input_chains(){
  command -v nft >/dev/null 2>&1 || return 0
  nft list ruleset 2>/dev/null | awk '
    /^table[[:space:]]+(ip|ip6|inet)[[:space:]]+/ {
      family=$2; table=$3; gsub(/[{}]/, "", table)
    }
    /^[[:space:]]*chain[[:space:]]+/ {
      chain=$2; gsub(/[{}]/, "", chain)
    }
    /type[[:space:]]+filter[[:space:]]+hook[[:space:]]+input/ {
      if (family != "" && table != "" && chain != "") print family, table, chain
    }'
}

chain_has_rule(){
  local family="$1" table="$2" chain="$3" iface="$4" port="$5"
  nft list chain "$family" "$table" "$chain" 2>/dev/null \
    | grep -F "${COMMENT}" \
    | grep -F "dport ${port}" \
    | grep -F "iifname \"${iface}\"" >/dev/null 2>&1
}

nft_rule_args(){
  local family="$1" iface="$2" port="$3" source="$4"
  printf '%s\0' iifname "$iface"
  if [[ -n "$source" && "$family" != ip6 ]]; then
    printf '%s\0' ip saddr "$source"
  fi
  printf '%s\0' tcp dport "$port" accept comment "$COMMENT"
}

install_nft_rules(){
  command -v nft >/dev/null 2>&1 || { warn "nft nicht gefunden; nft-Regeln uebersprungen"; return 1; }
  local iface="$1" source="$2" port family table chain found installed
  found=0
  installed=0
  while read -r family table chain; do
    [[ -n "$family" && -n "$table" && -n "$chain" ]] || continue
    found=1
    while read -r port; do
      [[ "$port" =~ ^[0-9]+$ ]] || fail "ungueltiger Port: ${port}"
      if chain_has_rule "$family" "$table" "$chain" "$iface" "$port"; then
        log "nft bereits vorhanden: ${family} ${table} ${chain} ${iface}:${port}"
        continue
      fi
      if [[ -n "$source" && "$family" == ip6 ]]; then
        continue
      fi
      mapfile -d '' -t args < <(nft_rule_args "$family" "$iface" "$port" "$source")
      run_priv nft insert rule "$family" "$table" "$chain" "${args[@]}" || fail "nft-Regel konnte nicht gesetzt werden: ${family} ${table} ${chain} ${iface}:${port}"
      log "nft akzeptiert: ${family} ${table}/${chain} ${iface}:${port}${source:+ source=${source}}"
      installed=1
    done < <(runtime_ports)
  done < <(list_input_chains)

  if [[ "$found" == 0 ]]; then
    warn "keine nft input-hook-chain gefunden; pruefe iptables-Fallback"
    return 1
  fi
  [[ "$installed" == 1 ]] || true
  return 0
}

iptables_rule_exists(){
  local iface="$1" port="$2"
  iptables -C INPUT -i "$iface" -p tcp --dport "$port" -m comment --comment "$COMMENT" -j ACCEPT >/dev/null 2>&1
}

install_iptables_rules(){
  command -v iptables >/dev/null 2>&1 || { warn "iptables nicht gefunden; iptables-Fallback uebersprungen"; return 1; }
  local iface="$1" source="$2" port args failed
  failed=0
  while read -r port; do
    [[ "$port" =~ ^[0-9]+$ ]] || fail "ungueltiger Port: ${port}"
    if iptables_rule_exists "$iface" "$port"; then
      log "iptables bereits vorhanden: INPUT ${iface}:${port}"
      continue
    fi
    args=(-I INPUT -i "$iface")
    if [[ -n "$source" ]]; then
      args+=(-s "$source")
    fi
    args+=(-p tcp --dport "$port" -m comment --comment "$COMMENT" -j ACCEPT)
    if run_priv iptables "${args[@]}"; then
      log "iptables akzeptiert: INPUT ${iface}:${port}${source:+ source=${source}}"
    else
      warn "iptables-Regel konnte nicht gesetzt werden: ${iface}:${port}"
      failed=1
    fi
  done < <(runtime_ports)
  [[ "$failed" == 0 ]]
}

install_service(){
  bool_env PHIMANTEC_APPLIANCE_INSTALL_INGRESS_SERVICE 1 || { log "systemd ingress service disabled"; return 0; }
  command -v systemctl >/dev/null 2>&1 || { warn "systemctl nicht gefunden; keine Persistenz installiert"; return 0; }
  [[ -d /run/systemd/system ]] || { warn "systemd nicht aktiv; keine Persistenz installiert"; return 0; }
  local unit tmp
  unit="/etc/systemd/system/${SERVICE_NAME}"
  tmp="$(mktemp)"
  cat > "$tmp" <<UNIT
[Unit]
Description=PHIMANTEC appliance ingress rules
Wants=network-online.target
After=network-online.target docker.service

[Service]
Type=oneshot
Environment=PHIMANTEC_ROOT_DIR=${ROOT_DIR}
Environment=PHIMANTEC_ENV_FILE=${ENV_FILE}
ExecStart=${ROOT_DIR}/scripts/phimantec_ingress_rules.sh apply
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNIT
  run_priv install -m 0644 "$tmp" "$unit" || { rm -f "$tmp"; warn "konnte ${unit} nicht installieren"; return 0; }
  rm -f "$tmp"
  run_priv systemctl daemon-reload || true
  run_priv systemctl enable "$SERVICE_NAME" >/dev/null 2>&1 || warn "konnte ${SERVICE_NAME} nicht aktivieren"
  log "systemd ingress service installiert: ${SERVICE_NAME}"
}

apply_rules(){
  bool_env PHIMANTEC_APPLIANCE_MANAGE_INGRESS 1 || { log "ingress management disabled"; return 0; }
  local iface source nft_status iptables_status
  iface="$(resolve_iface)"
  source="$(ingress_source)"
  nft_status=1
  iptables_status=1
  if install_nft_rules "$iface" "$source"; then
    nft_status=0
  fi
  if install_iptables_rules "$iface" "$source"; then
    iptables_status=0
  fi
  if [[ "$nft_status" != 0 && "$iptables_status" != 0 ]]; then
    fail "konnte keine Ingress-Regeln setzen; benoetigt root/CAP_NET_ADMIN und nftables oder iptables"
  fi
  log "ingress complete: iface=${iface}, ports=$(runtime_ports | paste -sd, -), source=${source:-any}"
}

show_status(){
  local iface
  iface="$(resolve_iface 2>/dev/null || printf '?')"
  log "root=${ROOT_DIR} env=${ENV_FILE} iface=${iface} ports=$(runtime_ports | paste -sd, -)"
  if command -v nft >/dev/null 2>&1; then
    nft list ruleset 2>/dev/null | grep -F "$COMMENT" || true
  fi
  if command -v iptables >/dev/null 2>&1; then
    iptables -S INPUT 2>/dev/null | grep -F "$COMMENT" || true
  fi
}

remove_rules(){
  warn "remove ist bewusst konservativ. Entferne bei Bedarf Regeln mit Kommentar '${COMMENT}' manuell aus nft/iptables."
  if command -v systemctl >/dev/null 2>&1; then
    run_priv systemctl disable --now "$SERVICE_NAME" >/dev/null 2>&1 || true
  fi
}

usage(){ cat <<'HELP'
Usage: scripts/phimantec_ingress_rules.sh <install|apply|status|remove>

install  Regeln jetzt setzen und systemd-Oneshot fuer Reboot-Persistenz installieren
apply    Regeln nur jetzt setzen
status   erkannte/gesetzte Regeln anzeigen
remove   systemd-Oneshot deaktivieren; Regeln bleiben konservativ erhalten
HELP
}

cmd="${1:-install}"
case "$cmd" in
  install) apply_rules; install_service ;;
  apply) apply_rules ;;
  status) show_status ;;
  remove) remove_rules ;;
  -h|--help|help) usage ;;
  *) fail "unbekannter Befehl: $cmd" ;;
esac
