# PHIMANTEC v0.3.17 — H1.1-only Page Loading und Multi-Stream Taktung

v0.3.17 bleibt bewusst H1.1-only:

```text
Downstream ALPN: http/1.1
Upstream ALPN:   http/1.1
Alt-Svc:         entfernt
Upgrade:         entfernt
HTTP2-Settings:  entfernt
```

Die Optimierung erfolgt nicht durch HTTP/2-Reaktivierung und nicht durch ungeprüftes Streaming, sondern durch bessere Parallelisierung der bestehenden H1.1-Inspection-Pipeline.

## Strict-Gate-Modell

```text
Upstream lesen
→ Sample/Request an Lane
→ Suricata/EVE/inspectord
→ FlowBus final verdict
→ erst dann zum Browser forwarden
```

Kein Byte wird aufgrund eines Timeouts oder einer Proxy-Heuristik erlaubt.

## Neue Default-Kapazität

```env
PHIMANTEC_LANE_POOL_SIZE=16
PHIMANTEC_FLOWBUS_POOL_SIZE=16
PHIMANTEC_INSPECTION_MAX_INFLIGHT=128
PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_HOST=32
PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_CLIENT=96
```

Diese Werte verbessern Multi-Tab- und Multi-Subresource-Laden, ohne Suricata als Entscheider zu umgehen.

## Diagnose

Bei Restfehlern zuerst prüfen:

```bash
scripts/phimantec_compose.sh compose logs --tail=1000 proxy suricata-node \
  | grep -E "Inspection unavailable|inspection-|flowbus-|queue-|suricata-alert|timeout|verdict" || true
```
