#!/usr/bin/env bash
set -Eeuo pipefail

NODE_CONFIG="${PHIMANTEC_NODE_CONFIG:-/etc/phimantec/phimantec-suricata-node.conf}"
SURIMON_CONFIG="${SURIMON_CONFIG:-/etc/phimantec/suricata-monitor.yaml}"
SURICATA_CONFIG="${PHIMANTEC_SURICATA_CONFIG:-/etc/suricata/suricata.yaml}"
SURICATA_INTERFACE="${PHIMANTEC_SURICATA_INTERFACE:-auto}"
RULE_FILE="/var/lib/suricata/rules/phimantec-local.rules"
SURICATA_LOG_DIR="/var/log/suricata"
MONITOR_DIR="/opt/phimantec/suricata-monitor"
RUNTIME_OVERRIDE_FILE="${PHIMANTEC_RUNTIME_OVERRIDE_FILE:-/var/lib/suricata-monitor/phimantec-runtime-overrides.env}"
PERSISTED_SURICATA_CONFIG="${PHIMANTEC_PERSISTED_SURICATA_CONFIG:-/var/lib/suricata-monitor/suricata.yaml.persisted}"
PYTHON_BIN="${PHIMANTEC_MONITOR_PYTHON:-/usr/bin/python3}"
if [[ -x "${MONITOR_DIR}/venv/bin/python" ]]; then
  PYTHON_BIN="${MONITOR_DIR}/venv/bin/python"
fi

REQUESTED_MPM="${PHIMANTEC_SURICATA_MPM:-hs}"
REQUESTED_SPM="${PHIMANTEC_SURICATA_SPM:-hs}"
RUNMODE="${PHIMANTEC_SURICATA_RUNMODE:-workers}"
AF_PACKET_THREADS="${PHIMANTEC_SURICATA_AF_PACKET_THREADS:-auto}"
ALLOW_FALLBACK="${PHIMANTEC_SURICATA_ALLOW_AC_FALLBACK:-1}"

log() {
  printf '[phimantec-suricata-node] %s\n' "$*" >&2
}


bool_enabled() {
  local value="$1"
  case "${value,,}" in
    1|true|yes|on) return 0 ;;
    *) return 1 ;;
  esac
}

refresh_runtime_knobs() {
  SURICATA_INTERFACE="${PHIMANTEC_SURICATA_INTERFACE:-auto}"
  REQUESTED_MPM="${PHIMANTEC_SURICATA_MPM:-hs}"
  REQUESTED_SPM="${PHIMANTEC_SURICATA_SPM:-hs}"
  RUNMODE="${PHIMANTEC_SURICATA_RUNMODE:-workers}"
  AF_PACKET_THREADS="${PHIMANTEC_SURICATA_AF_PACKET_THREADS:-auto}"
}

load_runtime_overrides() {
  local line key value
  if [[ ! -r "$RUNTIME_OVERRIDE_FILE" ]]; then
    refresh_runtime_knobs
    return 0
  fi
  log "loading persisted runtime overrides: ${RUNTIME_OVERRIDE_FILE}"
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      ""|\#*) continue ;;
    esac
    key="${line%%=*}"
    value="${line#*=}"
    case "$key" in
      PHIMANTEC_SURICATA_INTERFACE|PHIMANTEC_SURICATA_MPM|PHIMANTEC_SURICATA_SPM|PHIMANTEC_SURICATA_RUNMODE|PHIMANTEC_SURICATA_AF_PACKET_THREADS)
        export "${key}=${value}"
        ;;
    esac
  done < "$RUNTIME_OVERRIDE_FILE"
  refresh_runtime_knobs
}

restore_persisted_suricata_config() {
  if [[ -s "$PERSISTED_SURICATA_CONFIG" ]]; then
    log "restoring persisted Suricata YAML: ${PERSISTED_SURICATA_CONFIG}"
    cp "$PERSISTED_SURICATA_CONFIG" "$SURICATA_CONFIG"
  fi
}

apply_appliance_ingress() {
  local manager require_ingress
  manager="/usr/local/bin/phimantec_ingress_rules.sh"
  if ! bool_enabled "${PHIMANTEC_APPLIANCE_MANAGE_INGRESS:-1}"; then
    log "appliance ingress management disabled"
    return 0
  fi
  if [[ ! -x "$manager" ]]; then
    log "appliance ingress manager fehlt: ${manager}"
    if bool_enabled "${PHIMANTEC_APPLIANCE_REQUIRE_INGRESS:-1}"; then
      exit 32
    fi
    return 0
  fi
  require_ingress="${PHIMANTEC_APPLIANCE_REQUIRE_INGRESS:-1}"
  if PHIMANTEC_ENV_FILE=/dev/null PHIMANTEC_ROOT_DIR=/opt/phimantec "$manager" apply; then
    log "appliance ingress rules applied by container"
    return 0
  fi
  log "appliance ingress rules konnten nicht gesetzt werden"
  if bool_enabled "$require_ingress"; then
    log "PHIMANTEC_APPLIANCE_REQUIRE_INGRESS=1: Containerstart wird abgebrochen, damit der Fehler nicht verdeckt wird"
    exit 32
  fi
}


