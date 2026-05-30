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
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc 'command -v suricata-update'
scripts/phimantec_compose.sh compose exec -T suricata-node suricata --build-info | grep "This is Suricata version"
```

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
pid="$(pidof suricata)"
tr "\0" " " < "/proc/${pid}/cmdline"
echo
grep -nA16 "^af-packet:" /etc/suricata/suricata.yaml | sed -n "1,40p"
'
```

```bash
# Nach EICAR-Test im Browser: Erwartung = Block-Page, nicht der EICAR-String.
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
python3 - <<'"'"'PY'"'"'
import json
from collections import Counter
path = "/var/log/suricata/eve.json"
counts = Counter()
alerts = []
with open(path, "r", encoding="utf-8", errors="replace") as handle:
    for line in handle:
        try:
            obj = json.loads(line)
        except Exception:
            continue
        counts[obj.get("event_type", "?")] += 1
        if obj.get("event_type") == "alert":
            alerts.append(obj)
print("event_types=", dict(counts))
print("alerts=", len(alerts))
for item in alerts[-10:]:
    alert = item.get("alert") or {}
    payload = (item.get("payload_printable") or "")[:140].replace("\n", " ")
    print(item.get("timestamp"), alert.get("signature_id"), alert.get("signature"))
    print("  payload_printable:", payload)
PY
'

scripts/phimantec_compose.sh compose logs suricata-node \
  | grep -E 'verdict flow=.*action=block.*suricata-alert' \
  | tail -10
```

```powershell
Test-NetConnection 192.168.0.232 -Port 5050
Test-NetConnection 192.168.0.232 -Port 3128
```
