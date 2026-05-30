# SuricataMonitor v2 · Refactoring-Diagnose

> Dokument fuer den Uebergang von **SuricataMonitor** → **SuricataMonitor**.
> Beschreibt alle Umbenennungen, Layout-Umbauten und die neue
> **Schema-getriebene Suricata-Konfigurations-UI**.

---

## 1. Ausloeser

Nach dem ersten produktiven Test wurden drei UX-Mangel klar:

| # | Symptom | Wurzelursache |
|---|---|---|
| 1 | Produktname unscharf | "SuricataMonitor" ist ein Code-Name; "SuricataMonitor" beschreibt die Aufgabe |
| 2 | Dashboard nuechtern, Original-Vorlage besser | Im Re-Build war der **Status**, das **Log Summary** und das **Event Summary** zusammengeschmolzen — die optische Aufteilung war verloren |
| 3 | Settings/Suricata-Konfiguration **nur** als Raw-YAML | Adminstrative Aenderungen wie "Interface umstellen", "EVE-Output ein/aus", "App-Layer-Protokolle" brauchen Wissen ueber die Suricata-YAML-Struktur — Power-User-Tool, kein Operator-Tool |

## 2. Diagnose: was Suricata an Konfiguration bietet

Eine vollstaendige `suricata.yaml` umfasst >100 Top-Level-Direktiven. Ohne Vorsortierung wird jede UI unbedienbar. Wir gruppieren in **9 Sektionen** mit jeweils der am haeufigsten benoetigten Untermenge:

| # | Sektion | Beispiel-Settings | Bedienhaeufigkeit |
|---|---|---|---|
| 1 | **Allgemein** | `default-log-dir`, `default-rule-path`, `runmode`, `default-packet-size` | hoch |
| 2 | **Netzwerk-Variablen** | `vars.address-groups.HOME_NET`, `EXTERNAL_NET`, Port-Groups | hoch |
| 3 | **Capture (AF_PACKET)** | Liste von Interfaces mit `cluster-id`, `cluster-type`, `tpacket-v3` | mittel-hoch |
| 4 | **App-Layer-Protokolle** | HTTP, TLS, DNS, SMTP, FTP, SSH, … on/off + Optionen | mittel |
| 5 | **Outputs / EVE-Log** | `filename`, Event-Typen (alert/http/dns/flow/tls) on/off | hoch |
| 6 | **Detection-Engine** | `mpm-algo`, `spm-algo`, `inspection-recursion-limit` | niedrig-mittel |
| 7 | **Threading** | `set-cpu-affinity`, `detect-thread-ratio` | niedrig |
| 8 | **Stream / Flow** | `stream.memcap`, `stream.checksum-validation`, `flow.memcap` | mittel |
| 9 | **Logging** | `default-log-level`, weitere Logger | niedrig |

Was **nicht** als Formular umgesetzt wird (zu nischig, zu komplex):
- napatech / pf-ring / nflog / nfqueue / xdp (Spezial-Capture)
- pcap-file (Forensik-Modus, kein Live)
- engine-analysis / coredump
- host-os-policy / decoder-Detail-Settings

Diese sind ueber den **Raw-YAML-Tab** weiterhin voll editierbar.

## 3. Architektur-Entscheidung: Schema-getrieben

Statt jede Einstellung im JS-Code zu haerten, **definiert das Backend ein Schema** als JSON. Das Frontend rendert die Form **dynamisch** daraus. Vorteile:

* Nur eine Quelle der Wahrheit (`backend/services/config_schema.py`)
* Neue Felder erweitern ohne JS-Aenderung
* Validation kann zentral erweitert werden
* Help-Texte/Defaults dokumentiert in einer Datei

### Schema-Format (Auszug)

```python
{
  "sections": [
    {
      "key": "general",
      "label": "Allgemein",
      "icon": "settings",
      "fields": [
        {
          "path": "default-log-dir",
          "type": "string",
          "label": "Log-Verzeichnis",
          "default": "/var/log/suricata",
          "help": "Wohin Suricata seine Logs (eve.json, fast.log) schreibt."
        },
        {
          "path": "runmode",
          "type": "enum",
          "options": ["autofp", "workers", "single"],
          "default": "autofp",
          "label": "Run-Modus",
          "help": "workers = ein Thread pro CPU/Queue; autofp = Affinity-basiert."
        },
        {
          "path": "default-packet-size",
          "type": "int",
          "min": 64, "max": 9000,
          "default": 1538,
          "label": "Packet-Buffer-Size (Bytes)"
        }
      ]
    },
    ...
  ]
}
```

