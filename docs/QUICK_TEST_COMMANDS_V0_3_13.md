# Quick Test Commands v0.3.14

## 1. Frisches Deployment

```bash
cd "$HOME"

docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -rf phimantec-c11-appliance-v0.3.17-monitor-offline-vendor

unzip -q -o phimantec-v0.3.14-release.zip
cd phimantec-c11-appliance-v0.3.17-monitor-offline-vendor

sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env
sed -i 's/^PHIMANTEC_REGENERATE_CA=.*/PHIMANTEC_REGENERATE_CA=1/' .env
sed -i 's/^PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=.*/PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=2500/' .env
sed -i 's/^PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=.*/PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=1200/' .env

PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
```

## 2. Suricata- und Timeout-Konfiguration pruefen

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
echo "=== Runmode / Pattern Matcher ==="
grep -nE "^[[:space:]]*(runmode|mpm-algo|spm-algo):" /etc/suricata/suricata.yaml

echo
echo "=== HTTP Body Limits ==="
python3 - <<PY
import yaml
with open("/etc/suricata/suricata.yaml", "r", encoding="utf-8", errors="replace") as handle:
    data = yaml.safe_load(handle) or {}
http = data["app-layer"]["protocols"]["http"]["libhtp"]["default-config"]
for key in (
    "request-body-limit",
    "response-body-limit",
    "request-body-minimal-inspect-size",
    "request-body-inspect-window",
    "response-body-minimal-inspect-size",
    "response-body-inspect-window",
):
    print(f"{key}: {http.get(key)}")
PY
'

grep -E 'PHIMANTEC_(PROXY_VERDICT_TIMEOUT_MS|SURICATA_DECISION_TIMEOUT_MS)' .env
```

Erwartung:

```text
mpm-algo: hs
spm-algo: hs
runmode: workers
request-body-limit: 1048576
response-body-limit: 1048576
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=2500
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=1200
```

## 3. Einzelner EICAR-Test mit Connection-Close und Cache-Busting

```bash
CB="$(date +%s%N)"
curl \
  --silent \
  --show-error \
  --location \
  --insecure \
  --proxy http://127.0.0.1:3128 \
  --header 'Connection: close' \
  --header 'Cache-Control: no-store' \
  --output /tmp/phimantec-eicar-one.bin \
  --write-out 'HTTP=%{http_code}\n' \
  "https://secure.eicar.org/eicar.com.txt?phim_cb=${CB}"

if grep -a -q 'EICAR-STANDARD-ANTIVIRUS-TEST-FILE' /tmp/phimantec-eicar-one.bin; then
  echo 'FAIL: EICAR leaked'
  exit 1
fi
```

Erwartung: HTTP `403` oder `503`, aber kein EICAR-String im Client-Body.

## 4. Matrix-Test

```bash
PROXY_URL=http://127.0.0.1:3128 OUT_DIR=/tmp/phimantec-eicar-url-test scripts/eicar_url_matrix_test.sh
```

## 5. Logs nach dem Matrix-Test

```bash
scripts/phimantec_compose.sh compose logs --no-color suricata-node \
  | grep -E 'verdict flow=.*action=(block|allow|pending|error)' \
  | tail -60

scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
grep -aE "900002|900004|PHIMANTEC EICAR|suricata-alert" /var/log/suricata/eve.json | tail -60
'
```

Es darf fuer denselben inspizierten Flow keinen Client-Leak durch `allow` vor einem spaeteren `block` geben. Erlaubte Bodies werden erst nach finalem `suricata-no-alert-final` ausgeliefert.

## 6. Lokale Build-/Regressionstests

```bash
scripts/verify_release.sh
```

Alternativ kurz:

```bash
rm -rf build-verify
cmake -S . -B build-verify -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHREADS_PREFER_PTHREAD_FLAG=ON
cmake --build build-verify -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
ctest --test-dir build-verify --output-on-failure
python3 tests/test_proxy_streaming_guard.py
python3 tests/test_suricata_yaml_patch.py
```
