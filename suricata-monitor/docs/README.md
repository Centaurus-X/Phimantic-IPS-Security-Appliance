# SuricataMonitor · Suricata Management Web Interface

Web-basiertes Management-Interface fuer Suricata IDS/IPS, mit **echtem Python-Backend**
und **schema-getriebener Konfigurations-UI**. Designed als Companion zur PHIMANTEC C11
Appliance, aber auch standalone nutzbar.

> **v2 Update**: Original-Dashboard-Layout + schema-getriebene Suricata-Konfigurations-Form
> mit Switches, Dropdowns und Listen. Details siehe [`REFACTORING.md`](REFACTORING.md).

## Features

* **Dashboard** im Original-Layout: Server Status / Log Summary / Event Summary
  als drei separate Karten + Recent-Events-Tabelle.
* **Event-Log** mit Filter (Stage, Severity, Volltext) und Workflow (pending → investigation → completed).
* **Rulesets** zum Verwalten lokaler `.rules`-Dateien (Toggle/Add/Edit/Delete) und ET-Pattern-Quellen via `suricata-update`.
* **Settings** mit vier Tabs:
  - **Suricata Config** — Schema-getriebene Form mit 9 Sektionen (~65 Settings) und Sidebar-Navigation
  - **Raw YAML** — Power-User-Editor (bewahrt Kommentare)
  - **Service Control** — reload-rules / restart, Status-Anzeige
  - **Benutzer** — User-Management (admin only)
* **Light/Dark-Theme**, persistiert in localStorage.
* **Mock-Modus** fuer UI-Entwicklung ohne Suricata-Installation.
* **Role-Based Access Control**: admin / analyst / viewer.
* **Audit-Log** fuer alle konfigurations-aendernden Aktionen.

## Tech-Stack

| Schicht | Wahl | Begruendung |
|---|---|---|
| Backend | Python 3 + Flask | Direkter Suricata-Kontakt (suricatasc-Protokoll), `suricata-update`-Wrapper, YAML-Edit |
| Datenbank | SQLite | Keine externe DB noetig, einfaches Deploy |
| Frontend | Vanilla JS + CSS | Kein Build-Step, keine npm-Abhaengigkeit, leicht wartbar |
| Auth | JWT (HS256) + PBKDF2 | Stdlib-only, kein externes lib noetig |
| Service-Wrapper | systemd | Standardweg auf Linux |

Mehr Details: [`docs/ARCHITECTURE.md`](ARCHITECTURE.md).

## Schnellstart (Entwicklung)

```bash
cd suricata-monitor
bash scripts/run_dev.sh
# -> http://127.0.0.1:5050
# Login: admin / changeme
```

Der Dev-Server laeuft im **Mock-Modus**, d. h. ohne echtes Suricata. Damit kann
das UI sofort exploriert werden.

## Installation (Produktion)

```bash
sudo bash scripts/install.sh
sudo systemctl enable --now suricata-monitor
```

Der Installer legt einen System-Benutzer `suricata-monitor` an, kopiert den Code nach
`/opt/suricata-monitor`, erzeugt eine Konfiguration unter `/etc/suricata-monitor/suricata-monitor.yaml`
mit zufaelligem JWT-Secret, initialisiert die DB und installiert die
systemd-Unit. Sudoers-Eintrag fuer `systemctl restart suricata` wird mit
angelegt.

Nach dem ersten Start im Browser unter Port 5050:

1. Mit `admin` / `changeme` anmelden.
2. Sofort in **Settings → Benutzer** das Passwort aendern.
3. In `/etc/suricata-monitor/suricata-monitor.yaml` `mock_mode: false` setzen (sobald echte Suricata da ist).

## Verzeichnisstruktur

