# PHIMANTEC v0.3.17 — H1.1-only Seitenladung ohne Security-Absenkung

v0.3.17 behaelt die H1.1-only-Architektur bei und optimiert nur die Stabilitaet des Ladepfads.

## H1.1-only bleibt unveraendert

```text
Downstream ALPN: http/1.1
Upstream ALPN:   http/1.1
Alt-Svc:         entfernt
Alt-Used:        entfernt
Upgrade:         entfernt
HTTP2-Settings:  entfernt
Preconnect:      tolerant geschlossen, aber ohne ungeprueftes Forwarding
```

## Warum Seiten vorher langsam wurden

Komplexe Seiten erzeugen viele parallele Requests und grosse Response-Objekte. Mit 64-KB-Segmenten bedeutete das:

```text
Response 1 MiB -> ca. 16 segmentierte Verdicts
Response 5 MiB -> ca. 80 segmentierte Verdicts
jeder Allow-Pfad -> Lane + Suricata-EVE + FlowBus + Grace
```

Das ist sicher, aber fuer Alltagssurfen zu schwerfaellig.

## v0.3.17 Hot-Path

```text
1. Proxy liest H1.1 Request/Response.
2. Proxy sendet entschluesselten Sample an die Lane.
3. Suricata sieht genau diesen Lane-POST.
4. Proxy fragt metadata-only ueber FlowBus nach dem finalen Suricata-Verdict.
5. Nur finales allow wird forwarded.
6. block/pending/error/unavailable bleiben fail-closed.
```

## Default-Tuning

```env
PHIMANTEC_FLOWBUS_KEEPALIVE=0
PHIMANTEC_FLOWBUS_POOL_SIZE=4
PHIMANTEC_FLOWBUS_TIMEOUT_MS=3000
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=900
PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS=100
PHIMANTEC_SAMPLE_TARGET_BYTES=524288
PHIMANTEC_SAMPLE_MAX_BYTES=1048576
```

Diese Werte priorisieren weiter Sicherheit vor Maximaldurchsatz. Bei sehr schwachen Hosts kann `PHIMANTEC_FLOWBUS_POOL_SIZE=2` gesetzt werden. Bei schnellen Hosts kann spaeter konservativ auf 6 oder 8 erhoeht werden, ohne Keepalive zu aktivieren.

## Tests

```bash
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
curl -k -x http://127.0.0.1:3128 -I https://www.youtube.com/ \
  | grep -iE 'alt-svc|upgrade|http2-settings' || true
```

Erwartung:

```text
Smoke lokal: PASS
EICAR: blockiert / kein Leak
Alt-Svc/Upgrade/HTTP2-Settings: keine Ausgabe
Normale Seiten: kein pauschales Inspection unavailable
```
