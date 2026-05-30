# Release Checks v0.3.14

Ausgefuehrte Gates in der Build-Sandbox:

```text
bash -n scripts/*.sh                         PASS
python3 -m py_compile                        PASS
suricata-monitor config-manager tests        PASS
eve-reader cursor tests                      PASS
tests/test_suricata_yaml_patch.py            PASS
tests/test_proxy_streaming_guard.py          PASS
CMake configure/build                        PASS
CTest                                        9/9 PASS
```

Neue v0.3.14-Gates:

```text
- FlowBus JSON kann allow/block/pending/error lesen und schreiben.
- FlowBus disabled/timeout ist fail-closed, nicht fail-open.
- inspectord liefert pending, wenn der EVE-Flow noch nicht sichtbar ist.
- inspectord liefert allow nur als suricata-no-alert-final.
- Unvollstaendige EVE-EOF-Zeilen werden nicht als verarbeitet markiert.
- Alert-State darf nicht von spaeterer No-Alert-Zeile ueberschrieben werden.
- Proxy forwarded Fixed-Length-Responses erst nach finalem Allow.
- Proxy buffered Chunked/Until-Close-Responses bis zum finalen Allow.
- Oversize/Unavailable erzeugt 503 oder Stream-Close, aber kein Teil-Forwarding.
- EICAR-Matrix-Test verwendet Cache-Busting und Connection: close.
```

Docker-Runtime wurde in der Build-Sandbox nicht ausgefuehrt, weil kein Docker-Daemon verfuegbar ist. Der Zielhost-Runtime-Test ist in `docs/QUICK_TEST_COMMANDS_V0_3_14.md` beschrieben.
