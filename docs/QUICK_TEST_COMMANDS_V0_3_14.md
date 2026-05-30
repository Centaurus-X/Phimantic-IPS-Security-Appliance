# Quick Test Commands v0.3.14a

## Lokale Release-Integritaet

```bash
sha256sum -c MANIFEST.sha256
bash -n scripts/*.sh
python3 -m py_compile scripts/phimantec_suricata_yaml_patch.py suricata-monitor/backend/app.py suricata-monitor/backend/auth.py suricata-monitor/backend/config.py suricata-monitor/backend/db/connection.py suricata-monitor/backend/routes/*.py suricata-monitor/backend/services/*.py suricata-monitor/scripts/init_db.py tests/test_suricata_yaml_patch.py tests/test_proxy_streaming_guard.py
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHREADS_PREFER_PTHREAD_FLAG=ON
cmake --build build
ctest --test-dir build --output-on-failure
scripts/verify_docker_static.sh
```

## Zielhost-Rollout

```bash
cd "$HOME"
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -rf phimantec-c11-appliance-v0.3.17-monitor-offline-vendor
unzip -q -o phimantec-v0.3.17-final.zip
cd phimantec-c11-appliance-v0.3.17-monitor-offline-vendor
sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env
sed -i 's/^PHIMANTEC_REGENERATE_CA=.*/PHIMANTEC_REGENERATE_CA=1/' .env
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
```

## FlowBus-/inspectord-Probe

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node \
  /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-suricata-node.conf -t 1000

scripts/phimantec_compose.sh compose exec -T proxy \
  /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-proxyd.conf -t 1000
```

## EICAR-Matrix

```bash
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
```

## H1.1-/Alt-Svc-Test

```bash
curl -k -x http://127.0.0.1:3128 -I https://www.youtube.com/ \
  | grep -iE 'alt-svc|upgrade|http2-settings' || true
```

Erwartung: keine Ausgabe fuer Alt-Svc/Upgrade/HTTP2-Settings.

## Logpruefung

```bash
scripts/phimantec_compose.sh compose logs --tail=500 proxy suricata-node \
  | grep -E 'inspection|suricata-alert|pending|unavailable|timeout|FlowBus|verdict' || true
```
