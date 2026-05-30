# SuricataMonitor · Architektur-Diagnose

> Begleitendes Dokument zu jedem Code-Review. Erklaert **warum** wir die
> Architektur-Entscheidungen so getroffen haben, und welche Alternativen
> verworfen wurden.

---

## 1. Anforderungen (Recap)

* **4 Menue-Punkte**: Dashboard, Settings, Rulesets, Event-Log
* **Funktionen**:
  * Hell/Dunkel-Theme
  * User-Management (simpel, ausbaufaehig)
  * Events mit Filter + Farb-Aufbereitung
  * Settings (suricata.yaml Edit)
  * Rule-Management (lokal)
  * Pattern-Management (ET via suricata-update)
  * Log-Management vorbereiten, **aber noch nicht implementieren**
* **UX-Anspruch**: professionell, intuitiv, "modern aber nicht zu hip"
* **Code-Anspruch**: flexibel, leicht wartbar, JS-Libs, klare Struktur
* **Dokumentation**: detailliert fuer Debug + Weiterentwicklung
* **Integration**: nahtlos in PHIMANTEC-Setup

## 2. Diagnose: Was bringt SuriGuard1 (vorgegebene Referenz)?

Die hochgeladene `SuriGuard1-master`-Codebase ist ein **React + TypeScript + Vite**
Frontend mit erheblichen Designstaerken (UI-Konzept, Tabellen-Layout, Light-Theme),
**aber jeder Suricata-relevante Service ist nur ein Stub**:

```typescript
// SuriGuard1/src/services/suricataService.ts
export const getSuricataConfig = async () => {
  // TODO: Implement API call to get Suricata config
  const response = await fetch('/api/settings/suricata');
  // …
};
```

Die `/api/...`-Endpunkte sind **nicht implementiert**. Es gibt eine IndexedDB
in der Browser-Page, die als Pseudo-Backend dient. Damit:

* kann kein echtes `suricata.yaml` gelesen werden
* kein `suricata-update` aufgerufen werden
* kein `reload-rules` an die laufende Instanz geschickt werden
* keine echten `eve.json`-Events angezeigt werden

**Fazit der Diagnose**: SuriGuard1 ist ein **UI-Mockup**, kein Management-System.
Wir uebernehmen das **UI-Pattern** (Sidebar+Header+Table-Layout, Tab-Navigation,
Stage-Workflow), bauen aber ein **vollstaendiges Backend** drumherum.

## 3. Tech-Entscheidungen

### 3.1 Backend: Python + Flask

| Kandidat | Vor | Nachteil | Entscheidung |
|---|---|---|---|
| Node.js (express) | gleicher Sprachstack wie Frontend | suricatasc + suricata-update am besten ueber Python-Subprocess wrappen | Verworfen |
| PHP | breit deploybar | unsudden Subprocess/Long-Polling-Faehigkeit, weniger natuerlich fuer Suricata-Tools | Verworfen |
| Go | Single-Binary | Mehraufwand fuer YAML-Round-Trip + suricatasc-Protokoll | Verworfen |
| **Python + Flask** | direkter `suricatasc`-Klon ueber Unix-Socket; `subprocess` fuer `suricata-update`; Mock-Modus trivial; stdlib bringt sqlite3/hashlib/hmac/json mit | mehr RAM als Go | **Gewaehlt** |

Suricata-Tools (`suricata-update`, `suricatasc`) sind in Python geschrieben.
Eine Python-Anbindung macht die Wartung trivial.

### 3.2 Frontend: Vanilla JS, kein Build-Step

| Kandidat | Vor | Nachteil | Entscheidung |
|---|---|---|---|
| React/Vite | Komponenten, JSX | npm-Toolchain, Build-Step, lock-files, ~50 MB node_modules | Verworfen |
| Vue 3 | leichtgewichtiger als React | gleicher Build-Step-Aufwand | Verworfen |
| Lit | Web-Components | obscure fuer Wartung | Verworfen |
| **Vanilla JS** | kein Build, einfacher Patch-Pfad, direkt in produktiver Datei sichtbar; Lesbarkeit | mehr Boilerplate als JSX | **Gewaehlt** |

