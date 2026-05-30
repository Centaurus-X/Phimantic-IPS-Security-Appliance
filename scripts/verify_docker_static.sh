#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
EXPECTED_VERSION="0.3.18-monitor-offline-vendor"
EXPECTED_PROJECT="phimantec-c11-v0318"
fail(){ printf '[verify-docker-static] ERROR: %s\n' "$*" >&2; exit 1; }
check_file(){ [[ -f "$1" ]] || fail "missing file: $1"; }
check_path(){ [[ -e "$1" ]] || fail "missing path: $1"; }
check_no_unbounded_apt_retry(){ if grep -RIn 'Acquire::Retries "[1-9]' docker/common docker/*.Dockerfile docker 2>/dev/null; then fail "APT retry must stay fail-fast by default"; fi; }
check_docker_copy_paths(){ local path; while IFS= read -r path; do [[ -z "$path" ]] && continue; case "$path" in --from=*|/*) continue;; esac; check_path "$path"; done < <(awk '/^[[:space:]]*COPY[[:space:]]/ { for (i=2; i<NF; i++) { if ($i !~ /^--/) print $i } }' docker/proxy/Dockerfile docker/suricata-node/Dockerfile | sed 's#/$##'); }
check_compose_yaml(){ python3 - <<'PYVERIFY'
from pathlib import Path
import sys
try:
    import yaml
except ImportError:
    sys.exit(0)
with Path('docker-compose.yml').open('r', encoding='utf-8') as handle:
    data = yaml.safe_load(handle)
if not isinstance(data, dict):
    raise SystemExit('docker-compose.yml root is not a mapping')
services = data.get('services')
if not isinstance(services, dict):
    raise SystemExit('docker-compose.yml has no services mapping')
for name in ('proxy', 'suricata-node'):
    service = services.get(name)
    if not isinstance(service, dict):
        raise SystemExit('missing compose service: ' + name)
    if 'container_name' in service:
        raise SystemExit('fixed container_name is forbidden: ' + name)
    build = service.get('build')
    if not isinstance(build, dict) or 'network' not in build:
        raise SystemExit('missing build.network for: ' + name)
    if service.get('restart') != 'unless-stopped':
        raise SystemExit('restart policy must be unless-stopped in appliance mode: ' + name)
    if service.get('network_mode') != 'host':
        raise SystemExit('network_mode must be host for appliance root fix: ' + name)
    if service.get('ports'):
        raise SystemExit('ports must be absent with host-network runtime: ' + name)
    if 'healthcheck' not in service:
        raise SystemExit('missing healthcheck for: ' + name)
node = services['suricata-node']
cap_add = set(node.get('cap_add') or [])
for capability in ('NET_ADMIN', 'NET_RAW'):
    if capability not in cap_add:
        raise SystemExit('suricata-node missing capability: ' + capability)
node_env = node.get('environment') or {}
for key in ('PHIMANTEC_PROXY_PORT', 'PHIMANTEC_MONITOR_PORT', 'PHIMANTEC_APPLIANCE_MANAGE_INGRESS', 'PHIMANTEC_APPLIANCE_REQUIRE_INGRESS', 'PHIMANTEC_REQUIRE_SURICATA8'):
    if key not in node_env:
        raise SystemExit('suricata-node missing environment: ' + key)
node_build_args = (node.get('build') or {}).get('args') or {}
if 'PHIMANTEC_SURICATA_APT_PPA' not in node_build_args:
    raise SystemExit('suricata-node missing OISF Suricata PPA build arg')
depends = services['proxy'].get('depends_on')
if not isinstance(depends, dict) or depends.get('suricata-node', {}).get('condition') != 'service_healthy':
    raise SystemExit('suricata-node dependency must wait for service_healthy')
PYVERIFY
}
check_compose_versioned_images(){ grep -Fq "phimantec/proxy:\${PHIMANTEC_VERSION:-${EXPECTED_VERSION}}" docker-compose.yml || fail "proxy image tag is not environment-versioned"; grep -Fq "phimantec/suricata-node:\${PHIMANTEC_VERSION:-${EXPECTED_VERSION}}" docker-compose.yml || fail "suricata image tag is not environment-versioned"; grep -Fq 'network: ${PHIMANTEC_DOCKER_BUILD_NETWORK:-host}' docker-compose.yml || fail "build network knob missing"; grep -Fq "name: \${PHIMANTEC_COMPOSE_PROJECT_NAME:-${EXPECTED_PROJECT}}" docker-compose.yml || fail "compose project name knob missing"; }
check_entrypoint_no_infinite_watch_loop(){ grep -q 'while true' scripts/docker_suricata_node_entrypoint.sh && fail "suricata Docker entrypoint contains while true monitor loop"; grep -q 'wait -n' scripts/docker_suricata_node_entrypoint.sh || fail "suricata Docker entrypoint must use finite wait -n supervision"; grep -q 'phimantec_ingress_rules.sh' scripts/docker_suricata_node_entrypoint.sh || fail "suricata Docker entrypoint must apply container-managed ingress rules"; grep -q 'PHIMANTEC_APPLIANCE_REQUIRE_INGRESS' scripts/docker_suricata_node_entrypoint.sh || fail "ingress failure must be explicit"; }
check_dockerfile_ingress_support(){ grep -q 'nftables' docker/suricata-node/Dockerfile || fail "suricata-node image must include nftables"; grep -q 'iptables' docker/suricata-node/Dockerfile || fail "suricata-node image must include iptables fallback"; grep -q 'phimantec_ingress_rules.sh' docker/suricata-node/Dockerfile || fail "suricata-node image must copy ingress manager"; }
check_suricata8_install_path(){ grep -q 'ppa:oisf/suricata-stable' docker/suricata-node/Dockerfile || fail "suricata-node must use OISF Suricata stable PPA"; grep -Fq 'This is Suricata version ([8-9]' docker/suricata-node/Dockerfile || fail "suricata-node Dockerfile must fail if Suricata < 8"; grep -q 'verify_suricata_version' scripts/docker_suricata_node_entrypoint.sh || fail "suricata runtime version gate missing"; ! grep -Eq 'suricata[[:space:]]+suricata-update' docker/suricata-node/Dockerfile || fail "suricata-update must not be installed together with OISF suricata package"; grep -q 'command -v suricata-update' docker/suricata-node/Dockerfile || fail "suricata-update availability fallback missing"; }
check_ca_generation(){ grep -q 'basicConstraints = critical, CA:true' scripts/docker_proxy_entrypoint.sh || fail "browser CA basicConstraints missing"; grep -q 'keyUsage = critical, keyCertSign, cRLSign' scripts/docker_proxy_entrypoint.sh || fail "browser CA keyUsage missing"; grep -q 'PHIM_LEAF_SERIAL_BYTES 16u' appliance/src/phim_cert.c || fail "leaf certificate serial must be 128-bit"; grep -q 'critical,CA:FALSE' appliance/src/phim_cert.c || fail "leaf certificate basicConstraints must be critical"; grep -q 'test_cert_generation' CMakeLists.txt || fail "certificate generation unit test missing"; grep -q 'ca_has_required_extensions' scripts/install_proxy_vm_ubuntu.sh || fail "VM installer must regenerate non-browser-capable CAs"; }
check_suricata_only_block_gate(){
  ! grep -RIn 'local-safety-test-policy\|local-dummy-policy\|inspectord-eicar-test-policy\|inspectord-test-policy' appliance/src suricata-node/src || fail "local block policy leftovers detected"
  ! grep -q 'suricata-flowbus-timeout-fail-open' appliance/src/phim_verdict.c || fail "proxy must not fail open when Suricata/FlowBus is unavailable"
  grep -q 'suricata-flowbus-disabled' appliance/src/phim_verdict.c || fail "FlowBus disabled must be an explicit fail-closed error"
  grep -q 'suricata-inspection-unavailable' appliance/src/phim_verdict.c || fail "FlowBus/Suricata timeout must be explicit fail-closed inspection unavailable"
  grep -q 'PHIM_VERDICT_PENDING' shared/include/phimantec/phim_types.h || fail "pending verdict state missing"
  grep -q 'suricata-no-alert-final' suricata-node/src/inspectord_main.c || fail "inspectord final allow reason missing"
  grep -q 'suricata-pending-no-eve-flow' suricata-node/src/inspectord_main.c || fail "inspectord pending verdict reason missing"
  grep -q 'suricata-alert' suricata-node/src/inspectord_main.c || fail "inspectord Suricata alert block reason missing"
  grep -q 'test_suricata_only_gate' CMakeLists.txt || fail "Suricata-only block gate unit test missing"
}
check_flow_id_correlation_parser(){
  grep -q 'phim_copy_generated_flow_id_after_marker' shared/src/phim_eve.c || fail "strict generated Flow-ID parser missing"
  ! grep -q 'phim_is_flow_id_char' shared/src/phim_eve.c || fail "generic Flow-ID char scanner must not be used for EVE correlation"
  grep -q 'TP/1.1' tests/test_eve_correlation.c || fail "EVE correlation regression for collapsed HTTP tail missing"
  grep -q 'POST /phimantec/lane/%s/%s HTTP/1.1' appliance/src/phim_lane.c || fail "Flow-ID must be redundantly present in lane URL"
  grep -q '^alert http any any -> any 18080' config/suricata/local.rules || fail "PHIMANTEC local rules must use HTTP layer"; grep -q 'PHIMANTEC EICAR TEST PATH' config/suricata/local.rules || fail "Suricata EICAR test path rule missing"
  ! grep -RIn '^alert tcp any any -> any 18080.*PHIMANTEC' config scripts || fail "stale TCP-layer PHIMANTEC rules detected"
}
check_lab_capture_auto_loopback(){
  grep -q 'is_loopback_host' scripts/docker_suricata_node_entrypoint.sh || fail "loopback lane detection missing"
  grep -q 'lane_bind_host' scripts/docker_suricata_node_entrypoint.sh || fail "lane bind host parser missing"
  grep -q 'PHIMANTEC_LANE_PORT' scripts/docker_suricata_node_entrypoint.sh || fail "suricata entrypoint must inspect lane bind"
  grep -q 'PHIMANTEC_SURICATA_INTERFACE=auto' .env.example || fail "suricata interface must remain configurable"
}
check_monitor_event_path(){
  grep -q 'read_alerts_from_offset' suricata-monitor/backend/services/eve_reader.py || fail "incremental EVE reader missing"
  grep -q 'test_eve_reader_cursor' scripts/verify_release.sh || fail "EVE cursor unit smoke missing"
  grep -q 'eve_reader.cursor.v1' suricata-monitor/backend/routes/events_routes.py || fail "EVE cursor persistence missing"
  grep -q '_sync_from_eve' suricata-monitor/backend/routes/dashboard_routes.py || fail "dashboard must sync EVE before stats"
  grep -q 'total_lines' suricata-monitor/frontend/js/pages/dashboard.js || fail "dashboard total EVE lines display missing"
}
check_raw_yaml_editor(){
  grep -q 'writeEnabled' suricata-monitor/frontend/js/pages/settings.js || fail "raw YAML write-enable switch missing"
  grep -q 'ta.readOnly = !writeEnabled.checked' suricata-monitor/frontend/js/pages/settings.js || fail "raw YAML editor must default read-only"
  grep -q 'node.value' suricata-monitor/frontend/js/lib/utils.js || fail "textarea value DOM-property fix missing"
}
check_wrapper_files(){ for f in scripts/phimantec_compose.sh scripts/preflight_docker.sh scripts/smoke_docker.sh scripts/export_docker_ca.sh scripts/cleanup_stale_docker.sh scripts/rollout_docker.sh scripts/phimantec_ingress_rules.sh; do check_file "$f"; done; grep -q "$EXPECTED_VERSION" scripts/phimantec_compose.sh || fail "compose wrapper version guard missing"; grep -q 'export-ca' scripts/phimantec_compose.sh || fail "compose wrapper CA export command missing"; grep -q 'container applies ingress rules directly' scripts/phimantec_compose.sh || fail "compose wrapper must document container-managed ingress"; }

check_suricata_plain_body_depth(){
  grep -q 'request-body-limit' scripts/phimantec_suricata_yaml_patch.py || fail "Suricata HTTP request-body-limit patch missing"
  grep -q 'request-body-inspect-window' scripts/phimantec_suricata_yaml_patch.py || fail "Suricata HTTP inspect-window patch missing"
  grep -q 'PHIMANTEC_SURICATA_BODY_LIMIT' scripts/docker_suricata_node_entrypoint.sh || fail "body-limit runtime knob missing"
  grep -q '^PHIMANTEC_SURICATA_BODY_LIMIT=1048576$' .env.example || fail "body-limit default must be 1 MiB"
  grep -q '^PHIMANTEC_SURICATA_MPM=hs$' .env.example || fail "Hyperscan MPM must default to hs"
  grep -q '^PHIMANTEC_SURICATA_SPM=hs$' .env.example || fail "Hyperscan SPM must default to hs"
  grep -q 'PHIMANTEC_SURICATA_MPM: ${PHIMANTEC_SURICATA_MPM:-hs}' docker-compose.yml || fail "compose MPM default must be hs"
  grep -q 'PHIMANTEC_SURICATA_SPM: ${PHIMANTEC_SURICATA_SPM:-hs}' docker-compose.yml || fail "compose SPM default must be hs"
  grep -q 'PHIMANTEC_SURICATA_BODY_LIMIT: ${PHIMANTEC_SURICATA_BODY_LIMIT:-1048576}' docker-compose.yml || fail "compose body-limit env missing"
  grep -q 'test_suricata_yaml_patch' scripts/verify_release.sh || fail "Suricata body-limit regression test missing"
}
check_proxy_streaming_block_context(){
  grep -q 'headers_already_sent' appliance/src/phim_proxy_worker.c || fail "proxy block context flag missing"
  grep -q 'phim_write_inspection_unavailable_ssl' appliance/src/phim_proxy_worker.c || fail "inspection unavailable 503 path missing"
  grep -q 'phim_inspect_response_stream_chunk' appliance/src/phim_proxy_worker.c || fail "segment verdict barrier missing"
  grep -q 'PHIM_STREAM_INSPECT_OVERLAP_BYTES' appliance/src/phim_proxy_worker.c || fail "cross-segment overlap missing"
  grep -q 'phim_write_response_header_once' appliance/src/phim_proxy_worker.c || fail "response header-once guard missing"
  grep -q 'phim_h1_chunked_decoder_feed' appliance/src/phim_proxy_worker.c || fail "chunked request/response decoder guard missing"
  ! grep -q 'body_chunk' appliance/src/phim_proxy_worker.c || fail "streaming body_chunk forwarding must not be used before final verdict"
  grep -q 'test_proxy_streaming_guard' scripts/verify_release.sh || fail "proxy streaming regression test missing"
}
check_env_defaults(){
  grep -q '^PHIMANTEC_PROXY_PORT=3128$' .env.example || fail "proxy default must be external 3128"
  grep -q '^PHIMANTEC_MONITOR_PORT=5050$' .env.example || fail "monitor default must be external 5050"
  grep -q '^PHIMANTEC_RUNTIME_NETWORK=host$' .env.example || fail "runtime network must default to host"
  grep -q '^PHIMANTEC_APPLIANCE_MANAGE_INGRESS=1$' .env.example || fail "container ingress management must default to enabled"
  grep -q '^PHIMANTEC_APPLIANCE_REQUIRE_INGRESS=1$' .env.example || fail "ingress must default to required"
  grep -q '^PHIMANTEC_APPLIANCE_HOST_PRE_APPLY_INGRESS=0$' .env.example || fail "host pre-ingress must default to disabled"
  grep -q '^PHIMANTEC_AUTO_EXPORT_CA=1$' .env.example || fail "CA auto export must default to enabled"
  grep -q '^PHIMANTEC_ALLOW_LOCAL_BIND=0$' .env.example || fail "local bind override must default to disabled"
  grep -q '^PHIMANTEC_REQUIRE_SURICATA8=1$' .env.example || fail "Suricata 8 requirement must default to enabled"
  grep -q '^PHIMANTEC_REGENERATE_CA=0$' .env.example || fail "CA regeneration must default to disabled"
  grep -q '^PHIMANTEC_LANE_POOL_SIZE=16$' .env.example || fail "lane pool v0.3.18 default missing"
  grep -q '^PHIMANTEC_FLOWBUS_POOL_SIZE=16$' .env.example || fail "flowbus pool must default to sixteen fresh parallel clients"
  grep -q '^PHIMANTEC_FLOWBUS_TIMEOUT_MS=3000$' .env.example || fail "flowbus I/O timeout default missing"
  grep -q '^PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS=500$' .env.example || fail "flowbus connect timeout default missing"
  grep -q '^PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS=3000$' .env.example || fail "flowbus read timeout default missing"
  grep -q '^PHIMANTEC_FLOWBUS_KEEPALIVE=0$' .env.example || fail "flowbus keepalive must default to disabled"
  grep -q '^PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000$' .env.example || fail "proxy verdict timeout default missing"
  grep -q '^PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS=8000$' .env.example || fail "proxy under-load verdict timeout default missing"
  grep -q '^PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=900$' .env.example || fail "suricata decision timeout default missing"
  grep -q '^PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS=100$' .env.example || fail "inspectord no-alert grace default missing"
  grep -q '^PHIMANTEC_INSPECTION_QUEUE_MAX=1024$' .env.example || fail "inspection queue max default missing"
  grep -q '^PHIMANTEC_INSPECTION_QUEUE_WAIT_MS=3000$' .env.example || fail "inspection queue wait default missing"
  grep -q '^PHIMANTEC_INSPECTION_MAX_INFLIGHT=128$' .env.example || fail "inspection global inflight default missing"
  grep -q '^PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_HOST=32$' .env.example || fail "inspection host inflight default missing"
  grep -q '^PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_CLIENT=96$' .env.example || fail "inspection client inflight default missing"
  grep -q '^PHIMANTEC_SURICATA_MPM=hs$' .env.example || fail "mpm hs default missing"
  grep -q '^PHIMANTEC_SURICATA_SPM=hs$' .env.example || fail "spm hs default missing"
  grep -q '^PHIMANTEC_SURICATA_BODY_LIMIT=1048576$' .env.example || fail "suricata body limit default missing"
}

check_ruleset_reload_management(){
  grep -q 'ruleset-reload-rules' suricata-monitor/backend/services/suricata_ctl.py || fail "ruleset-reload-rules reload command missing"
  grep -q 'suricata -T' suricata-monitor/backend/services/suricata_ctl.py || fail "suricata config test before reload missing"
  grep -q 'ruleset-reload-time' suricata-monitor/backend/services/suricata_ctl.py || fail "ruleset reload-time status missing"
  grep -q 'default-rule-path.*var/lib/suricata/rules' scripts/phimantec_suricata_yaml_patch.py || fail "runtime default-rule-path must be /var/lib/suricata/rules"
  grep -q 'external/custom-local.rules' scripts/phimantec_suricata_yaml_patch.py || fail "external custom rules entry missing"
  ! grep -q -- '-S "$RULE_FILE"' scripts/docker_suricata_node_entrypoint.sh || fail "suricata must not start with -S single-rules override"
  grep -q 'prepare_runtime_rule_files' scripts/docker_suricata_node_entrypoint.sh || fail "runtime rule file preparation missing"
  grep -q 'phimantec-suricata-rules' docker-compose.yml || fail "persistent suricata rules volume missing"
  grep -q 'rules_dir: "/var/lib/suricata/rules"' config/docker/suricata-monitor.yaml || fail "docker monitor must manage active rules dir"
  grep -q 'suricata-update failed' suricata-monitor/backend/services/ruleset_manager.py || fail "suricata-update diagnostics missing"
}

check_flowbus_stability_defaults(){
  grep -q 'PHIMANTEC_FLOWBUS_POOL_SIZE: ${PHIMANTEC_FLOWBUS_POOL_SIZE:-16}' docker-compose.yml || fail "compose flowbus pool must default to 16"
  grep -q 'PHIMANTEC_FLOWBUS_TIMEOUT_MS: ${PHIMANTEC_FLOWBUS_TIMEOUT_MS:-3000}' docker-compose.yml || fail "compose flowbus timeout missing"
  grep -q 'PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS: ${PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS:-500}' docker-compose.yml || fail "compose flowbus connect timeout missing"
  grep -q 'PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS: ${PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS:-3000}' docker-compose.yml || fail "compose flowbus read timeout missing"
  grep -q 'PHIMANTEC_FLOWBUS_KEEPALIVE: ${PHIMANTEC_FLOWBUS_KEEPALIVE:-0}' docker-compose.yml || fail "compose flowbus keepalive must default to 0"
  grep -q 'PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS: ${PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS:-3000}' docker-compose.yml || fail "compose proxy verdict timeout must default to 3000"
  grep -q 'PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS: ${PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS:-8000}' docker-compose.yml || fail "compose proxy under-load verdict timeout missing"
  grep -q 'PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS: ${PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS:-900}' docker-compose.yml || fail "compose suricata decision timeout must default to 900"
  grep -q 'PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS: ${PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS:-100}' docker-compose.yml || fail "compose inspectord grace must default to 100"
  grep -q 'PHIMANTEC_INSPECTION_QUEUE_MAX: ${PHIMANTEC_INSPECTION_QUEUE_MAX:-1024}' docker-compose.yml || fail "compose inspection queue max missing"
  grep -q 'PHIMANTEC_INSPECTION_MAX_INFLIGHT: ${PHIMANTEC_INSPECTION_MAX_INFLIGHT:-128}' docker-compose.yml || fail "compose inspection max inflight missing"
  grep -q 'phimantec-flowbus-probe' CMakeLists.txt || fail "FlowBus probe target missing"
  grep -q 'phimantec-flowbus-probe' docker/proxy/Dockerfile || fail "proxy image must include FlowBus probe"
  grep -q 'phimantec-flowbus-probe' docker/suricata-node/Dockerfile || fail "suricata-node image must include FlowBus probe"
  grep -q 'phimantec-flowbus-probe' docker-compose.yml || fail "compose healthchecks must execute FlowBus probe"
  grep -q 'flowbus_read_timeout_ms' shared/src/phim_config.c || fail "FlowBus read timeout must be separate in config parser"
  grep -q 'no_alert_grace_ms' suricata-node/src/inspectord_main.c || fail "inspectord early no-alert grace missing"
}

check_file docker/common/apt_install.sh
check_file .dockerignore
check_file .env.example
check_no_unbounded_apt_retry
check_docker_copy_paths
check_compose_versioned_images
check_compose_yaml
check_entrypoint_no_infinite_watch_loop
check_dockerfile_ingress_support
check_suricata8_install_path
check_ca_generation
check_suricata_only_block_gate
check_flow_id_correlation_parser
check_lab_capture_auto_loopback
check_monitor_event_path
check_raw_yaml_editor
check_suricata_plain_body_depth
check_proxy_streaming_block_context
check_wrapper_files
check_env_defaults
check_flowbus_stability_defaults
printf '[verify-docker-static] complete\n'
