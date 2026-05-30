## v0.3.9 — Suricata-8-Paketkonflikt und CA-Gates

| Requirement | Umsetzung | Gate |
| --- | --- | --- |
| Suricata 8+ ohne dpkg-Dateikonflikt | OISF-`suricata` wird ohne paralleles Ubuntu-`suricata-update` installiert; `suricata-update` nur Fallback, falls Binary fehlt | `verify_docker_static.sh`, Zielhost-Docker-Build |
| Browserfaehige CA auch im VM-Installer | Alt-CA-Validierung auf `CA:TRUE`, `keyCertSign`, `cRLSign`, Ablaufzeit und Key/Cert-Match | `install_proxy_vm_ubuntu.sh`, `verify_release.sh` |
| Leaf-Zertifikate fuer DNS/IP pruefbar | neuer CTest `cert_generation` validiert Chain, SAN und private-key match | CMake/CTest |

# Requirements Trace

| Anforderung | Umsetzung | Test/Prüfung |
|---|---|---|
| Proxy-Host und Suricata-Host getrennt | separate Binaries und Configs | Verzeichnisstruktur, systemd Units |
| HTTP CONNECT Proxy | `phim_parse_connect_request()` | `test_connect_parser` |
| TLS-Terminierung | OpenSSL Server Context pro CONNECT Ziel | Build + Lab-Curl |
| Upstream TLS | OpenSSL Client Context mit SNI | `phim_proxy_out_connect_tls()` |
| H1 nativ | eigener Parser | `test_h1_parser` |
| Verdict-API in-process | `phim_verdict_check()` | `test_verdict_timeout` |
| FlowBus TCP + HMAC | `PHFB` Frame, HMAC-SHA256 | `test_flowbus_hmac` |
| Replay-Schutz Grundmodell | monotone Sequence je Verbindung im Inspectord | Codeprüfung `inspectord_main.c` |
| Lane unverschlüsselt | HTTP Envelope zu `:18080` | `phim_lane_send()` |
| EVE-Korrelation | Flow-ID Suche in `eve.json` | `test_eve_correlation` |
| Blockseite | HTTP 403 mit Flow-ID/Grund | `phim_blockpage_build()` |
| H3 adaptiv | Response Header `Alt-Svc` entfernen | `test_h1_parser` |
| Keine Envoy/Squid/mTLS Pflicht | keine Abhängigkeit enthalten | CMake/Source Review |
| Kein SSLSplit-Code | Clean-Room-Verzeichnis, keine Imports | `docs/CLEAN_ROOM.md` |

## v0.3.8 — Host-network und containerverwalteter Ingress

| Anforderung | Umsetzung | Dateien |
|---|---|---|
| Proxy muss von extern erreichbar sein | `proxy` laeuft mit `network_mode: host`; Entrypoint bindet aus `.env` direkt auf `0.0.0.0:3128` bzw. konfigurierten Host-Port | `docker-compose.yml`, `scripts/docker_proxy_entrypoint.sh` |
| Webmanagement muss von extern erreichbar sein | `suricata-node` laeuft mit `network_mode: host`; Entrypoint patcht `suricata-monitor.yaml` auf `0.0.0.0:5050` bzw. konfigurierten Host-Port | `docker-compose.yml`, `scripts/docker_suricata_node_entrypoint.sh`, `config/docker/suricata-monitor.yaml` |
| Kein Abhaengen an Docker-DNAT/docker-proxy | Keine `ports:`-Mappings fuer Proxy/Suricata-Node; echte Listener werden per `ss` geprueft | `docker-compose.yml`, `scripts/smoke_docker.sh`, `scripts/verify_docker_static.sh` |
| Ingress-Fix direkt im Container | `suricata-node` installiert `nftables`/`iptables`, startet mit `NET_ADMIN` und setzt Accept-Regeln beim Containerstart | `docker/suricata-node/Dockerfile`, `scripts/phimantec_ingress_rules.sh`, `scripts/docker_suricata_node_entrypoint.sh` |
| Reboot-stabile Appliance | Hauptservices verwenden `restart: unless-stopped`; Container setzt Ingress-Regeln nach Neustart erneut | `docker-compose.yml`, `.env.example` |
| Zertifikat unter `$HOME` verfuegbar | Export aus Proxy-Container nach `$HOME/phimantec-bump-ca.crt`; X.509-Validierung im Smoke | `scripts/export_docker_ca.sh`, `scripts/smoke_docker.sh` |



## v0.3.4 — Externe Erreichbarkeit und CA-Export

