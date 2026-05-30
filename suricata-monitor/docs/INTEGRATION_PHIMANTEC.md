# SuricataMonitor · Integration in PHIMANTEC-Lab

> Wie SuricataMonitor sich in das existierende 3-Host-Setup
> der PHIMANTEC C11 Appliance einfuegt.

## Bestehende Topologie (PHIMANTEC v0.2.0-lab)

```
┌────────────────────┐    ┌────────────────────┐    ┌──────────────────────┐
│ Win11 Client       │    │ Proxy-Host         │    │ Suricata-Host        │
│ 192.168.0.233      │───▶│ 192.168.0.232      │───▶│ 192.168.0.236        │
│ Browser            │    │ phimantec-proxyd   │    │ Suricata 8 + Hyperscan│
│                    │    │                    │    │ phimantec-lane-sink  │
│                    │    │                    │    │ phimantec-inspectord │
└────────────────────┘    └────────────────────┘    └──────────────────────┘
```

SuricataMonitor erweitert das, **ohne den Datenpfad zu beruehren**:

```
                                                  ┌──────────────────────┐
                                                  │ Suricata-Host        │
                                          ┌──────▶│ + suricata-monitor:5050  │
                                          │       │   (Flask, lokal)     │
                                          │       └──────────────────────┘
                          ┌───────────────┴──┐
        Admin-Browser ───▶│ Reverse-Proxy    │
                          │ HTTPS / nginx    │
                          └──────────────────┘
```

Der Admin laeuft *neben* dem normalen Lab-Traffic ueber HTTPS auf den
Suricata-Host. Empfohlen ist ein nginx davor, der TLS und Basic-Auth fuer den
SuricataMonitor-Port macht.

## Deployment auf dem Suricata-Host

```bash
# auf 192.168.0.236
cd /opt
sudo unzip suricata-monitor.zip
cd suricata-monitor
sudo bash scripts/install.sh
```

Anschliessend in `/etc/suricata-monitor/suricata-monitor.yaml`:

```yaml
server:
  bind_host: "127.0.0.1"   # nur via nginx erreichbar
  bind_port: 5050

suricata:
  socket_path: "/var/run/suricata/suricata-command.socket"
  eve_path:    "/var/log/suricata/eve.json"
  config_path: "/etc/suricata/suricata.yaml"
  rules_dir:   "/etc/suricata/rules"
  update_bin:  "/usr/bin/suricata-update"
  service_unit: "suricata.service"
  mock_mode: false           # echter Suricata vorhanden
```

## Suricata vorbereiten

Damit `suricatasc` ueber den Socket funktioniert, in `/etc/suricata/suricata.yaml`:

```yaml
unix-command:
  enabled: yes
  filename: /var/run/suricata/suricata-command.socket
```

Suricata neu starten:

```bash
sudo systemctl restart suricata
```

Pruefen:

```bash
sudo suricatasc -c "uptime"
```

## Zugriff auf eve.json

Wenn Suricata mit User `suricata` laeuft, hat dieser meist `0640`-Rechte auf
`/var/log/suricata/eve.json`. Damit SuricataMonitor auch ohne sudo lesen kann,
am einfachsten:

```bash
sudo usermod -a -G suricata suricata-monitor
sudo chmod g+r /var/log/suricata/eve.json
sudo systemctl restart suricata-monitor
```

## Sudoers fuer Service-Control

Vom Installer bereits gesetzt:

```
suricata-monitor ALL=(root) NOPASSWD: /bin/systemctl restart suricata.service
suricata-monitor ALL=(root) NOPASSWD: /bin/systemctl reload suricata.service
suricata-monitor ALL=(root) NOPASSWD: /bin/systemctl is-active suricata.service
```

## Nginx-Reverse-Proxy (Beispiel)

`/etc/nginx/sites-available/suricata-monitor`:

```nginx
server {
    listen 443 ssl http2;
    server_name suricata-monitor.lab.local;

    ssl_certificate     /etc/ssl/certs/suricata-monitor.crt;
    ssl_certificate_key /etc/ssl/private/suricata-monitor.key;

    # Optional: zusaetzliche Basic-Auth-Hardening
    # auth_basic "SuricataMonitor Admin";
    # auth_basic_user_file /etc/nginx/.htpasswd-suricata-monitor;

    location / {
        proxy_pass http://127.0.0.1:5050;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
        client_max_body_size 8m;
    }
}
```

In `/etc/suricata-monitor/suricata-monitor.yaml` dann zwingend:

```yaml
auth:
  cookie_secure: true
```

## Verhaeltnis zu PHIMANTEC-Komponenten

| PHIMANTEC-Komponente | SuricataMonitor | Anmerkung |
|---|---|---|
| phimantec-proxyd | UNABHAENGIG | SuricataMonitor greift nicht in Proxy-Pfad ein |
| phimantec-lane-sink | gleicher Host, gleicher Suricata | SuricataMonitor liest dieselbe `eve.json` |
| phimantec-inspectord | UNABHAENGIG | inspectord macht FlowBus-Verdicts; SuricataMonitor ist nur fuer Ops |
| Suricata 8 + Hyperscan | wird von SuricataMonitor gesteuert | reload-rules, restart, config-edit |
| Lab-Rules (SID 900001-3) | sichtbar in Rulesets-Tab | als `phimantec-local.rules` |

## Was SuricataMonitor **NICHT** macht

* Es ist **kein Ersatz fuer inspectord**. inspectord bedient die FlowBus, SuricataMonitor nur den HTTP-Admin-Port.
* Es **modifiziert nicht** den decrypted Lane-Stream.
* Es **liest keine PHFB-Frames** — der Verdict-Pfad bleibt unberuehrt.
* Es **veroeffentlicht keine eigenen Alerts** an die FlowBus.

So bleibt die Sicherheits-Architektur (decrypted-traffic-isolation, Suricata
als alleinige Quelle der Wahrheit) erhalten.

## Failure-Modes im Setup

| Beobachtung | Vermutete Ursache | Schritt |
|---|---|---|
| Dashboard zeigt `status: unknown` | sudoers-Eintrag fehlt | `sudoers.d/suricata-monitor` pruefen |
| Events leer obwohl Alerts da | eve.json nicht lesbar | `sudo -u suricata-monitor cat /var/log/suricata/eve.json | head` |
| `suricatasc` Aufrufe scheitern | Socket nicht aktiv | `ls -l /var/run/suricata/`, Suricata-Config `unix-command.enabled` |
| `suricata-update` Aktionen scheitern | Tool nicht installiert | `apt install suricata-update` |
| Config-Save bricht ab | suricata -T validate failed | Backup in `suricata.yaml.bak.*` ist vorhanden |

Mehr: [`DEBUGGING.md`](DEBUGGING.md).

## v0.3.3 Repository-Integration

SuricataMonitor liegt nun direkt unter `suricata-monitor/` im PHIMANTEC
Repository. Fuer die Appliance stehen zwei Startpfade zur Verfuegung:

```bash
# Docker-Lab
cp .env.example .env
docker compose up -d --build

# VM-/Host-Installation auf dem Suricata-Host
sudo bash scripts/install_suricata_monitor_vm_ubuntu.sh
```

Neu ist der Container-/Socket-Control-Modus:

```yaml
suricata:
  control_mode: "socket"
```

Damit funktionieren Status und `reload-rules` auch in Containern ohne systemd.
Ein voller Restart wird in diesem Modus bewusst nicht simuliert; dafuer wird der
Container neu gestartet.

Der Form-Editor zeigt PHIMANTEC-Hinweise fuer Einstellungen, die den Proxy-
Lane-Betrieb beeinflussen koennen. Diese Einstellungen bleiben bewusst
editierbar, damit SuricataMonitor auch in anderen Suricata-Kontexten nutzbar
bleibt.
