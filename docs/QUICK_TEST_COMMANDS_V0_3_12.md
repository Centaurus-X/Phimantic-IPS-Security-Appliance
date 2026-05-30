# Quick Test Commands v0.3.12

```bash
cd "$HOME"
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -rf phimantec-c11-appliance-v0.3.12-monitor-offline-vendor
unzip -q -o phimantec-v0.3.12-release.zip
cd phimantec-c11-appliance-v0.3.12-monitor-offline-vendor
sha256sum -c MANIFEST.sha256
rm -f .env
cp .env.example .env
sed -i 's/^PHIMANTEC_REGENERATE_CA=.*/PHIMANTEC_REGENERATE_CA=1/' .env
PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
```

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
grep -nE "^[[:space:]]*(mpm-algo|spm-algo|runmode):" /etc/suricata/suricata.yaml
python3 - <<PY
import yaml
with open("/etc/suricata/suricata.yaml", "r", encoding="utf-8", errors="replace") as handle:
    data = yaml.safe_load(handle) or {}
http = data["app-layer"]["protocols"]["http"]["libhtp"]["default-config"]
for key in ("request-body-limit", "response-body-limit", "request-body-inspect-window"):
    print(key, http.get(key))
PY
'
```

```bash
curl -k -x http://127.0.0.1:3128 https://secure.eicar.org/eicar.com.txt -o /tmp/eicar-file.out -w '%{http_code}\n'
curl -k -x http://127.0.0.1:3128 https://www.eicar.org/download-anti-malware-testfile/ -o /tmp/eicar-html.out -w '%{http_code}\n'
scripts/phimantec_compose.sh compose logs suricata-node | grep -E 'verdict flow=.*action=block.*suricata-alert' | tail -10
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc 'grep -aE "900002|EICAR" /var/log/suricata/eve.json | tail -20'
```
