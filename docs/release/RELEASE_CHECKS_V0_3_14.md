# Release Checks v0.3.14a

## Lokale Gates

```text
sha256sum -c MANIFEST.sha256
bash -n scripts/*.sh
python3 -m py_compile fuer Python-Komponenten
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
cmake build
ctest
scripts/verify_docker_static.sh
scripts/verify_release.sh
```

## FlowBus-Stabilitaets-Gates

```text
flowbus.timeout_ms und verdict.timeout_ms sind getrennt.
PHIMANTEC_FLOWBUS_KEEPALIVE defaultet auf 0.
PHIMANTEC_FLOWBUS_POOL_SIZE defaultet auf 1.
PHIMANTEC_SURICATA_DECISION_TIMEOUT_MS defaultet auf 900.
PHIMANTEC_PROXY_VERDICT_TIMEOUT_MS defaultet auf 3000.
PHIMANTEC_INSPECTORD_NO_ALERT_GRACE_MS defaultet auf 150.
Healthchecks fuehren phimantec-flowbus-probe aus.
Smoke-Test klassifiziert HTTP 503 als Inspection-/FlowBus-Fehler, nicht als CA-Fehler.
```

## Runtime Gates auf Zielhost

```text
Docker Deploy
phimantec_smoke.sh
FlowBus probe aus proxy und suricata-node
EICAR URL Matrix
Alt-Svc/Upgrade Header Strip
Browser-Privatfenster-Test
Logpruefung auf no silent allow
```

## Nicht erlaubt

```text
HTTP 200 mit EICAR-STANDARD-ANTIVIRUS-TEST-FILE im Client-Body
PENDING -> ALLOW
TIMEOUT -> ALLOW
ERROR -> ALLOW
FlowBus unavailable -> ALLOW
Domain-/Media-/Content-Type-Bypass
```