```
suricata-monitor/
├── backend/                # Python Flask-Backend
│   ├── app.py              # Application Factory
│   ├── auth.py             # JWT + PBKDF2 + Request-Guard
│   ├── config.py           # YAML-Config-Loader
│   ├── requirements.txt    # Flask + PyYAML
│   ├── db/
│   │   ├── connection.py   # SQLite Connection-Pool
│   │   └── schema.sql      # DB-Schema
│   ├── routes/
│   │   ├── auth_routes.py
│   │   ├── dashboard_routes.py
│   │   ├── events_routes.py
│   │   ├── rules_routes.py
│   │   ├── settings_routes.py
│   │   └── users_routes.py
│   └── services/
│       ├── suricata_ctl.py     # suricatasc native + Mock
│       ├── eve_reader.py       # eve.json Tail + Parsing
│       ├── rules_manager.py    # Rule-File-Edit (Toggle/Upsert/Delete)
│       ├── ruleset_manager.py  # suricata-update Wrapper
│       └── config_manager.py   # YAML Edit + suricata -T Validation
│
├── frontend/               # Vanilla-JS SPA
│   ├── index.html          # App-Shell
│   ├── login.html          # Login-Seite
│   ├── css/
│   │   ├── base.css        # Theme-Tokens (CSS vars) + Light/Dark
│   │   ├── layout.css      # App-Shell-Grid
│   │   └── components.css  # Buttons, Tabellen, Modals, …
│   ├── js/
│   │   ├── app.js          # Bootstrap + Auth-Check
│   │   ├── auth.js         # Login-Page-Logic
│   │   ├── lib/
│   │   │   ├── api.js         # fetch-Wrapper, 401-Redirect
│   │   │   ├── router.js      # Hash-basierter SPA-Router
│   │   │   ├── theme.js       # Light/Dark Persistenz
│   │   │   ├── components.js  # Toast, Modal, Confirm
│   │   │   └── utils.js       # DOM Helper + Formatter
│   │   └── pages/
│   │       ├── dashboard.js
│   │       ├── events.js
│   │       ├── rulesets.js
│   │       └── settings.js
│   └── assets/
│       └── favicon.svg
│
├── config/
│   └── suricata-monitor.example.yaml
├── scripts/
│   ├── install.sh
│   ├── init_db.py
│   └── run_dev.sh
├── systemd/
│   └── suricata-monitor.service
└── docs/
    ├── README.md             # diese Datei
    ├── ARCHITECTURE.md       # tiefe Architektur-Diagnose
    ├── API.md                # alle REST-Endpunkte
    ├── DEVELOPMENT.md        # Setup + Code-Stilrichtlinien
    ├── INTEGRATION_PHIMANTEC.md  # Einbindung in PHIMANTEC-Lab
    └── DEBUGGING.md          # Troubleshooting-Playbook
```

## Sicherheits-Hinweise

* **JWT-Secret** in Produktion durch zufaelligen Wert ersetzen (Installer macht das automatisch).
* **HTTPS** ist Pflicht in Produktion. `cookie_secure: true` setzen und Reverse-Proxy mit TLS davor stellen (nginx/Caddy/HAProxy).
* **Default-Admin** sofort nach Erst-Login Passwort aendern.
* **YAML-Edit** wird vor dem Schreiben mit `suricata -T` validiert; bei Fehler wird kein Schreibvorgang ausgefuehrt. Vorherige Version wird als `.bak.<timestamp>` gesichert.
* **Path-Traversal** wird in `services/rules_manager.py` durch `realpath`-Check verhindert.
* **Audit-Log** zeichnet jede schreibende Aktion auf (`audit_log`-Tabelle).

## Lizenzen / Drittquellen

* Code unter MIT (`LICENSE`).
* Suricata: GPLv2 ([Suricata-IDS.org](https://suricata.io/)) — wir steuern es nur, integrieren keinen Suricata-Code.
* PyYAML: MIT.
* Flask: BSD-3-Clause.

## Weiterfuehrende Dokumentation

* [Architektur-Diagnose](ARCHITECTURE.md) — Design-Entscheidungen
* [REST-API](API.md) — alle Endpunkte
* [Entwicklung](DEVELOPMENT.md) — Code-Stil, Tests, Patterns
* [PHIMANTEC-Integration](INTEGRATION_PHIMANTEC.md)
* [Debugging](DEBUGGING.md)
