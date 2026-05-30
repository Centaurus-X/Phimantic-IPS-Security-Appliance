# Release Checks v0.3.17

## Lokale Gates

```bash
sha256sum -c MANIFEST.sha256
bash -n scripts/*.sh
python3 -m py_compile scripts/phimantec_suricata_yaml_patch.py \
  suricata-monitor/backend/app.py \
  suricata-monitor/backend/auth.py \
  suricata-monitor/backend/config.py \
  suricata-monitor/backend/db/connection.py \
  suricata-monitor/backend/routes/*.py \
  suricata-monitor/backend/services/*.py \
  suricata-monitor/scripts/init_db.py \
  tests/test_suricata_yaml_patch.py \
  tests/test_proxy_streaming_guard.py
python3 suricata-monitor/tests/test_config_manager_paths.py
python3 suricata-monitor/tests/test_eve_reader_cursor.py
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHREADS_PREFER_PTHREAD_FLAG=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
scripts/verify_docker_static.sh
scripts/verify_release.sh
```

## Zielhost-Gates

```bash
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
```

## Akzeptanz

```text
EICAR-Matrix: PASS, kein Leak
harmlose lokale HTTPS-Smoke-Seite: HTTP 200
HTTP 503 wird als Inspection-/FlowBus-Problem diagnostiziert, nicht als CA-Fehler
FlowBus-Probe aus proxy und suricata-node: PASS
H1.1-only bleibt aktiv
Alt-Svc/Upgrade/HTTP2-Settings werden entfernt
FlowBus keepalive default: 0
FlowBus pool default: 4
Response sample target default: 524288
Fail policy: closed
```
