# Release Checks — v0.3.3-monitor-offline-vendor

Ausgefuehrte lokale Checks im Integrationsstand:

```text
Bash Syntax scripts/*.sh: OK
Python py_compile SuricataMonitor + Scripts: OK
SuricataMonitor Config-Manager Unit-Smoke: OK
JavaScript Syntax node --check: OK
Docker-/Compose Static Sanity: OK
CMake Configure: OK
C Build: OK
CTest: 7/7 OK
Third-Party No-Source-Smoke: OK
ZIP-Integritaetstest: OK
```

Docker-/Compose Static Sanity prueft:

```text
- docker/common/apt_install.sh existiert
- .dockerignore existiert
- Image-Tags sind PHIMANTEC_VERSION-basiert
- build.network ist ueber PHIMANTEC_DOCKER_BUILD_NETWORK steuerbar
- Docker-COPY-Quellpfade existieren
- Suricata-Entrypoint enthaelt keine while-true-Watch-Schleife
```

Nicht in dieser Umgebung ausgefuehrt:

```text
Docker Image Build und docker compose runtime test
Suricata -T gegen eine echte installierte Suricata 8.0.5 Instanz
Download/Einbindung echter third_party Source-Archive
Download/Einbindung echter offline-debs
```

Grund: Die Integrationsumgebung enthaelt keine Docker-Engine und soll das
Repository bewusst schlank halten. Die noetigen Skripte und Profile sind im
Repository enthalten.
