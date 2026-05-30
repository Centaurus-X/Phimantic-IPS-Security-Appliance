# Test- und Releaseprüfung

## Build Gates

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Bestandene Tests im Paketlauf

| Test | Zweck |
|---|---|
| `connect_parser` | CONNECT Host/Port/Keepalive |
| `h1_parser` | H1 Request/Response und Alt-Svc Strip |
| `flowbus_hmac` | Frame Build/Parse und Tamper-Erkennung |
| `eve_correlation` | Flow-ID Alert-Korrelation aus EVE-Zeile |
| `verdict_timeout` | Fail-Closed bei FlowBus-Ausfall |

## Manuelle Smoke Tests

```bash
scripts/gen_ca.sh
scripts/build.sh
scripts/run_suricata_node_lab.sh
scripts/run_proxy_lab.sh
scripts/smoke_curl.sh
```

## Automatisch nachgewiesene Prüfung

Die detaillierte Ausgabe des letzten Erzeugungslaufs steht in `docs/TEST_RESULTS_GENERATED.md`. Geprüft wurden Debug, ASAN/UBSAN, RelWithDebInfo und ein lokaler End-to-End-Smoke-Test mit HTTP 200 für Allow und HTTP 403 für Block.

## Release-Einschätzung

`v0.1.0-lab` ist für Lab- und Architekturvalidierung geeignet. Für einen produktiven Release fehlen noch Streaming-Body-Inspection, H2, Produktions-Cert-Cache, persistente Lane-Verbindungen mit Backpressure und ein skalierter FlowBus-Multiplexer.


## Suricata 8 Rule Validation

Auf der Suricata-VM kann die Regel-/Service-Reparatur idempotent ausgefuehrt werden:

```bash
cd /home/philo/phimantec-c11-appliance
PROXY_IP=192.168.0.232 scripts/repair_suricata_rules_vm.sh
```

Der Script fuehrt `suricata -T` vor dem Service-Restart aus und startet `suricata-phimantec`, `phimantec-lane-sink` und `phimantec-inspectord` erst nach erfolgreichem Regeltest.
