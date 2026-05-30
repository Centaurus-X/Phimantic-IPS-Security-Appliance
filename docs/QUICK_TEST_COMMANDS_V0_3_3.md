# PHIMANTEC C11 Appliance v0.3.3 — Kompakte Test-Commands

## Frisch entpacken

```bash
cd "$HOME"
rm -rf phimantec-c11-appliance-v0.3.3-monitor-offline-vendor
unzip -q -o phimantec-c11-appliance-v0.3.3-monitor-offline-vendor.zip
cd phimantec-c11-appliance-v0.3.3-monitor-offline-vendor
```

## Lokal pruefen

```bash
sha256sum -c MANIFEST.sha256
scripts/check_release_context.sh
scripts/verify_docker_static.sh
# Vollstaendiges lokales Gate:
PHIMANTEC_VERIFY_JOBS=1 scripts/verify_release.sh
```

## Docker Rollout

```bash
scripts/phimantec_compose.sh init-env
scripts/phimantec_compose.sh clean-stale
scripts/phimantec_doctor.sh
scripts/phimantec_compose.sh build --no-cache
scripts/phimantec_compose.sh up
scripts/phimantec_compose.sh ps
scripts/phimantec_smoke.sh
```

Oder kompakt:

```bash
scripts/phimantec_deploy.sh --no-cache
```

## Logs

```bash
scripts/phimantec_compose.sh logs suricata-node
scripts/phimantec_compose.sh logs proxy
```

## Zielports

```text
SuricataMonitor: http://127.0.0.1:5050
Proxy:            http://127.0.0.1:33128
Lane intern:      127.0.0.1:18080
FlowBus intern:   127.0.0.1:18090
```

Klassischer Proxy-Port 3128:

```bash
sed -i 's#^PHIMANTEC_PROXY_PORT=.*#PHIMANTEC_PROXY_PORT=0.0.0.0:3128#' .env
scripts/phimantec_compose.sh redeploy
```


## Optionaler externer Proxy-Test

```bash
PHIMANTEC_SMOKE_EXTERNAL=1 scripts/phimantec_smoke.sh --external
```

Der Standard-Smoke bleibt absichtlich lokal und benoetigt kein Internet.
