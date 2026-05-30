# PHIMANTEC v0.3.18 — Quick Test Commands

## Basis-Rollout

```bash
cd "$HOME/phimantec-c11-appliance-v0.3.18-monitor-offline-vendor"
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

## Ruleset-Reload im Container prüfen

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
set -e
suricata -T -c /etc/suricata/suricata.yaml -v
suricatasc -c ruleset-reload-rules /var/run/suricata/suricata-command.socket \
  || suricatasc -c ruleset-reload-rules \
  || kill -USR2 "$(pidof suricata)"
'
```

## Webmanagement-Regeltest

```text
1. Webmanagement → Rulesets → Rule Files öffnen.
2. SID 900004 deaktivieren und speichern.
3. Der UI-Status muss Reload erforderlich anzeigen.
4. ruleset-reload-rules klicken.
5. Ergebnis muss Stage und Detailmeldung anzeigen.
6. SID 900004 wieder aktivieren und erneut reloaden.
```

Hinweis: Wenn SID 900002 aktiv bleibt, kann EICAR weiterhin über Body-Pattern blockieren. SID 900004 ist die Path-Regel, SID 900002 die Body-Regel.

## ET/Open Update

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
set -e
suricata-update
suricata -T -c /etc/suricata/suricata.yaml -v
suricatasc -c ruleset-reload-rules /var/run/suricata/suricata-command.socket \
  || kill -USR2 "$(pidof suricata)"
ls -lh /var/lib/suricata/rules
grep -R "PHIMANTEC" /var/lib/suricata/rules/phimantec-local.rules | head || true
'
```

## Fehlerhafte Regel testen

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
cat >> /var/lib/suricata/rules/external/custom-local.rules <<RULE
alert http any any -> any any (msg:"BROKEN TEST"; sid:1234567; rev:1
RULE
suricata -T -c /etc/suricata/suricata.yaml -v || true
'
```

Erwartung: `suricata -T` schlägt fehl, der UI-Reload löst keinen Live-Reload aus, und die alte Engine bleibt aktiv.

## Logprüfung

```bash
scripts/phimantec_compose.sh compose logs --tail=1000 proxy suricata-node \
  | grep -E "Inspection unavailable|inspection-|flowbus-|queue-|suricata-alert|timeout|verdict|ruleset|reload" || true
```
