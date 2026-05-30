# PHIMANTEC C11 — Offline-Bundle Layout

## Trennung nach Groesse und Zweck

Das Repository trennt ab `v0.3.3` drei Stufen:

```text
runtime-libs/
  schlanke Runtime-Stufe mit fertigen .so Libraries und optionalen Binaries

offline-debs/
  nachvollziehbare Debian-Paketstufe fuer Rebuild/Review

third_party/sources/
  Original-Source-Archive nur fuer echte Air-Gap-Rebuilds
```

Diese Trennung verhindert, dass das normale Repository durch Builder-Container,
Ubuntu-Systembasis, Rulesets oder komplette Source-Trees unnoetig gross wird.

## Empfohlene Bundle-Varianten

### A. Admin Runtime Bundle

Fuer normalen Betrieb:

```text
Docker Runtime Images
docker-compose.yml
.env.example
config/docker/
config/suricata/
config/suricata-monitor/
runtime-libs/
SBOM
SHA256SUMS
Dokumentation
```

### B. Runtime + offline-debs

Fuer nachvollziehbare Rebuilds ohne komplettes Source-Vendoring:

```text
alles aus A
offline-debs/<arch>/*.deb
offline-debs/<arch>/SHA256SUMS
offline-debs/<arch>/packages.requested.txt
```

### C. Air-Gap Developer Rebuild

Fuer maximale Reproduzierbarkeit:

```text
alles aus A und B
third_party/sources/*.tar.gz
third_party/patches/**/series
scripts/vendor_verify.sh
scripts/vendor_unpack.sh
scripts/apply_third_party_patches.sh
scripts/build_third_party.sh
third_party/manifest.lock
```

## Was bleibt bewusst ausserhalb des schlanken Repos?

```text
Suricata Source oder fertige Suricata Binary-Pakete
Hyperscan Source-/Build-Tree
glibc / Ubuntu-Systemlibs
volle Builder-Images
umfangreiche Rulesets
```

Diese Bestandteile werden dokumentiert und optional ueber `offline-debs/`,
`runtime-libs/` oder `third_party/sources/` gesammelt.

## Grobe Groessenordnung

```text
PCRE2 + libhtp + jansson + yaml + pcap + libnet + cap-ng + zlib: 10-50 MB Runtime
NSS/nspr + libmagic:                                                   +30-100 MB
Ubuntu/glibc-Systembasis:                                             150-400+ MB
Suricata + Runtime-Abhaengigkeiten:                                    80-250 MB
Rulesets:                                                               5-150+ MB
Builder-Container:                                                       1-3 GB
```

## Befehle

Runtime Libraries sammeln:

```bash
scripts/collect_runtime_libs.sh /usr/bin/suricata build/phimantec-inspectord
```

Offline `.deb` Pakete sammeln:

```bash
scripts/collect_offline_debs.sh
```

Docker Runtime Bundle bauen:

```bash
scripts/build_docker_runtime_bundle.sh
```

Air-Gap Source-Stufe vorbereiten:

```bash
scripts/vendor_verify.sh --write
scripts/vendor_unpack.sh
scripts/apply_third_party_patches.sh
scripts/build_third_party.sh
```

## v0.3.3 Docker-Kontext

Normale Docker-Builds sollen klein bleiben. `.dockerignore` schliesst deshalb
Build-Verzeichnisse, Offline-DEBs, Runtime-Lib-Sammlungen, Third-Party-Builds
und Source-Archive aus dem Build-Kontext aus. Das verhindert, dass ein spaeteres
Air-Gap-Bundle versehentlich den Standard-Docker-Build auf hunderte MB oder GB
aufblaeht.

Das Runtime-Image nutzt Ubuntu-Pakete fuer Flask/PyYAML. Fuer echte Air-Gap-
Builds muessen diese Pakete in `offline-debs/` gesammelt oder als fertiges
Docker-Image-TAR ausgeliefert werden.

## v0.3.4 Runtime-Hinweis

Das Runtime-Bundle enthält weiterhin dieselben schlanken Docker-Bausteine wie v0.3.3. Geändert wurden nur die Docker-Publish-Defaults und der Host-Export der Proxy-CA:

```text
PHIMANTEC_PROXY_PORT=3128
PHIMANTEC_MONITOR_PORT=5050
PHIMANTEC_AUTO_EXPORT_CA=1
PHIMANTEC_CA_EXPORT_PATH=   # leer bedeutet ~/phimantec-bump-ca.crt
```

Für Air-Gap- oder Offline-Rollouts muss nach `docker compose up` zusätzlich der CA-Export geprüft werden:

```bash
scripts/phimantec_compose.sh export-ca
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject
```
