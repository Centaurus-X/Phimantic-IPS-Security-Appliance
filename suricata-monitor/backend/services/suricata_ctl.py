"""Suricata-Steuerung ueber Unix-Socket, systemctl oder Container-Kommandos.

SuricataMonitor bleibt dadurch in drei Kontexten nutzbar:
    1. klassische VM/systemd-Installation
    2. Docker-Container ohne systemd
    3. Spezialdeployments mit eigenen reload/status/restart Commands
"""

import json
import os
import shutil
import socket
import subprocess
import threading
import time

_SOCKET_TIMEOUT = 5.0
_SOCKET_TIMEOUT_RELOAD = 120.0
_SOCKET_TIMEOUT_TEST = 60.0
_RECV_BUFFER = 65536
_LOCK = threading.Lock()


def _is_mock(cfg):
    return bool(cfg.get("suricata", {}).get("mock_mode", False))


def _control_mode(cfg):
    return str(cfg.get("suricata", {}).get("control_mode", "auto") or "auto")


def _configured_command(cfg, key):
    value = cfg.get("suricata", {}).get(key, "")
    if isinstance(value, str):
        return value.strip()
    return ""


def _run_shell_command(command, timeout):
    if not command:
        return {"return": "NOK", "message": "command not configured"}
    try:
        proc = subprocess.run(
            ["/bin/sh", "-c", command],
            capture_output=True, text=True, timeout=timeout,
        )
        msg = (proc.stdout or proc.stderr or "").strip()
        if proc.returncode == 0:
            return {"return": "OK", "message": msg or "ok"}
        return {"return": "NOK", "message": msg or "command failed"}
    except (subprocess.TimeoutExpired, OSError) as exc:
        return {"return": "NOK", "message": str(exc)}


def _systemctl_available():
    return shutil.which("systemctl") is not None


def _process_is_running(name):
    try:
        proc = subprocess.run(
            ["pidof", name], capture_output=True, text=True, timeout=3,
        )
        return proc.returncode == 0
    except (subprocess.TimeoutExpired, OSError):
        return False


def _first_pid(name):
    try:
        proc = subprocess.run(
            ["pidof", name], capture_output=True, text=True, timeout=3,
        )
        if proc.returncode != 0:
            return None
        parts = (proc.stdout or "").split()
        if not parts:
            return None
        return int(parts[0])
    except (ValueError, subprocess.TimeoutExpired, OSError):
        return None


def _process_uptime_seconds(name):
    pid = _first_pid(name)
    if pid is None:
        return None
    try:
        with open("/proc/{0}/stat".format(pid), "r", encoding="utf-8") as handle:
            fields = handle.read().split()
        start_ticks = int(fields[21])
        ticks_per_second = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
        with open("/proc/uptime", "r", encoding="utf-8") as handle:
            host_uptime = float(handle.read().split()[0])
        return max(0, int(host_uptime - (start_ticks / ticks_per_second)))
    except (OSError, ValueError, IndexError, KeyError):
        return None


def _suricata_build_info():
    try:
        proc = subprocess.run(
            ["suricata", "--build-info"], capture_output=True, text=True,
            timeout=5,
        )
        if proc.returncode != 0:
            return ""
        return proc.stdout or proc.stderr or ""
    except (subprocess.TimeoutExpired, OSError):
        return ""


def _suricata_build_version():
    info = _suricata_build_info()
    for line in info.splitlines():
        text = line.strip()
        if text.startswith("This is Suricata version"):
            return text.replace("This is Suricata version", "").strip()
    return ""


def _read_yaml_config_value(cfg, key, default=""):
    path = cfg.get("suricata", {}).get("config_path")
    if not path or not os.path.isfile(path):
        return default
    try:
        import yaml
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            data = yaml.safe_load(handle) or {}
    except (OSError, ValueError):
        return default
    if not isinstance(data, dict):
        return default
    value = data.get(key, default)
    return value if value is not None else default


def _capture_mode_from_config(cfg):
    path = cfg.get("suricata", {}).get("config_path")
    if not path or not os.path.isfile(path):
        return "unknown"
    try:
        import yaml
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            data = yaml.safe_load(handle) or {}
    except (OSError, ValueError):
        return "unknown"
    af_packet = data.get("af-packet") if isinstance(data, dict) else None
    if isinstance(af_packet, list) and af_packet:
        first = af_packet[0] if isinstance(af_packet[0], dict) else {}
        iface = first.get("interface") or "?"
        return "AF_PACKET({0})".format(iface)
    return "unknown"


