# PHIMANTEC C11 Appliance v0.3.4 — Release Checks

## Lokale Gates

```text
Bash-Syntaxcheck
Python py_compile
SuricataMonitor Config-Manager Unit-Smoke
JavaScript-Syntaxcheck, falls node vorhanden
Docker-/Compose Static Sanity
CMake Configure
C Build
CTest
Third-Party No-Source-Smoke
ZIP-Integritaetstest
MANIFEST.sha256 Pruefung aus frischem ZIP
```

## Runtime-Gate auf Docker-Host

```bash
scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
```

Erwartet:

```text
keine festen container_name Werte
keine Endlos-Reparaturschleifen
keine automatische restart policy
Proxy extern published auf 3128
Monitor extern published auf 5050
Lane/FlowBus lokal gebunden
CA exportiert nach ~/phimantec-bump-ca.crt
Build-Netzwerk default host
APT fail-fast
```

## Zusätzliches Gate v0.3.4

- Proxy-Default darf nicht auf `127.0.0.1` binden.
- Monitor-Default darf nicht auf `127.0.0.1` binden.
- `scripts/preflight_docker.sh` blockiert Loopback-Bindings für Proxy/Monitor, außer `PHIMANTEC_ALLOW_LOCAL_BIND=1` ist bewusst gesetzt.
- `scripts/smoke_docker.sh` prüft `docker compose port` für Proxy und Monitor.
- `scripts/export_docker_ca.sh` kopiert und validiert die Docker-CA nach `$HOME/phimantec-bump-ca.crt`.
- AWK-Quote-Warnungen dürfen nicht mehr auftreten.
