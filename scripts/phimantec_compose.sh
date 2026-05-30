#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${PHIMANTEC_ENV_FILE:-${ROOT_DIR}/.env}"
COMPOSE_FILE="${PHIMANTEC_COMPOSE_FILE:-${ROOT_DIR}/docker-compose.yml}"
DEFAULT_PROJECT="phimantec-c11-v0318"
EXPECTED_VERSION="0.3.18-monitor-offline-vendor"

log(){ printf '[phimantec-compose] %s\n' "$*" >&2; }
fail(){ printf '[phimantec-compose] ERROR: %s\n' "$*" >&2; exit 64; }

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

ensure_env(){
  if [[ ! -f "$ENV_FILE" ]]; then
    cp "${ROOT_DIR}/.env.example" "$ENV_FILE"
    log "created ${ENV_FILE} from .env.example"
  fi
}

compose_base(){
  docker compose --project-directory "$ROOT_DIR" --env-file "$ENV_FILE" --project-name "$(project_name)" -f "$COMPOSE_FILE" "$@"
}

auto_export_enabled(){
  local value
  value="${PHIMANTEC_AUTO_EXPORT_CA:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_AUTO_EXPORT_CA "$ENV_FILE")"
  [[ -n "$value" ]] || value=1
  [[ "$value" != 0 ]]
}

export_ca_if_proxy_running(){
  auto_export_enabled || { log "CA export disabled by PHIMANTEC_AUTO_EXPORT_CA=0"; return 0; }
  if compose_base ps --services --status running | grep -qx proxy; then
    "${ROOT_DIR}/scripts/export_docker_ca.sh"
  else
    log "proxy not running; CA export skipped"
  fi
}

host_pre_apply_ingress_enabled(){
  local value
  value="${PHIMANTEC_APPLIANCE_HOST_PRE_APPLY_INGRESS:-}"
  [[ -n "$value" ]] || value="$(read_env_value PHIMANTEC_APPLIANCE_HOST_PRE_APPLY_INGRESS "$ENV_FILE")"
  [[ -n "$value" ]] || value=0
  [[ "$value" == 1 || "$value" == true || "$value" == yes ]]
}

install_ingress_if_enabled(){
  if host_pre_apply_ingress_enabled; then
    "${ROOT_DIR}/scripts/phimantec_ingress_rules.sh" install
  else
    log "host pre-ingress disabled; suricata-node container applies ingress rules directly"
  fi
}

usage(){ cat <<'USAGE'
Usage: scripts/phimantec_compose.sh <command> [args]

Commands:
  init-env             .env aus .env.example erzeugen, falls fehlend
  preflight|doctor     Docker-/Compose-/Port-Kontext pruefen
  build [args]         docker compose build ueber Repo-Root und .env
  up [args]            docker compose up -d --remove-orphans --force-recreate, dann CA exportieren
  export-ca            Docker-Proxy-CA nach ~/phimantec-bump-ca.crt exportieren
  ingress [args]       Appliance-Ingress-Regeln installieren/status/remove
  redeploy [args]      down, clean-stale, preflight, ingress, build, up, export-ca, smoke
  down [args]          docker compose down --remove-orphans
  ps [args]            docker compose ps
  logs [service]       docker compose logs --no-color --tail=160 [service]
  smoke                lokaler Monitor-/Proxy-Smoke inkl. Publish- und CA-Check
  clean-stale [--force] alte feste Container-Namen entfernen
  compose ...          direkte docker-compose-Argumente mit festem Root/env/project
USAGE
}

cmd="${1:-}"
[[ -n "$cmd" ]] || { usage; exit 2; }
shift || true

case "$cmd" in
  init-env)
    ensure_env
    ;;
  preflight|doctor)
    ensure_env
    "${ROOT_DIR}/scripts/preflight_docker.sh" "$@"
    ;;
  build)
    ensure_env
    "${ROOT_DIR}/scripts/preflight_docker.sh" --build-only
    compose_base --progress plain build "$@"
    ;;
  up)
    ensure_env
    "${ROOT_DIR}/scripts/preflight_docker.sh" --runtime-only
    install_ingress_if_enabled
    compose_base up -d --remove-orphans --force-recreate "$@"
    export_ca_if_proxy_running
    ;;
  export-ca)
    ensure_env
    "${ROOT_DIR}/scripts/export_docker_ca.sh" "$@"
    ;;
  ingress)
    ensure_env
    ingress_cmd="${1:-install}"
    shift || true
    "${ROOT_DIR}/scripts/phimantec_ingress_rules.sh" "$ingress_cmd" "$@"
    ;;
  redeploy)
    ensure_env
    "${ROOT_DIR}/scripts/check_release_context.sh"
    compose_base down --remove-orphans || true
    "${ROOT_DIR}/scripts/cleanup_stale_docker.sh" || true
    "${ROOT_DIR}/scripts/preflight_docker.sh" --all
    install_ingress_if_enabled
    compose_base --progress plain build "$@"
    compose_base up -d --remove-orphans --force-recreate
    "${ROOT_DIR}/scripts/export_docker_ca.sh"
    compose_base ps
    "${ROOT_DIR}/scripts/smoke_docker.sh"
    ;;
  down)
    ensure_env
    compose_base down --remove-orphans "$@"
    ;;
  ps)
    ensure_env
    compose_base ps "$@"
    ;;
  logs)
    ensure_env
    compose_base logs --no-color --tail=160 "$@"
    ;;
  smoke)
    ensure_env
    "${ROOT_DIR}/scripts/smoke_docker.sh" "$@"
    ;;
  clean-stale|clean-legacy)
    ensure_env
    "${ROOT_DIR}/scripts/cleanup_stale_docker.sh" "$@"
    ;;
  compose)
    ensure_env
    compose_base "$@"
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    fail "unknown command: $cmd"
    ;;
esac
