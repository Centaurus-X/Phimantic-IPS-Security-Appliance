# v0.3.17-final Hot-Path Stabilisierung

- TLS-Layer behandelt `SSL_ERROR_WANT_READ` / `SSL_ERROR_WANT_WRITE` und `EINTR` mit begrenztem Retry; `EAGAIN` / `EWOULDBLOCK` bleibt ein echtes Socket-Timeout.
- FlowBus bleibt fail-closed, HMAC-geschuetzt und keepalive=0, nutzt aber vier frische parallele Client-Slots als Default.
- FlowBus Verdict-Requests sind metadata-only; der entschluesselte Body wird nicht mehr doppelt ueber FlowBus transportiert, sondern nur ueber die Suricata-Lane.
- Response-Inspection-Segmentfenster steigt auf 512 KiB, Cross-Segment-Overlap bleibt aktiv.
- Lane-Send-Fehler werden sofort fail-closed behandelt und klar geloggt.
- Keine Security-Absenkung: kein fail-open, kein Domain-/Media-/CDN-Bypass, keine Proxy-Malware-Heuristik.

# v0.3.17-monitor-offline-vendor

- Minimal-invasiver H1.1-/Performance-/Stabilitaetspatch auf Basis von v0.3.13.
- Sicherheitsmodell unveraendert: Suricata bleibt einziger fachlicher Malware- und Pattern-Entscheider; der Proxy fuehrt keine lokale Malware-/EICAR-Heuristik und keinen Domain-/Media-/Content-Type-Bypass aus.
- H1.1-Only Mode als Default: `PHIMANTEC_HTTP_MODE=h1_only`, Downstream/Upstream ALPN jeweils `http/1.1`.
- Response-Header-Normalisierung entfernt `Alt-Svc`, `Alt-Used`, `HTTP2-Settings`, `Upgrade` und `Connection: Upgrade` / `Connection: HTTP2-Settings`.
- Preconnect-/Idle-Tunnel-Korrektur: erste H1-Request-Line bekommt ein kurzes konfigurierbares Fenster; reine speculative Preconnects werden sauber geschlossen und nicht als harte Fehlerflut geloggt.
- Segmentierte Response-Inspection: Fixed-Length-, Chunked- und Until-Close-Responses werden in erlaubnisgebundenen Segmenten inspiziert und erst nach finalem Suricata-Allow forwarded.
- Cross-Segment-Overlap reduziert Blindstellen an Segmentgrenzen, ohne Bytes doppelt an Clients zu schreiben.
- Chunked Request-Bodies werden jetzt gepuffert und ueber Suricata inspiziert; das bisherige MVP-Verhalten `chunked request header-only` ist geschlossen.
- `fail_policy` ist in Code, statischer Config und Docker-Entrypoints konsistent `closed`.
- Verdict-Fenster und Streaming-Parameter sind ueber `.env` sichtbar: `PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS`, `PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS`, `PHIMANTEC_SAMPLE_TARGET_BYTES`, `PHIMANTEC_SAMPLE_MAX_BYTES`.
- Neue/erweiterte Regressionen: Transport-Header-Strip, multi-suffix Flow-ID-Korrelation fuer segmentierte Response-IDs, CMake/CTest-Gates.

# v0.3.12-monitor-offline-vendor

- Minimal-invasiver Plain-Body-Inspection-Patch auf Basis von v0.3.11.
- Suricata bleibt der einzige Blocking-Entscheider. Proxy und inspectord fuehren weiterhin keine lokalen EICAR-/Malware-Heuristiken aus.
- `scripts/phimantec_suricata_yaml_patch.py` setzt die HTTP-App-Layer-Body-Tiefe fuer die PHIMANTEC-Lane auf 1 MiB: `request-body-limit`, `response-body-limit`, `request-body-minimal-inspect-size`, `request-body-inspect-window`, `response-body-minimal-inspect-size`, `response-body-inspect-window`.
- Dadurch werden entschluesselte Plain-Body-Samples bis zum Proxy-Sample-Fenster generisch von Suricata untersucht. EICAR in Text/HTML hinter Byte 4096 ist nicht mehr unsichtbar.
- Hyperscan ist Standard: `PHIMANTEC_SURICATA_MPM=hs`, `PHIMANTEC_SURICATA_SPM=hs`. `runmode=workers` bleibt unveraendert.
- Der Docker-Entrypoint uebergibt `PHIMANTEC_SURICATA_BODY_LIMIT=1048576` an den YAML-Patcher und persistiert UI-Overrides ueber den Monitor-State.
- SuricataMonitor zeigt HTTP Body Limit / Inspect Window im Form-Editor. Raw YAML bleibt vollstaendig und read-only bis zum Schreibschalter.
- Chunked/connection-close Responses lesen einen ersten Body-Record vor dem Header-Forwarding, damit ein frueher Suricata-Block wieder als saubere 403-Blockpage ausgegeben werden kann.
- Wenn Suricata erst nach bereits gesendeten Response-Headers blockt, injiziert der Proxy keine verschachtelte 403-Response in den laufenden Body-Stream, sondern beendet den Stream.
- Neue Regressionstests: `tests/test_suricata_yaml_patch.py` und `tests/test_proxy_streaming_guard.py`.

