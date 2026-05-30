# PHIMANTEC v0.3.14a — H1.1-Only, deterministische FlowBus-Inspection und stabile Seitenladung

## 1. Ziel

v0.3.14a stabilisiert die vorhandene v0.3.14-Architektur minimal-invasiv. H1.1-only, TLS-Bump, Lane, Suricata, EVE und FlowBus bleiben erhalten. Der Patch senkt keine Security-Regel ab.

```text
Browser -> Proxy TLS-Bump -> Plain HTTP/1.1 zur Lane -> Suricata -> EVE -> inspectord -> FlowBus -> Proxy Enforcement
```

## 2. H1.1-Only bleibt Pflicht

PHIMANTEC implementiert weiterhin keinen H2/H3-Stack. HTTP/1.1 ist der kontrollierte Inspection-Modus:

```env
PHIMANTEC_HTTP_MODE=h1_only
PHIMANTEC_DOWNSTREAM_ALPN=http/1.1
PHIMANTEC_UPSTREAM_ALPN=http/1.1
PHIMANTEC_STRIP_ALT_SVC=1
PHIMANTEC_STRIP_UPGRADE_HEADERS=1
```

Der Proxy entfernt Transport-/Upgrade-Hinweise wie Alt-Svc, Alt-Used, Upgrade und HTTP2-Settings, damit Browser nicht auf nicht inspizierte H2/H3/QUIC-Pfade ausweichen.

## 3. Preconnect und Idle-Tunnel

Speculative Browser-Verbindungen ohne ersten H1-Request bleiben tolerant behandelt:

```env
PHIMANTEC_PRECONNECT_GRACE_MS=250
PHIMANTEC_FIRST_REQUEST_TIMEOUT_MS=1500
PHIMANTEC_IDLE_TUNNEL_TIMEOUT_MS=30000
```

Wenn nach TLS-Bump kein H1-Request erscheint, wird die Verbindung sauber geschlossen. Es wird nichts ungeprueft forwarded.

## 4. FlowBus-Stabilitaet vor Performance

Der stabile Default ist pro Verdict eine frische FlowBus-Verbindung:

```env
PHIMANTEC_FLOWBUS_POOL_SIZE=1
PHIMANTEC_FLOWBUS_KEEPALIVE=0
PHIMANTEC_FLOWBUS_TIMEOUT_MS=3000
```

Ablauf:

```text
connect
write verdict request
read complete FlowBus response with HMAC
close
```

Keepalive/Pooling kann spaeter wieder optimiert werden, ist aber nicht die finale Default-Basis. Ein FlowBus-Fehler bleibt immer fail-closed.

## 5. Getrennte Timeouts

Die fachliche inspectord-Decision und der Proxy-FlowBus-Read duerfen nicht deckungsgleich sein:

```env
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS=900
PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS=150
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000
```

Bedeutung:

```text
Alert in Suricata-EVE        -> sofort Block
SEEN_NO_ALERT stabil         -> Allow nach kurzer Grace
NOT_SEEN bis Deadline        -> Pending, Proxy wartet weiter
FlowBus/EVE unavailable      -> Error, Proxy forwardet nicht
```

Damit endet ein sauberer Request normalerweise deutlich vor dem Proxy-Gesamtbudget.

## 6. Segmentierte Response-Inspection bleibt aktiv

Grosse Responses werden weiterhin segmentiert inspiziert. Jedes Segment muss ein finales Suricata-Allow bekommen, bevor es an den Client geschrieben wird.

```env
PHIMANTEC_SAMPLE_TARGET_BYTES=65536
PHIMANTEC_SAMPLE_MAX_BYTES=1048576
PHIMANTEC_STREAM_CHUNK_MIN_BYTES=32768
```

Cross-Segment-Overlap bleibt erhalten, damit einfache Pattern nicht durch Segmentgrenzen trivial abgeschnitten werden.

## 7. Was bewusst nicht gemacht wird

```text
kein fail-open
kein YouTube-/Google-/CDN-Bypass
kein Media-/Range-/Content-Type-Bypass
keine lokale EICAR-Erkennung im Proxy
kein ungeprueftes Forwarding bei Pending, Timeout oder unavailable
```

## 8. Erwartung

```text
Normale harmlose Seiten laden wieder.
Blockierte Inhalte bleiben blockiert.
HTTP 503 bedeutet echte Inspection-/FlowBus-Diagnose, kein CA-Fehlalarm.
Alt-Svc/Upgrade/HTTP2-Settings erscheinen nicht im Client-Response-Header.
```
