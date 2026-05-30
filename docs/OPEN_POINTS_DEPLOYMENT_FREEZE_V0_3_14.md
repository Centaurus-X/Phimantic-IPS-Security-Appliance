# Deployment Freeze v0.3.14a — FlowBus stabil, H1.1-only unveraendert

## Geschlossene Punkte

```text
[x] FlowBus-Keepalive standardmaessig deaktiviert
[x] Pro Verdict frische FlowBus-Verbindung als stabile Default-Basis
[x] flowbus.timeout_ms und verdict.timeout_ms im Parser getrennt
[x] inspectord Allow nicht mehr bis zur vollen Deadline gehalten
[x] no-alert Grace konfigurierbar
[x] Proxy-Budget groesser als Suricata-Decision-Budget
[x] FlowBus-HMAC-Probe im Healthcheck
[x] Smoke-Test trennt CA/TLS, HTTP 503, HTTP 403 und HTTP 200
[x] fail_policy wird durch Entrypoints hart auf closed gesetzt
[x] H1.1-only, Alt-Svc-/Upgrade-Strip und Preconnect-Toleranz bleiben aktiv
```

## Finale stabile Defaults

```env
PHIMANTEC_FLOWBUS_POOL_SIZE=1
PHIMANTEC_FLOWBUS_TIMEOUT_MS=3000
PHIMANTEC_FLOWBUS_KEEPALIVE=0
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=900
PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS=150
PHIMANTEC_INSPECTION_FAIL_POLICY=closed
```

## Weiterhin offene Zielhost-Gates

Diese Gates muessen auf dem echten Docker-Zielhost laufen, weil sie einen Docker-Daemon, Host-Netzwerk, Browser/CA-Trust und echte externe Seiten benoetigen:

```text
[ ] PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
[ ] scripts/phimantec_smoke.sh
[ ] PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
[ ] Browser-Privatfenster-Test mit importierter PHIMANTEC-CA
[ ] YouTube/Google/CDN-Header- und Ladeverhalten pruefen
```

## Nicht verhandelbare Security-Regeln

```text
Kein finales Suricata-Allow -> kein Forwarding.
Finaler Suricata-Block -> blockieren.
Pending / Timeout / FlowBus-Error / EVE-Unavailable -> nicht forwarden.
Keine lokalen Malware-Heuristiken im Proxy.
Keine Domain- oder Media-Bypaesse.
```

## Diagnose-Kommandos bei Runtime-Problemen

```bash
scripts/phimantec_compose.sh compose ps
scripts/phimantec_compose.sh compose logs --tail=200 proxy suricata-node
scripts/phimantec_compose.sh compose exec -T suricata-node /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-suricata-node.conf -t 1000
scripts/phimantec_compose.sh compose exec -T proxy /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-proxyd.conf -t 1000
```
