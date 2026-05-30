# PHIMANTEC C11 Appliance v0.3.4 — Diagnose und Korrekturbericht

## Ausgangslage

v0.3.3 baute erfolgreich und die Container starteten. Die Laufzeitdiagnose zeigte aber:

```text
127.0.0.1:33128->3128/tcp
127.0.0.1:5050->5050/tcp
[smoke-docker] external proxy check skipped
```

Damit waren Proxy und Webmanagement nur lokal am Docker-Host erreichbar. Von außen war kein Zugriff möglich.

## Technische Ursache

Der C-Proxy selbst hörte im Container korrekt auf allen Interfaces:

```text
phimantec-proxyd listening on 0.0.0.0:3128
```

Die Blockade lag in `docker-compose.yml` und `.env.example`: v0.3.3 setzte hostseitige Docker-Publish-Bindings auf Loopback:

```text
PHIMANTEC_PROXY_PORT=127.0.0.1:33128
PHIMANTEC_MONITOR_PORT=127.0.0.1:5050
```

Docker veröffentlicht Ports bei `127.0.0.1:...` nur auf dem Host-Loopback. Container-internes `0.0.0.0` reicht dann nicht aus, um aus dem LAN erreichbar zu sein.

## Zertifikatsursache

Die CA wurde im Proxy-Container erzeugt:

```text
/etc/phimantec/ca/phimantec-bump-ca.crt
/etc/phimantec/ca/phimantec-bump-ca.key
```

Durch das Docker-Volume `phimantec-ca` blieb das Zertifikat persistent, lag aber nicht sichtbar unter `$HOME`. Deshalb konnte man es nicht schnell vom Host kopieren oder in einen Client importieren.

## Korrekturen in v0.3.4

| Punkt | v0.3.3 | v0.3.4 |
|---|---|---|
| Proxy-Publish | `127.0.0.1:33128->3128` | `3128->3128`, extern erreichbar |
| Webmanagement-Publish | `127.0.0.1:5050->5050` | `5050->5050`, extern erreichbar |
| Lane/FlowBus | lokal | lokal beibehalten |
| CA auf Host | nicht automatisch exportiert | `~/phimantec-bump-ca.crt` |
| Preflight | meldete Port frei, aber nicht extern | erzwingt externe Proxy-/Monitor-Bindings |
| Smoke | lokaler TCP/HTTP-Smoke | Publish-Gate + CA-Export-Gate |
| AWK-Warnungen | sichtbar | entfernt |

## Neue Defaults

```text
PHIMANTEC_PROXY_PORT=3128
PHIMANTEC_MONITOR_PORT=5050
PHIMANTEC_LANE_PORT=127.0.0.1:18080
PHIMANTEC_FLOWBUS_PORT=127.0.0.1:18090
PHIMANTEC_AUTO_EXPORT_CA=1
PHIMANTEC_CA_EXPORT_PATH=
PHIMANTEC_ALLOW_LOCAL_BIND=0
```

`PHIMANTEC_CA_EXPORT_PATH` bleibt leer. Leer bedeutet:

```text
$HOME/phimantec-bump-ca.crt
```

## Erwartete Ausgabe nach v0.3.4-Rollout

```bash
scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_compose.sh ps
```

Erwartete Portveröffentlichung:

```text
0.0.0.0:3128->3128/tcp   oder   :::3128->3128/tcp
0.0.0.0:5050->5050/tcp   oder   :::5050->5050/tcp
127.0.0.1:18080->18080/tcp
127.0.0.1:18090->18090/tcp
```

Erwarteter CA-Export:

```bash
ls -lah "$HOME/phimantec-bump-ca.crt"
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject
```

## Externe Tests

Vom Docker-Host:

```bash
curl -fsS http://127.0.0.1:5050/ >/dev/null && echo monitor_ok
curl -sS -x http://127.0.0.1:3128 -I https://example.org/
```

Von einem anderen Rechner im LAN:

```bash
curl -fsS http://<docker-host-ip>:5050/ >/dev/null && echo monitor_extern_ok
curl -sS -x http://<docker-host-ip>:3128 -I https://example.org/
```

## Bewusst lokales Testprofil

Nur wenn Proxy und Webmanagement absichtlich nicht von außen erreichbar sein sollen:

```bash
sed -i 's#^PHIMANTEC_PROXY_PORT=.*#PHIMANTEC_PROXY_PORT=127.0.0.1:3128#' .env
sed -i 's#^PHIMANTEC_MONITOR_PORT=.*#PHIMANTEC_MONITOR_PORT=127.0.0.1:5050#' .env
sed -i 's#^PHIMANTEC_ALLOW_LOCAL_BIND=.*#PHIMANTEC_ALLOW_LOCAL_BIND=1#' .env
scripts/phimantec_compose.sh redeploy --no-cache
```

Ohne `PHIMANTEC_ALLOW_LOCAL_BIND=1` blockiert v0.3.4 den Start, damit die ursprüngliche Regression nicht wieder unbemerkt entsteht.
