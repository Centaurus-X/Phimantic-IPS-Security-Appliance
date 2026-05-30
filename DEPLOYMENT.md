# Phimantic-IPS-Security-Appliance

## Deployment Guide

This guide describes a direct rollout of Phimantic-IPS-Security-Appliance on a Linux host with Docker.

## 1. Host preparation

Install the required tools:

```bash
sudo apt-get update
sudo apt-get install -y docker.io docker-compose-plugin curl openssl unzip ca-certificates
sudo usermod -aG docker "$USER"
```

Log out and back in if Docker group membership changed.

Check the important ports:

```bash
ss -ltnp | grep -E ':(3128|5050|18080|18090)\b' || true
```

The public ports are:

```text
3128  proxy listener
5050  web management UI
```

Lane and FlowBus bind locally inside the Docker runtime path.

## 2. Obtain the repository

```bash
git clone https://github.com/<owner>/Phimantic-IPS-Security-Appliance.git
cd Phimantic-IPS-Security-Appliance
export PHIMANTEC_ALLOW_RENAMED_ROOT=1
```

For a release directory that keeps the historical name, the rename override is not required.

## 3. Verify the source bundle

```bash
sha256sum -c MANIFEST.sha256
```

## 4. Create runtime configuration

```bash
rm -f .env
cp .env.example .env
```

Before production use, edit `.env` and change default passwords and shared tokens.

## 5. Deploy containers

```bash
PHIMANTEC_ALLOW_RENAMED_ROOT=1 PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
```

## 6. Export and import the CA

The deployment exports:

```text
~/phimantec-bump-ca.crt
```

Import it into the test browser as a trusted certificate authority for websites.

## 7. Configure the browser

Use this proxy configuration:

```text
HTTP proxy: 127.0.0.1
Port:       3128
```

For deterministic validation, disable HTTP/3 in the browser and keep the appliance in H1.1-only mode.

## 8. Run validation

```bash
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
```

The EICAR matrix must block all test samples and must not leak the EICAR string to the client body.

## 9. Open web management

```text
http://127.0.0.1:5050/
```

Default lab credentials:

```text
admin / changeme
```

Change them immediately after first login.

## 10. Manage rulesets

Use the Rulesets page to edit local rules or trigger runtime reloads. The reload workflow validates the merged Suricata configuration before changing the running detection engine.

Manual reload validation:

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
set -e
suricata -T -c /etc/suricata/suricata.yaml -v
suricatasc -c ruleset-reload-rules /var/run/suricata/suricata-command.socket   || suricatasc -c ruleset-reload-rules   || kill -USR2 "$(pidof suricata)"
'
```

## 11. Stop or remove

```bash
scripts/phimantec_compose.sh down
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
```
