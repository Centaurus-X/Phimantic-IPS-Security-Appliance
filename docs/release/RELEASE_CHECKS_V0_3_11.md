# Release Checks v0.3.12

## Lokale Release-Gates

```text
sha256 manifest check
bash -n scripts/*.sh
Python py_compile suricata-monitor/backend
SuricataMonitor Config-Manager Smoke
SuricataMonitor EVE-Reader Cursor Smoke
JavaScript Syntaxcheck
CMake configure/build
CTest
scripts/verify_docker_static.sh
scripts/verify_release.sh
ZIP re-extract + MANIFEST.sha256
```

## Neue v0.3.12-Gates

```text
- test_suricata_only_gate: EICAR/Testmarker loesen lokal keinen Block aus.
- test_verdict_timeout: FlowBus/Suricata-Ausfall blockt nicht lokal.
- test_eve_correlation: korreliert <uuid>TP/1.1 korrekt auf <uuid>.
- verify_docker_static: keine local-safety/inspectord-test Policies im Code.
- verify_docker_static: kein generischer Flow-ID-Char-Scanner fuer EVE-Korrelation.
- verify_docker_static: Flow-ID wird redundant im Lane-URL-Pfad transportiert.
- verify_docker_static: PHIMANTEC-Regeln nutzen HTTP-Layer-Regeln, keine TCP-Layer-Legacy-Regeln.
- verify_docker_static: auto-loopback-Capture fuer lokale Lane vorhanden.
- verify_docker_static: inkrementeller EVE-Reader und Raw-YAML-ReadOnly vorhanden.
```

## Zielhost-Gate

```bash
scripts/phimantec_deploy.sh --no-cache
scripts/phimantec_smoke.sh
scripts/phimantec_compose.sh compose exec -T suricata-node suricata --build-info | grep "This is Suricata version"
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc 'grep -nA16 "^af-packet:" /etc/suricata/suricata.yaml | sed -n "1,40p"'
```

Nach EICAR-Test:

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc 'grep -aE "900002|EICAR|X-Phim-Flow-Id" /var/log/suricata/eve.json | tail -20'
scripts/phimantec_compose.sh compose logs suricata-node | grep -E 'verdict flow=.*action=block.*suricata-alert' | tail -10
```
