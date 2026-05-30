# Deployment Freeze v0.3.17

## Geschlossen

```text
[x] TLS WANT_READ/WANT_WRITE bounded retry
[x] EAGAIN/EWOULDBLOCK bleibt echter Timeout
[x] FlowBus keepalive weiter deaktiviert
[x] Pro Verdict frische FlowBus-Verbindung
[x] Vier parallele FlowBus-Client-Slots als Default
[x] FlowBus metadata-only; Body bleibt auf Suricata-Lane
[x] Response-Segmentfenster 512 KiB
[x] Lane-Send-Fehler fail-closed und klar geloggt
[x] H1.1-only / Alt-Svc-/Upgrade-Strip unveraendert
[x] Smoke-Test klassifiziert 503 getrennt von CA/TLS
```

## Bewusst offen fuer spaetere Performance-Linie

```text
[ ] Persistentes FlowBus-Keepalive wieder aktivieren, aber nur mit stale-FD-Erkennung und Integrationstest
[ ] Differenzierte TLS-Read-Statuswerte statt generischem -1
[ ] Nicht-blockierender/pipelined Verdict-Multiplexer fuer grosse Downloads
[ ] Produktions-WSGI fuer SuricataMonitor statt Flask-Dev-Server
```

Diese offenen Punkte duerfen v0.3.17 nicht blockieren, weil die stabile Security-Chain und Alltagstauglichkeit jetzt Vorrang haben.