def _decode_socket_json(chunks):
    try:
        return json.loads(b"".join(chunks).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None


def _send_socket_cmd(socket_path, command, arguments=None, timeout=None):
    """Roher suricatasc-Round-Trip mit kommandospezifischem Timeout.

    Kurze Statusbefehle behalten den kleinen Default. Ruleset-Reloads koennen
    mit ET/Open-Regeln erheblich laenger dauern und duerfen nicht nach 5 s als
    "incomplete response" abbrechen.
    """
    if not os.path.exists(socket_path):
        return {"return": "NOK", "message": "socket not found: " + socket_path}

    use_timeout = timeout if timeout is not None else _SOCKET_TIMEOUT
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(use_timeout)
    try:
        sock.connect(socket_path)
        sock.sendall(json.dumps({"version": "0.2"}).encode("utf-8"))
        resp = sock.recv(_RECV_BUFFER)
        if not resp:
            return {"return": "NOK", "message": "no version response"}
        version_reply = json.loads(resp.decode("utf-8"))
        if version_reply.get("return") != "OK":
            return version_reply

        request = {"command": command}
        if arguments is not None:
            request["arguments"] = arguments
        sock.sendall(json.dumps(request).encode("utf-8"))

        chunks = []
        while True:
            try:
                chunk = sock.recv(_RECV_BUFFER)
            except socket.timeout:
                return {"return": "NOK",
                        "message": "socket timeout after {:.1f}s waiting for response to {}".format(
                            use_timeout, command)}
            if not chunk:
                break
            chunks.append(chunk)
            parsed = _decode_socket_json(chunks)
            if parsed is not None:
                return parsed

        if not chunks:
            return {"return": "NOK", "message": "no response from suricata socket"}
        text = b"".join(chunks).decode("utf-8", errors="replace")[:400]
        return {"return": "NOK", "message": "incomplete response: " + text}
    except (socket.error, OSError, json.JSONDecodeError) as exc:
        return {"return": "NOK", "message": "socket error: " + str(exc)}
    finally:
        try:
            sock.close()
        except OSError:
            pass


def send_command(cfg, command, arguments=None, timeout=None):
    """Thread-sicherer Wrapper um _send_socket_cmd."""
    if _is_mock(cfg):
        return _mock_response(command, arguments)

    socket_path = cfg["suricata"]["socket_path"]
    with _LOCK:
        return _send_socket_cmd(socket_path, command, arguments, timeout=timeout)


def get_uptime(cfg):
    res = send_command(cfg, "uptime")
    if res.get("return") == "OK":
        return res
    uptime = _process_uptime_seconds("suricata")
    if uptime is not None:
        return {"return": "OK", "message": uptime,
                "source": "procfs", "socket_message": res.get("message", "")}
    return res


def get_running_mode(cfg):
    res = send_command(cfg, "running-mode")
    if res.get("return") == "OK":
        return res
    runmode = _read_yaml_config_value(cfg, "runmode", "unknown")
    return {"return": "OK", "message": runmode,
            "source": "suricata.yaml", "socket_message": res.get("message", "")}


def get_capture_mode(cfg):
    res = send_command(cfg, "capture-mode")
    if res.get("return") == "OK":
        return res
    capture = _capture_mode_from_config(cfg)
    return {"return": "OK", "message": capture,
            "source": "suricata.yaml", "socket_message": res.get("message", "")}


def get_version(cfg):
    res = send_command(cfg, "version")
    if res.get("return") == "OK":
        return res
    version = _suricata_build_version()
    if version:
        return {"return": "OK", "message": version,
                "source": "suricata --build-info",
                "socket_message": res.get("message", "")}
    return res


def get_iface_stats(cfg, iface=None):
    if iface is None:
        return send_command(cfg, "iface-list")
    return send_command(cfg, "iface-stat", {"iface": iface})


def _suricata_config_test(cfg):
    """Pruefe Suricata-Konfiguration und Rulesets vor jedem Reload."""
    config_path = cfg.get("suricata", {}).get("config_path", "/etc/suricata/suricata.yaml")
    try:
        proc = subprocess.run(
            ["suricata", "-T", "-c", config_path, "-v"],
            capture_output=True, text=True, timeout=_SOCKET_TIMEOUT_TEST,
        )
        msg = ((proc.stdout or "") + (proc.stderr or "")).strip()
        if proc.returncode == 0:
            return {"return": "OK", "stage": "suricata-test",
                    "message": (msg or "config test ok")[-4000:]}
        return {"return": "NOK", "stage": "suricata-test",
                "message": (msg or "suricata -T failed")[-4000:]}
    except (subprocess.TimeoutExpired, OSError) as exc:
        return {"return": "NOK", "stage": "suricata-test",
                "message": "suricata -T error: " + str(exc)}


def test_suricata_config(cfg):
    return _suricata_config_test(cfg)


def _run_suricatasc_command(cmd, timeout):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        msg = ((proc.stdout or "") + (proc.stderr or "")).strip()
        if proc.returncode == 0:
            return {"return": "OK", "message": msg or "ruleset reload requested",
                    "command": " ".join(cmd)}
        return {"return": "NOK", "message": msg or "command failed",
                "command": " ".join(cmd)}
    except (subprocess.TimeoutExpired, OSError) as exc:
        return {"return": "NOK", "message": str(exc),
                "command": " ".join(cmd)}


def _suricatasc_cli_reload(cfg):
    socket_path = cfg.get("suricata", {}).get(
        "socket_path", "/var/run/suricata/suricata-command.socket")
    if shutil.which("suricatasc") is None:
        return {"return": "NOK", "stage": "suricatasc-cli",
                "message": "suricatasc binary not found"}

    candidates = []
    if socket_path:
        candidates.append(["suricatasc", "-c", "ruleset-reload-rules", socket_path])
    candidates.append(["suricatasc", "-c", "ruleset-reload-rules"])
    if socket_path:
        candidates.append(["suricatasc", "-c", "ruleset-reload-nonblocking", socket_path])
    candidates.append(["suricatasc", "-c", "ruleset-reload-nonblocking"])

    errors = []
    for cmd in candidates:
        res = _run_suricatasc_command(cmd, _SOCKET_TIMEOUT_RELOAD)
        if res.get("return") == "OK":
            res["stage"] = "suricatasc-cli"
            return res
        errors.append(res.get("command", "") + ": " + res.get("message", ""))
    return {"return": "NOK", "stage": "suricatasc-cli",
            "message": " | ".join(errors)[-2000:]}


def _suricatasc_socket_reload(cfg):
    primary = send_command(cfg, "ruleset-reload-rules", timeout=_SOCKET_TIMEOUT_RELOAD)
    if primary.get("return") == "OK":
        primary["stage"] = "suricatasc-socket"
        primary["command"] = "ruleset-reload-rules"
        return primary

    secondary = send_command(cfg, "ruleset-reload-nonblocking", timeout=_SOCKET_TIMEOUT_RELOAD)
    if secondary.get("return") == "OK":
        secondary["stage"] = "suricatasc-socket"
        secondary["command"] = "ruleset-reload-nonblocking"
        secondary["note"] = "blocking variant failed: " + primary.get("message", "")
        return secondary

    return {"return": "NOK", "stage": "suricatasc-socket",
            "message": "ruleset-reload-rules failed: " + primary.get("message", "") +
                       " | ruleset-reload-nonblocking failed: " + secondary.get("message", "")}


def _suricatasc_reload(cfg):
    cli_res = _suricatasc_cli_reload(cfg)
    if cli_res.get("return") == "OK":
        return cli_res
    socket_res = _suricatasc_socket_reload(cfg)
    if socket_res.get("return") == "OK":
        socket_res["cli_message"] = cli_res.get("message", "")[-800:]
        return socket_res
    return {"return": "NOK", "stage": "suricatasc",
            "message": "cli failed: " + cli_res.get("message", "")[-1000:] +
                       " | socket failed: " + socket_res.get("message", "")[-1000:]}


def _sigusr2_reload():
    """Letzter Fallback: SIGUSR2 triggert Reload, ohne Socket."""
    try:
        proc = subprocess.run(["pidof", "suricata"],
                              capture_output=True, text=True, timeout=5)
        if proc.returncode != 0 or not proc.stdout.strip():
            return {"return": "NOK", "stage": "signal",
                    "message": "suricata pid not found"}
        pid = proc.stdout.split()[0]
        kill_proc = subprocess.run(["kill", "-USR2", pid],
                                   capture_output=True, text=True, timeout=5)
        if kill_proc.returncode == 0:
            return {"return": "OK", "stage": "signal",
                    "message": "SIGUSR2 sent to suricata pid " + pid +
                               " (reload runs asynchronously, check logs)"}
        return {"return": "NOK", "stage": "signal",
                "message": (kill_proc.stderr or kill_proc.stdout or "kill failed").strip()}
    except (subprocess.TimeoutExpired, OSError, IndexError) as exc:
        return {"return": "NOK", "stage": "signal", "message": str(exc)}


def reload_rules(cfg):
    """Reload aller Regeln im laufenden Suricata. Kein Restart noetig.

    Der Ablauf ist bewusst fail-safe:
      1. suricata -T prueft YAML und Rulesets.
      2. Nur bei erfolgreichem Test wird ein Live-Reload angefordert.
      3. CLI/socket/SIGUSR2 sind Transportvarianten fuer denselben Suricata-Reload.
      4. Bei Fehlern bleibt die alte Detection Engine aktiv.
    """
    if _is_mock(cfg):
        return {"return": "OK", "stage": "mock", "message": "[mock] reload skipped"}

    command = _configured_command(cfg, "reload_command")
    if command and _control_mode(cfg) == "command":
        res = _run_shell_command(command, 120)
        res.setdefault("stage", "reload_command")
        return res

    test_res = _suricata_config_test(cfg)
    if test_res.get("return") != "OK":
        return {"return": "NOK", "stage": "suricata-test",
                "message": "config test failed; reload NOT triggered; "
                           "running engine keeps previous rules. detail: " +
                           test_res.get("message", "")}

    sc_res = _suricatasc_reload(cfg)
    if sc_res.get("return") == "OK":
        sc_res["test_message"] = test_res.get("message", "")[-500:]
        return sc_res

    sig_res = _sigusr2_reload()
    if sig_res.get("return") == "OK":
        sig_res["suricatasc_message"] = sc_res.get("message", "")[-1200:]
        sig_res["test_message"] = test_res.get("message", "")[-500:]
        return sig_res

    return {"return": "NOK", "stage": "reload-failed",
            "message": "all reload paths failed",
            "suricatasc_message": sc_res.get("message", ""),
            "signal_message": sig_res.get("message", "")}


def restart_service(cfg):
    """Service-Neustart; in Containern bewusst nicht gefaked."""
    if _is_mock(cfg):
        return {"return": "OK", "message": "[mock] restart skipped"}

    command = _configured_command(cfg, "restart_command")
    if command:
        return _run_shell_command(command, 30)

    mode = _control_mode(cfg)
    if mode == "socket":
        return {"return": "OK", "requires_container_restart": True,
                "message": "container restart required: use scripts/phimantec_compose.sh compose restart suricata-node proxy"}

    if mode in ("auto", "systemctl") and _systemctl_available():
        unit = cfg["suricata"]["service_unit"]
        try:
            proc = subprocess.run(
                ["sudo", "-n", "systemctl", "restart", unit],
                capture_output=True, text=True, timeout=30,
            )
            if proc.returncode == 0:
                return {"return": "OK", "message": "restarted"}
            return {"return": "NOK", "message": proc.stderr or proc.stdout}
        except (subprocess.TimeoutExpired, OSError) as exc:
            return {"return": "NOK", "message": str(exc)}

    return {"return": "NOK",
            "message": "systemctl not available and no restart_command configured"}


def service_status(cfg):
    """Status fuer VM/systemd und Container ohne systemd."""
    if _is_mock(cfg):
        return {"return": "OK", "status": "active",
                "message": "[mock] simulated active"}

    command = _configured_command(cfg, "status_command")
    if command:
        res = _run_shell_command(command, 5)
        status = "active" if res.get("return") == "OK" else "unknown"
        res["status"] = status
        return res

    mode = _control_mode(cfg)
    if mode in ("auto", "systemctl") and _systemctl_available():
        unit = cfg["suricata"]["service_unit"]
        try:
            proc = subprocess.run(
                ["systemctl", "is-active", unit],
                capture_output=True, text=True, timeout=5,
            )
            status = proc.stdout.strip() or proc.stderr.strip()
            return {"return": "OK" if proc.returncode == 0 else "NOK",
                    "status": status, "message": status}
        except (subprocess.TimeoutExpired, OSError) as exc:
            return {"return": "NOK", "status": "unknown", "message": str(exc)}

    if _process_is_running("suricata"):
        return {"return": "OK", "status": "active",
                "message": "suricata process running"}
    return {"return": "NOK", "status": "inactive",
            "message": "suricata process not found"}


def _mock_response(command, arguments=None):
    _ = arguments
    if command == "uptime":
        return {"return": "OK", "message": 12345}
    if command == "running-mode":
        return {"return": "OK", "message": "workers"}
    if command == "capture-mode":
        return {"return": "OK", "message": "AF_PACKET_DEV"}
    if command == "version":
        return {"return": "OK", "message": "Suricata 8.0.5 mock"}
    if command == "ruleset-stats":
        return {"return": "OK", "message": {"rules_loaded": 4, "rules_failed": 0}}
    if command == "ruleset-reload-time":
        return {"return": "OK", "message": int(time.time())}
    if command in ("ruleset-reload-rules", "ruleset-reload-nonblocking", "reload-rules"):
        return {"return": "OK", "message": "[mock] rules reloaded"}
    return {"return": "OK", "message": "[mock] " + command}
