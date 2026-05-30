# PHIMANTEC v0.3.18 — Deployment Freeze / offene Punkte

## Finalisierte Punkte

```text
FlowBus-Keepalive bleibt default aus.
FlowBus-Pool-Cap ist 32, Runtime-Default 16.
Lane-Pool-Cap ist 32, Runtime-Default 16.
Inspection Gate mit Queue und Inflight-Limits ist aktiv.
No-Body-/HEAD-/204-/304-Fälle erzwingen keine Body-Inspection.
TLS-CTX-Cache erzeugt Zertifikate nicht mehr unter globalem Cache-Lock.
fail_policy bleibt closed.
```

## Bewusst nicht umgesetzt

```text
kein fail-open
kein Domain-/CDN-/Amazon-/YouTube-Bypass
kein Content-Type-Fastpath
kein Media-Bypass
kein Late-Block-Streaming als Default
kein Proxy-Verdict-Cache
kein vollständiger Worker-Thread-Pool
```

## Optionales Produktiv-Tuning

Stärkere Hosts können testweise erhöhen:

```env
PHIMANTEC_FLOWBUS_POOL_SIZE=24
PHIMANTEC_LANE_POOL_SIZE=24
PHIMANTEC_INSPECTION_MAX_INFLIGHT=256
PHIMANTEC_INSPECTION_MAX_INFLIGHT_PER_HOST=48
PHIMANTEC_INSPECTION_QUEUE_MAX=2048
PHIMANTEC_INSPECTION_QUEUE_WAIT_MS=5000
PHIMANTEC_PROXY_VERDICT_TIMEOUT_UNDER_LOAD_MS=10000
```

Erst nach EICAR-Matrix und Smoke-Test produktiv übernehmen.
