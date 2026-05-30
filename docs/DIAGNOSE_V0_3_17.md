# PHIMANTEC v0.3.17 Diagnose — Multi-Stream-Stabilität ohne Security-Absenkung

## Kurzbefund

v0.3.16 hat die harte FlowBus-Race-Condition und den TLS-WANT_READ/WANT_WRITE-Pfad bereits stabilisiert. Unter echten Browser-Lasten bleiben aber 503-Fälle bei Amazon, Wetter, YouTube und ähnlichen CDN-lastigen Seiten möglich.

Die beobachtete Ursache ist nicht ein fehlender Suricata-Block oder eine kaputte EICAR-Erkennung, sondern Lastspitzen in der synchronen Inspection-Pipeline:

```text
Browser Subrequests
→ Proxy Worker
→ Lane-Send
→ Suricata/EVE
→ inspectord
→ FlowBus Verdict
→ Proxy Enforcement
```

Wenn viele Subresources parallel eintreffen, können kleine Lane-/FlowBus-Pools, fehlende Backpressure und pauschale Timeouts einzelne Requests bis zum Verdict-Timeout stauen. Das korrekte fail-closed-Ergebnis war dann HTTP 503 `Inspection Unavailable`.

## Sicherheitsgrenze

Unverändert gilt:

```text
Kein finales Suricata-Allow → kein Forwarding.
Finaler Suricata-Alert      → blockieren.
Timeout/Unavailable         → fail-closed.
```

v0.3.17 enthält weiterhin keine Proxy-Malware-Heuristik, keinen Domain-Bypass, keinen Media-/Image-/Font-Bypass, kein fail-open und kein late-block streaming.

## Implementierte Korrekturen

### 1. Mehr parallele Prüfkapazität

```text
PHIM_VERDICT_FLOWBUS_CLIENTS = 32
PHIM_LANE_POOL_CLIENTS      = 32
PHIMANTEC_FLOWBUS_POOL_SIZE = 16
PHIMANTEC_LANE_POOL_SIZE    = 16
```

Die Runtime-Werte bleiben begrenzt. Werte über 32 werden im Config-Parser gekappt.

### 2. Bounded Inspection Gate

Neue Default-Backpressure:

```env
PHIMANTEC_INSPECTION_QUEUE_MAX=1024
PHIMANTEC_INSPECTION_QUEUE_WAIT_MS=3000
PHIMANTEC_INSPECTION_MAX_INFLIGHT=128
PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_HOST=32
PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_CLIENT=96
```

Wenn alle Slots belegt sind, wartet ein Request kontrolliert auf einen freien Inspection-Slot. Es wird nicht erlaubt, solange kein finales Suricata-Allow vorliegt. Bei Queue-Timeout bleibt das Ergebnis fail-closed, aber die Ursache wird präziser geloggt.

### 3. Zweistufiges Verdict-Budget

```env
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS=3000
PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS=8000
PHIMANTEC_FLOWBUS_CONNECT_TIMEOUT_MS=500
PHIMANTEC_FLOWBUS_READ_TIMEOUT_MS=3000
```

Normalpfade bleiben schnell. Unter Last erhält ein bereits eingereihter Request mehr Zeit, damit kurzzeitige Lastspitzen nicht als permanente Unavailable-Welle enden.

### 4. No-Body-/HEAD-Korrektur

Responses ohne Body werden nicht mehr in eine Body-Segment-Inspection gezwungen:

```text
HEAD
204 No Content
304 Not Modified
Content-Length: 0
```

Der Header-/Path-Request bleibt weiterhin Suricata-sichtbar. Das ist kein Bypass.

### 5. TLS-CTX-Cache entkoppelt

Downstream-Zertifikate werden bei Cache-Miss nicht mehr unter dem globalen Cache-Mutex erzeugt. Dadurch blockiert parallele Subdomain-Zertifikatserzeugung beim Cold-Start nicht mehr alle anderen TLS-Workers.

### 6. Diagnose verbessert

503-Antworten enthalten jetzt einen genaueren Grund über:

```text
X-Phimantec-Inspection-Reason
```

Beispiele:

```text
inspection-queue-full
inspection-queue-timeout
inspection-inflight-limit
lane-send-failed
flowbus-verdict-failed
suricata-inspection-unavailable
```

## Erwartete Wirkung

```text
weniger 503 bei Amazon/Wetter/YouTube
stabileres paralleles Laden mehrerer Seiten
weniger FlowBus-Slot-Stau
bessere Diagnose bei echten Restfehlern
EICAR bleibt blockiert
Security-Chain bleibt strikt geschlossen
```
