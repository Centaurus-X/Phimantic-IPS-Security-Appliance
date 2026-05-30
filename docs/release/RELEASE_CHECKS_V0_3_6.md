# PHIMANTEC C11 Appliance v0.3.8 — Release Checks

## Lokale Gates

```text
Bash-Syntaxcheck (inkl. neuem phimantec_stop_legacy_host_services.sh)
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
# Bei Wechsel von v0.2 zuerst:
scripts/phimantec_stop_legacy_host_services.sh
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
CA exportiert nach ~/phimantec-bump-ca.crt (CN = PHIMANTEC C11 Docker Lab Root CA)
Build-Netzwerk default host
APT fail-fast
```

## Zusätzliches Gate v0.3.8

- Preflight erkennt aktive v0.2-Host-systemd-Dienste und bricht mit klarer
  Migrationsanweisung ab (Default), bzw. stoppt sie bei
  `PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1`.
- `check_port_free()` nennt den belegenden Prozess (`ss -ltnp`) in der Meldung.
- `scripts/phimantec_stop_legacy_host_services.sh` ist idempotent und ohne
  systemd ein No-Op.
- Bestehende externe Publish-/CA-Export-Gates aus v0.3.4 bleiben unveraendert
  gueltig.
