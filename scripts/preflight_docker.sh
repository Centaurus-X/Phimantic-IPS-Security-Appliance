#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${PHIMANTEC_ENV_FILE:-${ROOT_DIR}/.env}"
EXPECTED_VERSION="0.3.18-monitor-offline-vendor"
CHECK_BUILD=1
CHECK_RUNTIME=1

log(){ printf '[preflight] %s\n' "$*"; }
warn(){ printf '[preflight] WARN: %s\n' "$*" >&2; }
fail(){ printf '[preflight] ERROR: %s\n' "$*" >&2; exit 65; }

usage(){ cat <<'HELP'
Usage: scripts/preflight_docker.sh [--all|--build-only|--runtime-only]

Prueft Docker/Compose, Release-Kontext, Compose-Konfiguration,
Altcontainer, Host-Port-Konflikte und v0.3.4-External-Bindings.
HELP
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

ensure_env(){
  if [[ ! -f "$ENV_FILE" ]]; then
    cp "${ROOT_DIR}/.env.example" "$ENV_FILE"
    log "created ${ENV_FILE} from .env.example"
  fi
}

project_name(){
  local value
  value="${PHIMANTEC_COMPOSE_PROJECT_NAME:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_COMPOSE_PROJECT_NAME "$ENV_FILE")"
  [[ -n "$value" ]] || value="${PHIMANTEC_COMPOSE_PROJECT:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_COMPOSE_PROJECT "$ENV_FILE")"
  [[ -n "$value" ]] || value="phimantec-c11-v0318"
  printf '%s\n' "$value"
}

ensure_docker(){
  command -v docker >/dev/null 2>&1 || fail "docker nicht gefunden"
  docker info >/dev/null 2>&1 || fail "Docker Engine ist nicht erreichbar"
  docker compose version >/dev/null 2>&1 || fail "docker compose plugin ist nicht erreichbar"
}

check_release_context(){
  [[ -f "$ROOT_DIR/docker-compose.yml" ]] || fail "docker-compose.yml fehlt"
  [[ -f "$ROOT_DIR/BUILD_INFO.txt" ]] || fail "BUILD_INFO.txt fehlt"
  [[ -f "$ROOT_DIR/.env.example" ]] || fail ".env.example fehlt"
  grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" "$ROOT_DIR/.env.example" || fail ".env.example passt nicht zu ${EXPECTED_VERSION}"
  grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" "$ROOT_DIR/BUILD_INFO.txt" || fail "BUILD_INFO.txt passt nicht zu ${EXPECTED_VERSION}"
  local env_version
  ensure_env
  env_version="$(read_env_value PHIMANTEC_VERSION "$ENV_FILE")"
  [[ -z "$env_version" || "$env_version" == "$EXPECTED_VERSION" ]] || fail ".env verwendet '${env_version}', erwartet '${EXPECTED_VERSION}'."
}

bind_host(){
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value%:*}" || printf '0.0.0.0\n'
}

bind_port(){
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value##*:}" || printf '%s\n' "$value"
}

port_listening(){
  local host="$1" port="$2"
  case "$host" in ""|0.0.0.0|::|\*) host=127.0.0.1;; esac
  timeout 1 bash -c ": </dev/tcp/${host}/${port}" >/dev/null 2>&1
}