# v0.3.8-monitor-offline-vendor

- Behaelt das funktionierende v0.3.7-Docker-Rollout mit `network_mode: host` bei.
- Erzeugt die Proxy-Root-CA browserfaehig mit kritischen CA-BasicConstraints und
  `keyCertSign,cRLSign`; alte oder unpassende CA-Dateien im Docker-Volume werden
  gesichert und neu erzeugt.
- Leaf-Zertifikate nutzen 128-bit Seriennummern, kritische `CA:FALSE`
  BasicConstraints, KeyUsage, EKU `serverAuth`, SKI/AKI und SAN fuer DNS/IP.
- CA-Export schreibt zusaetzlich eine SHA256-Fingerprint-Datei und der Smoke-Test
  prueft, dass Host-Export und Proxy-Container-CA identisch sind.
- Neuer lokaler HTTPS-Proxy-CA-Smoke: `curl --cacert ~/phimantec-bump-ca.crt`
  testet die vom Proxy erzeugte Zertifikatskette ohne Internetabhaengigkeit.
- Suricata wird im `suricata-node`-Container aus `ppa:oisf/suricata-stable`
  installiert; Build und Runtime schlagen fehl, wenn keine Suricata-Version >= 8
  erkannt wird.
- SuricataMonitor-Status im Container zeigt den Prozessstatus auch dann als aktiv,
  wenn die Command-Socket-Antwort unvollstaendig ist.

# Release Notes — v0.3.8-monitor-offline-vendor

## v0.3.8 — Host-network Runtime + Container-Ingress-Fix

### Geändert

```text
- Proxy und SuricataMonitor laufen nun mit network_mode: host.
- Docker-Port-Publish, Docker-DNAT und docker-proxy werden fuer die externen
  Appliance-Schnittstellen nicht mehr verwendet.
- Beide Hauptservices verwenden restart: unless-stopped.
- suricata-node installiert nftables/iptables und setzt die Ingress-Accept-
  Regeln direkt beim Containerstart.
- PHIMANTEC_APPLIANCE_REQUIRE_INGRESS=1 sorgt dafuer, dass ein nicht gesetzter
  Ingress-Fix nicht mehr still als gesunder Stack durchrutscht.
- Runtime-Ports aus .env werden in den Container-Entrypoints direkt in die
  Proxy-/Monitor-/Lane-/FlowBus-Konfiguration geschrieben.
- Smoke-Test prueft echte Host-Listener mit ss statt Docker-Publish-Spalten.
```

### Ursache

```text
v0.3.6 konnte lokal healthy sein und auf dem Host per curl antworten, waehrend
externe Windows-TCP-Tests weiter fehlschlugen. Der Stack hing weiterhin an
Docker-Publish/DNAT/docker-proxy und damit am konkreten Host-Firewallzustand.
Auf dem Zielsystem war ein nftables input hook mit policy drop sichtbar.
```

### Ergebnis

```text
Proxy:             0.0.0.0:3128, echter Host-Listener aus dem proxy-Container
SuricataMonitor:   0.0.0.0:5050, echter Host-Listener aus dem suricata-node-Container
Lane:              127.0.0.1:18080
FlowBus:           127.0.0.1:18090
CA-Export:         ~/phimantec-bump-ca.crt
```

### Verifikation

