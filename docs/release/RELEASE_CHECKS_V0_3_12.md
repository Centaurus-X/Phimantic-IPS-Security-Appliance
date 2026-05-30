# Release Checks v0.3.12

Ausgefuehrte Gates:

```text
scripts/verify_release.sh
scripts/verify_docker_static.sh
bash -n scripts/*.sh
python3 -m py_compile
suricata-monitor config-manager tests
eve-reader cursor tests
tests/test_suricata_yaml_patch.py
tests/test_proxy_streaming_guard.py
node --check fuer Frontend JavaScript
CA generation smoke
CMake configure/build
CTest 9/9
vendor no-source smoke
```

Wichtige neue v0.3.12-Gates:

```text
- HTTP request-body-limit, response-body-limit und inspect-window = 1048576
- Hyperscan als Default fuer MPM/SPM
- Worker-Runmode bleibt Standard
- Chunked/until-close Body-Prefetch vor Header-Forwarding
- Keine verschachtelte 403-Blockpage nach bereits gesendeten Response-Headers
```

Docker-Runtime wurde in der Build-Sandbox nicht ausgefuehrt, weil kein Docker-Daemon verfuegbar ist. Der Zielhost-Runtime-Test ist in `docs/QUICK_TEST_COMMANDS_V0_3_12.md` beschrieben.
