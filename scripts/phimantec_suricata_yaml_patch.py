#!/usr/bin/env python3
"""Container-/VM-sicherer Suricata-YAML-Patcher fuer PHIMANTEC.

Der Patcher veraendert keine Suricata-Engine-Logik. Er setzt nur die fuer die
Proxy-Lane noetigen Runtime-Defaults: EVE alert payload-printable,
Unix-Command-Socket, rule-files und ein AF_PACKET-Interface.
"""

import argparse
import os
import shutil
import sys
import time

try:
    import yaml
except ImportError:
    yaml = None


def load_yaml(path):
    if yaml is None:
        raise RuntimeError("PyYAML ist erforderlich")
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        data = yaml.safe_load(fh) or {}
    if not isinstance(data, dict):
        raise RuntimeError("Suricata YAML muss ein Mapping als Root haben")
    return data


def write_yaml(path, data, backup):
    if backup and os.path.exists(path):
        suffix = time.strftime("%Y%m%d-%H%M%S")
        shutil.copy2(path, path + ".bak." + suffix)
    rendered = yaml.dump(data, default_flow_style=False, sort_keys=False,
                         allow_unicode=True)
    if not rendered.startswith("%YAML"):
        rendered = "%YAML 1.1\n---\n" + rendered
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(rendered)
    os.replace(tmp, path)


def find_named_list_item(items, name):
    if not isinstance(items, list):
        return None
    for item in items:
        if isinstance(item, dict) and name in item:
            inner = item[name]
            if isinstance(inner, dict):
                return inner
    return None


def ensure_named_list_item(root, list_key, item_name):
    items = root.get(list_key)
    if not isinstance(items, list):
        items = []
        root[list_key] = items
    existing = find_named_list_item(items, item_name)
    if existing is not None:
        return existing
    entry = {}
    items.append({item_name: entry})
    return entry


def ensure_rule_file(root, rule_file):
    """Ensure PHIMANTEC, ET/Open and external rule files are loaded.

    We merge instead of overwriting so that suricata-update managed rules and
    external/*.rules survive restarts and UI edits. Missing managed files are
    created by the container entrypoint before Suricata starts.
    """
    existing = root.get("rule-files")
    if not isinstance(existing, list):
        existing = []
    desired = ["suricata.rules", rule_file, "external/custom-local.rules"]
    merged = []
    for item in desired + existing:
        if isinstance(item, str) and item and item not in merged:
            merged.append(item)
    root["rule-files"] = merged


def ensure_unix_command(root, socket_path):
    cmd = root.get("unix-command")
    if not isinstance(cmd, dict):
        cmd = {}
        root["unix-command"] = cmd
    cmd["enabled"] = True
    cmd["filename"] = socket_path


def ensure_eve_output(root, eve_filename):
    eve = ensure_named_list_item(root, "outputs", "eve-log")
    eve["enabled"] = True
    eve["filetype"] = eve.get("filetype", "regular")
    eve["filename"] = eve_filename
    types = eve.get("types")
    if not isinstance(types, list):
        types = []
        eve["types"] = types
    alert = find_named_list_item(types, "alert")
    if alert is None:
        alert = {}
        types.insert(0, {"alert": alert})
    alert["enabled"] = True
    alert["payload"] = True
    alert["payload-printable"] = True
    alert["metadata"] = True
    alert["payload-buffer-size"] = 65535
    http = find_named_list_item(types, "http")
    if http is None:
        http = {}
        types.append({"http": http})
    http["enabled"] = True
    http["extended"] = True


def ensure_app_layer_http(root, body_limit):
    """Enable HTTP parser and align body inspection depth with the proxy lane.

    PHIMANTEC forwards decrypted request/response samples to Suricata as HTTP
    POST bodies.  The proxy buffers up to tls.max_body_bytes, currently 1 MiB.
    Suricata's default HTTP request-body limit is too small for long text/HTML
    responses, so the clear-lane HTTP parser must inspect the same window the
    proxy forwards.
    """
    app_layer = root.get("app-layer")
    if not isinstance(app_layer, dict):
        app_layer = {}
        root["app-layer"] = app_layer
    protocols = app_layer.get("protocols")
    if not isinstance(protocols, dict):
        protocols = {}
        app_layer["protocols"] = protocols
    http = protocols.get("http")
    if not isinstance(http, dict):
        http = {}
        protocols["http"] = http
    http["enabled"] = True

    libhtp = http.get("libhtp")
    if not isinstance(libhtp, dict):
        libhtp = {}
        http["libhtp"] = libhtp
    default_config = libhtp.get("default-config")
    if not isinstance(default_config, dict):
        default_config = {}
        libhtp["default-config"] = default_config

    default_config["request-body-limit"] = body_limit
    default_config["response-body-limit"] = body_limit
    default_config["request-body-minimal-inspect-size"] = body_limit
    default_config["request-body-inspect-window"] = body_limit
    default_config["response-body-minimal-inspect-size"] = body_limit
    default_config["response-body-inspect-window"] = body_limit


def ensure_af_packet(root, interface, threads):
    values = root.get("af-packet")
    if not isinstance(values, list) or len(values) == 0:
        values = [{}]
        root["af-packet"] = values
    item = values[0]
    if not isinstance(item, dict):
        item = {}
        values[0] = item
    item["interface"] = interface
    if threads is not None and str(threads).strip() != "":
        item["threads"] = threads
    else:
        item.setdefault("threads", "auto")
    item.setdefault("cluster-id", 98)
    item.setdefault("cluster-type", "cluster_flow")
    item.setdefault("defrag", True)
    item.setdefault("use-mmap", True)
    item.setdefault("tpacket-v3", True)
    item.setdefault("ring-size", 2048)


def apply_phimantec_defaults(data, args):
    data["default-log-dir"] = args.log_dir
    data["default-rule-path"] = args.rule_path
    data["runmode"] = args.runmode
    data["mpm-algo"] = args.mpm_algo
    data["spm-algo"] = args.spm_algo
    ensure_unix_command(data, args.command_socket)
    ensure_rule_file(data, args.local_rules_file)
    ensure_eve_output(data, args.eve_file)
    ensure_app_layer_http(data, args.body_limit)
    ensure_af_packet(data, args.interface, args.af_packet_threads)
    return data


def build_parser():
    parser = argparse.ArgumentParser(description="Patch Suricata YAML for PHIMANTEC")
    parser.add_argument("--config", required=True)
    parser.add_argument("--interface", default="eth0")
    parser.add_argument("--log-dir", default="/var/log/suricata")
    parser.add_argument("--rule-path", default="/var/lib/suricata/rules")
    parser.add_argument("--local-rules-file", default="phimantec-local.rules")
    parser.add_argument("--eve-file", default="eve.json")
    parser.add_argument("--command-socket", default="/var/run/suricata/suricata-command.socket")
    parser.add_argument("--mpm-algo", default="hs")
    parser.add_argument("--spm-algo", default="hs")
    parser.add_argument("--runmode", default="workers")
    parser.add_argument("--af-packet-threads", default="auto")
    parser.add_argument("--body-limit", type=int, default=1048576,
                        help="HTTP request/response body inspection depth for the PHIMANTEC clear lane")
    parser.add_argument("--no-backup", action="store_true")
    return parser


def main(argv):
    parser = build_parser()
    args = parser.parse_args(argv)
    data = load_yaml(args.config)
    data = apply_phimantec_defaults(data, args)
    write_yaml(args.config, data, backup=not args.no_backup)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
