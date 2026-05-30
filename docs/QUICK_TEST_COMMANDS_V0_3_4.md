# PHIMANTEC C11 Appliance v0.3.4 — Kompakte Test-Commands

## Frisch entpacken

```bash
cd "$HOME"
rm -rf phimantec-c11-appliance-v0.3.4-monitor-offline-vendor
unzip -q -o phimantec-c11-appliance-v0.3.4-monitor-offline-vendor.zip
cd phimantec-c11-appliance-v0.3.4-monitor-offline-vendor
```

## Lokal prüfen

```bash
sha256sum -c MANIFEST.sha256
scripts/check_release_context.sh
scripts/verify_docker_static.sh
PHIMANTEC_VERIFY_JOBS=1 scripts/verify_release.sh
```

## Docker Rollout

```bash
scripts/phimantec_compose.sh init-env
scripts/phimantec_compose.sh clean-stale --force
scripts/phimantec_doctor.sh
scripts/phimantec_compose.sh build --no-cache
scripts/phimantec_compose.sh up
scripts/phimantec_compose.sh export-ca
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
SuricataMonitor extern: http://<docker-host-ip>:5050
Proxy extern:           http://<docker-host-ip>:3128
Lane intern:            127.0.0.1:18080
FlowBus intern:         127.0.0.1:18090
CA auf Host:            ~/phimantec-bump-ca.crt
```

## Port- und CA-Checks

```bash
scripts/phimantec_compose.sh ps
test -r "$HOME/phimantec-bump-ca.crt"
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject
```

Erwartet in `ps`:

```text
0.0.0.0:3128->3128/tcp oder :::3128->3128/tcp
0.0.0.0:5050->5050/tcp oder :::5050->5050/tcp
```

Nicht erwartet:

```text
127.0.0.1:33128->3128/tcp
127.0.0.1:5050->5050/tcp
```

## Externer Test vom LAN-Client

```bash
curl -fsS http://<docker-host-ip>:5050/ >/dev/null && echo monitor_extern_ok
curl -sS -x http://<docker-host-ip>:3128 -I https://example.org/
```

## Optionaler Internet-Proxy-Smoke

```bash
PHIMANTEC_SMOKE_EXTERNAL=1 scripts/phimantec_smoke.sh --external
```

Der Standard-Smoke bleibt ohne Internet-Abhängigkeit, prüft aber trotzdem, ob Docker die Ports extern published und ob die CA nach `$HOME` exportiert wurde.
