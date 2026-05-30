#!/usr/bin/env python3
"""Smoke-Test fuer inkrementellen Suricata-EVE-Reader."""

import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / "backend"
sys.path.insert(0, str(BACKEND))

from services import eve_reader


def write_event(handle, event):
    handle.write(json.dumps(event, separators=(",", ":")) + "\n")
    handle.flush()


def make_alert(flow_id, sid, msg):
    return {
        "event_type": "alert",
        "flow_id": flow_id,
        "timestamp": "2026-05-29T12:00:00.000000+0000",
        "src_ip": "192.168.0.34",
        "src_port": 51000,
        "dest_ip": "203.0.113.10",
        "dest_port": 443,
        "proto": "TCP",
        "alert": {
            "signature_id": sid,
            "signature": msg,
            "category": "Policy",
            "severity": 1,
        },
    }


def main():
    with tempfile.TemporaryDirectory() as tmp:
        eve_path = os.path.join(tmp, "eve.json")
        cfg = {"suricata": {"eve_path": eve_path}}
        with open(eve_path, "w", encoding="utf-8") as handle:
            write_event(handle, {"event_type": "http", "flow_id": "f-http"})
            write_event(handle, make_alert("f-alert-1", 900002,
                                          "PHIMANTEC EICAR TEST FILE"))
            for i in range(300):
                write_event(handle, {"event_type": "flow",
                                     "flow_id": "f-flow-{0}".format(i)})

        alerts, offset, identity = eve_reader.read_alerts_from_offset(cfg, 0)
        assert len(alerts) == 1, alerts
        assert alerts[0]["flow_id"] == "f-alert-1", alerts
        assert offset > 0, offset
        assert identity["size"] >= offset, identity

        tail = eve_reader.tail_alerts(cfg, limit=10)
        assert len(tail) == 1, tail
        assert tail[0]["rule_id"] == 900002, tail

        with open(eve_path, "a", encoding="utf-8") as handle:
            write_event(handle, make_alert("f-alert-2", 900003,
                                          "PHIMANTEC BODY BLOCK"))

        new_alerts, new_offset, new_identity = eve_reader.read_alerts_from_offset(
            cfg, offset)
        assert len(new_alerts) == 1, new_alerts
        assert new_alerts[0]["flow_id"] == "f-alert-2", new_alerts
        assert new_offset > offset, (offset, new_offset)
        assert new_identity["ino"] == identity["ino"], (identity, new_identity)

        stats = eve_reader.get_eve_stats(cfg)
        assert stats["alert_count"] == 2, stats
        assert stats["http_count"] == 1, stats
        assert stats["total_lines"] == 303, stats

    print("eve-reader cursor tests passed")


if __name__ == "__main__":
    main()
