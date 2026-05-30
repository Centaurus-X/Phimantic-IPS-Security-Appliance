#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def load_yaml(path):
    with path.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle) or {}


def http_default_config(data):
    return data["app-layer"]["protocols"]["http"]["libhtp"]["default-config"]


def run_patch(path, body_limit=None):
    cmd = [
        sys.executable,
        "scripts/phimantec_suricata_yaml_patch.py",
        "--config",
        str(path),
        "--interface",
        "lo",
        "--no-backup",
    ]
    if body_limit is not None:
        cmd.extend(["--body-limit", str(body_limit)])
    subprocess.run(cmd, check=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "suricata.yaml"
        path.write_text(
            "%YAML 1.1\n---\n"
            "outputs: []\n"
            "app-layer:\n"
            "  protocols:\n"
            "    http:\n"
            "      enabled: no\n",
            encoding="utf-8",
        )
        run_patch(path)
        data = load_yaml(path)
        config = http_default_config(data)
        require(data["mpm-algo"] == "hs", "default mpm-algo must be hs")
        require(data["spm-algo"] == "hs", "default spm-algo must be hs")
        require(data["runmode"] == "workers", "runmode must remain workers")
        require(data["app-layer"]["protocols"]["http"]["enabled"] is True, "http parser must be enabled")
        for key in (
            "request-body-limit",
            "response-body-limit",
            "request-body-minimal-inspect-size",
            "request-body-inspect-window",
            "response-body-minimal-inspect-size",
            "response-body-inspect-window",
        ):
            require(config[key] == 1048576, key + " must default to 1 MiB")
        require(data["af-packet"][0]["interface"] == "lo", "af-packet interface must be patched")
        require(data["default-rule-path"] == "/var/lib/suricata/rules", "default rule path must be runtime rules dir")
        require("suricata.rules" in data["rule-files"], "suricata-update rules must be present")
        require("phimantec-local.rules" in data["rule-files"], "PHIMANTEC local rules must be present")
        require("external/custom-local.rules" in data["rule-files"], "external custom rules must be present")

        path.write_text(
            "%YAML 1.1\n---\noutputs: []\nrule-files:\n  - external/imported.rules\n",
            encoding="utf-8",
        )
        run_patch(path)
        data = load_yaml(path)
        require("external/imported.rules" in data["rule-files"], "existing rule-files must survive merge")

        path.write_text("%YAML 1.1\n---\noutputs: []\n", encoding="utf-8")
        run_patch(path, body_limit=262144)
        data = load_yaml(path)
        config = http_default_config(data)
        require(config["request-body-limit"] == 262144, "custom request-body-limit not applied")
        require(config["request-body-inspect-window"] == 262144, "custom inspect-window not applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
