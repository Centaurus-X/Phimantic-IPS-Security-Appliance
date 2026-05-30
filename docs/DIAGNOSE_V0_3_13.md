# PHIMANTEC v0.3.13 — Tiefe Diagnose und Enforcement-Patch

## Ausgangslage

Die v0.3.12-Runtime zeigte im EICAR-Matrix-Test ein uneinheitliches Bild: einige Downloads wurden korrekt blockiert, andere erreichten den Client mit HTTP 200. Gleichzeitig standen in `eve.json` echte Suricata-Alerts fuer dieselben oder verwandte Flow-IDs. Das bedeutet: Die Suricata-Erkennung war nicht der Hauptfehler. Der Fehler lag in der Enforcement-Barriere zwischen Proxy, Lane, Suricata-EVE, inspectord und FlowBus.

Das auffaellige Muster war:

```text
verdict flow=<id> action=allow reason=suricata-no-alert
verdict flow=<id> action=block reason=suricata-alert
```

Damit wurde ein nicht-finales No-Alert-Ergebnis zu frueh als Allow ausgefuehrt. Der Proxy forwardete bereits, waehrend Suricata den Alert erst kurz danach in EVE sichtbar machte.

## Root Cause

Der alte Ablauf war:

```text
Proxy sendet Klartext-Sample an Lane
Suricata verarbeitet Lane-POST und schreibt EVE asynchron
Proxy fragt sofort FlowBus
inspectord sieht noch keinen Alert
inspectord antwortet allow/suricata-no-alert
Proxy forwardet Body an Client
Suricata-Alert erscheint spaeter im EVE-Log
```

Das ist eine klassische TOCTOU-Race-Condition. Besonders sichtbar wurde sie bei kleinen EICAR-Dateien und bei mehreren Requests auf persistenten Verbindungen.

## Sicherheitsanforderung

Der Patch folgt exakt diesem Enforcement-Asset:

```text
Suricata ist der einzige fachliche Entscheider.
Proxy fuehrt keine eigene Malware-Erkennung aus.
Kein finales Suricata-Allow -> kein Forwarding.
```

Der Proxy darf also nicht selbst EICAR, Malware, Dateinamen oder Payload-Pattern bewerten. Er darf aber erzwingen, dass ein Body nur nach finalem Suricata-Allow ausgeliefert wird.

## Patch-Entscheidungen

### 1. inspectord: no-alert nicht sofort finalisieren

`inspectord` nutzt nun einen dreistufigen EVE-Status:

```text
PHIM_EVE_FLOW_ALERT          -> Suricata-Alert gefunden
PHIM_EVE_FLOW_SEEN_NO_ALERT  -> Lane-Flow im EVE gesehen, aber kein Alert
PHIM_EVE_FLOW_NOT_SEEN       -> noch kein verwertbarer EVE-Fortschritt fuer Flow
PHIM_EVE_FLOW_UNAVAILABLE    -> EVE nicht lesbar / Status nicht verfuegbar
```

Ein Alert gewinnt immer gegen No-Alert. Eine spaetere HTTP-/Fileinfo-Zeile ohne Alert darf einen bereits gespeicherten Alert nicht ueberschreiben.

No-Alert wird erst nach Ablauf der inspectord-Decision-Barriere zu:

```text
allow reason=suricata-no-alert-final
```

Wenn der Flow bis dahin nicht im EVE-Pfad angekommen ist, antwortet inspectord:

```text
pending reason=suricata-pending-no-eve-flow
```

### 2. EVE-Cursor: partielle EOF-Zeilen nicht konsumieren

Suricata schreibt lange JSON-EVE-Zeilen appendend. Wenn inspectord genau waehrend dieses Schreibens liest, kann am EOF eine unvollstaendige Zeile stehen. v0.3.13 verschiebt den Cursor nicht mehr ueber eine solche Zeile. Die naechste Poll-Runde liest die vollstaendige Alert-Zeile erneut.