port_owner(){
  # Best-effort: benenne den Listener auf einem Port (Prozess/Unit), damit der
  # Anwender sofort sieht, wer den Port haelt (haeufig: v0.2 phimantec-proxyd
  # auf 3128 oder der Host-Dienst suricata-monitor auf 5050).
  local port="$1" line
  command -v ss >/dev/null 2>&1 || { printf 'unbekannt (ss nicht verfuegbar)\n'; return 0; }
  line="$(ss -ltnHp "( sport = :${port} )" 2>/dev/null | head -n1)"
  [[ -n "$line" ]] || line="$(ss -ltnp 2>/dev/null | grep -E "[:.]${port}([[:space:]]|$)" | head -n1)"
  # Fehlt der Prozess-Teil (unprivilegiert nicht sichtbar), best-effort mit sudo -n.
  if [[ "$line" != *users:* && "$(id -u)" != 0 ]] && command -v sudo >/dev/null 2>&1; then
    local sline
    sline="$(sudo -n ss -ltnHp "( sport = :${port} )" 2>/dev/null | head -n1)"
    [[ -n "$sline" ]] && line="$sline"
  fi
  if [[ -n "$line" ]]; then
    printf '%s\n' "$line" | tr -s '[:space:]' ' '
  else
    printf 'unbekannt\n'
  fi
}

check_port_free(){
  local name="$1" default_value="$2" value host port
  value="$(read_env_value "$name" "$ENV_FILE")"
  [[ -n "$value" ]] || value="$default_value"
  host="$(bind_host "$value")"
  port="$(bind_port "$value")"
  [[ "$port" =~ ^[0-9]+$ ]] || fail "ungueltiger Port fuer ${name}: ${value}"
  if port_listening "$host" "$port"; then
    local owner; owner="$(port_owner "$port")"
    fail "${name}=${value} ist bereits belegt durch: ${owner}. Haeufigste Ursache nach Upgrade von v0.2/v0.3-VM: Host-systemd-Dienste 'phimantec-proxyd' (3128) bzw. 'suricata-monitor' (5050) laufen noch. Sauberer Wechsel: scripts/phimantec_stop_legacy_host_services.sh (oder einmalig PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache). Manuelle Diagnose: sudo ss -ltnp | grep ':${port}'"
  fi
  log "port frei: ${name}=${value}"
}

is_local_bind(){
  case "$1" in
    127.*|localhost|::1|'[::1]') return 0 ;;
    *) return 1 ;;
  esac
}

check_external_bind(){
  local name="$1" default_value="$2" value host allow_local
  allow_local="${PHIMANTEC_ALLOW_LOCAL_BIND:-}"
  [[ -n "$allow_local" ]] || allow_local="$(read_env_value PHIMANTEC_ALLOW_LOCAL_BIND "$ENV_FILE")"
  [[ -n "$allow_local" ]] || allow_local=0
  [[ "$allow_local" == 1 ]] && return 0
  value="$(read_env_value "$name" "$ENV_FILE")"
  [[ -n "$value" ]] || value="$default_value"
  host="$(bind_host "$value")"
  if is_local_bind "$host"; then
    fail "${name}=${value} bindet nur lokal. v0.3.4 erwartet extern erreichbares Binding, z.B. '${default_value}' oder '0.0.0.0:${default_value##*:}'."
  fi
  log "external bind ok: ${name}=${value}"
}

check_ports(){
  check_external_bind PHIMANTEC_PROXY_PORT 3128
  check_external_bind PHIMANTEC_MONITOR_PORT 5050
  check_port_free PHIMANTEC_PROXY_PORT 3128
  check_port_free PHIMANTEC_MONITOR_PORT 5050
  check_port_free PHIMANTEC_LANE_PORT 127.0.0.1:18080
  check_port_free PHIMANTEC_FLOWBUS_PORT 127.0.0.1:18090
}

check_legacy_containers(){
  local name status image found
  found=0
  for name in phimantec-suricata-node phimantec-proxy; do
    if docker container inspect "$name" >/dev/null 2>&1; then
      status="$(docker inspect -f '{{.State.Status}}' "$name" 2>/dev/null || true)"
      image="$(docker inspect -f '{{.Config.Image}}' "$name" 2>/dev/null || true)"
      warn "Legacy-Container mit altem festen Namen existiert: ${name}, status=${status:-unknown}, image=${image:-unknown}."
      found=1
    fi
  done
  [[ "$found" == 0 ]] || fail "alte feste Container-Namen blockieren den Start. Bereinigung: scripts/phimantec_compose.sh clean-stale --force"
}

