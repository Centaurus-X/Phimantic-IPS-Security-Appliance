# Diagnose v0.3.14a / FlowBus-Stabilitaetsfix

## Kurzdiagnose

v0.3.14 war sicher, aber im Docker-H1.1-Only-Betrieb fuer normale Seiten nicht deterministisch nutzbar. Der Proxy blockierte harmlose Requests fail-closed mit `Inspection unavailable`, obwohl Suricata, Lane und FlowBus liefen.

Der Fehler war kein Suricata-Bypass-Problem und kein EICAR-Leak. Die EICAR-Matrix blieb sicher. Das Problem lag in der Timing- und FlowBus-Stabilitaet der Allow-Seite.

## Beweisbare Ursachen

### 1. Allow wurde im inspectord bis zur vollen Decision-Deadline gehalten

Der inspectord blockte Alerts sofort, merkte sich `SEEN_NO_ALERT` aber nur intern. Ein Allow wurde erst bei Ablauf der kompletten `verdict.timeout_ms` zurueckgegeben. Damit galt praktisch:

```text
Allow-Latenz == PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS
Block-Latenz == sofort nach Suricata-Alert
```

Bei identischen Proxy- und inspectord-Timeouts entstand ein totes Rennen: inspectord schreibt Allow ungefaehr bei 2500 ms, der Proxy bricht seinen FlowBus-Read ebenfalls bei 2500 ms ab.

### 2. FlowBus-I/O-Timeout und Verdict-Deadline waren im Parser gekoppelt

`flowbus.timeout_ms` und `verdict.timeout_ms` wurden bisher auf dasselbe interne Feld gemappt. Dadurch konnte die fachliche Suricata-Decision-Deadline nicht sauber vom Socket-/Frame-Read-Timeout getrennt werden.

### 3. Keepalive/Pooling war fuer die finale stabile Basis zu riskant

Persistente FlowBus-Verbindungen konnten durch Idle-Close oder stale FDs in einen `FlowBus verdict unavailable` laufen. Das darf niemals zu Allow fuehren, fuehrt aber fail-closed zu 503. Fuer die finale stabile Basis ist deshalb pro Verdict wieder der deterministische Ablauf aktiv:

```text
connect -> verdict request -> full response read -> close
```

## Korrektur

v0.3.14a trennt die Zeitfenster und macht Allows frueher final, ohne die Security-Kette umzubauen:

```env
PHIMANTEC_FLOWBUS_KEEPALIVE=0
PHIMANTEC_FLOWBUS_POOL_SIZE=1
PHIMANTEC_FLOWBUS_TIMEOUT_MS=3000
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=900
PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS=150
PHIMANTEC_INSPECTION_FAIL_POLICY=closed
```

Der inspectord kehrt bei `SEEN_NO_ALERT` nach einer kurzen Stabilitaets-Karenz zurueck. Alerts gewinnen weiterhin sofort. `NOT_SEEN`, FlowBus-Fehler und EVE-Unavailable bleiben fail-closed.

## Security-Invariante

Unveraendert gilt:

```text
Suricata bleibt der einzige fachliche Entscheider.
Kein finales Suricata-Allow -> kein Forwarding.
Finaler Suricata-Block -> deterministisch blockieren.
Pending / Timeout / FlowBus-Error / EVE-Unavailable -> kein stilles Allow.
Keine Domain-, CDN-, YouTube-, Media- oder EICAR-Heuristik im Proxy.
```

## Healthcheck- und Smoke-Korrektur

Der Docker-Healthcheck prueft jetzt nicht nur offene TCP-Ports, sondern einen echten FlowBus-Heartbeat mit HMAC. Damit wird ein Token-/HMAC-Mismatch oder ein nicht antwortender inspectord vor `healthy` sichtbar.

Der Smoke-Test trennt HTTP 503 von CA-/TLS-Fehlern. Bei 503 wird eine FlowBus-/inspectord-Diagnose ausgegeben statt faelschlich `Proxy-CA-Kette lokal nicht vertrauenswuerdig` zu melden.

## Erwartung nach Patch

```text
Harmlose Seiten bekommen ein finales Suricata-Allow innerhalb des Proxy-Budgets.
EICAR und Suricata-Alerts bleiben blockiert.
FlowBus unavailable tritt nicht dauerhaft bei laufendem inspectord auf.
HTTP/1.1-only, Alt-Svc-/Upgrade-Strip und segmentierte Inspection bleiben aktiv.
```
