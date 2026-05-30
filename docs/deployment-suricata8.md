# Deployment Suricata 8 Node

## Dienste

```bash
sudo install -m 0755 build/phimantec-lane-sink /usr/local/bin/
sudo install -m 0755 build/phimantec-inspectord /usr/local/bin/
sudo install -m 0644 config/phimantec-suricata-node.conf /etc/phimantec/
```

## Suricata-Konzept

Phase 1 nutzt Suricata als Detection-Komponente. Suricata erzeugt Alerts in `eve.json`; der Inspectord korreliert die Flow-ID und sendet ein Verdict über FlowBus zurück. Der Kernel-Drop ist in Phase 1 bewusst nicht Teil des Designs.

## AF_PACKET

Die Datei `config/suricata/phimantec-suricata.yaml.fragment` enthält einen Minimal-Startpunkt für AF_PACKET auf dem isolierten Lane-Interface. Das Interface muss an deine Umgebung angepasst werden.

## Rules

```bash
sudo cp config/suricata/local.rules /etc/suricata/rules/phimantec-local.rules
```

Danach `suricata.yaml` so anpassen, dass `phimantec-local.rules` geladen wird.

## EVE

EVE muss `alert` und idealerweise `payload-printable` enthalten, damit die MVP-Korrelation über die Flow-ID robust im Lab funktioniert.


## Suricata 8 Rule-Syntax Patch

Suricata 8 erwartet fuer moderne HTTP-Regeln Sticky Buffers wie `http.request_header` und `http.request_body`. Die alten Modifier `http_header` und `http_client_body` duerfen nicht als freistehende Buffers verwendet werden. Fuer die VM-Reparatur steht bereit:

```bash
PROXY_IP=192.168.0.232 scripts/repair_suricata_rules_vm.sh
```

Die Lab-Regeln enthalten nur echte Block-Alerts. Ein generischer Lane-Marker-Alert ist absichtlich deaktiviert, weil `phimantec-inspectord` in Phase 1 jeden Alert mit Flow-ID als Block-Verdict behandelt.


## Matcher Default ab v0.3.3

Das Docker-/VM-Profil setzt Suricata standardmaessig auf automatische Matcher-Auswahl:

```yaml
mpm-algo: auto
spm-algo: auto
```

Hyperscan kann weiterhin bewusst erzwungen werden:

```bash
PHIMANTEC_SURICATA_MPM=hs PHIMANTEC_SURICATA_SPM=hs scripts/repair_suricata_rules_vm.sh
```

Falls `hs` explizit angefordert wurde und Suricata ohne Hyperscan gebaut ist,
erfolgt maximal ein einmaliger Fallback auf `mpm-algo=ac-ks` und
`spm-algo=auto`. Mit `PHIMANTEC_REQUIRE_HYPERSCAN=1` kann ein harter Fail
erzwungen werden.


## v0.1.9 Lane/Inspectord Performance

`phimantec-lane-sink` unterstuetzt nun HTTP/1.1 Keep-Alive fuer sequenzielle Lane-POSTs. `phimantec-inspectord` nutzt die unveraenderte FlowBus-Schnittstelle, profitiert aber durch die inkrementelle EVE-Korrelation in `phim_eve_find_alert_for_flow()`.

## SuricataMonitor ab v0.3.3

Auf dem Suricata-Host kann die Web-UI direkt mit installiert werden:

```bash
sudo bash scripts/install_suricata_monitor_vm_ubuntu.sh
```

Standardprofil:

```text
Config:  /etc/phimantec/suricata-monitor.yaml
DB:      /var/lib/suricata-monitor/suricata-monitor.db
UI-Port: 5050
Login:   admin / changeme
```

Fuer den PHIMANTEC Proxy-Betrieb sollten diese Suricata-Optionen in der UI nur
bewusst geaendert werden:

```text
rule-files inklusive phimantec-local.rules
EVE-Log filename eve.json
EVE alert enabled
EVE alert payload-printable enabled
AF_PACKET Capture auf dem Lane-Interface
```

Die UI erlaubt Aenderungen, zeigt aber PHIMANTEC-Hinweise fuer kritische Felder.
