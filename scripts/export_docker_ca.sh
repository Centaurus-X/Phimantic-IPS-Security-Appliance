#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${PHIMANTEC_ENV_FILE:-${ROOT_DIR}/.env}"
COMPOSE_FILE="${PHIMANTEC_COMPOSE_FILE:-${ROOT_DIR}/docker-compose.yml}"
DEFAULT_PROJECT="phimantec-c11-v0318"
CA_SOURCE="/etc/phimantec/ca/phimantec-bump-ca.crt"

log(){ printf '[export-ca] %s\n' "$*"; }
fail(){ printf '[export-ca] ERROR: %s\n' "$*" >&2; exit 67; }

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

resolve_export_path(){
  local target
  target="${PHIMANTEC_CA_EXPORT_PATH:-}"
  [[ -n "$target" ]] || target="$(read_env_value PHIMANTEC_CA_EXPORT_PATH "$ENV_FILE")"
  [[ -n "$target" ]] || target="${HOME}/phimantec-bump-ca.crt"
  target="${target//\$\{HOME\}/$HOME}"
  target="${target//\$HOME/$HOME}"
  case "$target" in
    ~/*) target="${HOME}/${target#~/}" ;;
  esac
  printf '%s\n' "$target"
}

wait_for_proxy(){
  local i
  for i in $(seq 1 30); do
    if compose_base ps --services --status running | grep -qx proxy; then
      return 0
    fi
    sleep 1
  done
  return 1
}

copy_ca(){
  local target="$1" tmp="$2" i
  for i in $(seq 1 30); do
    rm -f "$tmp"
    if compose_base cp "proxy:${CA_SOURCE}" "$tmp" >/dev/null 2>&1 && [[ -s "$tmp" ]]; then
      return 0
    fi
    sleep 1
  done
  return 1
}

main(){
  cd "$ROOT_DIR"
  [[ -f "$ENV_FILE" ]] || cp .env.example "$ENV_FILE"
  command -v docker >/dev/null 2>&1 || fail "docker fehlt"

  wait_for_proxy || fail "Service proxy laeuft nicht; CA kann nicht exportiert werden"

  local target target_dir tmp
  target="$(resolve_export_path)"
  target_dir="$(dirname "$target")"
  mkdir -p "$target_dir"
  [[ -e "$target" && ! -w "$target" ]] && fail "Zieldatei existiert, ist aber nicht beschreibbar: ${target}"
  tmp="${target}.tmp.$$"
  trap 'rm -f "$tmp"' EXIT

  copy_ca "$target" "$tmp" || fail "CA konnte nicht aus dem Proxy-Container kopiert werden: proxy:${CA_SOURCE}"

  local text fingerprint fp_file
  if command -v openssl >/dev/null 2>&1; then
    openssl x509 -in "$tmp" -noout -subject >/dev/null || fail "exportierte Datei ist kein gueltiges X.509-Zertifikat: ${tmp}"
    text="$(openssl x509 -in "$tmp" -noout -text)"
    printf '%s\n' "$text" | grep -q 'CA:TRUE' || fail "exportierte Datei ist keine Root-CA mit BasicConstraints CA:TRUE"
    printf '%s\n' "$text" | grep -q 'Certificate Sign' || fail "exportierte Root-CA besitzt keine keyCertSign-Verwendung"
    printf '%s\n' "$text" | grep -q 'CRL Sign' || fail "exportierte Root-CA besitzt keine cRLSign-Verwendung"
    fingerprint="$(openssl x509 -in "$tmp" -noout -fingerprint -sha256 | sed 's/^.*=//')"
  else
    fingerprint=""
  fi

  chmod 0644 "$tmp"
  mv -f "$tmp" "$target"
  if [[ -n "$fingerprint" ]]; then
    fp_file="${target}.sha256-fingerprint.txt"
    printf '%s\n' "$fingerprint" > "$fp_file"
    chmod 0644 "$fp_file"
  fi
  trap - EXIT
  if [[ -n "$fingerprint" ]]; then
    log "CA exportiert: ${target} sha256=${fingerprint}"
  else
    log "CA exportiert: ${target}"
  fi
}

main "$@"
