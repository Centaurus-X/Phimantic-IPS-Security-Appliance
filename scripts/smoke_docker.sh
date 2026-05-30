#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${PHIMANTEC_ENV_FILE:-${ROOT_DIR}/.env}"
COMPOSE_FILE="${PHIMANTEC_COMPOSE_FILE:-${ROOT_DIR}/docker-compose.yml}"
DEFAULT_PROJECT="phimantec-c11-v0318"
EXTERNAL="${PHIMANTEC_SMOKE_EXTERNAL:-0}"

log(){ printf '[smoke-docker] %s\n' "$*"; }
warn(){ printf '[smoke-docker] WARN: %s\n' "$*" >&2; }
fail(){ printf '[smoke-docker] ERROR: %s\n' "$*" >&2; exit 66; }

usage(){ cat <<'USAGE'
Usage: scripts/smoke_docker.sh [--external]
Default: prueft lokalen Stack ohne Internet-Abhaengigkeit, aber mit Host-Network-Listener-Gate und CA-Export-Gate.
USAGE
}

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

project_name(){
  local value
  value="${PHIMANTEC_COMPOSE_PROJECT_NAME:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_COMPOSE_PROJECT_NAME "$ENV_FILE")"
  [[ -n "$value" ]] || value="${PHIMANTEC_COMPOSE_PROJECT:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_COMPOSE_PROJECT "$ENV_FILE")"
  [[ -n "$value" ]] || value="$DEFAULT_PROJECT"
  printf '%s\n' "$value"
}

compose_base(){
  docker compose --project-directory "$ROOT_DIR" --env-file "$ENV_FILE" --project-name "$(project_name)" -f "$COMPOSE_FILE" "$@"
}

bind_host(){
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value%:*}" || printf '0.0.0.0\n'
}

bind_port(){
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value##*:}" || printf '%s\n' "$value"
}

http_host(){
  case "$1" in ""|0.0.0.0|::|\*) printf '127.0.0.1\n' ;; *) printf '%s\n' "$1" ;; esac
}

