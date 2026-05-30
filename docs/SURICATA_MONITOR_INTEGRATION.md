# PHIMANTEC C11 — SuricataMonitor Integration

## Zielbild

SuricataMonitor ist ab `v0.3.3-monitor-offline-vendor` Bestandteil des
PHIMANTEC C11 Appliance Repositorys und wird direkt neben der Suricata-Instanz
betrieben.

```text
Browser/Client
  -> phimantec-proxyd :3128
  -> klare PHIMANTEC Lane :18080
  -> Suricata AF_PACKET Analyse auf dem Lane-Interface
  -> eve.json alert payload-printable mit X-Phim-Flow-Id
  -> phimantec-inspectord
  -> FlowBus :18090
  -> allow/block im Proxy

Admin
  -> SuricataMonitor Web UI :5050
  -> Suricata YAML, Rules, EVE Events, reload-rules, Status
```

Wichtig: SuricataMonitor erweitert die Konfigurierbarkeit. Es ersetzt nicht die
PHIMANTEC Sicherheitskette. Blocking bleibt im Proxy. Suricata bleibt Analyse-
und Signallieferant.

## Integrierte Dateien

```text
suricata-monitor/                         Web-UI + Flask Backend
config/suricata-monitor/                  VM-/Host-Profil
docker/suricata-node/Dockerfile            Suricata + Monitor Runtime Image
docker/proxy/Dockerfile                    Proxy Runtime Image
docker-compose.yml                         Lab-/Runtime-Start mit Port 5050
packaging/systemd/suricata-monitor.service VM-Service
scripts/install_suricata_monitor_vm_ubuntu.sh
scripts/run_suricata_monitor_lab.sh
scripts/phimantec_suricata_yaml_patch.py
```

## Docker-Zugriff

```bash
cp .env.example .env
docker compose up -d --build
```

Zugriff:

```text
Proxy:             http://127.0.0.1:3128
SuricataMonitor:  http://127.0.0.1:5050
Default Login:    admin / changeme
```

Der Default-Login wird lokal in SQLite erzeugt und muss sofort geaendert
werden.

## VM-/Host-Installation

Auf dem Suricata-Host:

```bash
sudo bash scripts/install_suricata_monitor_vm_ubuntu.sh
```

Danach:

```bash
systemctl status suricata-monitor
ss -ltnp | grep ':5050'
```

## Proxy-sichere Konfigurationshinweise

Die Web-UI ermoeglicht bewusst die breite Suricata-Konfiguration, weil sie auch
in anderen Kontexten wiederverwendbar bleiben soll. Felder, die fuer das
PHIMANTEC Proxy-Lane-Profil kritisch sind, sind im Form-Editor mit einem
PHIMANTEC-Hinweis versehen.

Besonders kritisch fuer den Proxy-Betrieb:

```text
rule-files muss phimantec-local.rules enthalten
outputs[eve-log].enabled sollte aktiv bleiben
outputs[eve-log].filename sollte eve.json bleiben
outputs[eve-log].types[alert].enabled sollte aktiv bleiben
outputs[eve-log].types[alert].payload-printable sollte aktiv bleiben
AF_PACKET sollte das Lane-Interface beobachten
NFQUEUE/DPDK/PCAP sind moegliche Spezialprofile, aber nicht Standard
```

Die UI blockiert diese Aenderungen nicht. Sie informiert nur, damit ein anderes
Suricata-Profil weiterhin moeglich bleibt.

## Technische Diagnose der Ausgangslage

### Appliance-Kern

Der vorhandene C11-Kern ist schlank und testbar:

```text
shared/             Parser, FlowBus, EVE-Korrelation, Socket, Config
appliance/          Proxy, TLS-Bump, Lane-Client, Verdict-Engine
suricata-node/      Lane-Sink und Inspectord
tests/              7 CTest Regressionen
```

Die bestehende Pipeline war bereits korrekt getrennt:

