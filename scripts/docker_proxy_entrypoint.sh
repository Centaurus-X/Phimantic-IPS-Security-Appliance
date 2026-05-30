#!/usr/bin/env bash
set -Eeuo pipefail

CONFIG_PATH="${PHIMANTEC_PROXY_CONFIG:-/etc/phimantec/phimantec-proxyd.conf}"
CA_DIR="/etc/phimantec/ca"
CA_CERT="${CA_DIR}/phimantec-bump-ca.crt"
CA_KEY="${CA_DIR}/phimantec-bump-ca.key"
CA_CN="${PHIMANTEC_CA_CN:-PHIMANTEC C11 Docker Lab Root CA}"
CA_ORG="${PHIMANTEC_CA_ORG:-PHIMANTEC}"
CA_OU="${PHIMANTEC_CA_OU:-C11 Docker Appliance}"
CA_DAYS="${PHIMANTEC_CA_DAYS:-3650}"

log() {
  printf '[phimantec-proxy-entrypoint] %s\n' "$*" >&2
}

trim_text() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s\n' "$value"
}

bool_enabled() {
  local value="$1"
  case "${value,,}" in
    1|true|yes|on) return 0 ;;
    *) return 1 ;;
  esac
}

bind_host() {
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value%:*}" || printf '0.0.0.0\n'
}

bind_port() {
  local value="$1"
  [[ "$value" == *:* ]] && printf '%s\n' "${value##*:}" || printf '%s\n' "$value"
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
  local proxy_value proxy_host proxy_port lane_value flowbus_value token_value
  [[ -f "$CONFIG_PATH" ]] || return 0
  proxy_value="${PHIMANTEC_PROXY_PORT:-3128}"
  proxy_host="$(bind_host "$proxy_value")"
  proxy_port="$(bind_port "$proxy_value")"
  lane_value="${PHIMANTEC_LANE_PORT:-127.0.0.1:18080}"
  flowbus_value="${PHIMANTEC_FLOWBUS_PORT_LOCAL:-${PHIMANTEC_FLOWBUS_PORT:-127.0.0.1:18090}}"
  token_value="${PHIMANTEC_FLOWBUS_TOKEN:-}"

  require_config_value "$CONFIG_PATH" listener bind "${proxy_host}:${proxy_port}"
  require_config_value "$CONFIG_PATH" lane addr "$lane_value"
  require_config_value "$CONFIG_PATH" flowbus addr "$flowbus_value"
  require_config_value "$CONFIG_PATH" verdict fail_policy closed
  require_config_bool_false "$CONFIG_PATH" flowbus keepalive
  if [[ -n "$token_value" ]]; then
    require_config_value "$CONFIG_PATH" flowbus token "$token_value"
  fi
  log "runtime config ok: proxy=${proxy_host}:${proxy_port}, lane=${lane_value}, flowbus=${flowbus_value}, fail_policy=closed, keepalive=0"
}