| Anforderung | Umsetzung | Dateien |
|---|---|---|
| Proxy muss von extern erreichbar sein | Docker-Default `PHIMANTEC_PROXY_PORT=3128`; Compose-Publish `${PHIMANTEC_PROXY_PORT:-3128}:3128`; Preflight blockiert Loopback-Bindings | `.env.example`, `docker-compose.yml`, `scripts/preflight_docker.sh` |
| Webmanagement muss von extern erreichbar sein | Docker-Default `PHIMANTEC_MONITOR_PORT=5050`; Compose-Publish `${PHIMANTEC_MONITOR_PORT:-5050}:5050`; Smoke prüft `docker compose port` | `.env.example`, `docker-compose.yml`, `scripts/smoke_docker.sh` |
| Zertifikat soll schnell unter `$HOME` verfügbar sein | Export aus Proxy-Container nach `$HOME/phimantec-bump-ca.crt`; X.509-Validierung per OpenSSL, falls verfügbar | `scripts/export_docker_ca.sh`, `scripts/phimantec_compose.sh`, `scripts/smoke_docker.sh` |
| v0.3.2-Proxyverhalten nicht unnötig ändern | Proxy-Port 3128 wiederhergestellt; C11-Proxy-/Lane-/FlowBus-Code unverändert | `docker-compose.yml`, `config/docker/phimantec-proxyd.conf` |
| Lokale Abhängigkeiten und Webmanagement bleiben integriert | SuricataMonitor und Docker-Lokalpakete bleiben erhalten; nur fehlerhafte Publish-Defaults korrigiert | `docker/suricata-node/Dockerfile`, `suricata-monitor/`, `docker-compose.yml` |
| Regression gegen Loopback-Publish verhindern | Static-Verify sucht externe Defaults; Preflight und Smoke enthalten Runtime-Gates | `scripts/verify_docker_static.sh`, `scripts/preflight_docker.sh`, `scripts/smoke_docker.sh` |

## v0.3.3 — SuricataMonitor, Offline-Bundle und Docker-Rollout

| Anforderung | Umsetzung | Dateien |
|---|---|---|
| SuricataMonitor nahtlos in Suricata-Instanz integrieren | Web-UI unter `suricata-monitor/`, systemd-Service, Docker-Suricata-Node mit Port 5050 | `suricata-monitor/`, `packaging/systemd/suricata-monitor.service`, `docker/suricata-node/Dockerfile` |
| Proxy-Funktion erhalten | Docker-/VM-Profile behalten Lane `18080` und FlowBus `18090`; Web-UI greift nur auf Suricata-Konfig/Rules/EVE zu | `config/docker/*.conf`, `config/suricata-monitor/phimantec-suricata-monitor.yaml` |
| Kritische Proxy-Einstellungen weiterhin moeglich, aber mit Hinweis | Schema-Felder enthalten `phimantec_warning`, Frontend rendert Hinweisboxen | `suricata-monitor/backend/services/config_schema.py`, `suricata-monitor/frontend/js/pages/settings.js` |
| Docker-Zugriff auf Web-UI | Compose exponiert `5050:5050` | `docker-compose.yml`, `.env.example` |
| Offline-Bundle klein halten | Trennung `runtime-libs/`, `offline-debs/`, `third_party/sources/` | `docs/OFFLINE_BUNDLE_LAYOUT.md`, `runtime-libs/`, `offline-debs/`, `third_party/` |
| Automatische Third-Party-Patch-Mechanik | Verify/Unpack/Patch/Build-Skripte und Patch-Series-Struktur | `scripts/vendor_verify.sh`, `scripts/vendor_unpack.sh`, `scripts/apply_third_party_patches.sh`, `scripts/build_third_party.sh` |
| Suricata Source vorerst nicht einchecken | `manifest.lock` dokumentiert `included_in_repository: false`; Build-Skript ueberspringt fehlenden Source | `third_party/manifest.lock`, `third_party/sources/README.md` |
| Release-Gates | CMake/CTest, Python/Bash/JS-Smokes, No-Source Vendor-Smoke | `scripts/verify_release.sh`, `docs/release/RELEASE_CHECKS_V0_3_3.md` |
| Docker-Build soll bei APT/DNS-Fehlern nicht minutenlang schleifen | Fail-Fast APT-Wrapper mit Timeout, optionalem Mirror und `PHIMANTEC_DOCKER_BUILD_NETWORK` | `docker/common/apt_install.sh`, `docker-compose.yml`, `scripts/verify_docker_static.sh` |
| Minimale Container-/systemd-Sicherheitsfeatures fuer Out-of-Box-Lab | systemd-Hardening reduziert, Compose `restart: "no"`, StartLimits statt Dauer-Reparatur | `packaging/systemd/*.service`, `suricata-monitor/systemd/suricata-monitor.service`, `docker-compose.yml` |
| Keine unendliche Selbstreparatur im Suricata-Container | Entrypoint nutzt `wait -n`, Hyperscan-Fallback hoechstens einmal und nur bei explizitem `hs` | `scripts/docker_suricata_node_entrypoint.sh`, `scripts/phimantec_suricata_yaml_patch.py` |
| Docker-Runtime ohne PyPI/venv-Zwang | SuricataMonitor nutzt Ubuntu-Pakete `python3-flask` und `python3-yaml` | `docker/suricata-node/Dockerfile`, `scripts/install_suricata_monitor_vm_ubuntu.sh` |
