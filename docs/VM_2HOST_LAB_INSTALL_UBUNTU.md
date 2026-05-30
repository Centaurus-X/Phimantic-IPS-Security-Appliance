# PHIMANTEC 2-Host Lab Quickstart für Ubuntu 24.04/22.04

Dieses Dokument beschreibt den stabilen Copy-&-Paste-Weg für zwei Linux-VMs:

```text
win11-client : 192.168.0.233
proxy        : 192.168.0.232
suricata     : 192.168.0.236
linux user   : philo
```

## Wichtige Bedienregel

`set -euo pipefail` niemals direkt in der interaktiven SSH-Login-Shell setzen. Wenn danach ein Befehl wie `sha256sum`, `cd` oder `suricata -T` fehlschlägt, kann die Login-Shell beendet werden und der SSH-Client zeigt `Session stopped`.

Stattdessen immer ein Skript erzeugen und mit `bash /tmp/name.sh` ausführen. Dann beendet sich bei Fehlern nur das Skript, nicht die SSH-Sitzung.

## Suricata-Host prüfen

Auf `192.168.0.236`:

```bash
sudo ss -ltnp | grep -E ':(18080|18090)\b' || true
journalctl -u suricata-phimantec -u phimantec-lane-sink -u phimantec-inspectord --no-pager -n 80
```

Korrekt ist:

```text
0.0.0.0:18080 phimantec-lane-sink
0.0.0.0:18090 phimantec-inspectord
suricata-phimantec active/running
Suricata Engine started
```

## Proxy-Host installieren oder reparieren

Vorher muss mindestens ein Projekt-ZIP in `/home/philo` liegen, zum Beispiel:

```text
/home/philo/phimantec-c11-appliance-v0.1.2-lab-patch2.zip
/home/philo/phimantec-c11-appliance-v0.1.1-lab-patch1.zip
/home/philo/phimantec-c11-appliance-v0.1.0-lab.zip
```

Auf `192.168.0.232`:

```bash
cat >/tmp/phimantec-proxy-install.sh <<'SCRIPT_EOF'
#!/usr/bin/env bash
set -Eeuo pipefail

PROXY_IP="192.168.0.232"
SURICATA_IP="192.168.0.236"
WIN11_IP="192.168.0.233"
TOKEN="phimantec-lab-token-2026-05-24-change-me"
PROJECT_DIR="/home/philo/phimantec-c11-appliance"
ZIP_DIR="/home/philo"
REGENERATE_CA="0"
START_TEST_UPSTREAM="1"
RUN_SMOKE="1"

export PROXY_IP SURICATA_IP WIN11_IP TOKEN PROJECT_DIR ZIP_DIR REGENERATE_CA START_TEST_UPSTREAM RUN_SMOKE

if [[ -x "$PROJECT_DIR/scripts/install_proxy_vm_ubuntu.sh" ]]; then
  exec bash "$PROJECT_DIR/scripts/install_proxy_vm_ubuntu.sh"
fi

ZIP_FILE=""
for candidate in \
  "$ZIP_DIR/phimantec-c11-appliance-v0.1.2-lab-patch2.zip" \
  "$ZIP_DIR/phimantec-c11-appliance-v0.1.1-lab-patch1.zip" \
  "$ZIP_DIR/phimantec-c11-appliance-v0.1.1-lab.zip" \
  "$ZIP_DIR/phimantec-c11-appliance-v0.1.0-lab.zip"
do
  if [[ -f "$candidate" ]]; then
    ZIP_FILE="$candidate"
    break
  fi
done

if [[ -z "$ZIP_FILE" ]]; then
  ZIP_FILE="$(find "$ZIP_DIR" -maxdepth 1 -type f -name 'phimantec-c11-appliance*.zip' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1 {print $2}')"
fi

if [[ -z "$ZIP_FILE" || ! -f "$ZIP_FILE" ]]; then
  echo "FEHLER: Kein phimantec-c11-appliance*.zip in $ZIP_DIR gefunden." >&2
  exit 20
fi

sudo apt-get update
sudo apt-get install -y unzip
rm -rf "$PROJECT_DIR"
unzip -q "$ZIP_FILE" -d "$(dirname "$PROJECT_DIR")"
exec bash "$PROJECT_DIR/scripts/install_proxy_vm_ubuntu.sh"
SCRIPT_EOF

chmod +x /tmp/phimantec-proxy-install.sh
bash /tmp/phimantec-proxy-install.sh 2>&1 | tee /tmp/phimantec-proxy-install.log
```

## Proxy-Diagnose

Auf `192.168.0.232`:

```bash
bash /home/philo/phimantec-c11-appliance/scripts/proxy_diagnose_vm.sh 2>&1 | tee /tmp/phimantec-proxy-diagnose.log
```

## Erwartete Proxy-Ausgabe

```text
0.0.0.0:3128 phimantec-proxyd
127.0.0.1:8443 openssl
allow_http_code=200
suricata_block_http_code=403
```

Hinweis zum Suricata-Blocktest: `phimantec-inspectord` wartet bei `/suricata-block` im Lab kurz auf den passenden EVE-Alert. Dadurch ist der Test wesentlich deterministischer, ohne jede saubere Allow-Anfrage künstlich zu verzögern.

## Win11 CA holen

Auf Win11 PowerShell:

```powershell
scp philo@192.168.0.232:/home/philo/phimantec-bump-ca.crt .
certutil -user -addstore Root .\phimantec-bump-ca.crt
Test-NetConnection 192.168.0.232 -Port 3128
```

## Patch6 Kurztest

Proxy-Host:

```bash
CA="/home/philo/phimantec-bump-ca.crt"
curl --http1.1 -sS --noproxy "" --proxy http://127.0.0.1:3128 --cacert "$CA" -o /tmp/phimantec-allow.html -w "allow_http_code=%{http_code}\n" https://127.0.0.1:8443/
curl --http1.1 -sS --noproxy "" --proxy http://127.0.0.1:3128 --cacert "$CA" -o /tmp/phimantec-suricata-block.html -w "suricata_block_http_code=%{http_code}\n" https://127.0.0.1:8443/suricata-block || true
```

Erwartung:

```text
allow_http_code=200
suricata_block_http_code=403
```

Win11 importiert immer die Datei vom Proxy-Host:

```powershell
scp philo@192.168.0.232:/home/philo/phimantec-bump-ca.crt .
certutil -user -addstore Root .\phimantec-bump-ca.crt
```
