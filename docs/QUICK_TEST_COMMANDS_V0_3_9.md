# Quick Test Commands v0.3.9

```bash
cd "$HOME"
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -rf phimantec-c11-appliance-v0.3.12-monitor-offline-vendor
unzip -q -o phimantec-v0.3.9-release.zip
cd phimantec-c11-appliance-v0.3.12-monitor-offline-vendor

sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env

PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
scripts/phimantec_compose.sh ps
scripts/phimantec_compose.sh ingress status
```

## CA pruefen

```bash
ls -lah "$HOME/phimantec-bump-ca.crt"
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject -issuer -fingerprint -sha256
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -text | grep -E 'CA:TRUE|Certificate Sign|CRL Sign'
cat "$HOME/phimantec-bump-ca.crt.sha256-fingerprint.txt"
```

## Suricata pruefen

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node suricata --build-info | grep 'This is Suricata version'
scripts/phimantec_compose.sh compose exec -T suricata-node command -v suricata-update
scripts/phimantec_compose.sh compose exec -T suricata-node suricata -T -c /etc/suricata/suricata.yaml -S /etc/suricata/rules/phimantec-local.rules -l /var/log/suricata -k none
```

## Windows-Client

```powershell
Test-NetConnection 192.168.0.232 -Port 5050
Test-NetConnection 192.168.0.232 -Port 3128
```

Firefox:

```text
HTTP-Proxy: 192.168.0.232
Port:       3128
Kein Proxy fuer: localhost, 127.0.0.1, ::1, 192.168.0.232
```

Danach die Datei `~/phimantec-bump-ca.crt` erneut in Firefox als
Zertifizierungsstelle importieren und die Website-Vertrauensoption aktivieren.
Wenn zuvor alte PHIMANTEC-CAs importiert wurden, diese zuerst aus Firefox
entfernen.