resolve_export_path(){
  local target
  target="${PHIMANTEC_CA_EXPORT_PATH:-}"
  [[ -n "$target" ]] || target="$(read_env_value PHIMANTEC_CA_EXPORT_PATH "$ENV_FILE")"
  [[ -n "$target" ]] || target="${HOME}/phimantec-bump-ca.crt"
  target="${target//\$\{HOME\}/$HOME}"
  target="${target//\$HOME/$HOME}"
  case "$target" in ~/*) target="${HOME}/${target#~/}" ;; esac
  printf '%s\n' "$target"
}

wait_for_tcp(){
  local host="$1" port="$2" label="$3" i
  for i in $(seq 1 30); do
    timeout 1 bash -c ": </dev/tcp/${host}/${port}" >/dev/null 2>&1 && { log "tcp ready: ${label} ${host}:${port}"; return 0; }
    sleep 1
  done
  return 1
}

require_service_running(){
  local service="$1"
  compose_base ps --services --status running | grep -qx "$service" || { compose_base ps || true; fail "Service laeuft nicht: ${service}"; }
}

allow_local_bind(){
  local value
  value="${PHIMANTEC_ALLOW_LOCAL_BIND:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_ALLOW_LOCAL_BIND "$ENV_FILE")"
  [[ -n "$value" ]] || value=0
  [[ "$value" == 1 ]]
}

listener_lines(){
  local port="$1"
  command -v ss >/dev/null 2>&1 || return 0
  ss -ltnH "( sport = :${port} )" 2>/dev/null || ss -ltnH 2>/dev/null | awk -v p=":${port}" '$4 ~ p {print}'
}

require_external_listener(){
  local port="$1" label="$2" lines
  lines="$(listener_lines "$port" || true)"
  [[ -n "$lines" ]] || fail "kein Host-Listener fuer ${label} auf Port ${port}"
  if allow_local_bind; then
    log "listener ok: ${label} port=${port}"
    return 0
  fi
  if printf '%s\n' "$lines" | grep -Eq '(^|[[:space:]])(0\.0\.0\.0|\[::\]|\*):'; then
    log "external listener ok: ${label} port=${port}"
    return 0
  fi
  if printf '%s\n' "$lines" | grep -Eq '(^|[[:space:]]):::'; then
    log "external listener ok: ${label} port=${port}"
    return 0
  fi
  fail "${label} lauscht nicht extern auf Port ${port}; ss: ${lines}"
}

verify_ca_export(){
  local ca_path text fp_host fp_container
  ca_path="$(resolve_export_path)"
  [[ -r "$ca_path" ]] || fail "CA-Export fehlt oder ist nicht lesbar: ${ca_path}"
  command -v openssl >/dev/null 2>&1 || fail "openssl fehlt fuer CA-Smoke"
  openssl x509 -in "$ca_path" -noout -subject >/dev/null || fail "CA-Export ist kein gueltiges X.509-Zertifikat: ${ca_path}"
  text="$(openssl x509 -in "$ca_path" -noout -text)"
  printf '%s\n' "$text" | grep -q 'CA:TRUE' || fail "CA-Export ist keine CA mit BasicConstraints CA:TRUE: ${ca_path}"
  printf '%s\n' "$text" | grep -q 'Certificate Sign' || fail "CA-Export besitzt keine keyCertSign-Verwendung: ${ca_path}"
  printf '%s\n' "$text" | grep -q 'CRL Sign' || fail "CA-Export besitzt keine cRLSign-Verwendung: ${ca_path}"
  fp_host="$(openssl x509 -in "$ca_path" -noout -fingerprint -sha256 | sed 's/^.*=//')"
  fp_container="$(compose_base exec -T proxy openssl x509 -in /etc/phimantec/ca/phimantec-bump-ca.crt -noout -fingerprint -sha256 2>/dev/null | sed 's/^.*=//' || true)"
  [[ -n "$fp_container" ]] || fail "Container-CA-Fingerprint konnte nicht gelesen werden"
  [[ "$fp_host" == "$fp_container" ]] || fail "exportierte CA passt nicht zur aktuell vom Proxy genutzten CA"
  log "ca_export_ok ${ca_path} sha256=${fp_host}"
}

find_free_local_port(){
  local port
  for port in 18443 18444 18445 18446 18447 18448 18449; do
    if ! listener_lines "$port" | grep -q .; then
      printf '%s\n' "$port"
      return 0
    fi
  done
  return 1
}

dump_inspection_diagnostics(){
  log "inspection_diagnostics_begin"
  compose_base ps || true
  log "--- proxy logs ---"
  compose_base logs --tail=120 proxy || true
  log "--- suricata-node logs ---"
  compose_base logs --tail=160 suricata-node || true
  log "--- suricata-node runtime ---"
  compose_base exec -T suricata-node sh -lc '
    echo "listeners:"; { command -v ss >/dev/null 2>&1 && ss -ltnp | grep -E ":(18080|18090|5050|3128)\b"; } || true
    echo "processes:"; ps aux | grep -E "phimantec-lane-sink|phimantec-inspectord|suricata" | grep -v grep || true
    echo "config:"; grep -R "flowbus\|token\|verdict\|timeout\|fail_policy\|keepalive" /etc/phimantec 2>/dev/null | sed -E "s/(token[[:space:]]*=[[:space:]]*").*(")/\1<redacted>\2/" || true
    echo "flowbus probe:"; /usr/local/bin/phimantec-flowbus-probe -c "${PHIMANTEC_NODE_CONFIG:-/etc/phimantec/phimantec-suricata-node.conf}" -t 1000 || true
  ' || true
  log "--- proxy runtime ---"
  compose_base exec -T proxy sh -lc '
    echo "config:"; grep -R "flowbus\|token\|verdict\|timeout\|fail_policy\|keepalive" /etc/phimantec 2>/dev/null | sed -E "s/(token[[:space:]]*=[[:space:]]*").*(")/\1<redacted>\2/" || true
    echo "flowbus probe:"; /usr/local/bin/phimantec-flowbus-probe -c "${PHIMANTEC_PROXY_CONFIG:-/etc/phimantec/phimantec-proxyd.conf}" -t 1000 || true
  ' || true
  log "inspection_diagnostics_end"
}

verify_proxy_ca_chain_local(){
  local ca_path proxy_host proxy_port tmpdir port server_pid status
  ca_path="$(resolve_export_path)"
  proxy_host="$1"
  proxy_port="$2"
  tmpdir=""
  server_pid=""
  command -v openssl >/dev/null 2>&1 || fail "openssl fehlt fuer lokalen TLS-CA-Smoke"
  port="$(find_free_local_port)" || fail "kein freier lokaler TLS-Testport gefunden"
  tmpdir="$(mktemp -d)"
  trap '[[ -n "${server_pid:-}" ]] && kill "$server_pid" 2>/dev/null || true; [[ -n "${tmpdir:-}" ]] && rm -rf "$tmpdir" || true; trap - RETURN' RETURN

  openssl req -x509 -newkey rsa:2048 -sha256 -days 1 -nodes \
    -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1" \
    -keyout "${tmpdir}/upstream.key" -out "${tmpdir}/upstream.crt" >/dev/null 2>&1
  openssl s_server -quiet -www -accept "127.0.0.1:${port}" \
    -cert "${tmpdir}/upstream.crt" -key "${tmpdir}/upstream.key" >/dev/null 2>&1 &
  server_pid=$!
  wait_for_tcp 127.0.0.1 "$port" local-upstream-tls || fail "lokaler TLS-Upstream nicht erreichbar"

  local curl_exit curl_err
  curl_err="${tmpdir}/curl.err"
  curl_exit=0
  status="$(curl --noproxy '' -sS --max-time 10 -o /dev/null -w '%{http_code}' \
    --proxy "http://${proxy_host}:${proxy_port}" \
    --cacert "$ca_path" \
    "https://127.0.0.1:${port}/" 2>"$curl_err")" || curl_exit=$?
  case "$status" in
    200)
      log "proxy_ca_chain_ok local_https_status=${status}"
      ;;
    503)
      dump_inspection_diagnostics
      fail "Proxy lieferte HTTP 503: inspection unavailable / FlowBus-/inspectord-Diagnose erforderlich"
      ;;
    403)
      dump_inspection_diagnostics
      fail "Proxy lieferte HTTP 403 bei harmloser lokaler Testseite: Security-Policy/Suricata-Block pruefen"
      ;;
    000|"")
      fail "CA-/TLS-/Proxy-Verbindungsfehler beim lokalen HTTPS-Smoke: curl_exit=${curl_exit} error=$(cat "$curl_err" 2>/dev/null || true)"
      ;;
    *)
      fail "unerwarteter lokaler Proxy-HTTPS-Status: status=${status} curl_exit=${curl_exit}"
      ;;
  esac

  [[ -n "${server_pid:-}" ]] && kill "$server_pid" 2>/dev/null || true
  [[ -n "${tmpdir:-}" ]] && rm -rf "$tmpdir" || true
  server_pid=""
  tmpdir=""
  trap - RETURN
}

verify_suricata8(){
  local info
  info="$(compose_base exec -T suricata-node suricata --build-info 2>/dev/null || true)"
  printf '%s\n' "$info" | grep -Eq 'This is Suricata version ([8-9]|[1-9][0-9])\.' || {
    printf '%s\n' "$info" >&2
    fail "Suricata >= 8 wurde nicht erkannt"
  }
  log "suricata8_ok $(printf '%s\n' "$info" | grep -m1 'This is Suricata version' | sed 's/^ *//')"
}

