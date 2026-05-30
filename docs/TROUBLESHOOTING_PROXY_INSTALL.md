# Troubleshooting: Proxy-VM Installation

## Symptom (v0.3.5, Docker): `PHIMANTEC_PROXY_PORT=3128 ist bereits belegt`

Auftreten direkt im Preflight, `docker compose ps` bleibt leer, Smoke meldet
`Service laeuft nicht: suricata-node`.

Bedeutung: Auf dem Host laeuft noch ein Prozess auf dem Zielport. Haeufigste
Ursachen nach einem Wechsel von einer **v0.2/v0.3-VM-/Lab-Installation**:
der Host-systemd-Dienst `phimantec-proxyd` (v0.2) auf `0.0.0.0:3128` und/oder
der Host-Dienst `suricata-monitor` (v0.3-VM-Web-UI) auf `0.0.0.0:5050`. Der
Docker-Cleanup entfernt nur Container, nie Host-Dienste.

Pruefen (mit sudo, sonst fehlt der Prozessname):

```bash
sudo ss -ltnp | grep -E ':(3128|5050)\b'
systemctl is-active phimantec-proxyd.service suricata-monitor.service
```

Lösung (sauberer Wechsel v0.2 -> v0.3.x):

```bash
cd "$HOME/phimantec-c11-appliance-v0.3.17-monitor-offline-vendor"
scripts/phimantec_stop_legacy_host_services.sh
scripts/phimantec_deploy.sh --no-cache
# oder in einem Schritt:
# PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
```

Kontrolle der frischen Docker-CA (nicht mehr die alte v0.2-CA):

```bash
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject
# Erwartet: subject=CN = PHIMANTEC C11 Docker Lab Root CA
```

## Symptom: curl meldet `Failed to connect to 127.0.0.1 port 3128`

Bedeutung: `phimantec-proxyd` läuft nicht oder hört nicht auf Port `3128`.

Prüfen:

```bash
sudo ss -ltnp | grep -E ':3128\b' || true
sudo systemctl --no-pager --full status phimantec-proxyd || true
sudo journalctl -u phimantec-proxyd --no-pager -n 120 || true
```

## Symptom: `sha256sum: ... No such file or directory`

Bedeutung: Das erwartete ZIP liegt nicht auf der VM. Häufiger Fehler: Es wurde nach `phimantec-c11-appliance-v0.1.1-lab.zip` gesucht, vorhanden war aber `phimantec-c11-appliance-v0.1.1-lab-patch1.zip` oder `v0.1.0-lab.zip`.

Lösung:

```bash
ls -lah /home/philo | grep phimantec-c11-appliance || true
```

Dann den vorhandenen ZIP-Namen verwenden oder das aktuelle ZIP erneut per `scp` kopieren.

## Symptom: `cd: /home/philo/phimantec-c11-appliance: No such file or directory`

Bedeutung: Das Projekt wurde noch nicht entpackt. Ursache ist meistens ein vorher fehlgeschlagener ZIP-Pfad.

Lösung: Proxy-Install-Skript erneut ausführen. Das Skript findet automatisch das neueste `phimantec-c11-appliance*.zip` in `/home/philo`.

## Symptom: SSH/MobaXterm zeigt `Session stopped`

Ursache: `set -euo pipefail` wurde direkt in der interaktiven Login-Shell gesetzt. Bei Fehlern beendet Bash dann die aktive Shell.

Lösung: Nie lange Blöcke mit `set -euo pipefail` direkt in die Login-Shell pasten. Immer:

```bash
cat >/tmp/script.sh <<'SCRIPT_EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
# Befehle
SCRIPT_EOF
bash /tmp/script.sh
```

## Symptom: Konfigurations-Heredoc frisst Folgekommandos

Wenn ein `sudo tee ... <<EOF` ohne abschließendes `EOF` eingefügt wird, landen Folgekommandos in der Config-Datei. Das robuste Proxy-Install-Skript verwendet eindeutige Marker wie `EOF_CONF`, damit dieser Fehler vermieden wird.

## Symptom: `curl: (77) error setting certificate file`

Bedeutung: `curl` konnte die mit `--cacert` angegebene CA-Datei nicht oeffnen oder nicht als PEM-Zertifikat lesen. Im Lab trat das auf, obwohl `phimantec-proxyd` und der lokale Test-Upstream bereits liefen. Ursache war die geschuetzte Directory-Kette `/etc/phimantec/ca`: die Datei selbst war public-read, aber der lokale Testuser konnte den Pfad nicht traversieren.

Sofortpruefung:

```bash
namei -l /etc/phimantec/ca/phimantec-bump-ca.crt
ls -lah /etc/phimantec /etc/phimantec/ca /etc/phimantec/ca/phimantec-bump-ca.crt /home/philo/phimantec-bump-ca.crt
openssl x509 -in /home/philo/phimantec-bump-ca.crt -noout -subject
```

Sofortfix:

```bash
sudo chmod 0751 /etc/phimantec /etc/phimantec/ca
sudo chmod 0644 /etc/phimantec/ca/phimantec-bump-ca.crt
sudo chmod 0640 /etc/phimantec/ca/phimantec-bump-ca.key
sudo install -m 0644 /etc/phimantec/ca/phimantec-bump-ca.crt /home/philo/phimantec-bump-ca.crt
sudo chown philo:philo /home/philo/phimantec-bump-ca.crt
openssl x509 -in /home/philo/phimantec-bump-ca.crt -noout -subject
```

Der Proxy-Service nutzt weiterhin `/etc/phimantec/ca/phimantec-bump-ca.crt` und `/etc/phimantec/ca/phimantec-bump-ca.key`. Lokale Client-Tests und Win11-Importe verwenden die exportierte Public-CA `/home/philo/phimantec-bump-ca.crt`.

## Patch6: Curl CA-Pfad und EICAR-Test

Für lokale Curl-Tests und Win11-Import ist das korrekte öffentliche Zertifikat:

```text
/home/philo/phimantec-bump-ca.crt
```

Der Pfad unter `/etc/phimantec/ca/` enthält auch den privaten CA-Key und kann je nach Rechten für normale Benutzer nicht traversierbar sein. Deshalb kann dieser Test weiterhin fehlschlagen:

```bash
CA="/etc/phimantec/ca/phimantec-bump-ca.crt"
```

Korrekt ist:

```bash
CA="/home/philo/phimantec-bump-ca.crt"
```

Wenn EICAR im Browser sichtbar ist, müssen beide Hosts auf mindestens `v0.1.6-lab-patch6` aktualisiert werden. Danach muss auf dem Suricata-Host `scripts/repair_suricata_rules_vm.sh` laufen, damit die EICAR-Regel und der neue `inspectord` aktiv sind.