bind_host() {
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value%:*}" || printf '0.0.0.0\n'
}

bind_port() {
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value##*:}" || printf '%s\n' "$value"
}

is_loopback_host() {
  local host="$1"
  case "$host" in
    ""|localhost|127.*|::1|[::1]) return 0 ;;
    *) return 1 ;;
  esac
}

lane_bind_host() {
  bind_host "${PHIMANTEC_LANE_PORT:-127.0.0.1:18080}"
}

patch_toml_string() {
  local file="$1" section="$2" key="$3" value="$4" tmp
  tmp="$(mktemp)"
  awk -v section="$section" -v key="$key" -v value="$value" '
    /^[[:space:]]*\[/ {
      current=$0
      gsub(/^[[:space:]]*\[/, "", current)
      gsub(/\][[:space:]]*$/, "", current)
      in_section=(current == section)
    }
    in_section && $0 ~ "^[[:space:]]*" key "[[:space:]]*=" {
      print key " = \"" value "\""
      next
    }
    { print }
  ' "$file" > "$tmp"
  cat "$tmp" > "$file"
  rm -f "$tmp"
}

read_config_value() {
  local file="$1" section="$2" key="$3"
  awk -v section="$section" -v key="$key" '
    function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
    function unquote(s) {
      s = trim(s)
      first = substr(s, 1, 1)
      last = substr(s, length(s), 1)
      if ((first == "\"" && last == "\"") || (first == "\047" && last == "\047")) {
        s = substr(s, 2, length(s) - 2)
      }
      return s
    }
    /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
    /^[[:space:]]*\[/ {
      current=$0
      gsub(/^[[:space:]]*\[/, "", current)
      gsub(/\][[:space:]]*$/, "", current)
      current=trim(current)
      in_section=(current == section)
      next
    }
    in_section {
      line=$0
      sub(/[[:space:]]*#.*/, "", line)
      eq=index(line, "=")
      if (eq <= 0) { next }
      k=trim(substr(line, 1, eq - 1))
      if (k == key) {
        v=substr(line, eq + 1)
        print unquote(v)
        exit
      }
    }
  ' "$file"
}

require_config_value() {
  local file="$1" section="$2" key="$3" expected="$4" actual
  actual="$(read_config_value "$file" "$section" "$key")"
  if [[ "$actual" != "$expected" ]]; then
    log "Konfigurationsfehler: [$section].$key ist '$actual', erwartet '$expected' in $file"
    exit 43
  fi
}

require_config_bool_false() {
  local file="$1" section="$2" key="$3" actual
  actual="$(read_config_value "$file" "$section" "$key")"
  case "${actual,,}" in
    0|false|no|off) return 0 ;;
    *)
      log "Konfigurationsfehler: [$section].$key muss fuer stabile Defaults deaktiviert sein, ist '$actual' in $file"
      exit 43
      ;;
  esac
}

validate_runtime_config() {
  local lane_value flowbus_value token_value
  [[ -f "$NODE_CONFIG" ]] || return 0
  lane_value="${PHIMANTEC_LANE_PORT:-127.0.0.1:18080}"
  flowbus_value="${PHIMANTEC_FLOWBUS_PORT_LOCAL:-${PHIMANTEC_FLOWBUS_PORT:-127.0.0.1:18090}}"
  token_value="${PHIMANTEC_FLOWBUS_TOKEN:-}"

  require_config_value "$NODE_CONFIG" lane addr "$lane_value"
  require_config_value "$NODE_CONFIG" flowbus addr "$flowbus_value"
  require_config_value "$NODE_CONFIG" verdict fail_policy closed
  require_config_bool_false "$NODE_CONFIG" flowbus keepalive
  if [[ -n "$token_value" ]]; then
    require_config_value "$NODE_CONFIG" flowbus token "$token_value"
  fi
  log "runtime config ok: lane=${lane_value}, flowbus=${flowbus_value}, fail_policy=closed, keepalive=0"
}

apply_runtime_ports() {
  local monitor_value monitor_host monitor_port lane_value flowbus_value
  monitor_value="${PHIMANTEC_MONITOR_PORT:-5050}"
  monitor_host="$(bind_host "$monitor_value")"
  monitor_port="$(bind_port "$monitor_value")"
  lane_value="${PHIMANTEC_LANE_PORT:-127.0.0.1:18080}"
  flowbus_value="${PHIMANTEC_FLOWBUS_PORT_LOCAL:-${PHIMANTEC_FLOWBUS_PORT:-127.0.0.1:18090}}"

  patch_toml_string "$NODE_CONFIG" lane addr "$lane_value"
  patch_toml_string "$NODE_CONFIG" flowbus addr "$flowbus_value"
  patch_toml_string "$NODE_CONFIG" flowbus pool_size "${PHIMANTEC_FLOWBUS_POOL_SIZE:-16}"
  patch_toml_string "$NODE_CONFIG" flowbus timeout_ms "${PHIMANTEC_FLOWBUS_TIMEOUT_MS:-3000}"
  patch_toml_string "$NODE_CONFIG" flowbus connect_timeout_ms "${PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS:-500}"
  patch_toml_string "$NODE_CONFIG" flowbus read_timeout_ms "${PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS:-3000}"
  patch_toml_string "$NODE_CONFIG" flowbus keepalive "${PHIMANTEC_FLOWBUS_KEEPALIVE:-0}"
  patch_toml_string "$NODE_CONFIG" verdict timeout_ms "${PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS:-900}"
  patch_toml_string "$NODE_CONFIG" verdict no_alert_grace_ms "${PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS:-100}"
  patch_toml_string "$NODE_CONFIG" verdict fail_policy "closed"

  SURIMON_CONFIG_PATH="$SURIMON_CONFIG" \
  SURIMON_BIND_HOST="$monitor_host" \
  SURIMON_BIND_PORT="$monitor_port" \
  "$PYTHON_BIN" - <<'PYCONF'
import os
from pathlib import Path
import yaml
path = Path(os.environ["SURIMON_CONFIG_PATH"])
with path.open("r", encoding="utf-8") as handle:
    data = yaml.safe_load(handle) or {}
server = data.setdefault("server", {})
server["bind_host"] = os.environ["SURIMON_BIND_HOST"]
server["bind_port"] = int(os.environ["SURIMON_BIND_PORT"])
with path.open("w", encoding="utf-8") as handle:
    yaml.safe_dump(data, handle, sort_keys=False)
PYCONF
}

resolve_suricata_interface() {
  local requested="$1"
  local dev=""
  local lane_host=""
  case "$requested" in
    ""|auto|AUTO)
      lane_host="$(lane_bind_host)"
      if is_loopback_host "$lane_host"; then
        printf '%s\n' "lo"
        return 0
      fi
      dev="$(ip -o route show default 2>/dev/null | awk '{for (i=1; i<=NF; i++) if ($i=="dev") {print $(i+1); exit}}' | head -n1)"
      if [[ -z "$dev" ]]; then
        dev="$(ip -o route get 1.1.1.1 2>/dev/null | awk '{for (i=1; i<=NF; i++) if ($i=="dev") {print $(i+1); exit}}' | head -n1)"
      fi
      if [[ -z "$dev" ]]; then
        dev="$(ip -o link show up 2>/dev/null | awk -F': ' '$2 != "lo" {print $2; exit}' | cut -d@ -f1)"
      fi
      if [[ -z "$dev" ]]; then
        dev="eth0"
      fi
      printf '%s\n' "$dev"
      ;;
    *)
      printf '%s\n' "$requested"
      ;;
  esac
}