```text
- check_release_context.sh: OK
- verify_docker_static.sh: OK
- bash -n scripts/*.sh: OK
- Python py_compile: OK
- SuricataMonitor Config-Manager Unit-Smoke: OK
- JavaScript Syntaxcheck: OK
- CMake Configure/Build: OK
- CTest: OK
- Frisch entpacktes ZIP: MANIFEST.sha256 OK
```

---

# Release Notes — v0.3.5-monitor-offline-vendor

## v0.3.5 — Saubere v0.2-Migration / Host-Dienst-Konflikt-Fix

### Geändert

```text
- Preflight erkennt aktive v0.2-Host-systemd-Dienste (phimantec-proxyd/
  -inspectord/-lane-sink), die nach einem Wechsel von der VM-/Lab-Installation
  weiterhin Port 3128 belegen und den Docker-Deploy blockieren.
- check_port_free() benennt den belegenden Prozess (ss -ltnp) direkt in der
  Fehlermeldung und verweist auf den Migrationsweg.
- Neues Skript scripts/phimantec_stop_legacy_host_services.sh: idempotenter
  Wechsel v0.2-Host -> v0.3.x-Docker (stop+disable der Altdienste).
- Neuer Schalter PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES (Default 0).
```

### Ursache aus v0.3.4-Betrieb auf v0.2-Hosts

```text
v0.3.4 band Proxy/Monitor korrekt extern (0.0.0.0). Auf Hosts mit
vorhergehender v0.2-Installation lief jedoch der Host-systemd-Dienst
phimantec-proxyd (bind 0.0.0.0:3128) weiter. Der Docker-Cleanup beruehrt nur
Container, nie Host-Dienste -> Preflight brach korrekt ab, aber ohne klaren
Migrationspfad. Folge: 5050 extern leer, keine v0.3.x-CA, nur die alte v0.2-CA
in ~/phimantec-bump-ca.crt.
```

### Verifikation

```text
- bash -n fuer alle geaenderten/neuen Skripte: OK
- check_release_context.sh gegen v0.3.5-Baum: OK
- sha256sum -c MANIFEST.sha256 aus frischem ZIP: OK
- Preflight ohne systemd: Legacy-Host-Dienst-Check wird sauber uebersprungen.
```

### Migration aus v0.3.4-.env

```text
Eine aus v0.3.4 uebernommene .env enthaelt PHIMANTEC_VERSION=0.3.4-... und wird
vom v0.3.5-Preflight bewusst abgelehnt. Loesung: .env loeschen (Neuerzeugung aus
.env.example) oder PHIMANTEC_VERSION manuell auf 0.3.5-monitor-offline-vendor setzen.
```

---

# Release Notes — v0.3.4-monitor-offline-vendor

## v0.3.4 — External-Publish- und CA-Export-Fix

### Geändert

```text
- Proxy ist im Docker-Default wieder extern erreichbar: PHIMANTEC_PROXY_PORT=3128.
- SuricataMonitor/Webmanagement ist im Docker-Default wieder extern erreichbar: PHIMANTEC_MONITOR_PORT=5050.
- Lane und FlowBus bleiben aus Sicherheitsgründen lokal gebunden: 127.0.0.1:18080 / 127.0.0.1:18090.
- Docker-generierte Proxy-CA wird automatisch nach ~/phimantec-bump-ca.crt exportiert.
- Neuer Wrapper-Befehl: scripts/phimantec_compose.sh export-ca.
- Preflight prüft Proxy-/Monitor-Loopback-Bindings und blockiert sie ohne bewusste Ausnahme.
- Smoke-Test prüft externes Docker-Publishing und CA-Export.
- AWK-Quote-Warnungen in Preflight/Compose/Smoke wurden beseitigt.
```

### Ursache aus v0.3.3

v0.3.3 setzte konfliktarme, aber für das Ziel falsche Default-Bindings:

```text
PHIMANTEC_PROXY_PORT=127.0.0.1:33128
PHIMANTEC_MONITOR_PORT=127.0.0.1:5050
```

Dadurch war trotz Container-Listener `0.0.0.0:3128` kein Zugriff aus dem LAN möglich. Die CA existierte nur im Docker-Volume und wurde nicht auf den Host exportiert.

### Verifikation

```bash
scripts/check_release_context.sh
scripts/verify_docker_static.sh
PHIMANTEC_VERIFY_JOBS=1 scripts/verify_release.sh
```

