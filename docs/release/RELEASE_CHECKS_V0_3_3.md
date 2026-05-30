# PHIMANTEC C11 Appliance v0.3.3 — Release Checks

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
```

Erwartet:

```text
keine festen container_name Werte
keine Endlos-Reparaturschleifen
keine automatische restart policy
Proxy auf 127.0.0.1:33128
Monitor auf 127.0.0.1:5050
Build-Netzwerk default host
APT fail-fast
```

## Zusaetzliches Gate v0.3.3

- Feste Docker-`container_name` Werte sind verboten.
- Optionaler externer Proxy-Smoke akzeptiert kein `405 CONNECT Required` als Erfolg.
- Legacy-Container mit altem festen Namen werden nur explizit entfernt.