Feldtypen: `string`, `int`, `bool`, `enum`, `list-of-strings`, `list-of-objects`
(letzteres fuer `af-packet.interfaces` etc.)

## 4. Read/Write-Strategie

Suricata YAML enthaelt Kommentare. Naive `yaml.dump()` zerstoert die. Ansatz:

1. **Lesen**: PyYAML parsed in Python-Dict. Aus dem Dict per Pfad gezielt Werte ziehen.
2. **Schreiben**: Ganzes Dict mit `yaml.dump(default_flow_style=False, sort_keys=False)` zurueck. **Backup** der Original-Datei.
3. **Validation**: vorher `suricata -T -c <neue_datei>`. Bei Fehler kein Commit.
4. **Raw-YAML-Tab** bleibt fuer Editing mit Kommentaren erhalten — Power-User koennen dort weiter manuell editieren.

Trade-Off: Bei Verwendung des Form-Editors gehen **handgepflegte Kommentare verloren**. Das wird im UI prominent angezeigt (Warnung vor dem Speichern). Backup-Datei enthaelt die letzte Version mit Kommentaren.

## 5. Dashboard-Layout (Original-Stil)

Bezug auf den User-Screenshot von SuriGuard1:

```
+-----------------+  +-----------------+  +-----------------+
| Server Status   |  | Log Summary     |  | Event Summary   |
|   Uptime        |  |  Alert / DNS    |  |  Pending /      |
|   CPU / Memory  |  |  HTTP / Total   |  |  Investigating  |
|   Network Load  |  |  View All Logs  |  |  Completed /    |
|                 |  |                 |  |  Critical       |
+-----------------+  +-----------------+  +-----------------+

                                          +-----------------+
                                          | Recent Events   |
                                          |  Severity/Source|
                                          |  Stage/Time     |
                                          +-----------------+
```

Drei Karten oben, "Recent Events" rechts unten. Karten haben:
- Icon links
- Ueberschrift + Action-Icon rechts oben
- 2x2-Grid mit Mini-Kacheln pro Karte

## 6. Renaming-Inventar

| Alt | Neu |
|---|---|
| Projekt-Name | SuricataMonitor → **SuricataMonitor** |
| Sidebar-Titel | "SuricataMonitor" / "Suricata Management" → "**SuricataMonitor**" / "Suricata IDS Management" |
| ZIP-Top-Level | `suriguard-ng/` → `suricata-monitor/` |
| systemd-Unit | `suriguard.service` → `suricata-monitor.service` |
| Service-User (install.sh) | `suriguard` → `suricata-monitor` |
| Cookie-Name | `suriguard_token` → `surimon_token` |
| LocalStorage-Keys | `suriguard.*` → `surimon.*` |
| Verzeichnis (Install) | `/opt/suriguard-ng`, `/etc/suriguard`, `/var/lib/suriguard` → `/opt/suricata-monitor`, `/etc/suricata-monitor`, `/var/lib/suricata-monitor` |
| Config-Datei | `suricata-monitor.yaml` → `suricata-monitor.yaml` |
| Env-Variable | `SURIGUARD_CONFIG` → `SURIMON_CONFIG` |

## 7. Was bleibt unveraendert

* Backend-Tech-Stack (Flask + SQLite + JWT)
* Frontend-Tech-Stack (Vanilla JS, kein Build)
* API-Pfade (`/api/v1/...`) → bleiben kompatibel
* DB-Schema → bleibt kompatibel
* Code-Stilregeln (kein Decorator, `functools.partial`)

## 8. Migrations-Pfad fuer bestehende Installationen

Nicht implementiert, aber dokumentiert:

```bash
# Service stoppen
sudo systemctl stop suriguard
# Daten uebernehmen
sudo cp -r /var/lib/suriguard /var/lib/suricata-monitor
sudo cp /etc/suriguard/suricata-monitor.yaml /etc/suricata-monitor/suricata-monitor.yaml
# Neue Version installieren (Installer mit MIGRATE-Modus)
sudo bash scripts/install.sh
# Alte Reste entfernen
sudo systemctl disable suriguard
sudo rm /etc/systemd/system/suriguard.service
sudo systemctl daemon-reload
```

## 9. Checkliste fuer Refactoring (Selbstkontrolle)

