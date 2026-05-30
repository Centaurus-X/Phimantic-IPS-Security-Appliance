# PHIMANTEC v0.3.17 — Quick Test Commands

```bash
cd "$HOME/phimantec-c11-appliance-v0.3.17-monitor-offline-vendor"
sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
```

## Basis-Seiten

```bash
curl -k -x http://127.0.0.1:3128 -I https://example.org/
curl -k -x http://127.0.0.1:3128 -I https://www.google.com/
curl -k -x http://127.0.0.1:3128 -I https://www.youtube.com/
curl -k -x http://127.0.0.1:3128 -I https://www.amazon.de/
```

## Paralleltest

```bash
seq 1 40 | xargs -P 20 -I{} sh -c '
curl -k -sS -o /dev/null -w "%{http_code} %{url_effective}\n" \
  -x http://127.0.0.1:3128 https://www.amazon.de/ || true
'
```

## Logprüfung

```bash
scripts/phimantec_compose.sh compose logs --tail=1000 proxy suricata-node \
  | grep -E "Inspection unavailable|inspection-|flowbus-|queue-|suricata-alert|timeout|verdict" || true
```
