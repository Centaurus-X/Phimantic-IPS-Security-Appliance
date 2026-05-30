# PHIMANTEC v0.3.18 Release Checks

## Lokale Prüfmatrix

```text
bash -n scripts/*.sh
python3 -m py_compile relevante Python-Dateien
node --check suricata-monitor/frontend/js/pages/*.js
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
python3 suricata-monitor/tests/test_config_manager_paths.py
python3 suricata-monitor/tests/test_eve_reader_cursor.py
python3 suricata-monitor/tests/test_rules_reload_management.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHREADS_PREFER_PTHREAD_FLAG=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
scripts/verify_docker_static.sh
scripts/verify_release.sh
sha256sum -c MANIFEST.sha256
```

## Security-Invarianten

```text
Suricata bleibt einzige fachliche Entscheidungsinstanz.
Kein finales Suricata-Allow bedeutet kein Forwarding.
Finaler Suricata-Alert blockiert deterministisch.
Timeout, unavailable, queue-timeout und FlowBus-Fehler bleiben fail-closed.
Kein Domain-, CDN-, Media- oder Content-Type-Bypass.
Keine Proxy-Malware-Heuristik.
```

## Neue v0.3.18-Akzeptanzpunkte

```text
Ruleset-Reload im Webmanagement nutzt suricata -T vor Live-Reload.
Kein Reload bei fehlerhafter Suricata-Konfiguration.
ruleset-reload-rules / ruleset-reload-nonblocking / SIGUSR2-Fallback verfügbar.
Kein unklarer incomplete-response-Fehler bei normalem Reload-Pfad.
Suricata startet ohne -S-Override; YAML rule-files werden geladen.
default-rule-path ist /var/lib/suricata/rules.
ET/Open suricata.rules, phimantec-local.rules und external/custom-local.rules werden zusammen verwaltet.
PHIMANTEC-Lokalregeln bleiben nach suricata-update erhalten.
UI zeigt Reload-erforderlich-Drift nach Dateiänderungen.
UI zeigt Stage und Detailmeldung bei Test-/Reload-Fehlern.
```
