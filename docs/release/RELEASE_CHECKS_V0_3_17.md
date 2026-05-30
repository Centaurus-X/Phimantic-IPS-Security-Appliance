# PHIMANTEC v0.3.17 Release Checks

## Lokale Prüfmatrix

```text
bash -n scripts/*.sh
python3 -m py_compile relevante Python-Dateien
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
python3 suricata-monitor/tests/test_config_manager_paths.py
python3 suricata-monitor/tests/test_eve_reader_cursor.py
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
```

## Neue v0.3.17-Akzeptanzpunkte

```text
FlowBus-Pool default 16, Cap 32.
Lane-Pool default 16, Cap 32.
Inspection Queue default 1024.
Global Inflight default 128.
Per-Host Inflight default 32.
Per-Client Inflight default 96.
No-Body-/HEAD-/204-/304-Pfade korrekt.
503 enthält differenzierten Inspection-Reason.
```
