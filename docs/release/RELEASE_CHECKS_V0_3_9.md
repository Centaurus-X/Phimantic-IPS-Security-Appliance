# Release Checks v0.3.9

## Lokale statische Gates

- `sha256sum -c MANIFEST.sha256`
- `scripts/check_release_context.sh`
- `scripts/verify_docker_static.sh`
- `bash -n scripts/*.sh`
- Python `py_compile`
- JavaScript `node --check`, falls Node vorhanden
- `scripts/gen_ca.sh` CA-Smoke mit `CA:TRUE`, `keyCertSign`, `cRLSign`, `openssl verify`
- CMake configure/build
- CTest inklusive `cert_generation`

## Docker-Build-Gate auf dem Zielhost

- `docker/suricata-node/Dockerfile` installiert nicht mehr `suricata` und
  `suricata-update` im selben APT-Aufruf.
- OISF-PPA bleibt aktiv: `PHIMANTEC_SURICATA_APT_PPA=ppa:oisf/suricata-stable`.
- Build bricht ab, wenn `suricata --build-info` keine Version >= 8 zeigt.
- `suricata-update` muss im Container als Binary vorhanden sein.

## Zielhost-Runtime-Gates

- `scripts/phimantec_deploy.sh --no-cache`
- `scripts/phimantec_smoke.sh`
- echte Host-Listener auf `0.0.0.0:3128` und `0.0.0.0:5050`
- CA-Export nach `~/phimantec-bump-ca.crt`
- Proxy-CA-Fingerprint-Gleichheit zwischen Host-Export und Proxy-Container
- lokaler HTTPS-Proxy-Test mit exportierter CA
- Suricata >= 8 im suricata-node-Container

## Zielhost-Erwartung

```text
[smoke-docker] proxy_ca_chain_ok
[smoke-docker] ca_export_ok
[smoke-docker] suricata8_ok This is Suricata version 8.x.x ...
[smoke-docker] complete
```
