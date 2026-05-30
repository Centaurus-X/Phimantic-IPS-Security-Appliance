# SuricataMonitor · Debugging Playbook

> Systematischer Fehler-Suchpfad bei den haeufigsten Lab-Problemen.
> Erst die Symptome, dann die Diagnose, dann der Fix.

## 1. Logs zuerst

```bash
# Backend-Logs (wenn als Service):
journalctl -u suricata-monitor -f --since "10 min ago"

# Backend-Logs (Dev-Mode):
# direkt in der Konsole, in der run_dev.sh laeuft

# Browser-Konsole:
F12 -> Console + Network Tab
```

## 2. Symptom-Tabelle

| Symptom | Diagnose-Schritt | Vermutete Ursache | Fix |
|---|---|---|---|
| `502 Bad Gateway` von nginx | `curl http://127.0.0.1:5050/api/v1/auth/whoami` direkt am Suricata-Host | Backend nicht gestartet / Port falsch | `systemctl status suricata-monitor`, `bind_port` in YAML pruefen |
| `401 Unauthenticated` direkt nach Login | DevTools → Application → Cookies pruefen | Cookie nicht gesetzt (z.B. wegen cross-origin) | Frontend muss von gleicher Origin wie Backend ausgeliefert werden, oder `cookie_secure` mit HTTPS verwenden |
| Login erfolgreich, dann sofort Redirect zu `/login` | `SGApi.whoami()`-Response pruefen | JWT abgelaufen, oder DB-Sessions-Tabelle nicht migiriert | `sqlite3 data/suricata-monitor.db ".schema sessions"` prufen |
| Dashboard zeigt `mock_mode` Demo-Daten | `cat /etc/suricata-monitor/suricata-monitor.yaml | grep mock_mode` | Mock-Modus aktiv | `mock_mode: false` setzen + `systemctl restart suricata-monitor` |
| Service-Status `unknown` | `sudo -u suricata-monitor systemctl is-active suricata.service` als ServiceUser | sudoers-Eintrag fehlt | `/etc/sudoers.d/suricata-monitor` ergaenzen |
| Events-Tabelle leer, aber Suricata schlaegt an | `sudo -u suricata-monitor cat /var/log/suricata/eve.json | head` | Datei nicht lesbar fuer SuricataMonitor-User | `chmod g+r /var/log/suricata/eve.json` + Gruppen-Mitgliedschaft |
| `suricatasc`-Aufrufe failen mit `socket not found` | `ls -l /var/run/suricata/` | `unix-command: enabled: no` in suricata.yaml | `enabled: yes` setzen + Suricata neu starten |
| `suricata-update list-sources` schlaegt fehl | `which suricata-update` | Tool nicht installiert | `apt install suricata-update` |
| Rule-Toggle ohne Effekt im IDS | nach Toggle: `suricatasc -c "reload-rules"` | reload-rules wurde nicht getriggert | im UI **Rulesets → reload-rules** druecken oder `/rules/reload` per API |
| Config-Save bricht ab mit "validation_failed" | Suricata-Fehler-Output aus der Toast-Nachricht | YAML-Syntax-Fehler oder unbekannte Direktive | Backup in `suricata.yaml.bak.<timestamp>` zurueckspielen |
| Frontend laed CSS nicht (kein Theme) | DevTools → Network → 404 auf `/css/...` | Static-Path falsch | `frontend/`-Ordner liegt parallel zu `backend/`? Strukturpfad in `app.py` pruefen |
| Theme-Switch wirkt nicht | `localStorage.getItem("suricata-monitor.theme")` in Console | Werte falsch | `localStorage.removeItem("suricata-monitor.theme")` und neu setzen |
| `/api/v1/events?stage=pending` liefert immer leer | DB-Check: `sqlite3 data/suricata-monitor.db "SELECT COUNT(*) FROM events"` | Sync von eve.json hat nicht ausgeloest | erstes List-API-Call triggert Sync; bei mock_mode kommen 30 Eintraege automatisch |
| 500 Internal Server Error | `journalctl -u suricata-monitor --since "1 min ago"` | Python-Traceback | Stack im Log analysieren, meistens Pfad-Permission |

## 3. Mock-Modus als Diagnose-Werkzeug

Bei jedem Fall, wo unklar ist ob das Problem **im UI** oder **in der Suricata-Anbindung** liegt:

```bash
sudo sed -i 's/mock_mode: false/mock_mode: true/' /etc/suricata-monitor/suricata-monitor.yaml
sudo systemctl restart suricata-monitor
```

Jetzt sollte das UI mit Demo-Daten arbeiten:
* Liefert die UI ploetzlich Daten? → Problem ist die Suricata-Integration (Sockets, Pfade, Rechte).
* Liefert die UI immer noch nichts? → Problem ist im UI / im Backend selbst.

## 4. Datenbank-Notreparatur