Runtime-Gate auf dem Zielhost:

```bash
scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
```

Erwartung:

```text
0.0.0.0:3128->3128/tcp oder :::3128->3128/tcp
0.0.0.0:5050->5050/tcp oder :::5050->5050/tcp
~/phimantec-bump-ca.crt vorhanden und X.509-valide
```

---

# Release Notes — v0.3.3-monitor-offline-vendor

## v0.3.3 — Compose-/Rollout-Fix

- Entfernt feste `container_name`-Werte. Gestoppte Altcontainer mit reservierten Namen blockieren den neuen Stack nicht mehr.
- Fuehrt `PHIMANTEC_COMPOSE_PROJECT` ein. Compose erzeugt projektgebundene Container-Namen statt global fixer Namen.
- Setzt konfliktarme Default-Bindings: Proxy `127.0.0.1:33128`, Monitor `127.0.0.1:5050`, Lane `127.0.0.1:18080`, FlowBus `127.0.0.1:18090`.
- Ergaenzt `scripts/phimantec_compose.sh`, damit Compose immer aus dem Repository-Root mit der richtigen `.env`, `docker-compose.yml` und Projektkennung laeuft.
- Ergaenzt `scripts/preflight_docker.sh` fuer Release-Kontext, Docker-Erreichbarkeit, Altcontainer und Portkonflikte.
- Ergaenzt `scripts/cleanup_stale_docker.sh` zum expliziten Entfernen alter fester PHIMANTEC-Containernamen.
- Ergaenzt `scripts/rollout_docker.sh` als endlichen Init/Down/Preflight/Build/Up/Smoke-Ablauf.
- Ergaenzt `scripts/smoke_docker.sh` fuer Monitor-/Proxy-Smoke nach dem Start.
- Fuegt Healthchecks fuer `suricata-node` und `proxy` hinzu; `proxy` wartet auf einen healthy `suricata-node`.
- Keine Endlosschleifen-Reparatur und keine automatische Restart-Policy.

# Release Notes — v0.3.3-monitor-offline-vendor

## v0.3.3 — Docker-/Rollout-Korrektur

- Docker Compose nutzt jetzt versionierte Image-Tags ueber `PHIMANTEC_VERSION`.
- Docker-Builds bekommen `build.network=${PHIMANTEC_DOCKER_BUILD_NETWORK:-host}` und koennen bei DNS-Problemen auf Host-Netzwerk bauen.
- Ubuntu-APT im Dockerfile laeuft ueber `docker/common/apt_install.sh` mit kurzer Timeout-/Fail-Fast-Konfiguration statt minutenlanger Retry-Kaskade.
- Kritischer Shell-Fehler im Suricata-Dockerfile korrigiert: `usermod ... || true` kann nach einem fehlgeschlagenen APT-Schritt die Build-Kette nicht mehr versehentlich fortsetzen.
- SuricataMonitor im Docker-Runtime-Image nutzt Ubuntu-Pakete `python3-flask` und `python3-yaml`; dadurch ist kein PyPI-Pip im normalen Docker-Runtime-Build mehr noetig.
- Suricata-Docker-Entrypoint nutzt `wait -n` statt einer unendlichen Watch-Schleife.
- Default-Matcher fuer Suricata ist jetzt `auto`; `hs` wird nur erzwungen, wenn der Betreiber es explizit setzt.
- Hyperscan-Fallback ist nur noch einmalig und nutzt `mpm=ac-ks`, `spm=auto` statt fehleranfaelligem `spm=ac-ks`.
- systemd-Units wurden auf ein minimales, kompatibles Lab-Profil reduziert und mit `StartLimitBurst=3` begrenzt.
- `.dockerignore` verhindert, dass grosse Build-, Offline- und Third-Party-Artefakte in den normalen Docker-Build-Kontext geraten.
- `scripts/verify_docker_static.sh` als statischer Docker-/Compose-Sanity-Check ergaenzt.

## SuricataMonitor Integration

- `suricata-monitor/` in das Appliance-Repository integriert.
- Docker-Profil mit Web-UI-Port `5050` ergaenzt.
- systemd-Service `suricata-monitor.service` fuer Suricata-VMs ergaenzt.
- SuricataMonitor Backend um Container-/Socket-Control ohne systemd erweitert.
- Form-Editor zeigt PHIMANTEC-Hinweise fuer proxy-kritische Einstellungen.
- Config-Manager korrigiert numerische Listenpfade wie `pcap[0].interface`.