apply_runtime_ports() {
  local proxy_value proxy_host proxy_port lane_value flowbus_value
  proxy_value="${PHIMANTEC_PROXY_PORT:-3128}"
  proxy_host="$(bind_host "$proxy_value")"
  proxy_port="$(bind_port "$proxy_value")"
  lane_value="${PHIMANTEC_LANE_PORT:-127.0.0.1:18080}"
  flowbus_value="${PHIMANTEC_FLOWBUS_PORT_LOCAL:-${PHIMANTEC_FLOWBUS_PORT:-127.0.0.1:18090}}"
  patch_toml_string "$CONFIG_PATH" listener bind "${proxy_host}:${proxy_port}"
  patch_toml_string "$CONFIG_PATH" lane addr "$lane_value"
  patch_toml_string "$CONFIG_PATH" flowbus addr "$flowbus_value"
  patch_toml_string "$CONFIG_PATH" lane pool_size "${PHIMANTEC_LANE_POOL_SIZE:-16}"
  patch_toml_string "$CONFIG_PATH" flowbus pool_size "${PHIMANTEC_FLOWBUS_POOL_SIZE:-16}"
  patch_toml_string "$CONFIG_PATH" flowbus timeout_ms "${PHIMANTEC_FLOWBUS_TIMEOUT_MS:-3000}"
  patch_toml_string "$CONFIG_PATH" flowbus connect_timeout_ms "${PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS:-500}"
  patch_toml_string "$CONFIG_PATH" flowbus read_timeout_ms "${PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS:-3000}"
  patch_toml_string "$CONFIG_PATH" flowbus keepalive "${PHIMANTEC_FLOWBUS_KEEPALIVE:-0}"
  patch_toml_string "$CONFIG_PATH" http mode "${PHIMANTEC_HTTP_MODE:-h1_only}"
  patch_toml_string "$CONFIG_PATH" http downstream_alpn "${PHIMANTEC_DOWNSTREAM_ALPN:-http/1.1}"
  patch_toml_string "$CONFIG_PATH" http upstream_alpn "${PHIMANTEC_UPSTREAM_ALPN:-http/1.1}"
  patch_toml_string "$CONFIG_PATH" http strip_alt_svc "${PHIMANTEC_STRIP_ALT_SVC:-1}"
  patch_toml_string "$CONFIG_PATH" http strip_upgrade_headers "${PHIMANTEC_STRIP_UPGRADE_HEADERS:-1}"
  patch_toml_string "$CONFIG_PATH" http preconnect_grace_ms "${PHIMANTEC_PRECONNECT_GRACE_MS:-250}"
  patch_toml_string "$CONFIG_PATH" http first_request_timeout_ms "${PHIMANTEC_FIRST_REQUEST_TIMEOUT_MS:-1500}"
  patch_toml_string "$CONFIG_PATH" http idle_tunnel_timeout_ms "${PHIMANTEC_IDLE_TUNNEL_TIMEOUT_MS:-30000}"
  patch_toml_string "$CONFIG_PATH" inspection sample_target_bytes "${PHIMANTEC_SAMPLE_TARGET_BYTES:-524288}"
  patch_toml_string "$CONFIG_PATH" inspection sample_max_bytes "${PHIMANTEC_SAMPLE_MAX_BYTES:-1048576}"
  patch_toml_string "$CONFIG_PATH" inspection stream_chunk_min_bytes "${PHIMANTEC_STREAM_CHUNK_MIN_BYTES:-524288}"
  patch_toml_string "$CONFIG_PATH" inspection queue_max "${PHIMANTEC_INSPECTION_QUEUE_MAX:-1024}"
  patch_toml_string "$CONFIG_PATH" inspection queue_wait_ms "${PHIMANTEC_INSPECTION_QUEUE_WAIT_MS:-3000}"
  patch_toml_string "$CONFIG_PATH" inspection max_inflight "${PHIMANTEC_INSPECTION_MAX_INFLIGHT:-128}"
  patch_toml_string "$CONFIG_PATH" inspection max_inflight_per_host "${PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_HOST:-32}"
  patch_toml_string "$CONFIG_PATH" inspection max_inflight_per_client "${PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_CLIENT:-96}"
  patch_toml_string "$CONFIG_PATH" tls max_body_bytes "${PHIMANTEC_SAMPLE_MAX_BYTES:-1048576}"
  patch_toml_string "$CONFIG_PATH" verdict timeout_ms "${PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS:-3000}"
  patch_toml_string "$CONFIG_PATH" verdict timeout_under_load_ms "${PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS:-8000}"
  patch_toml_string "$CONFIG_PATH" verdict fail_policy "closed"
}

openssl_text() {
  local path="$1"
  openssl x509 -in "$path" -noout -text 2>/dev/null || true
}

openssl_subject() {
  local path="$1"
  openssl x509 -in "$path" -noout -subject 2>/dev/null || true
}

ca_subject_matches() {
  local subject
  subject="$(openssl_subject "$CA_CERT")"
  [[ "$subject" == *"CN = ${CA_CN}"* || "$subject" == *"CN=${CA_CN}"* ]]
}

ca_has_required_extensions() {
  local text
  text="$(openssl_text "$CA_CERT")"
  [[ -n "$text" ]] || return 1
  printf '%s\n' "$text" | grep -q 'CA:TRUE' || return 1
  printf '%s\n' "$text" | grep -q 'Certificate Sign' || return 1
  printf '%s\n' "$text" | grep -q 'CRL Sign' || return 1
  return 0
}

