"""EVE-Reader: liest Suricata eve.json robust und inkrementell.

Suricata schreibt NDJSON: eine JSON-Zeile pro Event. Der Reader stellt zwei
Dinge bereit:

1. tail_alerts(): letzte Alert-Events fuer UI/DB-Fallback.
2. read_alerts_from_offset(): inkrementeller Cursor-Reader fuer Persistenz.

Wichtig fuer PHIMANTEC: Nur Suricata-EVE-Alerts sind Block-Signale. HTTP/DNS-
Events werden fuer Dashboard-Zahlen gezaehlt, aber nicht als Block-Entscheidung
verwendet.
"""

import json
import os
import time
from collections import Counter, deque
from datetime import datetime, timezone


def _is_mock(cfg):
    return bool(cfg.get("suricata", {}).get("mock_mode", False))


def _eve_path(cfg):
    return cfg["suricata"]["eve_path"]


def _normalize_alert(line_obj):
    """Aus dem rohen eve.json-Objekt ein flaches Event-Dict ableiten."""
    alert = line_obj.get("alert") or {}
    return {
        "flow_id": line_obj.get("flow_id"),
        "timestamp": line_obj.get("timestamp"),
        "src_ip": line_obj.get("src_ip"),
        "src_port": line_obj.get("src_port"),
        "dest_ip": line_obj.get("dest_ip"),
        "dest_port": line_obj.get("dest_port"),
        "proto": line_obj.get("proto"),
        "rule_id": alert.get("signature_id"),
        "rule_msg": alert.get("signature"),
        "category": alert.get("category"),
        "severity": alert.get("severity"),
        "raw": line_obj,
    }


def _alert_timestamp_sort_key(item):
    return item.get("timestamp") or ""


def _parse_eve_line(line):
    line = line.strip()
    if not line:
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    if not isinstance(obj, dict):
        return None
    return obj


def tail_alerts(cfg, limit=200):
    """Letzte N Alerts chronologisch absteigend aus eve.json.

    Anders als die alte tail-N-Zeilen-Variante sammelt diese Funktion Alerts
    selbst als Ringpuffer. Dadurch verschwinden Alerts nicht, nur weil viele
    HTTP/DNS/flow-Zeilen nachgeschrieben wurden.
    """
    if _is_mock(cfg):
        return _mock_alerts(limit)

    path = _eve_path(cfg)
    if not os.path.isfile(path):
        return []

    alerts = deque(maxlen=int(limit))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                obj = _parse_eve_line(line)
                if obj is None or obj.get("event_type") != "alert":
                    continue
                alerts.append(_normalize_alert(obj))
    except OSError:
        return []

    out = list(alerts)
    out.sort(key=_alert_timestamp_sort_key, reverse=True)
    return out[: int(limit)]


def read_alerts_from_offset(cfg, offset=0):
    """Liest neue Alert-Events ab Byte-Offset.

    Rueckgabe: (alerts, new_offset, identity)
    identity enthaelt dev/ino/size und erlaubt Rotationserkennung in der DB.
    """
    if _is_mock(cfg):
        return _mock_alerts(50), 0, {"mock": True}

    path = _eve_path(cfg)
    st = os.stat(path)
    identity = {
        "path": path,
        "dev": int(st.st_dev),
        "ino": int(st.st_ino),
        "size": int(st.st_size),
    }
    safe_offset = int(offset or 0)
    if safe_offset < 0 or safe_offset > st.st_size:
        safe_offset = 0

    alerts = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        fh.seek(safe_offset)
        while True:
            line = fh.readline()
            if line == "":
                break
            obj = _parse_eve_line(line)
            if obj is None or obj.get("event_type") != "alert":
                continue
            alerts.append(_normalize_alert(obj))
        new_offset = fh.tell()
    return alerts, int(new_offset), identity


def get_eve_stats(cfg):
    """Datei-Health-Check und Event-Type-Zaehler fuer die UI."""
    path = _eve_path(cfg)
    info = {
        "path": path,
        "exists": False,
        "size_bytes": 0,
        "mtime_iso": None,
        "total_lines": 0,
        "event_type_counts": {},
        "alert_count": 0,
        "http_count": 0,
        "dns_count": 0,
    }
    if _is_mock(cfg):
        info.update({
            "exists": True,
            "size_bytes": 4096,
            "mtime_iso": datetime.now(timezone.utc).isoformat(),
            "mock": True,
            "total_lines": 30,
            "event_type_counts": {"alert": 30},
            "alert_count": 30,
        })
        return info
    try:
        st = os.stat(path)
        info["exists"] = True
        info["size_bytes"] = int(st.st_size)
        info["mtime_iso"] = datetime.fromtimestamp(
            st.st_mtime, tz=timezone.utc).isoformat()
        counts = Counter()
        total = 0
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                obj = _parse_eve_line(line)
                if obj is None:
                    continue
                total += 1
                counts[obj.get("event_type") or "?"] += 1
        info["total_lines"] = total
        info["event_type_counts"] = dict(counts)
        info["alert_count"] = counts.get("alert", 0)
        info["http_count"] = counts.get("http", 0)
        info["dns_count"] = counts.get("dns", 0)
    except OSError:
        pass
    return info


# =========================================================================
# Mock-Daten
# =========================================================================

def _mock_alerts(limit):
    """Beispiel-Datenset im Stil der Suricata-Lab-Rules von PHIMANTEC."""
    base_ts = time.time() - 60.0
    mock_data = [
        {"sid": 900002, "msg": "PHIMANTEC EICAR TEST FILE",
         "category": "Trojan", "severity": 1,
         "src": "192.168.0.232", "dst": "203.0.113.10"},
        {"sid": 900001, "msg": "PHIMANTEC SURICATA PATH BLOCK",
         "category": "Policy", "severity": 2,
         "src": "192.168.0.232", "dst": "203.0.113.20"},
        {"sid": 900003, "msg": "PHIMANTEC BODY BLOCK",
         "category": "Policy", "severity": 2,
         "src": "192.168.0.232", "dst": "203.0.113.30"},
        {"sid": 2013504, "msg": "ET POLICY Cleartext Authentication",
         "category": "Policy", "severity": 3,
         "src": "192.168.0.233", "dst": "198.51.100.42"},
        {"sid": 2025710, "msg": "ET MALWARE Suspicious User-Agent",
         "category": "Malware", "severity": 1,
         "src": "192.168.0.233", "dst": "198.51.100.45"},
    ]
    out = []
    for i, item in enumerate(mock_data * 6):
        ts = datetime.fromtimestamp(base_ts - i * 15.0,
                                    tz=timezone.utc).isoformat()
        out.append({
            "flow_id": "{:016x}".format(0xC0FFEE00 + i),
            "timestamp": ts,
            "src_ip": item["src"],
            "src_port": 40000 + i,
            "dest_ip": item["dst"],
            "dest_port": 443,
            "proto": "TCP",
            "rule_id": item["sid"],
            "rule_msg": item["msg"],
            "category": item["category"],
            "severity": item["severity"],
            "raw": {"mock": True},
        })
    return out[: int(limit)]
