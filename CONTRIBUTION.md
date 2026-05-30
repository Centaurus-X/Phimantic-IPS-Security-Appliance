# Phimantic-IPS-Security-Appliance

## Contribution Guide

Thank you for helping improve Phimantic-IPS-Security-Appliance.

## Core rules for contributions

Security invariants must not be weakened:

```text
No final Suricata allow verdict -> no forwarding.
Suricata alert                  -> block.
Pending / timeout / unavailable -> fail-closed.
```

Do not add:

- fail-open behavior;
- proxy-side malware heuristics;
- CDN, media, image, font, Amazon, YouTube or Google bypasses;
- uninspected forwarding;
- unauthenticated management actions;
- hidden changes to default credentials or tokens.

## Development workflow

1. Create a focused branch.
2. Keep the patch minimal and deterministic.
3. Add or update tests for the changed behavior.
4. Run the verification commands.
5. Document operational impact and security impact.
6. Open a pull request with the exact problem, root cause, patch and test evidence.

## Local verification

```bash
sha256sum -c MANIFEST.sha256
bash -n scripts/*.sh
python3 tests/test_suricata_yaml_patch.py
python3 tests/test_proxy_streaming_guard.py
python3 suricata-monitor/tests/test_config_manager_paths.py
python3 suricata-monitor/tests/test_eve_reader_cursor.py
python3 suricata-monitor/tests/test_rules_reload_management.py
scripts/verify_docker_static.sh
scripts/verify_release.sh
```

## Pull request checklist

- The security chain is unchanged or explicitly strengthened.
- EICAR remains blocked.
- Harmless traffic is not silently allowed on inspection failure.
- Ruleset reload still validates with `suricata -T` before runtime reload.
- Documentation and screenshots are updated when UI behavior changes.
- No secrets, CA private keys or production tokens are committed.

## Documentation style

Use clear international English in public-facing documentation. Historical diagnostic notes may remain in their original form when they are preserved for traceability.
