# PHIMANTEC C11 Appliance v0.3.8 — Kompakte Test-Commands

## Frisch entpacken

```bash
cd "$HOME"
rm -rf phimantec-c11-appliance-v0.3.8-monitor-offline-vendor
unzip -q -o phimantec-c11-appliance-v0.3.8-monitor-offline-vendor.zip
cd phimantec-c11-appliance-v0.3.8-monitor-offline-vendor
```

## Lokal prüfen

```bash
sha256sum -c MANIFEST.sha256
scripts/check_release_context.sh
scripts/verify_docker_static.sh
PHIMANTEC_VERIFY_JOBS=1 scripts/verify_release.sh
```

## Wechsel von einer v0.2-VM-/Lab-Installation (WICHTIG)

```bash
# v0.2/v0.3-VM betreiben Host-systemd-Dienste: phimantec-proxyd (3128) und
# suricata-monitor (5050). Beide vor dem Docker-Deploy stoppen+deaktivieren:
scripts/phimantec_stop_legacy_host_services.sh

# Kontrolle:
sudo ss -ltnp | grep -E ':(3128|5050)\b' || echo "3128/5050 sind frei"
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

Oder kompakt (stoppt Altdienste optional automatisch):

```bash
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
```

## Externe Erreichbarkeit + CA

```bash
HOST_IP="$(hostname -I | awk '{print $1}')"
curl -sS -m5 "http://${HOST_IP}:5050/" -o /dev/null -w 'monitor extern: %{http_code}\n'
curl -k -sS -m10 -x "http://${HOST_IP}:3128" -I https://example.org/ -o /dev/null -w 'proxy: %{http_code}\n'
ls -lah "$HOME/phimantec-bump-ca.crt"
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject -issuer
# Erwartet nach erfolgreichem v0.3.8-Deploy:
#   subject=CN = PHIMANTEC C11 Docker Lab Root CA
```

## Logs

```bash
scripts/phimantec_compose.sh logs suricata-node
scripts/phimantec_compose.sh logs proxy
```