patch_suricata_yaml() {
  local mpm_algo="$1"
  local spm_algo="$2"
  local python_patch="/usr/local/bin/phimantec_suricata_yaml_patch.py"

  if [[ ! -x "$python_patch" ]]; then
    log "Suricata YAML patcher fehlt: ${python_patch}"
    return 1
  fi

  "$PYTHON_BIN" "$python_patch" \
    --config "$SURICATA_CONFIG" \
    --interface "$SURICATA_INTERFACE" \
    --log-dir "$SURICATA_LOG_DIR" \
    --rule-path /var/lib/suricata/rules \
    --local-rules-file phimantec-local.rules \
    --eve-file eve.json \
    --command-socket /var/run/suricata/suricata-command.socket \
    --mpm-algo "$mpm_algo" \
    --spm-algo "$spm_algo" \
    --runmode "$RUNMODE" \
    --af-packet-threads "$AF_PACKET_THREADS" \
    --body-limit "${PHIMANTEC_SURICATA_BODY_LIMIT:-1048576}"
}

prepare_runtime_rule_files() {
  install -d -m 0755 /var/lib/suricata/rules /var/lib/suricata/rules/external /var/lib/suricata/rules/backup
  if [[ ! -f /var/lib/suricata/rules/phimantec-local.rules && -f /etc/suricata/rules/phimantec-local.rules ]]; then
    cp -f /etc/suricata/rules/phimantec-local.rules /var/lib/suricata/rules/phimantec-local.rules
  fi
  touch /var/lib/suricata/rules/suricata.rules
  touch /var/lib/suricata/rules/external/custom-local.rules
}

