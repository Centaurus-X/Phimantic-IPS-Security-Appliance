"""Ruleset-Management: Pattern-Quellen wie ET (Emerging Threats) verwalten.

Suricata bietet das Tool ``suricata-update`` zum Verwalten externer Quellen.
Wir wrappen die wichtigsten Kommandos:

    suricata-update list-sources           -- alle bekannten Quellen
    suricata-update list-enabled-sources   -- aktuell aktive
    suricata-update enable-source <name>   -- aktivieren
    suricata-update disable-source <name>  -- deaktivieren
    suricata-update update-sources         -- Source-Index refreshen
    suricata-update                        -- Regeln tatsaechlich runterziehen

Nach einem Pull wird die kombinierte Suricata-Konfiguration getestet und nur bei
OK live reloadet. Der Proxy bleibt unveraendert Suricata-only und fail-closed.
"""

import datetime
import os
import re
import shutil
import subprocess
import time


def _is_mock(cfg):
    return bool(cfg.get("suricata", {}).get("mock_mode", False))


def _update_bin(cfg):
    return cfg["suricata"].get("update_bin", "suricata-update")


def _rules_dir(cfg):
    return cfg.get("suricata", {}).get("rules_dir", "/var/lib/suricata/rules")


def _config_path(cfg):
    return cfg.get("suricata", {}).get("config_path", "/etc/suricata/suricata.yaml")


def _run(cmd, timeout=120):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, proc.stdout, proc.stderr
    except (subprocess.TimeoutExpired, OSError) as exc:
        return -1, "", str(exc)


def _parse_list_sources(text):
    """suricata-update list-sources hat eine recht freie Text-Ausgabe.

    Wir greifen die haeufigsten Zeilenformate ab.
    """
    out = []
    current = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("---") or line.lower().startswith("warning"):
            continue
        # Header-Zeile: "Name: et/open"
        m = re.match(r"^Name:\s*(.+)$", line)
        if m:
            if current is not None:
                out.append(current)
            current = {"name": m.group(1).strip(), "summary": "",
                       "license": "", "vendor": "", "url": ""}
            continue
        if current is None:
            continue
        m = re.match(r"^Summary:\s*(.+)$", line)
        if m:
            current["summary"] = m.group(1).strip()
            continue
        m = re.match(r"^License:\s*(.+)$", line)
        if m:
            current["license"] = m.group(1).strip()
            continue
        m = re.match(r"^Vendor:\s*(.+)$", line)
        if m:
            current["vendor"] = m.group(1).strip()
            continue
        m = re.match(r"^URL:\s*(.+)$", line, re.IGNORECASE)
        if m:
            current["url"] = m.group(1).strip()
            continue
    if current is not None:
        out.append(current)
    return out


def _parse_list_enabled(text):
    """``list-enabled-sources`` druckt einen Namen pro Zeile."""
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("---") or line.lower().startswith("warning"):
            continue
        if " " in line or ":" in line:
            continue
        out.append(line)
    return out


def _backup_rule_files(cfg):
    """Lege Backups der relevanten Ruleset-Dateien an."""
    ts = datetime.datetime.utcnow().strftime("%Y%m%d_%H%M%S")
    rules_dir = _rules_dir(cfg)
    backup_dir = os.path.join(rules_dir, "backup")
    saved = []
    try:
        os.makedirs(backup_dir, exist_ok=True)
    except OSError as exc:
        return {"ok": False, "message": "backup dir create failed: " + str(exc), "saved": saved}

    candidates = [(_config_path(cfg), "suricata.yaml")]
    for base in (rules_dir, "/etc/suricata/rules"):
        if not os.path.isdir(base):
            continue
        for current_dir, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d != "backup" and not d.startswith(".")]
            for name in filenames:
                if not name.endswith(".rules"):
                    continue
                src = os.path.join(current_dir, name)
                rel = os.path.relpath(src, base).replace(os.sep, "__")
                prefix = "active" if base == rules_dir else "etc"
                candidates.append((src, prefix + "__" + rel))

    for src, name in candidates:
        if os.path.exists(src):
            dst = os.path.join(backup_dir, "{}.bak.{}".format(name, ts))
            try:
                shutil.copy2(src, dst)
                saved.append(dst)
            except OSError:
                pass
    return {"ok": True, "saved": saved, "ts": ts}


