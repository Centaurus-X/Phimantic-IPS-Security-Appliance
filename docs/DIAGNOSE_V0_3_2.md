# Diagnose v0.3.3 — Docker, Ubuntu-APT, SuricataMonitor, Offline-Bundle

## Ausgangsfehler

Der gemeldete Rollout brach im Docker-Build waehrend `apt-get update` und
anschliessendem `apt-get install` ab. Der Build-Container konnte die Ubuntu-
Hosts `archive.ubuntu.com` und `security.ubuntu.com` nicht aufloesen. Dadurch
waren die Paketindizes leer oder unbrauchbar und selbst Basispakete wie
`ca-certificates`, `build-essential`, `cmake`, `openssl`, `tini` und
`suricata` wurden nicht gefunden.

Zusatzproblem im alten Dockerfile: Die Kette

```sh
... && usermod -aG adm phimantec || true && install -d ...
```

kann nach einem vorherigen Fehler aufgrund der Shell-Prioritaet von `&&`/`||`
weiterlaufen. Genau dadurch wirkt der Build so, als wuerde er nach einem
APT-Fehler noch Verzeichnisse reparieren wollen. In v0.3.3 ist die optionale
`usermod`-Stelle gekapselt:

```sh
&& { usermod -aG adm phimantec || true; } \
&& install -d ...
```

## Ursachenklasse

```text
1. Docker-DNS / Build-Netzwerk instabil oder gesperrt
2. APT wartet minutenlang auf Standard-Repositories
3. APT-Update-Warnungen koennen in leeren Indizes enden
4. Shell-Kette mit ungekapseltem `|| true` laesst Folgekommandos laufen
5. Docker-Runtime installierte Python-Abhaengigkeiten ueber PyPI statt Ubuntu
6. Entrypoint ueberwachte Prozesse per while-true-Schleife
7. Default `hs` konnte Suricata ohne Hyperscan unnoetig in Fallback-Pfade bringen
```

## Korrekturen

```text
- docker/common/apt_install.sh hinzugefuegt
- APT-Repositories fail-fast: Retries=0, Timeout default 12s, optional ForceIPv4
- docker-compose.yml build.network default host
- PHIMANTEC_APT_MIRROR und PHIMANTEC_APT_SECURITY_MIRROR als Build-Args
- Docker Compose Image-Tags per PHIMANTEC_VERSION
- SuricataMonitor im Container nutzt python3-flask + python3-yaml aus Ubuntu
- Python-Venv/PyPI im normalen Docker-Runtime-Build entfernt
- Suricata-Entrypoint nutzt wait -n und beendet bei Kindprozess-Ende sauber
- Default mpm/spm im Docker-/VM-Profil: auto
- Hyperscan-Fallback nur einmalig und nur bei explizitem hs-Wunsch
- systemd-Units minimalisiert und durch StartLimitBurst begrenzt
- .dockerignore fuer kleine Build-Kontexte
- scripts/verify_docker_static.sh hinzugefuegt
```

## Minimaler Docker-Test

```bash
cp .env.example .env
docker compose build --no-cache
docker compose up -d
docker compose ps
curl -fsS http://127.0.0.1:5050/ >/dev/null
```

## Bei Docker-DNS-Problemen

Standard in `.env`:

```env
PHIMANTEC_DOCKER_BUILD_NETWORK=host
PHIMANTEC_APT_RETRIES=0
PHIMANTEC_APT_TIMEOUT=12
PHIMANTEC_APT_FORCE_IPV4=1
```

Alternative, falls Build-Host-Netzwerk nicht erlaubt ist:

```env
PHIMANTEC_DOCKER_BUILD_NETWORK=default
PHIMANTEC_APT_MIRROR=http://de.archive.ubuntu.com/ubuntu
PHIMANTEC_APT_SECURITY_MIRROR=http://security.ubuntu.com/ubuntu
```

## Erwartete Ports

```text
3128  PHIMANTEC Proxy
5050  SuricataMonitor Web-UI
18080 Lane-Sink, standardmaessig nur 127.0.0.1 gebunden
18090 FlowBus, standardmaessig nur 127.0.0.1 gebunden
```

## Nicht in diesem Repository geloest

Dieses schlanke Repository enthaelt weiterhin keine grossen Suricata-Archive,
keine Hyperscan-Source-Trees, keine Ubuntu/glibc-Systembasis und keine fertigen
Docker-Image-TARs. Diese Artefakte gehoeren in getrennte Runtime-/Air-Gap-
Bundles.