validate_suricata() {
  # rule-files kommen vollstaendig aus suricata.yaml.
  # Kein -S, sonst werden ET/Open und external/*.rules ignoriert.
  suricata -T -c "$SURICATA_CONFIG" --af-packet="$SURICATA_INTERFACE" \
    -l "$SURICATA_LOG_DIR" -k none
}

verify_suricata_version() {
  local info require
  require="${PHIMANTEC_REQUIRE_SURICATA8:-1}"
  info="$(suricata --build-info 2>&1 || true)"
  printf '%s\n' "$info" | sed -n '1,8p' >&2
  if [[ "$require" == 1 || "$require" == true || "$require" == yes ]]; then
    if ! printf '%s\n' "$info" | grep -Eq 'This is Suricata version ([8-9]|[1-9][0-9])\.'; then
      log "Suricata >= 8 ist erforderlich; erkannte Version ist nicht akzeptabel"
      exit 21
    fi
  fi
}

term_all() {
  local pid
  for pid in "${SURICATA_PID:-}" "${LANE_PID:-}" "${INSPECT_PID:-}" "${MONITOR_PID:-}"; do
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
    fi
  done
}

install -d -o root -g phimantec -m 0750 /etc/phimantec
install -d -o phimantec -g phimantec -m 0750 /var/log/phimantec
install -d -o root -g adm -m 0755 "$SURICATA_LOG_DIR"
install -d -o root -g root -m 0755 /var/run/suricata /etc/suricata/rules /var/lib/suricata/rules /var/lib/suricata/rules/external /var/lib/suricata/rules/backup
install -d -o phimantec -g phimantec -m 0750 /var/lib/suricata-monitor

restore_persisted_suricata_config
load_runtime_overrides

touch "$SURICATA_LOG_DIR/eve.json"
chmod 0644 "$SURICATA_LOG_DIR/eve.json"

if [[ -n "${PHIMANTEC_FLOWBUS_TOKEN:-}" ]]; then
  sed -i -E "s#^token = .*#token = \"${PHIMANTEC_FLOWBUS_TOKEN}\"#" "$NODE_CONFIG"
fi

apply_runtime_ports
validate_runtime_config
apply_appliance_ingress

SURICATA_INTERFACE="$(resolve_suricata_interface "$SURICATA_INTERFACE")"
log "resolved Suricata interface: ${SURICATA_INTERFACE}"

SURIMON_CONFIG="$SURIMON_CONFIG" "$PYTHON_BIN" /opt/phimantec/suricata-monitor/scripts/init_db.py

verify_suricata_version

prepare_runtime_rule_files

log "patch Suricata YAML: interface=${SURICATA_INTERFACE}, threads=${AF_PACKET_THREADS}, mpm=${REQUESTED_MPM}, spm=${REQUESTED_SPM}"
patch_suricata_yaml "$REQUESTED_MPM" "$REQUESTED_SPM"

if ! validate_suricata; then
  if [[ "$ALLOW_FALLBACK" == "1" && ( "$REQUESTED_MPM" == "hs" || "$REQUESTED_SPM" == "hs" ) ]]; then
    log "Suricata-Test mit Hyperscan schlug fehl; einmaliger Fallback auf mpm=ac-ks, spm=auto."
    patch_suricata_yaml "ac-ks" "auto"
    validate_suricata
  else
    log "Suricata-Konfigurationstest fehlgeschlagen; kein automatischer Reparaturloop."
    exit 20
  fi
fi

# rule-files werden aus suricata.yaml geladen (suricata.rules + phimantec-local + external/*.rules).
# Kein -S, sonst wuerde Suricata die yaml-Liste ignorieren.
suricata -c "$SURICATA_CONFIG" --af-packet="$SURICATA_INTERFACE" \
  -l "$SURICATA_LOG_DIR" -k none &
SURICATA_PID=$!

/usr/local/bin/phimantec-lane-sink -c "$NODE_CONFIG" -v &
LANE_PID=$!

/usr/local/bin/phimantec-inspectord -c "$NODE_CONFIG" -v &
INSPECT_PID=$!

cd "$MONITOR_DIR"
SURIMON_CONFIG="$SURIMON_CONFIG" "$PYTHON_BIN" backend/app.py &
MONITOR_PID=$!

trap 'term_all' INT TERM EXIT

log "started: suricata=${SURICATA_PID}, lane=${LANE_PID}, inspectord=${INSPECT_PID}, monitor=${MONITOR_PID}"
set +e
wait -n "$SURICATA_PID" "$LANE_PID" "$INSPECT_PID" "$MONITOR_PID"
EXIT_CODE=$?
set -e
log "ein Kindprozess wurde beendet; stoppe restliche Prozesse. exit=${EXIT_CODE}"
term_all
wait || true
exit "$EXIT_CODE"
