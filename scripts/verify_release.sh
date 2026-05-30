#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PHIMANTEC_VERIFY_BUILD_DIR:-${ROOT_DIR}/build-verify}"
EXPECTED_VERSION="0.3.18-monitor-offline-vendor"
EXPECTED_RELEASE="phimantec-c11-appliance-v0.3.18-monitor-offline-vendor"
VERIFY_JOBS="${PHIMANTEC_VERIFY_JOBS:-1}"
log(){ printf '[verify] %s\n' "$*"; }
fail(){ printf '[verify] ERROR: %s\n' "$*" >&2; exit 1; }
trim_text(){ local value="$1"; value="${value#"${value%%[![:space:]]*}"}"; value="${value%"${value##*[![:space:]]}"}"; printf '%s\n' "$value"; }
strip_outer_quotes(){ local value="$1"; if [[ ${#value} -ge 2 ]]; then if [[ "${value:0:1}" == '"' && "${value: -1}" == '"' ]]; then value="${value:1:${#value}-2}"; elif [[ "${value:0:1}" == "'" && "${value: -1}" == "'" ]]; then value="${value:1:${#value}-2}"; fi; fi; printf '%s\n' "$value"; }
read_env_value(){ local key="$1" file="$2" line env_key value; [[ -f "$file" ]] || return 0; while IFS= read -r line || [[ -n "$line" ]]; do line="${line%$'\r'}"; [[ -n "$(trim_text "$line")" ]] || continue; [[ "$(trim_text "$line")" != \#* ]] || continue; [[ "$line" == *=* ]] || continue; env_key="$(trim_text "${line%%=*}")"; [[ "$env_key" == "$key" ]] || continue; value="$(trim_text "${line#*=}")"; strip_outer_quotes "$value"; return 0; done < "$file"; }
check_release_guard(){ local base env_version; base="$(basename "$ROOT_DIR")"; [[ "$base" == "$EXPECTED_RELEASE" ]] || fail "falscher Arbeitsstand: ${ROOT_DIR}. Erwartet ${EXPECTED_RELEASE}."; grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" BUILD_INFO.txt || fail "BUILD_INFO.txt passt nicht zu ${EXPECTED_VERSION}"; grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" .env.example || fail ".env.example passt nicht zu ${EXPECTED_VERSION}"; if [[ -f .env ]]; then env_version="$(read_env_value PHIMANTEC_VERSION .env)"; [[ -z "$env_version" || "$env_version" == "$EXPECTED_VERSION" ]] || fail ".env verwendet ${env_version}, erwartet ${EXPECTED_VERSION}"; fi; }
cd "$ROOT_DIR"
log "release guard"; check_release_guard
log "shell syntax"; while IFS= read -r -d '' file; do bash -n "$file"; done < <(find scripts -type f -name '*.sh' -print0)
log "python syntax"; python3 -m py_compile scripts/phimantec_suricata_yaml_patch.py suricata-monitor/backend/app.py suricata-monitor/backend/auth.py suricata-monitor/backend/config.py suricata-monitor/backend/db/connection.py suricata-monitor/backend/routes/*.py suricata-monitor/backend/services/*.py suricata-monitor/scripts/init_db.py suricata-monitor/tests/test_config_manager_paths.py suricata-monitor/tests/test_eve_reader_cursor.py suricata-monitor/tests/test_rules_reload_management.py tests/test_suricata_yaml_patch.py tests/test_proxy_streaming_guard.py
log "suricata-monitor unit smoke"; python3 suricata-monitor/tests/test_config_manager_paths.py; python3 suricata-monitor/tests/test_eve_reader_cursor.py; python3 suricata-monitor/tests/test_rules_reload_management.py
log "PHIMANTEC v0.3.18 regression smoke"; python3 tests/test_suricata_yaml_patch.py; python3 tests/test_proxy_streaming_guard.py
if command -v node >/dev/null 2>&1; then log "javascript syntax"; find suricata-monitor/frontend/js -type f -name '*.js' -print0 | sort -z | xargs -0 -n 1 node --check; else log "node not found; javascript syntax check skipped"; fi
log "docker static sanity"; scripts/verify_docker_static.sh
log "CA generation smoke"; CA_SMOKE_DIR="$(mktemp -d)"; trap 'rm -rf "$CA_SMOKE_DIR"' EXIT; PHIMANTEC_CA_DIR="$CA_SMOKE_DIR/ca" scripts/gen_ca.sh >/dev/null 2>&1; openssl x509 -in "$CA_SMOKE_DIR/ca/phimantec-bump-ca.crt" -noout -subject >/dev/null; CA_SMOKE_TEXT="$(openssl x509 -in "$CA_SMOKE_DIR/ca/phimantec-bump-ca.crt" -noout -text)"; printf '%s\n' "$CA_SMOKE_TEXT" | grep -q 'CA:TRUE'; printf '%s\n' "$CA_SMOKE_TEXT" | grep -q 'Certificate Sign'; printf '%s\n' "$CA_SMOKE_TEXT" | grep -q 'CRL Sign'; openssl verify -CAfile "$CA_SMOKE_DIR/ca/phimantec-bump-ca.crt" "$CA_SMOKE_DIR/ca/phimantec-bump-ca.crt" >/dev/null; rm -rf "$CA_SMOKE_DIR"; trap - EXIT
log "cmake build + ctest"; rm -rf "$BUILD_DIR"; cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHREADS_PREFER_PTHREAD_FLAG=ON; cmake --build "$BUILD_DIR" -j"$VERIFY_JOBS"; ctest --test-dir "$BUILD_DIR" --output-on-failure
log "vendor scripts no-source smoke"; scripts/vendor_unpack.sh; scripts/apply_third_party_patches.sh; scripts/build_third_party.sh
log "complete"
