"""Rule-File-Management fuer Suricata.

Regeln in Suricata sind Textzeilen. Eine deaktivierte Regel ist eine
auskommentierte Zeile mit fuehrendem '#'. Wir parsen die Datei tolerant
und liefern strukturierte Daten an die UI.

Zentrale Operationen:
    - list_rule_files()           Verzeichnis-Inventur
    - read_rules(file)            zeilenweise Parser
    - toggle_rule(file, sid, on)  aktivieren/deaktivieren
    - upsert_rule(file, raw)      Regel anhaengen oder ersetzen
    - delete_rule(file, sid)      Regel entfernen
    - validate_rule(raw)          Mini-Parser fuer Wohlgeformtheit

Sicherheits-Hinweis: WAR alle Pfade gegen Path-Traversal. Wir akzeptieren nur
relative .rules-Pfade aus dem konfigurierten rules_dir.
"""

import os
import re
import shutil
import time
from functools import partial

_SID_RE = re.compile(r"\bsid:(\d+)")
_MSG_RE = re.compile(r'\bmsg:"([^"]+)"')
_REV_RE = re.compile(r"\brev:(\d+)")
_CLASS_RE = re.compile(r"\bclasstype:([\w_-]+)")
_ACTION_RE = re.compile(r"^\s*(?:#\s*)?(alert|drop|pass|reject|log)\b",
                        re.IGNORECASE)
_RULE_HEAD_RE = re.compile(
    r"^\s*(?P<disabled>#)?\s*(?P<action>alert|drop|pass|reject|log)\s+",
    re.IGNORECASE,
)


def _safe_filename(rules_dir, filename):
    """Verhindert Path-Traversal und erlaubt sichere Unterordner.

    Dadurch koennen neben phimantec-local.rules auch externe Dateien wie
    external/custom-local.rules im Webmanagement bearbeitet werden.
    """
    if not filename or "\\" in filename:
        return None
    normalized = filename.replace("/", os.sep)
    if os.path.isabs(normalized):
        return None
    parts = [part for part in normalized.split(os.sep) if part]
    if not parts:
        return None
    for part in parts:
        if part in (".", "..") or part.startswith("."):
            return None
    if not parts[-1].endswith(".rules"):
        return None
    candidate = os.path.join(rules_dir, *parts)
    base = os.path.realpath(rules_dir)
    real = os.path.realpath(candidate)
    if real != base and not real.startswith(base + os.sep):
        return None
    return real


