# PHIMANTEC C11 Appliance v0.3.5 — Diagnose und Korrekturbericht

## Ausgangslage

v0.3.4 baute korrekt und band Proxy/Webmanagement laut `docker-compose.yml`
wieder extern (`0.0.0.0:3128` / `0.0.0.0:5050`). Auf einem Host, der zuvor mit
einer **v0.2-VM-/Lab-Installation** betrieben wurde, scheiterte der Deploy aber
reproduzierbar bereits im Preflight:

```text
[release-context] OK: /home/philo/phimantec-c11-appliance-v0.3.4-monitor-offline-vendor
[preflight] external bind ok: PHIMANTEC_PROXY_PORT=3128
[preflight] external bind ok: PHIMANTEC_MONITOR_PORT=5050
[preflight] ERROR: PHIMANTEC_PROXY_PORT=3128 ist bereits belegt. ...
NAME      IMAGE     COMMAND   SERVICE   CREATED   STATUS    PORTS
[smoke-docker] ERROR: Service laeuft nicht: suricata-node
```

Folgesymptome:

```text
- Webmanagement (5050) von extern nicht erreichbar.
- Kein (v0.3.x-)Zertifikat fuer Clients.
- ~/phimantec-bump-ca.crt traegt CN "PHIMANTEC Lab Bump CA" (= v0.2),
  nicht "PHIMANTEC C11 Docker Lab Root CA" (= v0.3.x-Proxy-Container).
```

## Technische Ursache

Die zwei `external bind ok`-Zeilen sind **kein** Widerspruch zum direkt
folgenden Fehler. `check_external_bind()` prueft nur, ob der konfigurierte
Host-Teil nicht-lokal ist (Textpruefung, kein Socket). Erst `check_port_free()`
testet via `/dev/tcp` einen echten Listener. Es lauschte also tatsaechlich ein
Prozess auf `0.0.0.0:3128`.

Quelle dieses Listeners ist die v0.2-Installation. `install_proxy_vm_ubuntu.sh`
richtet einen **Host-systemd-Dienst** ein:

```text
# packaging/systemd/phimantec-proxyd.service (v0.2)
ExecStart=/usr/local/bin/phimantec-proxyd -c /etc/phimantec/phimantec-proxyd.conf
Restart=on-failure
WantedBy=multi-user.target
# config/phimantec-proxyd.conf (v0.2): bind = "0.0.0.0:3128"
```

Dieser Dienst bleibt nach dem Wechsel auf den Docker-Stack aktiv und belegt
3128. Der Docker-Cleanup (`cleanup_stale_docker.sh`) entfernt jedoch
ausschliesslich alte **Docker-Container** (`phimantec-suricata-node`,
`phimantec-proxy`) und beruehrt Host-systemd-Dienste nie. Damit blockiert v0.2
den v0.3.x-Deploy dauerhaft.

## Warum "kein externer Zugriff" und "kein Zertifikat" dieselbe Wurzel haben

Der Preflight bricht den `redeploy` ab, bevor irgendein Container gebaut/gestartet
wird (`docker compose ps` ist leer). Daraus folgt zwangslaeufig:

```text
- suricata-node (haelt das Webmanagement auf 5050) startet nie -> 5050 extern leer.
- proxy (depends_on: suricata-node healthy) startet nie -> keine TLS-Bump-Leaf-Certs.
- export_docker_ca.sh hat keinen laufenden Proxy -> nur die alte v0.2-CA liegt in ~/.
```

Die Runtime-Konfiguration von v0.3.4 ist dabei korrekt und unveraendert gueltig:
Proxy `bind = "0.0.0.0:3128"`, Monitor `bind_host: "0.0.0.0"`, Compose
publiziert nackte Ports (`3128:3128`, `5050:5050`) auf alle Host-Interfaces.

## Korrektur in v0.3.5 (minimal-invasiv)

Es wurde keine funktionierende Runtime-Logik angefasst. Geaendert wurde nur die
Migrations-/Diagnose-Schicht im Preflight plus ein eigenstaendiges Helferskript:

```text
1. scripts/preflight_docker.sh
   - check_port_free() benennt den belegenden Prozess (ss -ltnp) direkt in der
     Fehlermeldung und verweist auf den sauberen Migrationsweg.
   - Neuer Schritt check_legacy_host_services(): erkennt aktive v0.2-Host-Dienste
     (phimantec-proxyd/-inspectord/-lane-sink). Standard: warnen + abbrechen mit
     klarer Handlungsanweisung. Opt-in: automatisch stoppen+deaktivieren.
   - Eingehaengt in main() vor check_ports().

2. scripts/phimantec_stop_legacy_host_services.sh  (NEU)
   - Idempotenter, eigenstaendiger Migrationsschritt v0.2-Host -> v0.3.x-Docker.
   - Stoppt+deaktiviert phimantec-proxyd/-inspectord/-lane-sink/-test-upstream.

3. .env.example
   - Neuer Schalter PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=0 (dokumentiert).
```

## Verifikation auf dem Zielhost

```bash
# 1) Wer haelt 3128 wirklich?
ss -ltnp | grep ':3128'
systemctl is-active phimantec-proxyd.service

# 2) Sauberer Wechsel v0.2 -> v0.3.x
cd "$HOME/phimantec-c11-appliance-v0.3.5-monitor-offline-vendor"
scripts/phimantec_stop_legacy_host_services.sh        # oder Auto-Stop, s.u.

# 3) Deploy
scripts/phimantec_deploy.sh --no-cache
# alternativ in einem Schritt:
# PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache

# 4) Externe Erreichbarkeit + frische CA
scripts/phimantec_compose.sh ps
curl -sS -m5 "http://$(hostname -I | awk '{print $1}'):5050/" -o /dev/null -w 'monitor extern: %{http_code}\n'
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject   # erwartet: CN = PHIMANTEC C11 Docker Lab Root CA
```

## Hinweis zum .env-Upgrade

Eine aus v0.3.4 stammende `.env` enthaelt `PHIMANTEC_VERSION=0.3.4-...`. Der
Preflight von v0.3.5 erwartet `0.3.5-...` und bricht mit klarer Meldung ab. Das
ist beabsichtigt. Loesung: alte `.env` loeschen (wird aus `.env.example` neu
erzeugt) oder die Zeile manuell auf `0.3.5-monitor-offline-vendor` setzen.