# --- v0.3.5/v0.3.12: Erkennung kollidierender Host-Dienste (systemd) ---------
# v0.2/v0.3-VM-Installationen betreiben phimantec-proxyd/-inspectord/-lane-sink
# und die Web-UI suricata-monitor als Host-systemd-Dienste. Nach dem Wechsel auf
# den Docker-Stack belegen vor allem phimantec-proxyd (0.0.0.0:3128) und
# suricata-monitor (0.0.0.0:5050) die Ports und blockieren den Deploy. Der
# Docker-Cleanup beruehrt diese Host-Dienste nicht - daher hier.
legacy_host_units(){
  printf '%s\n' phimantec-proxyd phimantec-inspectord phimantec-lane-sink suricata-monitor
}

systemd_available(){
  command -v systemctl >/dev/null 2>&1 && [[ -d /run/systemd/system ]]
}

auto_stop_legacy_enabled(){
  local value
  value="${PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES "$ENV_FILE")"
  [[ -n "$value" ]] || value=0
  [[ "$value" == 1 ]]
}

stop_legacy_unit(){
  local unit="$1"
  if [[ "$(id -u)" == 0 ]]; then
    systemctl disable --now "$unit" >/dev/null 2>&1
  elif command -v sudo >/dev/null 2>&1; then
    sudo -n systemctl disable --now "$unit" >/dev/null 2>&1
  else
    return 1
  fi
}

check_legacy_host_services(){
  systemd_available || { log "kein systemd; Legacy-Host-Dienst-Check uebersprungen"; return 0; }
  local unit active blocking auto
  blocking=0
  auto=0
  if auto_stop_legacy_enabled; then auto=1; fi
  while IFS= read -r unit; do
    systemctl list-unit-files "${unit}.service" >/dev/null 2>&1 || continue
    active="$(systemctl is-active "${unit}.service" 2>/dev/null || true)"
    [[ "$active" == active ]] || continue
    if [[ "$auto" == 1 ]]; then
      if stop_legacy_unit "${unit}.service"; then
        log "Legacy-Host-Dienst gestoppt+deaktiviert: ${unit}.service"
      else
        warn "konnte ${unit}.service nicht automatisch stoppen (fehlende Rechte?). Manuell: sudo systemctl disable --now ${unit}.service"
        blocking=1
      fi
    else
      warn "aktiver v0.2-Host-Dienst kollidiert mit dem Docker-Stack: ${unit}.service"
      blocking=1
    fi
  done < <(legacy_host_units)
  if [[ "$blocking" == 1 ]]; then
    fail "v0.2-Host-Dienste blockieren den Docker-Deploy. Sauberer Wechsel v0.2->v0.3.x: scripts/phimantec_stop_legacy_host_services.sh (oder einmalig PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache)."
  fi
}

check_compose_config(){
  docker compose --project-directory "$ROOT_DIR" --env-file "$ENV_FILE" --project-name "$(project_name)" -f "$ROOT_DIR/docker-compose.yml" config -q >/dev/null
}

check_build_network(){
  local network
  network="$(read_env_value PHIMANTEC_DOCKER_BUILD_NETWORK "$ENV_FILE")"
  [[ -n "$network" ]] || network=host
  log "build network: ${network}"
}

main(){
  cd "$ROOT_DIR"
  check_release_context
  ensure_docker
  check_compose_config
  [[ "$CHECK_RUNTIME" == 1 ]] && { check_legacy_containers; check_legacy_host_services; check_ports; }
  [[ "$CHECK_BUILD" == 1 ]] && check_build_network
  log complete
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all) CHECK_BUILD=1; CHECK_RUNTIME=1 ;;
    --build-only) CHECK_BUILD=1; CHECK_RUNTIME=0 ;;
    --runtime-only) CHECK_BUILD=0; CHECK_RUNTIME=1 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unbekannte Option: $1" ;;
  esac
  shift
done

main