```text
Proxy Host:      phimantec-proxyd
Suricata Host:   phimantec-lane-sink + phimantec-inspectord + Suricata
```

Die Integration musste deshalb nicht in den C-Pfad eingreifen. Die bessere
Loesung ist, die Web-UI an die Suricata-Host-Seite zu setzen und dieselben
Dateien/Sockets zu verwenden, die Suricata und inspectord ohnehin nutzen.

### SuricataMonitor

SuricataMonitor war bereits gut integrierbar:

```text
kein Flask-Decorator-Zwang; Routen per app.add_url_rule
keine OOP-Struktur
SQLite lokal
Suricata-Steuerung ueber Unix-Socket
Raw-YAML und schema-getriebener Form-Editor
Regelverwaltung und EVE-Event-Ansicht vorhanden
```

Korrigiert/erweitert wurde:

```text
Config-Path-Handling fuer numerische Listenpfade, z.B. pcap[0].interface
Container-Modus ohne systemd fuer Status/Reload
PHIMANTEC-Hinweise im Schema und Frontend
Docker-/systemd-Profile
Unit-Smoke-Test fuer Config-Manager
```

## Release-Gates

```bash
scripts/verify_release.sh
```

Dieses Gate prueft:

```text
Bash-Syntax
Python-Syntax
SuricataMonitor Config-Manager Unit-Smoke
JavaScript-Syntax, falls node vorhanden
CMake Build
CTest Regressionen
Third-Party-Skripte im No-Source-Smoke
```

## Sicherheitsnotizen

SuricataMonitor kann Suricata-Konfiguration und Regeln schreiben. Deshalb gilt:

```text
Web-UI nicht ungeschuetzt ins WAN exponieren
Default admin/changeme sofort aendern
TLS/Reverse-Proxy davor setzen, falls produktiv
Dateirechte fuer /etc/suricata und /var/log/suricata eng halten
Aenderungen ueber Audit-Log nachvollziehen
```

## v0.3.3 Rollout-Anpassung

Im Docker-Profil nutzt SuricataMonitor keine eigene Python-Virtualenv mehr.
Die Runtime installiert die minimalen Ubuntu-Pakete `python3-flask` und
`python3-yaml`. Dadurch entfaellt ein zusaetzlicher PyPI-Zugriff im normalen
Container-Build.

Der Suricata-Node-Entrypoint ist bewusst endlich: Er validiert die
Konfiguration, startet Suricata, Lane-Sink, Inspectord und Monitor, und beendet
den Container sauber, sobald einer der Kindprozesse endet. Es gibt keine
endlose Selbstreparatur-Schleife.

Suricata-Matcher:

```text
Default: PHIMANTEC_SURICATA_MPM=auto, PHIMANTEC_SURICATA_SPM=auto
Explizit Hyperscan: PHIMANTEC_SURICATA_MPM=hs, PHIMANTEC_SURICATA_SPM=hs
Einmaliger Fallback: mpm=ac-ks, spm=auto
```

## v0.3.4 External-Publish-Korrektur

v0.3.4 korrigiert die Docker-Publish-Defaults, ohne die Monitor-Integration oder den C11-Proxy-Pfad umzubauen.

```text
Proxy:            http://<docker-host-ip>:3128
SuricataMonitor: http://<docker-host-ip>:5050
Lane:             127.0.0.1:18080
FlowBus:          127.0.0.1:18090
CA Export:        ~/phimantec-bump-ca.crt
```

Der Monitor läuft weiterhin im Suricata-Node-Container auf `0.0.0.0:5050`; der entscheidende Fix ist die hostseitige Docker-Veröffentlichung auf `5050` statt `127.0.0.1:5050`.

Prüfung:

```bash
scripts/phimantec_compose.sh ps
scripts/phimantec_smoke.sh
```

Erwartet:

```text
0.0.0.0:5050->5050/tcp oder :::5050->5050/tcp
```
