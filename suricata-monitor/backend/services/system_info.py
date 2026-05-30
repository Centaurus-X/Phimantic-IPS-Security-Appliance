"""System-Informationen vom Host: Interface-Liste, CPU/Memory etc.

Wird vom Frontend genutzt fuer Dropdowns (z.B. Interface-Auswahl in der
AF_PACKET-Konfiguration).
"""

import os


def _is_mock(cfg):
    return bool(cfg.get("suricata", {}).get("mock_mode", False))


def list_interfaces(cfg):
    """Liefert alle Netzwerk-Interfaces des Hosts.

    Quelle: /sys/class/net/. 'lo' wird mitgenommen (User entscheidet, ob
    er auf Loopback lauscht; bei Container-Setups manchmal sinnvoll).

    Rueckgabe: Liste von Dicts {name, state, mac}.
    """
    if _is_mock(cfg):
        return _mock_interfaces()

    out = []
    base = "/sys/class/net"
    if not os.path.isdir(base):
        return out

    try:
        names = sorted(os.listdir(base))
    except OSError:
        return out

    for name in names:
        if name.startswith("."):
            continue
        entry = {"name": name, "state": "unknown", "mac": ""}
        try:
            with open(os.path.join(base, name, "operstate"),
                      "r", encoding="utf-8") as fh:
                entry["state"] = fh.read().strip()
        except OSError:
            pass
        try:
            with open(os.path.join(base, name, "address"),
                      "r", encoding="utf-8") as fh:
                entry["mac"] = fh.read().strip()
        except OSError:
            pass
        out.append(entry)
    return out


def _mock_interfaces():
    """Demo-Interfaces fuer mock_mode."""
    return [
        {"name": "ens18", "state": "up", "mac": "52:54:00:12:34:56"},
        {"name": "ens19", "state": "up", "mac": "52:54:00:12:34:57"},
        {"name": "ens20", "state": "down", "mac": "52:54:00:12:34:58"},
        {"name": "lo", "state": "unknown", "mac": "00:00:00:00:00:00"},
    ]