Das verhindert, dass ein Alert durch zu fruehen Cursor-Fortschritt verloren geht.

### 3. FlowBus: pending/error als eigene Zustände

FlowBus serialisiert und parst nun:

```text
allow
block
pending
error
```

Alles ausser `allow` und `block` ist nicht final. Der Proxy darf daraus kein Forwarding ableiten.

### 4. Proxy: fail-closed statt stilles Allow

`phim_verdict_check()` pollt bis zum Gesamt-Timeout. Final sind nur:

```text
PHIM_VERDICT_ALLOW
PHIM_VERDICT_BLOCK
```

Bei `pending`, `error`, Socket-Timeout, FlowBus-Ausfall oder Suricata-Unavailability wird nicht forwarded. Vor HTTP-Response-Headern antwortet der Proxy mit:

```text
HTTP/1.1 503 Inspection Unavailable
```

Nach bereits gesendeten Headern wird der Stream geschlossen. Eine verschachtelte 403-Response im 200-Body wird weiterhin vermieden.

### 5. Body-Forwarding: erst nach finalem Allow

Fixed-Length-, Chunked- und Until-Close-Responses werden bis zum konfigurierten Limit gepuffert und erst nach finalem Suricata-Allow weitergeleitet.

```text
Upstream Body lesen
Lane-POST an Suricata
FlowBus Verdict poll/warten
BLOCK     -> 403 Blockpage
ALLOW     -> Header + Body forwarden
PENDING   -> warten bis Timeout
ERROR     -> 503 / close
OVERSIZE  -> 503 / close
```

Damit ist ein `allow-then-block` fuer denselben inspizierten Flow nicht mehr als Client-Leak moeglich: Der Client bekommt Bodybytes erst nach dem finalen Allow.

### 6. Suricata-Testregeln

Die Content-Regel fuer den EICAR-Teststring bleibt die primaere Erkennung:

```text
sid:900002 PHIMANTEC EICAR TEST FILE
```

Ergaenzend gibt es eine Suricata-Regel auf den PHIMANTEC-Lane-Header `X-Phim-Path`, wenn dieser einen EICAR-Testpfad enthaelt:

```text
sid:900004 PHIMANTEC EICAR TEST PATH
```

Das ist weiterhin eine Suricata-Entscheidung, keine Proxy-Heuristik. Sie verbessert die Lab-/Matrix-Abdeckung fuer verpackte EICAR-Testdateien, deren Klartextmarker im HTTP-Body nicht sichtbar ist.

## Grenzen und bewusst nicht geaenderte Punkte

- Der Proxy erkennt weiterhin keine Malware selbst.
- Die fachliche Erkennungsleistung fuer echte Malware haengt von Suricata-Regeln, Suricata-Datei-/HTTP-Faehigkeiten und den konfigurierten Inspection-Limits ab.
- Wenn Suricata fuer einen inspizierten Flow nach der finalen Barriere explizit `suricata-no-alert-final` liefert, wird forwarded. Das entspricht dem Architekturprinzip.
- Wenn Suricata/FlowBus nicht verfuegbar ist oder keine finale Entscheidung entsteht, wird nicht forwarded.

## Regressionen

```text
CTest 9/9
- connect_parser
- h1_parser
- flowbus_hmac inklusive pending/error JSON
- flowbus_write_read
- eve_correlation inklusive partial EVE alert und alert-preserves-over-no-alert
- verdict_timeout fail-closed
- suricata_only_gate: Proxy blockt EICAR nicht lokal
- h1_chunked_decoder
- cert_generation

Python/static
- test_suricata_yaml_patch.py
- test_proxy_streaming_guard.py
- eicar_url_matrix_test.sh bereitgestellt fuer Zielhost-Runtime
```

## Erwarteter Zielhost-Nachweis

```text
verdict flow=<id> action=block reason=suricata-alert
```

oder bei nicht verfuegbarer finaler Inspection:

```text
HTTP/1.1 503 Inspection Unavailable
verdict/action pending oder error, aber kein Forwarding
```