def list_rule_files(cfg):
    """Listet alle .rules-Dateien im konfigurierten Verzeichnis."""
    rules_dir = cfg["suricata"]["rules_dir"]
    out = []
    if not os.path.isdir(rules_dir):
        if cfg.get("suricata", {}).get("mock_mode"):
            return _mock_rule_files()
        return out

    for current_dir, dirnames, filenames in os.walk(rules_dir):
        dirnames[:] = [d for d in sorted(dirnames)
                       if not d.startswith(".") and d != "backup"]
        for name in sorted(filenames):
            if not name.endswith(".rules"):
                continue
            full = os.path.join(current_dir, name)
            rel = os.path.relpath(full, rules_dir).replace(os.sep, "/")
            try:
                st = os.stat(full)
            except OSError:
                continue
            # Schnellzaehlung Regel-Anzahl
            active = 0
            disabled = 0
            try:
                with open(full, "r", encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        head = _RULE_HEAD_RE.match(line)
                        if not head:
                            continue
                        if head.group("disabled"):
                            disabled += 1
                        else:
                            active += 1
            except OSError:
                pass
            out.append({
                "filename": rel,
                "size_bytes": int(st.st_size),
                "mtime": int(st.st_mtime),
                "active_rules": active,
                "disabled_rules": disabled,
            })
    return out


def _parse_rule_line(line, line_no):
    head = _RULE_HEAD_RE.match(line)
    if not head:
        return None
    sid_match = _SID_RE.search(line)
    msg_match = _MSG_RE.search(line)
    rev_match = _REV_RE.search(line)
    class_match = _CLASS_RE.search(line)
    return {
        "line_no": int(line_no),
        "enabled": head.group("disabled") is None,
        "action": head.group("action").lower(),
        "sid": int(sid_match.group(1)) if sid_match else None,
        "rev": int(rev_match.group(1)) if rev_match else None,
        "msg": msg_match.group(1) if msg_match else "",
        "classtype": class_match.group(1) if class_match else "",
        "raw": line.rstrip("\n"),
    }


def read_rules(cfg, filename, limit=None, offset=0, query=None):
    """Liefert die Regeln einer Datei mit Pagination + Suchfilter.

    query matched case-insensitive gegen msg/raw/sid.
    """
    rules_dir = cfg["suricata"]["rules_dir"]
    if cfg.get("suricata", {}).get("mock_mode") and not os.path.isdir(rules_dir):
        return _mock_rules_for(filename, limit, offset, query)

    path = _safe_filename(rules_dir, filename)
    if path is None or not os.path.isfile(path):
        return {"filename": filename, "rules": [], "total": 0, "error": "not_found"}

    all_rules = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for idx, line in enumerate(fh, start=1):
                parsed = _parse_rule_line(line, idx)
                if parsed is not None:
                    all_rules.append(parsed)
    except OSError as exc:
        return {"filename": filename, "rules": [], "total": 0,
                "error": str(exc)}

    if query:
        q = query.lower()
        all_rules = [r for r in all_rules
                     if q in (r["msg"] or "").lower()
                     or q in (r["raw"] or "").lower()
                     or q == str(r["sid"] or "")]

    total = len(all_rules)
    if limit is not None:
        try:
            limit_i = int(limit)
            offset_i = int(offset or 0)
            all_rules = all_rules[offset_i: offset_i + limit_i]
        except (TypeError, ValueError):
            pass

    return {"filename": filename, "rules": all_rules, "total": total}


def toggle_rule(cfg, filename, sid, enabled):
    """Findet Regel per SID und (de)aktiviert sie."""
    rules_dir = cfg["suricata"]["rules_dir"]
    path = _safe_filename(rules_dir, filename)
    if path is None or not os.path.isfile(path):
        return {"ok": False, "error": "not_found"}

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        return {"ok": False, "error": str(exc)}

    sid_target = int(sid)
    enabled_bool = bool(enabled)
    changed = False
    for i, line in enumerate(lines):
        sid_match = _SID_RE.search(line)
        if not sid_match:
            continue
        if int(sid_match.group(1)) != sid_target:
            continue
        head = _RULE_HEAD_RE.match(line)
        if not head:
            continue
        currently_enabled = head.group("disabled") is None
        if currently_enabled == enabled_bool:
            return {"ok": True, "changed": False}
        if enabled_bool:
            # Kommentarzeichen entfernen (vor 'alert/drop/...')
            lines[i] = re.sub(r"^\s*#\s*", "", line, count=1)
        else:
            stripped = line.lstrip()
            indent = line[: len(line) - len(stripped)]
            lines[i] = "{0}# {1}".format(indent, stripped)
        changed = True
        break

    if not changed:
        return {"ok": False, "error": "sid_not_found"}

    _atomic_write(path, "".join(lines))
    return {"ok": True, "changed": True}


def upsert_rule(cfg, filename, raw_rule):
    """Anhaengen oder Ersetzen einer Regel anhand der SID."""
    rules_dir = cfg["suricata"]["rules_dir"]
    path = _safe_filename(rules_dir, filename)
    if path is None:
        return {"ok": False, "error": "invalid_filename"}

    valid = validate_rule(raw_rule)
    if not valid["ok"]:
        return valid

    sid_target = valid["sid"]
    raw_line = raw_rule.rstrip("\n") + "\n"

    if not os.path.isfile(path):
        # neue Datei anlegen
        _atomic_write(path, raw_line)
        return {"ok": True, "action": "created", "sid": sid_target}

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        return {"ok": False, "error": str(exc)}

    replaced = False
    for i, line in enumerate(lines):
        sid_match = _SID_RE.search(line)
        if sid_match and int(sid_match.group(1)) == sid_target:
            lines[i] = raw_line
            replaced = True
            break

    if not replaced:
        lines.append(raw_line)

    _atomic_write(path, "".join(lines))
    return {"ok": True, "action": "replaced" if replaced else "appended",
            "sid": sid_target}


def delete_rule(cfg, filename, sid):
    rules_dir = cfg["suricata"]["rules_dir"]
    path = _safe_filename(rules_dir, filename)
    if path is None or not os.path.isfile(path):
        return {"ok": False, "error": "not_found"}

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        return {"ok": False, "error": str(exc)}

    sid_target = int(sid)
    new_lines = []
    removed = False
    for line in lines:
        sid_match = _SID_RE.search(line)
        if sid_match and int(sid_match.group(1)) == sid_target:
            removed = True
            continue
        new_lines.append(line)

    if not removed:
        return {"ok": False, "error": "sid_not_found"}

    _atomic_write(path, "".join(new_lines))
    return {"ok": True, "removed": True}


def validate_rule(raw_rule):
    """Mini-Parser. Pruef: Action, msg, sid, schliessende Klammer."""
    if not raw_rule or "\n" in raw_rule.strip():
        return {"ok": False, "error": "single_line_required"}
    line = raw_rule.strip()
    if not _ACTION_RE.match(line):
        return {"ok": False, "error": "missing_action"}
    if "(" not in line or not line.rstrip().endswith(")"):
        return {"ok": False, "error": "missing_options_block"}
    sid_match = _SID_RE.search(line)
    if not sid_match:
        return {"ok": False, "error": "missing_sid"}
    msg_match = _MSG_RE.search(line)
    if not msg_match:
        return {"ok": False, "error": "missing_msg"}
    return {"ok": True, "sid": int(sid_match.group(1)),
            "msg": msg_match.group(1)}


def _atomic_write(path, content):
    """Schreibt mit Backup-Datei + atomarem Rename."""
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    backup = path + ".bak"
    if os.path.isfile(path):
        try:
            shutil.copy2(path, backup)
        except OSError:
            pass
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(content)
    os.replace(tmp, path)


# =========================================================================
# Mock-Daten
# =========================================================================

def _mock_rule_files():
    now = int(time.time())
    return [
        {"filename": "phimantec-local.rules", "size_bytes": 1024,
         "mtime": now, "active_rules": 3, "disabled_rules": 0},
        {"filename": "emerging-malware.rules", "size_bytes": 124000,
         "mtime": now - 86400, "active_rules": 412, "disabled_rules": 18},
        {"filename": "emerging-policy.rules", "size_bytes": 56000,
         "mtime": now - 86400, "active_rules": 188, "disabled_rules": 4},
        {"filename": "emerging-trojan.rules", "size_bytes": 320000,
         "mtime": now - 86400, "active_rules": 920, "disabled_rules": 35},
    ]


def _mock_rules_for(filename, limit, offset, query):
    samples = [
        {"sid": 900001, "msg": "PHIMANTEC SURICATA PATH BLOCK",
         "action": "alert",
         "raw": 'alert tcp any any -> any 18080 (msg:"PHIMANTEC SURICATA PATH BLOCK"; '
                'content:"X-Phim-Path|3a 20|"; sid:900001; rev:3;)'},
        {"sid": 900002, "msg": "PHIMANTEC EICAR TEST FILE",
         "action": "alert",
         "raw": 'alert tcp any any -> any 18080 (msg:"PHIMANTEC EICAR TEST FILE"; '
                'content:"EICAR-STANDARD-ANTIVIRUS-TEST-FILE"; sid:900002; rev:1;)'},
        {"sid": 900003, "msg": "PHIMANTEC BODY BLOCK",
         "action": "alert",
         "raw": 'alert tcp any any -> any 18080 (msg:"PHIMANTEC BODY BLOCK"; '
                'content:"PHIMANTEC_TEST_BLOCK"; sid:900003; rev:3;)'},
    ]
    rules = []
    for i, s in enumerate(samples, start=1):
        rules.append({
            "line_no": i,
            "enabled": True,
            "action": s["action"],
            "sid": s["sid"],
            "rev": 1,
            "msg": s["msg"],
            "classtype": "",
            "raw": s["raw"],
        })
    if query:
        q = query.lower()
        rules = [r for r in rules
                 if q in r["msg"].lower() or q in r["raw"].lower()
                 or q == str(r["sid"])]
    total = len(rules)
    if limit is not None:
        try:
            limit_i = int(limit)
            offset_i = int(offset or 0)
            rules = rules[offset_i: offset_i + limit_i]
        except (TypeError, ValueError):
            pass
    return {"filename": filename, "rules": rules, "total": total}