main(){
  cd "$ROOT_DIR"
  [[ -f "$ENV_FILE" ]] || cp .env.example "$ENV_FILE"
  command -v docker >/dev/null 2>&1 || fail "docker fehlt"
  command -v curl >/dev/null 2>&1 || fail "curl fehlt"

  require_service_running suricata-node
  require_service_running proxy

  if [[ "$(read_env_value PHIMANTEC_AUTO_EXPORT_CA "$ENV_FILE")" != 0 ]]; then
    "${ROOT_DIR}/scripts/export_docker_ca.sh"
  fi

  local monitor_bind proxy_bind monitor_host monitor_port proxy_host proxy_port status
  monitor_bind="$(read_env_value PHIMANTEC_MONITOR_PORT "$ENV_FILE")"
  proxy_bind="$(read_env_value PHIMANTEC_PROXY_PORT "$ENV_FILE")"
  [[ -n "$monitor_bind" ]] || monitor_bind=5050
  [[ -n "$proxy_bind" ]] || proxy_bind=3128

  monitor_host="$(http_host "$(bind_host "$monitor_bind")")"
  monitor_port="$(bind_port "$monitor_bind")"
  proxy_host="$(http_host "$(bind_host "$proxy_bind")")"
  proxy_port="$(bind_port "$proxy_bind")"

  require_external_listener "$monitor_port" monitor
  require_external_listener "$proxy_port" proxy
  wait_for_tcp "$monitor_host" "$monitor_port" monitor || fail "Monitor TCP nicht erreichbar"
  wait_for_tcp "$proxy_host" "$proxy_port" proxy || fail "Proxy TCP nicht erreichbar"

  curl -fsS --max-time 5 "http://${monitor_host}:${monitor_port}/" >/dev/null || fail "SuricataMonitor HTTP-Smoke fehlgeschlagen"
  log monitor_ok
  log proxy_tcp_ok
  verify_ca_export
  verify_proxy_ca_chain_local "$proxy_host" "$proxy_port"
  verify_suricata8

  if [[ "$EXTERNAL" == 1 ]]; then
    status="$(curl -k -sS --max-time 10 -o /dev/null -w '%{http_code}' -x "http://${proxy_host}:${proxy_port}" -I https://example.org/ || true)"
    case "$status" in
      200|301|302|403) log "external proxy responded with HTTP status ${status}" ;;
      *) fail "unerwarteter externer Proxy-Smoke-Status: ${status}" ;;
    esac
  else
    log "external internet proxy check skipped"
  fi

  if [[ -x "${ROOT_DIR}/scripts/phimantec_ingress_rules.sh" ]]; then
    "${ROOT_DIR}/scripts/phimantec_ingress_rules.sh" status || warn "ingress status konnte nicht gelesen werden"
  fi

  compose_base ps
  log complete
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --external) EXTERNAL=1 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unbekannte Option: $1" ;;
  esac
  shift
done

main
