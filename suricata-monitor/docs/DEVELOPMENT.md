# SuricataMonitor · Entwicklung

## Quickstart

```bash
git clone <this-repo>
cd suricata-monitor
bash scripts/run_dev.sh
# -> http://127.0.0.1:5050 · admin / changeme
```

## Voraussetzungen

* Python 3.10+
* Linux/macOS (Windows: WSL2)
* (optional fuer Echtbetrieb) Suricata 6+ mit Unix-Socket aktiviert,
  `suricata-update`, `systemctl`

## Verzeichnislogik

```
backend/  → ALLE Python-Module + Schema + Requirements
frontend/ → ALLE static files, direkt von Flask ausgeliefert
config/   → Beispiel + lokale YAML
scripts/  → Installer, DB-Init, Dev-Helper
systemd/  → Unit-Template
docs/     → diese Dokumentation
data/     → wird automatisch angelegt (SQLite + logs)
```

## Code-Stilrichtlinien

### Python

* **Keine `@dataclass`, `@classmethod`, `@staticmethod`, `@contextmanager` Decorators.**
* **Keine Flask-Routen-Decorators** — alle Routen werden in `register_*_routes()` per `app.add_url_rule()` verdrahtet.
* `functools.partial` statt `lambda` wo immer moeglich.
* Kein OOP fuer Geschaeftslogik. Klassen nur dort, wo Stdlib es erzwingt (sqlite3.Row, Flask-App).
* Public Functions haben Docstrings im Numpy-/Google-Style.

### JavaScript

* IIFE-Module-Pattern: jede Datei ist eine `(function (global) { … }(window));`-Klammer.
* Ein einziges globales Symbol pro Datei (`SGApi`, `SGRouter`, …).
* Kein Build-Step. Module werden in `index.html` per `<script>`-Tag eingebunden.
* `async/await` ja, kein `.then()`-Spaghetti.
* DOM-Erzeugung via `SGUtils.el()` statt Template-Strings (XSS-sicher).

### CSS

* Tokens nur in `css/base.css` (CSS variables).
* `[data-theme="dark"]`-Selector ueberschreibt Tokens fuer Dark-Mode.
* Component-Styles in `css/components.css`, niemals an HTML inline (Ausnahme: einmalig dynamische Werte wie `style="flex:1"`).

## Hinzufuegen einer neuen Page

1. `frontend/js/pages/myfeature.js` anlegen mit IIFE:

   ```javascript
   (function () {
     "use strict";
     async function render(params) {
       const content = document.getElementById("app-content");
       SGUtils.clear(content);
       // … rendern …
     }
     window.SGPageMyfeature = render;
   }());
   ```

2. In `frontend/index.html` `<script src="/js/pages/myfeature.js"></script>` ergaenzen.

3. In `frontend/js/app.js` registrieren:

   ```javascript
   SGRouter.register("myfeature", window.SGPageMyfeature);
   ```

4. Sidebar-Link in `index.html` ergaenzen:

   ```html
   <a class="nav-item" data-route="myfeature">
     <span class="nav-icon">…SVG…</span>
     <span class="nav-label">My Feature</span>
   </a>
   ```

## Hinzufuegen einer API-Route

1. Neue Datei `backend/routes/myfeature_routes.py`:

   ```python
   from functools import partial
   from flask import jsonify
   from auth import require_auth

   def _do_something(cfg):
       ok, ctx = require_auth(cfg)
       if not ok: return ctx
       return jsonify({"ok": True})

   def register_myfeature_routes(app, prefix, cfg):
       app.add_url_rule(prefix + "/myfeature", "myfeature_get",
                        partial(_do_something, cfg), methods=["GET"])
   ```

2. In `backend/app.py` importieren und im Factory wiren:

   ```python
   from routes.myfeature_routes import register_myfeature_routes
   # …
   register_myfeature_routes(app, api_prefix, cfg)
   ```

## Hinzufuegen eines Suricata-Services

1. Neuer Service-Modul `backend/services/myservice.py`. Pattern:

   ```python
   def _is_mock(cfg):
       return bool(cfg.get("suricata", {}).get("mock_mode", False))

   def fetch_thing(cfg):
       if _is_mock(cfg):
           return _mock_thing()
       # real implementation
       return {...}

   def _mock_thing():
       return {"ok": True, "mock": True, "data": [...]}
   ```

2. Aus Routes konsumieren, Tests dagegen schreiben.

## Datenbank-Migration

Aktuell keine Migrations-Toolchain — `schema.sql` ist idempotent (`CREATE TABLE IF NOT EXISTS`).
Fuer destruktive Aenderungen:

1. Migration-Script unter `scripts/migrations/NNN_description.sql` ablegen.
2. In `backend/db/connection.py` `init_db_pool()` so erweitern, dass alle
   `migrations/*.sql` in lexikographischer Reihenfolge ausgefuehrt werden.

Wenn das System einmal in Produktion ist, beim ersten Migration-Bedarf:
`alembic` evaluieren.

## Testen

Aktuell keine automatisierten Tests im Repo (Anforderung war Build des Systems).
Empfohlene Test-Pyramide bei Erweiterung:

* **Unit**: `pytest backend/tests/test_rules_manager.py` — parse, toggle, upsert.
* **Integration**: `pytest backend/tests/test_routes_events.py` mit Test-Client + Mock-Config.
* **Smoke-E2E**: `playwright` gegen `mock_mode: true`-Instanz.

## Performance-Hinweise

* `read_rules()` mit `q=...` parsed die gesamte Datei. Bei sehr grossen
  ET-Files (>50k Regeln) sollte das mit einem inkrementellen Index ersetzt
  werden — analog zum EVE-Index in PHIMANTEC.
* SQLite ist single-writer; das reicht fuer < 50 gleichzeitige Benutzer.
* `suricata-update` Pull kann minutenlang dauern — Frontend muss laenger
  warten (Timeout in `services/ruleset_manager.py` auf 600s gesetzt).

## Sicherheits-Checkliste vor Produktion

- [ ] `auth.secret_key` zufaellig generiert
- [ ] `auth.cookie_secure: true` + HTTPS-Reverse-Proxy davor
- [ ] Default-Admin-Passwort geaendert
- [ ] Sudoers-Eintrag prueft (nur explizite Kommandos NOPASSWD)
- [ ] `mock_mode: false`
- [ ] Backup-Strategie fuer `data/suricata-monitor.db` (SQLite VACUUM + rsync)
- [ ] `/etc/suricata/` Verzeichnis fuer Service-User schreibbar

## Hinzufuegen eines neuen Suricata-Schema-Feldes

Datei: `backend/services/config_schema.py`

```python
# In der gewuenschten Sektion ein neues Feld einfuegen:
{
    "path": "stream.async-oneside",       # Dot-Notation gemaess Suricata-YAML
    "type": "bool",                        # bool | enum | int | string | list-of-strings | list-of-objects
    "label": "Async-Oneside",
    "default": False,
    "help": "Aktiviert die Verarbeitung von Flows, "
            "wenn nur eine Richtung gesehen wird.",
},
```

Kein JS-Code-Change noetig — das Frontend rendert die Form dynamisch.

Bei `list-of-objects` (z.B. Interface-Listen) `item_fields` mit dem
Sub-Schema definieren. Beispiel siehe `_CAPTURE` in `config_schema.py`.