```bash
# DB-Integritaet pruefen
sqlite3 /var/lib/suricata-monitor/suricata-monitor.db "PRAGMA integrity_check"

# Default-Admin neu setzen (wenn Login total verloren)
python3 -c "
import sqlite3, sys
sys.path.insert(0, '/opt/suricata-monitor/backend')
from auth import hash_password
conn = sqlite3.connect('/var/lib/suricata-monitor/suricata-monitor.db')
conn.execute('UPDATE users SET password_hash=?, active=1 WHERE username=?',
             (hash_password('changeme'), 'admin'))
conn.commit()
print('Admin-Passwort auf changeme gesetzt')
"

# Alle Sessions revoken (Force-Logout aller Benutzer)
sqlite3 /var/lib/suricata-monitor/suricata-monitor.db "UPDATE sessions SET revoked = 1"

# DB komplett zuruecksetzen (nur im Lab!)
sudo systemctl stop suricata-monitor
sudo rm /var/lib/suricata-monitor/suricata-monitor.db
sudo systemctl start suricata-monitor   # legt frische DB an
```

## 5. API-Direkttests mit curl

```bash
# Variable fuer Cookie-Jar
JAR=/tmp/sg-jar.txt

# Login
curl -s -c $JAR -X POST http://localhost:5050/api/v1/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"admin","password":"changeme"}' | jq .

# Whoami
curl -s -b $JAR http://localhost:5050/api/v1/auth/whoami | jq .

# Dashboard
curl -s -b $JAR http://localhost:5050/api/v1/dashboard/overview | jq .

# Service-Status (sollte 'active' liefern)
curl -s -b $JAR http://localhost:5050/api/v1/settings/service/status | jq .

# Rule-Files
curl -s -b $JAR http://localhost:5050/api/v1/rules/files | jq .

# Iface-Stats (echte Suricata-Anbindung)
curl -s -b $JAR 'http://localhost:5050/api/v1/dashboard/iface-stats?iface=ens18' | jq .
```

Wenn alle direkten API-Calls funktionieren, aber das UI nicht: Browser-Cache leeren
(Ctrl+Shift+R) oder DevTools → Network deaktivieren Cache.

## 6. Suricata-Side-Checks

```bash
# Lebt der Socket?
sudo -u suricata-monitor suricatasc -c "version"
# Ausgabe: {"message": "8.0.0", "return": "OK"}

# Wie viele Regeln sind geladen?
sudo -u suricata-monitor suricatasc -c "ruleset-stats"

# EVE-Datei waechst?
watch -n1 'ls -l /var/log/suricata/eve.json'

# Letzter Alert
tail -1 /var/log/suricata/eve.json | jq 'select(.event_type=="alert")'

# Suricata-Konfig wuerde validieren?
sudo -u suricata-monitor suricata -T -c /etc/suricata/suricata.yaml
```

## 7. Performance-Checks

```bash
# CPU des Backend-Prozesses
ps -o pid,rss,pcpu,comm -C python3 | grep suricata-monitor

# Anzahl offener Verbindungen
ss -lntp | grep 5050

# SQLite-DB-Groesse
ls -lh /var/lib/suricata-monitor/suricata-monitor.db

# Vacuum, wenn DB stark gewachsen
sudo systemctl stop suricata-monitor
sudo -u suricata-monitor sqlite3 /var/lib/suricata-monitor/suricata-monitor.db "VACUUM"
sudo systemctl start suricata-monitor
```

## 8. Browser-Debugging

```javascript
// In der DevTools-Console (jederzeit verfuegbar):

// Aktueller User-Status
window.SGCurrentUser

// API-Call manuell
await SGApi.get("/dashboard/overview")

// Toast manuell triggern
SGComponents.toast("Hallo", "success")

// Theme manuell wechseln
SGTheme.toggle()

// Router-Status
SGRouter.current()

// Manuelle Navigation
SGRouter.navigate("events", { stage: "pending" })
```

## 9. Bekannte Fallstricke

* **Pfade in install.sh**: Wenn nicht aus dem Projekt-Root aufgerufen, schlaegt der Kopier-Schritt fehl. Immer aus dem entpackten Verzeichnis starten.
* **Path-Traversal-Schutz**: Rule-Dateinamen mit `/` oder beginnend mit `.` werden geblockt — also keine "subdir/foo.rules"; alle Files muessen direkt im `rules_dir` liegen.
* **suricata-update braucht Internet**: Im air-gapped Lab muss man die Rules-Tarballs manuell ins `update_bin`-Cache spielen oder Mirror-Quellen konfigurieren.
* **Cookie und sub-domains**: Wenn nginx auf `suricata-monitor.lab.local` und Backend auf `localhost`, ist das fuer den Browser eine Origin. SameSite=Lax greift OK. Wenn jemand das aufteilt, muss `cookie_secure` + `samesite="Strict"` neu evaluiert werden (in `auth_routes.py`).

## 10. Wenn nichts hilft

Diagnose-Tarball fuer einen Bug-Report:

```bash
sudo tar czf /tmp/suricata-monitor-diag.tar.gz \
    /etc/suricata-monitor/suricata-monitor.yaml \
    /var/log/suricata/eve.json \
    /etc/suricata/suricata.yaml \
    <(journalctl -u suricata-monitor --since "1 day ago") \
    /var/lib/suricata-monitor/suricata-monitor.db
```

Vor dem Versenden Passwoerter aus `suricata-monitor.yaml` rausnehmen!