ca_key_matches() {
  [[ -s "$CA_CERT" && -s "$CA_KEY" ]] || return 1
  openssl x509 -in "$CA_CERT" -noout >/dev/null 2>&1 || return 1
  openssl pkey -in "$CA_KEY" -noout >/dev/null 2>&1 || return 1
  openssl x509 -in "$CA_CERT" -noout -pubkey 2>/dev/null | openssl pkey -pubin -outform der 2>/dev/null | openssl sha256 >/tmp/phimantec_ca_cert_pub.sha256.$$ || return 1
  openssl pkey -in "$CA_KEY" -pubout -outform der 2>/dev/null | openssl sha256 >/tmp/phimantec_ca_key_pub.sha256.$$ || return 1
  local rc
  if cmp -s /tmp/phimantec_ca_cert_pub.sha256.$$ /tmp/phimantec_ca_key_pub.sha256.$$; then
    rc=0
  else
    rc=$?
  fi
  rm -f /tmp/phimantec_ca_cert_pub.sha256.$$ /tmp/phimantec_ca_key_pub.sha256.$$
  return "$rc"
}

ca_needs_regeneration() {
  bool_enabled "${PHIMANTEC_REGENERATE_CA:-0}" && return 0
  [[ -s "$CA_CERT" && -s "$CA_KEY" ]] || return 0
  ca_key_matches || return 0
  ca_subject_matches || return 0
  ca_has_required_extensions || return 0
  openssl x509 -checkend 604800 -noout -in "$CA_CERT" >/dev/null 2>&1 || return 0
  return 1
}

backup_existing_ca() {
  local suffix
  suffix="$(date -u +%Y%m%d-%H%M%S)"
  if [[ -e "$CA_CERT" ]]; then
    mv -f "$CA_CERT" "${CA_CERT}.bak.${suffix}"
    log "alte CA gesichert: ${CA_CERT}.bak.${suffix}"
  fi
  if [[ -e "$CA_KEY" ]]; then
    mv -f "$CA_KEY" "${CA_KEY}.bak.${suffix}"
    log "alter CA-Key gesichert: ${CA_KEY}.bak.${suffix}"
  fi
}

generate_ca() {
  local conf tmp_cert tmp_key
  conf="${CA_DIR}/phimantec-root-ca.openssl.cnf"
  tmp_cert="${CA_CERT}.tmp.$$"
  tmp_key="${CA_KEY}.tmp.$$"
  cat > "$conf" <<EOF_CONF
[ req ]
default_bits = 4096
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = v3_ca

[ dn ]
CN = ${CA_CN}
O = ${CA_ORG}
OU = ${CA_OU}

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true, pathlen:0
keyUsage = critical, keyCertSign, cRLSign
EOF_CONF

  openssl req -x509 -newkey rsa:4096 -sha256 -days "$CA_DAYS" -nodes \
    -config "$conf" -extensions v3_ca \
    -keyout "$tmp_key" -out "$tmp_cert"
  mv -f "$tmp_key" "$CA_KEY"
  mv -f "$tmp_cert" "$CA_CERT"
  chown root:phimantec "$CA_KEY" "$CA_CERT"
  chmod 0640 "$CA_KEY"
  chmod 0644 "$CA_CERT"
}

ensure_ca() {
  install -d -o root -g phimantec -m 0750 "$CA_DIR"
  if ca_needs_regeneration; then
    log "erzeuge browserfaehige PHIMANTEC Root-CA: CN=${CA_CN}"
    backup_existing_ca
    generate_ca
  else
    log "bestehende PHIMANTEC Root-CA ist gueltig: $(openssl_subject "$CA_CERT")"
  fi
  ca_key_matches || { log "CA-Key passt nicht zum CA-Zertifikat"; exit 42; }
  ca_has_required_extensions || { log "CA-Zertifikat besitzt nicht die erforderlichen CA-Erweiterungen"; exit 42; }
  log "CA SHA256: $(openssl x509 -in "$CA_CERT" -noout -fingerprint -sha256 | sed 's/^.*=//')"
}

install -d -o phimantec -g phimantec -m 0750 /var/cache/phimantec/certs
install -d -o root -g phimantec -m 0750 "$CA_DIR"

if [[ -f "$CONFIG_PATH" ]]; then
  apply_runtime_ports
fi

if [[ -n "${PHIMANTEC_FLOWBUS_TOKEN:-}" && -f "$CONFIG_PATH" ]]; then
  sed -i -E "s#^token = .*#token = \"${PHIMANTEC_FLOWBUS_TOKEN}\"#" "$CONFIG_PATH"
fi

validate_runtime_config
ensure_ca

exec /usr/local/bin/phimantec-proxyd -c "$CONFIG_PATH" -v
