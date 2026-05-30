# PHIMANTEC C11 Appliance v0.3.8 — Diagnose und Korrekturbericht

## Ausgangslage

Nach v0.3.5 wurde `phimantec-proxyd` korrekt gestoppt, Port 3128 war frei
(`[preflight] port frei: PHIMANTEC_PROXY_PORT=3128`). Der Preflight brach jedoch
am naechsten Port ab:

```text
[preflight] port frei: PHIMANTEC_PROXY_PORT=3128
[preflight] ERROR: PHIMANTEC_MONITOR_PORT=5050 ist bereits belegt durch:
            LISTEN 0 4096 0.0.0.0:5050 0.0.0.0:* . ...
```

Auffaellig: Die `ss`-Zeile zeigt **keinen Prozessnamen**. Grund: Der Deploy
laeuft als nicht-root; `ss -ltnp` darf den Owner eines fremden Sockets dann
nicht aufloesen.

## Technische Ursache

`scripts/install_suricata_monitor_vm_ubuntu.sh` richtet einen Host-systemd-
Dienst ein:

```text
# packaging/systemd/suricata-monitor.service
ExecStart=/usr/bin/python3 /opt/phimantec/suricata-monitor/backend/app.py
Environment=SURIMON_CONFIG=/etc/phimantec/suricata-monitor.yaml
# installierte Config: bind_host "0.0.0.0", bind_port 5050
# Installer: systemctl enable --now suricata-monitor.service
```

Dieser Host-Dienst `suricata-monitor` lauscht also auf `0.0.0.0:5050` und
kollidiert mit dem Container-Monitor (der im suricata-node-Container ebenfalls
5050 hält). In v0.3.5 war `suricata-monitor` weder in der Erkennungsliste des
Preflight (`legacy_host_units`) noch in der Stoppliste des Helferskripts
(`LEGACY_UNITS`) enthalten — daher der Folgekonflikt nach dem proxyd-Stopp.

## Korrektur in v0.3.8 (minimal-invasiv)

```text
1. scripts/preflight_docker.sh
   - legacy_host_units(): 'suricata-monitor' ergaenzt.
   - port_owner(): wenn der Prozess-Teil unprivilegiert fehlt, best-effort
     Eskalation per 'sudo -n ss -ltnHp', damit der Owner benannt wird.
   - check_port_free(): Meldung nennt beide Ursachen (3128/proxyd, 5050/
     suricata-monitor) und empfiehlt 'sudo ss -ltnp'.

2. scripts/phimantec_stop_legacy_host_services.sh
   - LEGACY_UNITS um 'suricata-monitor' erweitert.
```

Es wurde keine Runtime-/Build-Logik geaendert.

## Verifikation auf dem Zielhost

```bash
cd "$HOME/phimantec-c11-appliance-v0.3.8-monitor-offline-vendor"

# Wer haelt 5050 wirklich?
sudo ss -ltnp | grep ':5050'
systemctl is-active suricata-monitor.service

# Alt-Host-Dienste (inkl. suricata-monitor) stoppen und deployen:
scripts/phimantec_stop_legacy_host_services.sh
scripts/phimantec_deploy.sh --no-cache
# oder in einem Schritt:
# PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache

# Ergebnis pruefen:
scripts/phimantec_compose.sh ps
HOST_IP="$(hostname -I | awk '{print $1}')"
curl -sS -m5 "http://${HOST_IP}:5050/" -o /dev/null -w 'monitor extern: %{http_code}\n'
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject
# erwartet: CN = PHIMANTEC C11 Docker Lab Root CA
```

## Hinweis

Eine bereits aus v0.3.5 erzeugte `.env` traegt `PHIMANTEC_VERSION=0.3.5-...` und
wird vom v0.3.8-Preflight bewusst abgelehnt. Loesung: `.env` loeschen (wird aus
`.env.example` neu erzeugt) oder die Zeile auf `0.3.8-monitor-offline-vendor`
setzen.