Wir trennen Frontend in **Libs** (`api.js`, `router.js`, `theme.js`, `components.js`, `utils.js`)
und **Pages** (`dashboard.js`, `events.js`, `rulesets.js`, `settings.js`). Jede Page
ist eine eigenstaendige IIFE, die ein einziges globales `window.SGPageXxx` exportiert.
Patches an einer Page betreffen nur eine Datei.

### 3.3 Datenbank: SQLite

| Kandidat | Vor | Nachteil | Entscheidung |
|---|---|---|---|
| PostgreSQL | skaliert | Setup-Overhead | nicht im Lab-Massstab |
| MySQL/MariaDB | gleicher Punkt | wie oben | wie oben |
| IndexedDB (Browser) | wie in SuriGuard1 | keine Server-Persistenz, kein Multi-User | verworfen |
| **SQLite** | stdlib, kein Server, atomar | konkurrenzlose Schreibrate begrenzt | **Gewaehlt** |

Migration zu PostgreSQL ist spaeter trivial moeglich: alle SQL-Statements sind
plain ANSI, kein SQLite-spezifisches Feature genutzt (FOREIGN KEYS aktiviert).

### 3.4 Auth: JWT in HTTP-only Cookie + PBKDF2

| Aspekt | Wahl | Begruendung |
|---|---|---|
| Token-Format | JWT HS256 | weit verbreitet, minimal in 80 Zeilen Stdlib implementiert |
| Storage | HTTP-only Cookie | XSS-resistent, simpler als LocalStorage |
| Revoke | Server-Side Session-Tabelle | JWT-Stateless-Schwaeche kompensiert |
| Password-Hash | PBKDF2-SHA256, 200k Iterationen | stdlib, keine bcrypt/argon2-Lib noetig |
| Decorator-Pattern | **NICHT verwendet** | Repository-Style: explizites `require_auth(cfg)` am Funktionsbeginn |

## 4. Wichtige Architektur-Patterns

### 4.1 Application Factory + functools.partial

```python
# backend/app.py
app.add_url_rule("/api/v1/events", "events_list",
                 partial(_list_events, cfg), methods=["GET"])
```

Statt Flask-Decorators (`@app.route`) verdrahten wir alle Routen explizit in
`register_*_routes(app, prefix, cfg)`. Der Vorteil:

1. Config wird per `partial(...)` injiziert, statt globaler Variable
2. Keine Import-Reihenfolgen-Probleme (Decorator binden zur Import-Zeit)
3. Routen sind testbar (Funktion ist direkt aufrufbar)

### 4.2 Mock-Modus

Jeder `services/*.py`-Modul prueft `cfg["suricata"]["mock_mode"]`. Bei
`true` liefert die Funktion deterministische Demo-Daten. Damit:

* lassen sich UI-Aenderungen lokal entwickeln, ohne Suricata zu installieren
* kann CI laufen
* kann der Installer initial den Service starten, bevor `suricata.yaml` existiert

Mock-Implementierungen sind in derselben Datei wie der Service, am unteren Ende:

```python
def list_sources(cfg):
    if _is_mock(cfg):
        return _mock_sources()
    # … echte suricata-update Aufrufe …
```

### 4.3 Atomic Writes + Backup

Konfigurations-Files werden NIE in-place bearbeitet:

1. Backup als `suricata.yaml.bak.<timestamp>` anlegen
2. Neue Version in `suricata.yaml.tmp` schreiben
3. `suricata -T -c suricata.yaml.tmp` zur Validierung
4. Bei Erfolg: `os.replace()` (atomisch)
5. Bei Fehler: tmp loeschen, Original bleibt unangetastet

Analoge Strategie fuer `.rules`-Dateien.

### 4.4 Role-Based Access Control

Drei Rollen:

| Rolle | Darf |
|---|---|
| `viewer` | Alle GETs |
| `analyst` | viewer + Rule-Toggle, Rule-Upsert, Event-Process |
| `admin` | analyst + Config-Write, User-CRUD, Rule-Delete, Source-Enable/Disable |

Implementiert per `require_auth(cfg, allowed_roles=("admin",))` am Anfang
jeder geschuetzten Route.

## 5. Datenfluss-Diagramm

```
                    ┌──────────────┐
                    │  Browser     │
                    │  (Vanilla    │
                    │   JS SPA)    │
                    └──┬───────────┘
                       │ HTTPS + JWT-Cookie
                       ▼
                ┌──────────────────┐
                │   Flask-App      │
                │  ┌────────────┐  │
                │  │ Routes     │  │   ┌────────────┐
                │  │  /events   ◄──┤───┤ SQLite DB  │
                │  │  /rules    │  │   │  users     │
                │  │  /settings │  │   │  sessions  │
                │  └────┬───────┘  │   │  events    │
                │       │          │   │  audit_log │
                │  ┌────▼───────┐  │   └────────────┘
                │  │ Services   │  │
                │  │  suricata_ctl
                │  │  eve_reader
                │  │  rules_manager
                │  │  ruleset_manager
                │  │  config_manager
                │  └────┬───────┘  │
                └───────┼──────────┘
                        │
       ┌────────────────┼─────────────────┬─────────────────────┐
       ▼                ▼                 ▼                     ▼
┌─────────────┐  ┌──────────────┐  ┌──────────────┐    ┌──────────────┐
│ suricatasc  │  │ eve.json     │  │ /etc/suricata│    │  systemctl   │
│ Unix-Socket │  │ NDJSON-Tail  │  │  suricata.yaml│   │  restart/    │
│             │  │              │  │  + rules/    │    │  reload      │
└─────────────┘  └──────────────┘  └──────────────┘    └──────────────┘
       │                │                 │                     │
       └────────────────┴─────────────────┴─────────────────────┘
                                Suricata-Host
```

## 6. Bewusste Nicht-Features (Phase 2)

Folgendes ist **vorbereitet, aber bewusst nicht implementiert**:

* **Persistente Logs**: Schema-Feld `logging.persistence_enabled` in der
  YAML-Konfiguration ist vorbereitet, aber kein Tailing nach DB. Begruendung:
  in den Anforderungen explizit "vorbereiten, noch nicht umsetzen".
* **Realtime-Updates (WebSocket)**: aktuell pollt das UI per Refresh-Button.
* **Multi-Suricata-Instanzen**: aktuell ein Suricata pro SuricataMonitor.
* **API-Token fuer Maschinen-Clients**: aktuell nur Cookie-Auth (Bearer-Token wird in `auth.py` aber bereits akzeptiert).

## 7. Zusammenfassung in einem Satz

**SuricataMonitor ist die Antwort auf die Frage "Wie sieht SuricataMonitor aus, wenn das
Backend tatsaechlich existiert?"** — mit Schwerpunkt auf direkter
Suricata-Integration, sauberer Trennung von Frontend/Backend, und einer
Code-Struktur, die nicht in 6 Monaten "Legacy" ist.

---

## Anhang: v2 Refactoring (Mai 2026)

Nach erstem Lab-Test wurden drei UX-Probleme adressiert:
1. **Umbenennung** SuriGuard-NG → SuricataMonitor (Service-User, Paths, Env-Vars, Cookies)
2. **Dashboard** im Original-Layout (Server Status / Log Summary / Event Summary)
3. **Settings/Suricata-Config**: schema-getriebene Form ergaenzt Raw-YAML-Editor

Details in [`REFACTORING.md`](REFACTORING.md). Die Architektur-Grundsaetze
(no decorator, functools.partial, Vanilla JS, SQLite) wurden NICHT veraendert.

Das **Konfigurations-Schema** unter `backend/services/config_schema.py` definiert
9 Sektionen mit ~65 Settings. Frontend rendert die Form dynamisch — Erweiterungen
brauchen nur Schema-Aenderungen, kein neuer JS-Code.
