# FlowBus TCP + HMAC

## Transport

FlowBus bleibt ein persistenter TCP-Kanal mit HMAC-SHA256 pro Frame. Seit `v0.1.9-lab` ist die Poolgroesse konfigurierbar:

```toml
[flowbus]
pool_size = 4
```

Der interne Cap liegt bei 8 Verbindungen. Jede Verbindung hat eine eigene monotone Sequence. Inspectord verwirft Sequence-Rollback pro TCP-Verbindung.

## Frame Layout

```text
magic[4]        = "PHFB"
version[1]      = 1
flags[1]
type[2]         = big endian
seq[8]          = big endian
timestamp_ms[8] = big endian
nonce[16]       = random
payload_len[4]  = big endian
payload[N]      = JSON v1
hmac[32]        = HMAC-SHA256(header + payload)
```

## Typen

| Typ | Wert |
|---|---:|
| HELLO | 1 |
| HEARTBEAT | 2 |
| VERDICT_REQUEST | 10 |
| VERDICT_RESPONSE | 11 |
| ERROR | 99 |

## v0.1.9 Hot-Path-Optimierung

`phim_flowbus_write_frame()` baut kleine Frames bis 768 Byte auf dem Stack. Das Wire-Format, die HMAC-Berechnung und die Parser-Semantik bleiben unveraendert. Fuer groessere Frames bleibt ein Heap-Fallback erhalten.

## Sicherheitsmodell

- HMAC-SHA256 pro Frame
- Shared Secret aus Konfiguration
- Sequence je TCP-Verbindung
- Inspectord verwirft Sequence-Rollback
- Nonce pro Frame

Das ist bewusst kein mTLS-Pflichtpfad. In Produktion muss das Lane-/FlowBus-Netz segmentiert und gefiltert sein.