## Offline-/Vendoring-Struktur

- `runtime-libs/` fuer schlanke Runtime-Libraries/Binaries.
- `offline-debs/` fuer nachvollziehbare Debian-Paket-Rebuilds.
- `third_party/sources/` fuer echte Air-Gap-Source-Rebuilds.
- `third_party/manifest.lock` und Patch-Series-Verzeichnisse ergaenzt.
- Vendor-Skripte fuer Verify, Unpack, Patch und Build ergaenzt.

## Docker

- `docker/proxy/Dockerfile`
- `docker/suricata-node/Dockerfile`
- `docker/common/apt_install.sh`
- `docker-compose.yml`
- `.dockerignore`
- `.env.example`

## Release-Gates

- CMake/CTest Regressionen bleiben erhalten.
- Python-/Bash-/JS-Syntaxchecks in `scripts/verify_release.sh` ergaenzt.
- Docker-/Compose-Static-Sanity in `scripts/verify_docker_static.sh` ergaenzt.
- Third-Party No-Source-Smoke prueft, dass fehlende Source-Archive das schlanke
  Repository nicht brechen.

---

# Release Notes — PHIMANTEC C11 Appliance v0.2.0-lab

## v0.2.0-lab — Minimal-invasive Page-Loading-Optimierung

Ziel dieses Releases ist schnelleres Laden komplexer Websites wie YouTube und Amazon, ohne die Security-Architektur zu schwächen.

### Geändert

```text
- H1 Keep-Alive im CONNECT-Tunnel eingefuehrt.
- Mehrere sequenzielle H1 Requests pro Client-TLS-Tunnel moeglich.
- Pro Request im Tunnel wird eine eigene Flow-ID abgeleitet.
- Tunnel-lokale Upstream-TLS-Verbindung wird wiederverwendet.
- Response-Forwarding erkennt Content-Length, chunked und close-delimited Grenzen.
- Chunked-Response-Decoder mit Trailer-Support hinzugefuegt.
- Slowloris-Schutz ueber Idle-Timeout und Max-Requests-pro-Tunnel.
```

### Nicht geändert

```text
- keine Domain-/CDN-Bypasses
- keine Abschaltung der Response-Inspection
- kein H2/H3-nativer Pfad
- kein Verdict-Cache fuer dynamische oder statische Pfade
- keine zweite Route fuer decrypted Traffic
```

### Tests

```text
RelWithDebInfo Build: PASS
CTest: 7/7 PASS
ASAN/UBSAN: 7/7 PASS
Keep-Alive Integration Smoke: PASS
Upstream-Reuse Integration Smoke: PASS
Chunked-Keep-Alive Integration Smoke: PASS
```

---

## v0.1.9-lab

Performance- und Diagnose-Release fuer den Proxy-Hot-Path. Sicherheitsarchitektur und Features bleiben erhalten; entschluesselter Traffic laeuft weiterhin nur ueber die klare Lane zu Suricata IDS/IPS und der Proxy bleibt Enforcement-Punkt.

### Added

- Downstream TLS Context Cache mit Hostname-normalisiertem Key und CA-SHA256-Fingerprint.
- ECDSA-P-256 Leaf-Zertifikate fuer deutlich schnellere Cache-Miss-Zertifikatserzeugung.
- Host-spezifischer Upstream SSL_CTX Cache mit aktiviertem OpenSSL Client-Session-Cache.
- Persistenter Lane-Client-Pool im Proxy mit automatischem Reconnect.
- Lane-Sink verarbeitet mehrere sequenzielle HTTP/1.1-POSTs ueber dieselbe TCP-Verbindung.
- Inkrementeller EVE-Index mit Rotation-/Truncation-Erkennung statt Full-File-Scan pro Verdict-Lookup.
- FlowBus-Frame-Write nutzt fuer kleine Frames einen Stack-Buffer und faellt nur fuer groessere Frames auf Heap zurueck.
- `flowbus.pool_size`, `lane.pool_size` und `tls.cert_cache_entries` in Config/Installer dokumentiert.
- Neuer Test `flowbus_write_read`; EVE-Test erweitert um Append+Lookup.
- Neue Diagnose-Dokumentation `docs/PERFORMANCE_DIAGNOSIS_V0_1_9.md`.

