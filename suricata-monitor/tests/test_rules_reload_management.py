#!/usr/bin/env python3
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from services import rules_manager, ruleset_manager, suricata_ctl


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        rules_dir = Path(tmp) / "rules"
        rules_dir.mkdir()
        external = rules_dir / "external"
        external.mkdir()
        (rules_dir / "phimantec-local.rules").write_text(
            'alert tcp any any -> any any (msg:"A"; sid:1; rev:1;)\n',
            encoding="utf-8",
        )
        (external / "custom-local.rules").write_text(
            '# alert tcp any any -> any any (msg:"B"; sid:2; rev:1;)\n',
            encoding="utf-8",
        )
        cfg = {"suricata": {"rules_dir": str(rules_dir), "mock_mode": False}}
        files = rules_manager.list_rule_files(cfg)
        names = sorted(item["filename"] for item in files)
        require("phimantec-local.rules" in names, "top-level rule missing")
        require("external/custom-local.rules" in names, "external rule missing")
        res = rules_manager.toggle_rule(cfg, "external/custom-local.rules", 2, True)
        require(res.get("ok") and res.get("changed"), "external toggle failed")
        require(not rules_manager._safe_filename(str(rules_dir), "../evil.rules"), "path traversal accepted")
        require(not rules_manager._safe_filename(str(rules_dir), ".hidden.rules"), "hidden file accepted")

        cfg2 = {"suricata": {"mock_mode": True}}
        require(suricata_ctl.reload_rules(cfg2).get("return") == "OK", "mock reload failed")
        require(ruleset_manager.update_rules(cfg2).get("ok"), "mock ruleset update failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
