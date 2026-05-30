# SuricataMonitor · REST-API

Alle Endpunkte unterhalb `/api/v1`. Authentifizierung per HTTP-only-Cookie
oder `Authorization: Bearer <token>` Header.

## Auth

| Methode | Pfad | Body / Params | Rolle | Beschreibung |
|---|---|---|---|---|
| POST | `/auth/login` | `{username, password}` | – | Loggt ein, setzt Cookie |
| POST | `/auth/logout` | – | beliebig | Revoked Session, loescht Cookie |
| GET  | `/auth/whoami` | – | beliebig | Liefert aktuelles User-Objekt |

## Dashboard

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET | `/dashboard/overview` | viewer | Service-Status, EVE-Stats, Top-Rules |
| GET | `/dashboard/iface-stats?iface=ens18` | viewer | Interface-Statistik per `suricatasc` |

## Events

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET    | `/events?stage=&severity=&q=&limit=&offset=` | viewer | Paginierte Event-Liste |
| GET    | `/events/<id>` | viewer | Event-Detail + History |
| POST   | `/events/<id>/process` | analyst | `{stage, note, take_ownership}` |

Beispiel-Filter-Parameter:

* `stage=pending|investigation|completed`
* `severity=1|2|3` (1=high)
* `q=<string>` matched gegen `rule_msg`, `src_ip`, `dest_ip`, exact `rule_id`

## Rule-Files

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET    | `/rules/files` | viewer | Verzeichnis-Inventur |
| GET    | `/rules/files/<filename>?limit=&offset=&q=` | viewer | Regeln einer Datei |
| POST   | `/rules/files/<filename>/toggle` | analyst | `{sid, enabled}` |
| POST   | `/rules/files/<filename>/rule` | analyst | `{raw: "alert tcp ..."}` |
| DELETE | `/rules/files/<filename>/rule/<sid>` | admin | Regel loeschen |
| POST   | `/rules/reload` | analyst | `suricatasc reload-rules` |

## Rulesets (Pattern-Quellen)

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET  | `/rulesets/sources` | viewer | `suricata-update list-sources` |
| POST | `/rulesets/sources/<name>/enable` | admin | aktivieren |
| POST | `/rulesets/sources/<name>/disable` | admin | deaktivieren |
| POST | `/rulesets/sources/update` | admin | Source-Index neu laden |
| POST | `/rulesets/update` | admin | Regeln tatsaechlich pullen |

## Settings

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET  | `/settings/suricata` | viewer | Raw-YAML-Inhalt lesen (bewahrt Kommentare) |
| POST | `/settings/suricata` | admin | `{content}` — atomarer Raw-Write + Validate |
| GET  | `/settings/suricata/structured` | viewer | **NEU**: Schema + aktuelle Werte fuer Form-UI |
| POST | `/settings/suricata/structured` | admin | **NEU**: `{updates: {path: value}}` — gezielte Pfad-Updates |
| POST | `/settings/suricata/validate` | analyst | `suricata -T` |
| GET  | `/settings/suricata/summary` | viewer | wichtige Settings extrahiert |
| POST | `/settings/service` | admin | `{action: "reload"\|"restart"}` |
| GET  | `/settings/service/status` | viewer | `systemctl is-active` |

### `/settings/suricata/structured` (neue Schema-API)

**GET** liefert:

```json
{
  "ok": true,
  "schema": {
    "version": "1.0",
    "sections": [
      {
        "key": "general",
        "label": "Allgemein",
        "fields": [
          {"path": "default-log-dir", "type": "string",
           "label": "Log-Verzeichnis", "default": "/var/log/suricata",
           "help": "…"},
          {"path": "runmode", "type": "enum",
           "options": ["autofp", "workers", "single"],
           "default": "autofp", "label": "Run-Modus"}
        ]
      },
      ...
    ]
  },
  "values": {
    "default-log-dir": "/var/log/suricata",
    "runmode": "workers",
    "app-layer.protocols.http.enabled": true,
    "outputs[eve-log].types[alert].enabled": true,
    "af-packet": [{"interface": "ens18", "cluster-id": 99, ...}]
  },
  "warnings": ["nfqueue (IPS-Mode) konfiguriert - vorsichtig editieren."]
}
```

**POST** Body:

```json
{
  "updates": {
    "runmode": "workers",
    "app-layer.protocols.http.enabled": false,
    "outputs[eve-log].types[dns].enabled": true,
    "af-packet": [
      {"interface": "ens18", "cluster-id": 99, "cluster-type": "cluster_flow"}
    ]
  }
}
```

Response:

```json
{
  "ok": true,
  "applied": 4,
  "failed_paths": [],
  "backup": "/etc/suricata/suricata.yaml.bak.20260525-150000",
  "path": "/etc/suricata/suricata.yaml"
}
```

Hinweis: structured-write schreibt die ganze YAML neu. **Kommentare gehen
verloren** — Backup wird automatisch angelegt. Vor Commit wird
`suricata -T` ausgefuehrt, bei Fehler wird nichts geschrieben.

## Users

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET    | `/users` | admin | Liste |
| POST   | `/users` | admin | `{username, password, role, ...}` |
| GET    | `/users/<id>` | admin | Detail |
| PATCH  | `/users/<id>` | admin | `{role, full_name, email, active}` |
| POST   | `/users/<id>/password` | admin | `{password}` — Reset (revoked Sessions) |
| DELETE | `/users/<id>` | admin | Loeschen (nicht self) |
| POST   | `/users/me/password` | beliebig | `{old_password, new_password}` |

## Antwort-Format

Erfolg:

```json
{ "ok": true, "data...": ... }
```

Fehler:

```json
{ "error": "code", "message": "Beschreibung" }
```

HTTP-Statuscodes:

* `200` Erfolg
* `400` Ungueltige Eingabe (`error: "invalid_*"`)
* `401` Nicht authentifiziert (`error: "unauthenticated"`)
* `403` Falsche Rolle (`error: "forbidden"`)
* `404` Nicht gefunden (`error: "not_found"`)
* `409` Konflikt (z.B. `error: "username_taken"`)

## Beispiel-Calls

```bash
# Login
curl -c jar.txt -X POST http://localhost:5050/api/v1/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"admin","password":"changeme"}'

# Whoami
curl -b jar.txt http://localhost:5050/api/v1/auth/whoami

# Top Events
curl -b jar.txt 'http://localhost:5050/api/v1/events?stage=pending&limit=10'

# Regel aktivieren
curl -b jar.txt -X POST \
     'http://localhost:5050/api/v1/rules/files/phimantec-local.rules/toggle' \
     -H "Content-Type: application/json" \
     -d '{"sid": 900001, "enabled": true}'

# ET-Pattern-Quelle aktivieren
curl -b jar.txt -X POST \
     'http://localhost:5050/api/v1/rulesets/sources/et%2Fopen/enable'

# Rules pullen
curl -b jar.txt -X POST http://localhost:5050/api/v1/rulesets/update
```

## System

| Methode | Pfad | Rolle | Beschreibung |
|---|---|---|---|
| GET | `/system/interfaces` | viewer | **NEU**: Netzwerk-Interfaces des Hosts (fuer Form-Dropdowns) |

### `/system/interfaces`

Liest `/sys/class/net/` aus. Im Mock-Modus liefert es Demo-Interfaces.

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

Wird vom Settings-Frontend beim Rendern von Interface-Feldern (`type: "interface-select"`) abgerufen und in eine HTML5-`<datalist>` befuellt — User koennen waehlen oder frei eingeben.