def _ensure_runtime_rule_files(cfg):
    rules_dir = _rules_dir(cfg)
    external_dir = os.path.join(rules_dir, "external")
    try:
        os.makedirs(external_dir, exist_ok=True)
        managed = os.path.join(rules_dir, "suricata.rules")
        if not os.path.exists(managed):
            open(managed, "a", encoding="utf-8").close()
        active_local = os.path.join(rules_dir, "phimantec-local.rules")
        packaged_local = "/etc/suricata/rules/phimantec-local.rules"
        if not os.path.exists(active_local) and os.path.exists(packaged_local):
            shutil.copy2(packaged_local, active_local)
        custom = os.path.join(external_dir, "custom-local.rules")
        if not os.path.exists(custom):
            open(custom, "a", encoding="utf-8").close()
    except OSError as exc:
        return {"ok": False, "message": str(exc)}
    return {"ok": True}


def _ensure_yaml_rule_files(cfg):
    path = _config_path(cfg)
    rules_dir = _rules_dir(cfg)
    wanted = ["suricata.rules", "phimantec-local.rules", "external/custom-local.rules"]
    try:
        import yaml
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            data = yaml.safe_load(handle) or {}
        if not isinstance(data, dict):
            data = {}
        data["default-rule-path"] = rules_dir
        current = data.get("rule-files")
        if not isinstance(current, list):
            current = []
        merged = []
        for item in wanted + current:
            if isinstance(item, str) and item and item not in merged:
                merged.append(item)
        data["rule-files"] = merged
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as handle:
            yaml.safe_dump(data, handle, sort_keys=False)
        os.replace(tmp, path)
    except (OSError, ValueError) as exc:
        return {"ok": False, "message": str(exc)}
    return {"ok": True, "rule_files": wanted, "default_rule_path": rules_dir}


def list_sources(cfg):
    """Liefert die kombinierte Liste 'name -> enabled? + meta'."""
    if _is_mock(cfg):
        return _mock_sources()

    rc1, out1, err1 = _run([_update_bin(cfg), "list-sources"])
    if rc1 != 0:
        return {"ok": False, "error": err1 or out1, "sources": []}

    rc2, out2, _ = _run([_update_bin(cfg), "list-enabled-sources"])
    enabled = set(_parse_list_enabled(out2)) if rc2 == 0 else set()

    parsed = _parse_list_sources(out1)
    for entry in parsed:
        entry["enabled"] = entry["name"] in enabled
    return {"ok": True, "sources": parsed}


def enable_source(cfg, name):
    if _is_mock(cfg):
        return {"ok": True, "mock": True, "name": name,
                "message": "[mock] enabled", "reload_required": True}
    rc, out, err = _run([_update_bin(cfg), "enable-source", name])
    return {"ok": rc == 0, "message": out + err, "reload_required": True}


def disable_source(cfg, name):
    if _is_mock(cfg):
        return {"ok": True, "mock": True, "name": name,
                "message": "[mock] disabled", "reload_required": True}
    rc, out, err = _run([_update_bin(cfg), "disable-source", name])
    return {"ok": rc == 0, "message": out + err, "reload_required": True}


def update_sources_index(cfg):
    if _is_mock(cfg):
        return {"ok": True, "mock": True,
                "message": "[mock] sources index refreshed"}
    rc, out, err = _run([_update_bin(cfg), "update-sources"])
    return {"ok": rc == 0, "message": out + err}


