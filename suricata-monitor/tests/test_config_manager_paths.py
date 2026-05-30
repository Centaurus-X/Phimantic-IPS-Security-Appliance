#!/usr/bin/env python3
"""Unit-Smoke fuer SuricataMonitor Config-Path Handling.

Bewusst ohne pytest-Abhaengigkeit, damit der Test auch in Offline- und
Container-Minimalumgebungen laeuft.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from services.config_manager import _get_by_tokens, _parse_path, _set_by_tokens
from services.config_manager import _detect_warnings


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def test_numeric_list_path():
    data = {}
    tokens = _parse_path("pcap[0].interface")
    check(_set_by_tokens(data, tokens, "eth0"), "set pcap[0].interface")
    ok, value = _get_by_tokens(data, tokens)
    check(ok, "get pcap[0].interface")
    check(value == "eth0", "pcap value")
    check(data == {"pcap": [{"interface": "eth0"}]}, "pcap structure")


def test_named_list_path():
    data = {"outputs": [{"eve-log": {"types": [{"alert": {}}]}}]}
    tokens = _parse_path("outputs[eve-log].types[alert].payload-printable")
    check(_set_by_tokens(data, tokens, True), "set alert payload-printable")
    ok, value = _get_by_tokens(data, tokens)
    check(ok, "get alert payload-printable")
    check(value is True, "payload printable true")


def test_proxy_safe_warnings():
    parsed = {
        "rule-files": ["suricata.rules"],
        "outputs": [{"eve-log": {"enabled": True,
                                   "filename": "eve.json",
                                   "types": [{"alert": {
                                       "enabled": True,
                                       "payload-printable": False}}]}}],
        "af-packet": [{"interface": "eth0", "copy-mode": "ips"}],
    }
    warnings = _detect_warnings(parsed, {"phimantec": {
        "local_rules_file": "phimantec-local.rules",
        "required_eve_file": "eve.json",
    }})
    joined = "\n".join(warnings)
    check("phimantec-local.rules" in joined, "missing local rule warning")
    check("payload-printable" in joined, "payload warning")
    check("copy-mode=ips" in joined, "copy mode warning")


def main():
    test_numeric_list_path()
    test_named_list_path()
    test_proxy_safe_warnings()
    print("suricata-monitor config-manager tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