### Verified

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` erfolgreich.
- `cmake --build build -j$(nproc)` erfolgreich, 0 Warnings unter `-Werror`.
- `ctest --test-dir build --output-on-failure`: 6/6 bestanden.
- `bash scripts/build_asan.sh`: 6/6 bestanden.
- Lane-Sink Keep-Alive manuell mit zwei POSTs ueber eine TCP-Verbindung verifiziert.

### Not changed

- Kein Domain-/CDN-Bypass.
- Keine Deaktivierung der Response-Body-Inspection.
- Keine zweite Route fuer entschluesselten Traffic.
- H2/H3 nativ und echter Upstream-Connection-Pool bleiben Folgearbeiten.

## v0.1.7-lab-patch7-hyperscan

- Hyperscan als bevorzugter Suricata-Default integriert: `scripts/repair_suricata_rules_vm.sh` setzt standardmaessig `mpm-algo: hs` und `spm-algo: hs`, wenn Suricata Hyperscan-Unterstuetzung meldet.
- Robuster Fallback auf `ac-ks`, falls Hyperscan im installierten Suricata-Paket nicht verfuegbar ist oder der Konfigurationstest fehlschlaegt.
- `PHIMANTEC_REQUIRE_HYPERSCAN=1` ermoeglicht einen harten Fail, wenn Hyperscan zwingend erwartet wird.
- Patch 7 Performance-Optimierungen bleiben unveraendert: deduplizierte Response-Inspection und FlowBus-Client-Pool.

## v0.1.6-lab-patch6

- Response-Body-Inspection fuer kleine/Content-Length-Responses vor Client-Auslieferung ergaenzt. Dadurch wird EICAR im HTTPS-Response-Body vor Weitergabe an Browser/Curl geblockt.
- Lab-Sicherheitsfallback fuer explizite Testindikatoren (`/suricata-block`, `PHIMANTEC_TEST_BLOCK`, EICAR-Marker) in Proxy-Verdict und inspectord ergaenzt.
- Suricata-Lab-Regeln auf robuste TCP-Payload-Regeln fuer Lane-Port 18080 umgestellt und EICAR-Regel `sid:900002` ergaenzt.
- Proxy-Installer startet Services nun per echtem `restart`, nutzt 1500 ms Verdict-Timeout und validiert Allow/Block-Smoke-Tests.
- Suricata-Reparaturskript schreibt Config, Regeln und Units neu und installiert frisch gebaute `lane-sink`/`inspectord` Binaries, wenn vorhanden.

## v0.1.3-lab-patch3

- Proxy-Installer fuer Curl-Fehler 77 gepatcht: Der lokale Smoke-Test nutzt nun die bewusst exportierte Client-CA `/home/philo/phimantec-bump-ca.crt` statt der geschuetzten CA-Kopie unter `/etc/phimantec/ca`.
- `/etc/phimantec` und `/etc/phimantec/ca` werden mit `0751` angelegt, damit bekannte oeffentliche Zertifikatspfade traversierbar sind, waehrend der private Key weiterhin `0640 root:phimantec` bleibt.
- Installer validiert die CA mit `openssl x509 -noout` und prueft die Lesbarkeit fuer `phimantec` und den lokalen Testuser vor dem Smoke-Test.
- Troubleshooting-Dokumentation fuer `curl: (77) error setting certificate file` ergaenzt.



## v0.1.2-lab-patch2

- Dokumentation für robuste Zwei-Host-VM-Installation ergänzt.
- `scripts/install_proxy_vm_ubuntu.sh` ergänzt: erkennt vorhandene ZIPs, baut das Projekt, installiert `phimantec-proxyd`, erzeugt/preserviert die Lab-CA, startet lokalen HTTPS-Upstream und führt Smoke-Tests aus.
- `scripts/proxy_diagnose_vm.sh` ergänzt: sammelt Proxy-Service-, Port-, Config-, CA- und Journaldiagnose.
- Troubleshooting für `Session stopped`, fehlende ZIP-Dateien, fehlenden Projektordner und unvollständige Heredocs ergänzt.
- `phimantec-inspectord` wartet im Lab bei expliziten Suricata-Testindikatoren kurz auf den passenden EVE-Alert, damit `/suricata-block` im Zwei-Host-Test deterministischer blockt.
# Release Notes

## v0.3.8-monitor-offline-vendor

- Root-Fix fuer externe Erreichbarkeit: Proxy und SuricataMonitor laufen jetzt im Docker-Host-Netzwerk statt ueber Docker-Bridge/DNAT/docker-proxy.
- Proxy bleibt extern auf `0.0.0.0:3128`; SuricataMonitor bleibt extern auf `0.0.0.0:5050`.
- Lane und FlowBus bleiben lokal auf `127.0.0.1:18080` und `127.0.0.1:18090`.
- `restart: unless-stopped` ist jetzt Standard fuer Appliance-Betrieb nach Reboot.
- `scripts/phimantec_ingress_rules.sh` integriert nftables/iptables-Ingress-Freigaben; der suricata-node-Container wendet sie beim Start direkt an.
- `PHIMANTEC_SURICATA_INTERFACE=auto` erkennt im host-network-Modus das Host-Interface, z.B. `ens18`.
- Smoke-Test prueft jetzt echte Host-Listener statt Docker-Publish-Portspalte.


## v0.1.0-lab

Erste umgesetzte C11-Linie der PHIMANTEC Appliance.

### Added

- `phimantec-proxyd`
- `phimantec-lane-sink`
- `phimantec-inspectord`
- FlowBus `PHFB` Frame-Protokoll mit HMAC-SHA256
- TLS-Bump mit dynamischer Leaf-Zertifikat-Erzeugung
- H1 Parser und Alt-Svc-Stripping
- Suricata-Lane Envelope
- EVE-Korrelation
- Blockpage
- CMake + CTest
- systemd Beispielunits

### Verified

- Build mit `-Wall -Wextra -Wpedantic -Werror`
- 5/5 CTest Tests bestanden
- Debug, ASAN/UBSAN und RelWithDebInfo erfolgreich gebaut und getestet
- lokaler End-to-End-Smoke-Test: Allow = HTTP 200, Block = HTTP 403
- ZIP-Extraktionscheck aus frischem Verzeichnis erfolgreich gebaut und getestet

### Known Limits

- H2/H3 nativ nicht implementiert
- Chunked/Großkörper werden im MVP noch nicht vollständig gestreamt inspiziert
- EVE-Korrelation ist Dateiscan-MVP, noch kein indexierter Tailer
- FlowBus Client ist persistent, aber im MVP durch Mutex serialisiert


## v0.1.1-lab-patch1

- Suricata-8-Lab-Regeln auf moderne Sticky-Buffer-Syntax umgestellt.
- Ungueltige Legacy-Kombinationen `http_header` / `http_client_body` entfernt.
- Generischen Lane-Marker-Alert entfernt, damit Allow-Flows nicht durch reine Marker-Alerts geblockt werden.
- Reparaturskript `scripts/repair_suricata_rules_vm.sh` hinzugefuegt.

## v0.1.7-lab-patch7

Performance-Patch für den Lab-Hot-Path:

- Response-Inspection dedupliziert: Body-Sample wird nicht mehr zusätzlich als `response/headers` geprüft.
- FlowBus-Client-Pool mit 4 persistenten HMAC/TCP-Verbindungen ergänzt.
- EICAR-/Suricata-Blocking unverändert erhalten.
- Dokumentation `docs/PERFORMANCE_TUNING_LAB.md` ergänzt.

## v0.3.14a FlowBus Stable Finalization

This patch keeps the v0.3.14 security architecture but fixes the deterministic allow path:

```text
- FlowBus keepalive defaults to disabled.
- Default FlowBus basis is connect -> request -> read full response -> close per verdict.
- flowbus.timeout_ms and verdict.timeout_ms are parsed into separate config fields.
- inspectord returns SEEN_NO_ALERT after a configurable no-alert grace window instead of waiting for the full decision deadline.
- Docker defaults split Suricata decision timeout (900 ms) from proxy verdict timeout (3000 ms).
- Docker healthchecks execute phimantec-flowbus-probe to validate real HMAC FlowBus responses.
- Smoke test separates CA/TLS failures from HTTP 503 inspection unavailable and HTTP 403 policy blocks.
- fail_policy remains closed; no fail-open or domain/media bypass was added.
```