def update_rules(cfg):
    """Pull aller aktivierten Quellen, Backup, Validierung und Live-Reload."""
    if _is_mock(cfg):
        time.sleep(0.4)
        return {"ok": True, "mock": True,
                "message": "[mock] suricata-update would run now",
                "stage": "mock", "rules_total": 12345}

    backup = _backup_rule_files(cfg)

    ensure_res = _ensure_runtime_rule_files(cfg)
    if not ensure_res.get("ok"):
        return {"ok": False, "stage": "prepare-rules-dir",
                "message": ensure_res.get("message", "prepare failed"),
                "backup": backup}

    yaml_res = _ensure_yaml_rule_files(cfg)
    if not yaml_res.get("ok"):
        return {"ok": False, "stage": "suricata-yaml",
                "message": yaml_res.get("message", "yaml update failed"),
                "backup": backup}

    rc, out, err = _run([_update_bin(cfg)], timeout=600)
    update_msg = (out or "") + (err or "")
    if rc != 0:
        return {"ok": False, "stage": "suricata-update",
                "message": "suricata-update failed: " + update_msg[-3000:],
                "backup": backup}

    ensure_after = _ensure_runtime_rule_files(cfg)
    if not ensure_after.get("ok"):
        return {"ok": False, "stage": "post-update-rules-dir",
                "message": ensure_after.get("message", "post-update prepare failed"),
                "update_message": update_msg[-1000:],
                "backup": backup}

    from services import suricata_ctl
    test = suricata_ctl._suricata_config_test(cfg)
    if test.get("return") != "OK":
        return {"ok": False, "stage": "suricata-test",
                "message": "config test FAILED after suricata-update; "
                           "running engine keeps OLD rules; detail: " +
                           test.get("message", "")[-2500:],
                "update_message": update_msg[-1000:],
                "backup": backup}

    reload_res = suricata_ctl.reload_rules(cfg)
    if reload_res.get("return") != "OK":
        return {"ok": False, "stage": "reload",
                "message": "config OK but reload failed: " +
                           reload_res.get("message", ""),
                "reload_result": reload_res,
                "update_message": update_msg[-1000:],
                "backup": backup,
                "test_message": test.get("message", "")[-500:]}

    return {"ok": True, "stage": "complete",
            "message": "rules updated, tested and reloaded successfully",
            "reload_stage": reload_res.get("stage", ""),
            "reload_result": reload_res,
            "update_message_tail": update_msg[-1000:],
            "backup": backup}


# =========================================================================
# Mock-Daten
# =========================================================================

def _mock_sources():
    sources = [
        {"name": "et/open",
         "summary": "Emerging Threats Open Ruleset",
         "license": "MIT", "vendor": "Proofpoint",
         "url": "https://rules.emergingthreats.net/open/",
         "enabled": True},
        {"name": "et/pro",
         "summary": "Emerging Threats Pro (kommerziell, Token erforderlich)",
         "license": "Commercial", "vendor": "Proofpoint",
         "url": "https://rules.emergingthreats.net/pro/",
         "enabled": False},
        {"name": "oisf/trafficid",
         "summary": "Suricata Traffic-ID Ruleset",
         "license": "MIT", "vendor": "OISF",
         "url": "https://openinfosecfoundation.org/",
         "enabled": True},
        {"name": "ptresearch/attackdetection",
         "summary": "Positive Technologies Attack Detection",
         "license": "MIT", "vendor": "Positive Technologies",
         "url": "https://github.com/ptresearch/AttackDetection",
         "enabled": False},
        {"name": "scwx/security",
         "summary": "Secureworks Security Rules",
         "license": "Commercial", "vendor": "Secureworks",
         "url": "https://www.secureworks.com/",
         "enabled": False},
        {"name": "abuse.ch/sslbl-blacklist",
         "summary": "abuse.ch SSL Blacklist",
         "license": "CC BY-SA 4.0", "vendor": "abuse.ch",
         "url": "https://sslbl.abuse.ch/",
         "enabled": True},
        {"name": "abuse.ch/urlhaus",
         "summary": "abuse.ch URLhaus",
         "license": "CC0", "vendor": "abuse.ch",
         "url": "https://urlhaus.abuse.ch/",
         "enabled": False},
    ]
    return {"ok": True, "mock": True, "sources": sources}
