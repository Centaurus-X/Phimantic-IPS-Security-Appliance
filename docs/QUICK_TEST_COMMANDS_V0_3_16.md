# Quick Test Commands v0.3.17

## Rollout

```bash
cd "$HOME"
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -rf phimantec-c11-appliance-v0.3.17-monitor-offline-vendor
unzip -q -o phimantec-v0.3.17-final.zip
cd phimantec-c11-appliance-v0.3.17-monitor-offline-vendor
sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
```

## Smoke und Security

```bash
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
```

## FlowBus und Runtime

```bash
scripts/phimantec_compose.sh compose ps
scripts/phimantec_compose.sh compose exec -T proxy \
  /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-proxyd.conf -t 1000
scripts/phimantec_compose.sh compose exec -T suricata-node \
  /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-suricata-node.conf -t 1000
```

## H1.1 Header-Kontrolle

```bash
curl -k -x http://127.0.0.1:3128 -I https://www.youtube.com/ \
  | grep -iE 'alt-svc|upgrade|http2-settings' || true
```

## Diagnose bei unavailable

```bash
scripts/phimantec_compose.sh compose logs --no-color --tail=800 proxy suricata-node \
  | grep -E 'Inspection unavailable|FlowBus verdict unavailable|lane send failed|suricata-alert|suricata-no-alert-final|timeout|WANT|EAGAIN' || true

scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
  ss -ltnp | grep -E ":(18080|18090|5050|3128)\\b" || true
  ps aux | grep -E "phimantec-lane-sink|phimantec-inspectord|suricata" | grep -v grep || true
  grep -R "flowbus\|verdict\|timeout\|fail_policy\|keepalive" /etc/phimantec 2>/dev/null || true
'
```