- [x] Diagnose-Dokument
- [x] Schema-Datei `backend/services/config_schema.py`
- [x] Erweiterte `config_manager.py` mit `read_structured()` / `write_structured()`
- [x] Neue Routen `/settings/suricata/schema`, `/settings/suricata/structured`
- [x] Dashboard im Original-Layout (Server Status / Log Summary / Event Summary / Recent Events)
- [x] Settings-Page mit drei Tabs (Form / YAML / Service / Users)
- [x] CSS-Erweiterungen (Help-Tooltip, Field-Group, Sub-Section)
- [x] Renaming durchgaengig
- [x] Doku komplett aktualisiert
- [x] End-to-End Smoke-Test
- [x] v2.1: Schema-Gruppen + IDS/IPS-Sektion + NFQUEUE-Sektion + interface-select Feldtyp

---

**Refactoring-Ergebnis**: ~25 Dateien angefasst, 0 API-Breakings, neue Schema-Datei mit ~80 strukturierten Settings.

---

## 10. v2.1 Erweiterung: Capture/Mode-Trennung + Gruppen-Sidebar

Nach Lab-Feedback wurde die Settings-Sidebar in **vier logische Gruppen**
strukturiert und die Suricata-Konzepte **Capture-Backend** vs.
**IDS/IPS-Verhalten** sauber getrennt.

### Schema v1.1

Neuer Top-Level-Key `groups`. Jede Section bekommt ein `group`-Property,
das auf einen Gruppen-Key zeigt. Frontend rendert die Sidebar als
flache Liste mit Gruppen-Headers (kein Untermenue, kein Double-Click).

```python
SCHEMA = {
  "version": "1.1",
  "groups": [
    {"key": "general_group", "label": "Allgemein"},
    {"key": "modes",         "label": "Betriebsmodus"},
    {"key": "detection",     "label": "Detektion"},
    {"key": "performance",   "label": "Performance"},
  ],
  "sections": [
    {"key": "general", "group": "general_group", ...},
    {"key": "ids_ips", "group": "modes", ...},
    ...
  ]
}
```

### Drei neue Sektionen unter "Betriebsmodus"

| Sektion | Suricata-Settings | Zweck |
|---|---|---|
| **IDS / IPS Verhalten** | `host-mode`, `stream.inline`, `action-order`, `exception-policy` | Wie das System auf Treffer reagiert (passiv vs. aktiv) |
| **Capture: NFQUEUE (IPS)** | `nfq.mode`, `nfq.batchcount`, `nfq.fail-open`, `nfq.repeat-mark`, ... | IPS via Linux netfilter |
| **Capture: PCAP / DPDK** | `pcap[0].interface`, `pcap[0].buffer-size`, `dpdk.eal-params.proc-type` | Alternative Capture-Backends |

`capture_afpacket` (bisher "capture") wurde umbenannt und um `copy-mode`
+ `copy-iface` erweitert, damit auch L2-Bridge-IPS-Setups direkt aus
der Form heraus konfigurierbar sind.

### Neuer Feldtyp: `interface-select`

Felder mit `type: "interface-select"` rendern als HTML5
`<input list="...">` mit zugeordneter `<datalist>`. Backend-Endpoint
`GET /api/v1/system/interfaces` liefert alle Interfaces aus
`/sys/class/net/`. User koennen aus der Liste waehlen ODER frei
eingeben (falls ein NIC noch nicht da ist).

```json
{
  "ok": true,
  "interfaces": [
    {"name": "ens18", "state": "up",   "mac": "52:54:00:12:34:56"},
    {"name": "ens19", "state": "up",   "mac": "52:54:00:12:34:57"},
    {"name": "lo",    "state": "unknown", "mac": "00:00:00:00:00:00"}
  ]
}
```

### Sidebar-Layout

```
ALLGEMEIN
  • Grundeinstellungen
  • Netzwerk-Variablen

BETRIEBSMODUS
  • IDS / IPS Verhalten
  • Capture: AF_PACKET
  • Capture: NFQUEUE (IPS)
  • Capture: PCAP / DPDK / Weitere

DETEKTION
  • App-Layer-Protokolle
  • Outputs (EVE-Log)
  • Detection-Engine

PERFORMANCE
  • Threading
  • Stream & Flow
  • Logging (Engine-Logs)
```

### Warum kein echtes Untermenue?

* Ein Klick zur Sektion ist immer besser als zwei Klicks (Aufklappen + Auswahl)
* Sidebar-Header sind nicht klickbar - klare Semantik
* Funktioniert auf Mobile und Desktop gleichermassen
* Erweiterbar ohne UI-Layout-Aenderung (einfach neue Section mit neuem `group`-Key)

### Statistik

* **Schema v1.1**: 12 Sektionen (vorher 9), 79 strukturierte Pfade (vorher 65)
* Neu: 4 Gruppen-Header in Sidebar
* Neu: 1 Feldtyp `interface-select` + 1 API-Endpoint `/system/interfaces`
