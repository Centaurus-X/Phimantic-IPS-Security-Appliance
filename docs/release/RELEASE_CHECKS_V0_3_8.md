# Release Checks v0.3.8

## Lokale statische Gates

- `sha256sum -c MANIFEST.sha256`
- `scripts/check_release_context.sh`
- `scripts/verify_docker_static.sh`
- `bash -n scripts/*.sh`
- Python `py_compile`
- CMake configure/build
- CTest

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
