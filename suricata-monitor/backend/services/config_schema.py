"""Suricata-Konfigurations-Schema (v1.3).

Single Source of Truth fuer die Form-driven Settings-UI.

NEUE STRUKTUR v1.1:
    - Top-Level "groups": logische Cluster (Allgemein / Betriebsmodus / Detektion / Performance)
    - Top-Level "sections": flache Liste, jede Section verweist via "group" auf ihre Gruppe
    - Frontend rendert Sidebar mit Gruppen-Headers + Sections

CAPTURE vs. MODE - Klarstellung:
    Suricata trennt
        1) WIE Pakete reinkommen (Capture-Backend: AF_PACKET / NFQUEUE / PCAP / ...)
        2) WAS damit passiert (IDS = passiv beobachten, IPS = aktiv blocken)
    Daher gibt es jetzt eine separate "IDS/IPS Verhalten"-Sektion
    (action-order, stream.inline, host-mode) und mehrere Capture-Sektionen.

Feld-Typen:
    string             einzeiliger Text
    text               mehrzeilig (textarea)
    int                Ganzzahl mit optional min/max
    range              Trackbar/Zahlenwert mit min/max/step
    range-or-auto      Trackbar plus Auto-Schalter
    bool               on/off Switch
    enum               Auswahl aus options[]
    interface-select   wie string, Frontend zeigt Dropdown via /system/interfaces
    list-of-strings    z.B. HOME_NET
    list-of-objects    z.B. af-packet Interfaces (Sub-Schema in 'item_fields')

Path-Syntax:
    "default-log-dir"
    "vars.address-groups.HOME_NET"
    "outputs[eve-log].types[alert].enabled"
"""

# =========================================================================
# Gruppe 1: ALLGEMEIN
# =========================================================================

_GENERAL = {
    "key": "general",
    "group": "general_group",
    "label": "Grundeinstellungen",
    "icon": "settings",
    "description": "Grundlegende Pfade und Run-Modus.",
    "fields": [
        {"path": "default-log-dir", "type": "string",
         "label": "Log-Verzeichnis", "default": "/var/log/suricata",
         "help": "Hier landen eve.json, fast.log, stats.log usw."},
        {"path": "default-rule-path", "type": "string",
         "label": "Regel-Verzeichnis", "default": "/etc/suricata/rules",
         "help": "Basis-Pfad fuer alle in 'rule-files' aufgefuehrten Dateien.",
         "phimantec_warning": "Beim PHIMANTEC Proxy-Lane-Profil muss dieses Verzeichnis mit inspectord/Suricata und der Datei phimantec-local.rules konsistent bleiben."},
        {"path": "rule-files", "type": "list-of-strings",
         "label": "Rule-Files",
         "default": ["suricata.rules", "phimantec-local.rules"],
         "help": "Regeldateien, die Suricata laden soll.",
         "phimantec_warning": "phimantec-local.rules sollte fuer Proxy IPS/IDS-Verdicts aktiv bleiben. Entfernen ist moeglich, kann aber Blocking-Signale verhindern."},
        {"path": "default-packet-size", "type": "int",
         "label": "Packet-Buffer-Size (Bytes)", "default": 1538,
         "min": 64, "max": 9000,
         "help": "Erhoehen, wenn Jumbo-Frames auf dem Capture-Interface laufen."},
        {"path": "runmode", "type": "enum",
         "label": "Run-Modus",
         "options": ["autofp", "workers", "single"], "default": "autofp",
         "help": "workers: ein Thread pro NIC-Queue (empfohlen). "
                 "autofp: Affinity-basiert. single: ein Thread (Debug)."},
        {"path": "unix-command.enabled", "type": "bool",
         "label": "Unix-Command-Socket aktivieren", "default": True,
         "help": "Wird von SuricataMonitor fuer Live-Kommandos genutzt."},
        {"path": "unix-command.filename", "type": "string",
         "label": "Socket-Pfad",
         "default": "/var/run/suricata/suricata-command.socket"},
    ],
}

_VARS = {
    "key": "vars",
    "group": "general_group",
    "label": "Netzwerk-Variablen",
    "icon": "network",
    "description": "HOME_NET / EXTERNAL_NET und weitere Adress-/Port-Gruppen, "
                   "die in Regeln referenziert werden.",
    "fields": [
        {"path": "vars.address-groups.HOME_NET", "type": "list-of-strings",
         "label": "HOME_NET",
         "default": ["192.168.0.0/16", "10.0.0.0/8", "172.16.0.0/12"],
         "help": "Eigene/interne Netze."},
        {"path": "vars.address-groups.EXTERNAL_NET", "type": "list-of-strings",
         "label": "EXTERNAL_NET", "default": ["!$HOME_NET"],
         "help": "Externe Netze. '!$HOME_NET' = alles ausser HOME_NET."},
        {"path": "vars.address-groups.HTTP_SERVERS", "type": "list-of-strings",
         "label": "HTTP_SERVERS", "default": ["$HOME_NET"]},
        {"path": "vars.address-groups.SMTP_SERVERS", "type": "list-of-strings",
         "label": "SMTP_SERVERS", "default": ["$HOME_NET"]},
        {"path": "vars.address-groups.DNS_SERVERS", "type": "list-of-strings",
         "label": "DNS_SERVERS", "default": ["$HOME_NET"]},
        {"path": "vars.port-groups.HTTP_PORTS", "type": "list-of-strings",
         "label": "HTTP_PORTS", "default": ["80"]},
        {"path": "vars.port-groups.SHELLCODE_PORTS", "type": "list-of-strings",
         "label": "SHELLCODE_PORTS", "default": ["!80"]},
    ],
}


# =========================================================================
# Gruppe 2: BETRIEBSMODUS (NEU)
# =========================================================================

_IDS_IPS = {
    "key": "ids_ips",
    "group": "modes",
    "label": "IDS / IPS Verhalten",
    "icon": "shield",
    "description": "Steuert wie Suricata mit Regel-Treffern umgeht. "
                   "IDS = nur Alerts, IPS = aktiv blocken (Pakete laufen "
                   "durch Suricata). Fuer IPS muss ZUSAETZLICH ein "
                   "passendes Capture-Backend konfiguriert sein "
                   "(AF_PACKET mit copy-mode=ips ODER NFQUEUE).",
    "fields": [
        {"path": "host-mode", "type": "enum",
         "label": "Host-Modus",
         "options": ["auto", "sniffer-only", "router"],
         "default": "auto",
         "help": "sniffer-only = klassischer IDS. router = Decoder-Pfade "
                 "fuer In-Path-Setup (IPS). auto = aus Capture-Mode ableiten.",
         "phimantec_warning": "Fuer PHIMANTEC bleibt der Standardpfad Proxy -> klare Lane -> Suricata -> FlowBus. router/Inline-Profile bitte getrennt testen."},
        {"path": "stream.inline", "type": "enum",
         "label": "Stream-Engine Inline-Modus",
         "options": ["no", "yes", "auto"],
         "default": "auto",
         "help": "auto/yes = Stream-Reassembly im Inline-Modus, Pflicht fuer "
                 "sinnvolles IPS. no = klassische IDS-Reassembly.",
         "phimantec_warning": "Aenderungen am Inline-Modus koennen die Proxy-Lane-Korrelation beeinflussen; fuer das Standardprofil auto beibehalten."},
        {"path": "action-order", "type": "list-of-strings",
         "label": "Action-Order",
         "default": ["pass", "drop", "reject", "alert"],
         "help": "Reihenfolge entscheidet welche Action bei mehreren "
                 "matchenden Regeln gewinnt. Top = staerkster Effekt.",
         "phimantec_warning": "PHIMANTEC nutzt Alerts als Block-Signal im Proxy. Action-Order nur mit Regeltest aendern."},
        {"path": "exception-policy", "type": "enum",
         "label": "Exception-Policy (Suricata 7+)",
         "options": ["", "ignore", "drop-flow", "drop-packet",
                     "reject", "pass-flow", "pass-packet", "bypass", "auto"],
         "default": "",
         "help": "Verhalten wenn interner Limit ueberschritten wird "
                 "(z.B. memcap erreicht). Leer = Suricata-Default."},
    ],
}

_CAPTURE_AFPACKET = {
    "key": "capture_afpacket",
    "group": "modes",
    "label": "Capture: AF_PACKET",
    "icon": "network",
    "description": "Linux AF_PACKET-Capture. Fuer IPS-L2-Bridge: zwei "
                   "Interfaces mit copy-mode=ips + copy-iface (auf das "
                   "jeweils andere Interface verweisend).",
    "fields": [
        {
            "path": "af-packet",
            "type": "list-of-objects",
            "label": "AF_PACKET Interfaces",
            "help": "Mehrere Interfaces moeglich. Fuer Bridge-IPS: ZWEI "
                    "Eintraege mit copy-mode=ips + copy-iface ueberkreuz.",
            "item_fields": [
                {"path": "interface", "type": "interface-select",
                 "label": "Interface", "required": True,
                 "placeholder": "ens18",
                 "help": "Dropdown listet alle aktiven NICs des Hosts."},
                {"path": "threads", "type": "range-or-auto", "label": "Threads / Pool",
                 "default": "auto", "min": 1, "max": 16, "step": 1,
                 "help": "'auto' = NIC-Queues nutzen. Trackbar: single=1, Pool=2..16."},
                {"path": "cluster-id", "type": "int", "label": "Cluster-ID",
                 "default": 99, "min": 1, "max": 65535,
                 "help": "Pro Interface eindeutig. Wichtig fuer Multi-NIC."},
                {"path": "cluster-type", "type": "enum",
                 "label": "Cluster-Type",
                 "options": ["cluster_flow", "cluster_cpu", "cluster_qm",
                             "cluster_ebpf"],
                 "default": "cluster_flow",
                 "help": "cluster_flow = symmetric hashing. cluster_cpu = "
                         "Round-Robin auf CPUs."},
                {"path": "defrag", "type": "bool", "label": "IP-Defrag",
                 "default": True},
                {"path": "use-mmap", "type": "bool", "label": "Use MMAP",
                 "default": True},
                {"path": "tpacket-v3", "type": "bool", "label": "TPACKET v3",
                 "default": True,
                 "help": "Effizienter Ringpuffer (empfohlen)."},
                {"path": "ring-size", "type": "int", "label": "Ring-Size",
                 "default": 2048,
                 "help": "Anzahl Slots im Capture-Ring."},
                {"path": "copy-mode", "type": "enum",
                 "label": "Copy-Mode (IPS-Bridge)",
                 "options": ["", "none", "tap", "ips"], "default": "",
                 "help": "Leer/none = klassisches IDS-Sniffing. tap = "
                         "Pakete forwarden, kein Block. ips = Pakete an "
                         "copy-iface forwarden, drop-Regeln greifen.",
                 "phimantec_warning": "copy-mode=tap/ips ist moeglich, aber fuer die PHIMANTEC Proxy-Lane normalerweise nicht erforderlich."},
                {"path": "copy-iface", "type": "interface-select",
                 "label": "Copy-Iface (Partner-NIC)",
                 "placeholder": "ens19",
                 "help": "Nur sinnvoll wenn copy-mode != leer. Das Partner-"
                         "Interface der L2-Bridge."},
            ],
        },
    ],
}

_CAPTURE_NFQUEUE = {
    "key": "capture_nfqueue",
    "group": "modes",
    "label": "Capture: NFQUEUE (IPS)",
    "icon": "shield",
    "description": "IPS via Linux netfilter. Pakete kommen ueber "
                   "'iptables -j NFQUEUE --queue-num N' in Suricata "
                   "und werden nach Inspektion an den Kernel zurueckgegeben.",
    "fields": [
        {"path": "nfq.mode", "type": "enum",
         "label": "NFQ-Mode",
         "options": ["accept", "repeat", "route"], "default": "accept",
         "help": "accept = Paket akzeptieren (haeufigste Wahl). "
                 "repeat = nach Inspektion wieder in iptables einreichen. "
                 "route = an Custom-Routing.",
         "phimantec_warning": "NFQUEUE ist ein anderes IPS-Profil. Fuer den Proxy-Betrieb bleibt die klare Lane plus FlowBus der empfohlene Pfad."},
        {"path": "nfq.repeat-mark", "type": "int",
         "label": "Repeat-Mark", "default": 1,
         "help": "Mark-Wert wenn mode=repeat. iptables-Rule braucht "
                 "passende Mark-Bedingung."},
        {"path": "nfq.repeat-mask", "type": "int",
         "label": "Repeat-Mask", "default": 1},
        {"path": "nfq.route-queue", "type": "int",
         "label": "Route-Queue",
         "default": 2,
         "help": "Wenn mode=route: Ziel-Queue-Nr."},
        {"path": "nfq.batchcount", "type": "int",
         "label": "Batch-Count", "default": 20, "min": 1, "max": 256,
         "help": "Wieviele Pakete pro Verdict-Batch. Erhoeht Throughput, "
                 "erhoeht Latenz."},
        {"path": "nfq.fail-open", "type": "bool",
         "label": "Fail-Open",
         "default": True,
         "help": "Bei Suricata-Crash: Pakete durchlassen (fail-open) statt "
                 "blocken (fail-closed). Sicherheit vs. Verfuegbarkeit."},
        {"path": "nfq.bypass", "type": "bool",
         "label": "Bypass (Conntrack)", "default": False,
         "help": "Pakete ueber Conntrack-Bypass an Kernel zurueck. "
                 "Reduziert CPU."},
    ],
}

_CAPTURE_OTHER = {
    "key": "capture_other",
    "group": "modes",
    "label": "Capture: PCAP / DPDK / Weitere",
    "icon": "activity",
    "description": "Alternative Capture-Backends. Nur eines davon "
                   "zur Zeit nutzen. Spezialisten-Backends (PF-RING, "
                   "Napatech, Netmap, XDP) bitte ueber den Raw-YAML-"
                   "Tab editieren.",
    "fields": [
        {"path": "pcap[0].interface", "type": "interface-select",
         "label": "PCAP-Interface",
         "placeholder": "(leer = AF_PACKET nutzen)",
         "help": "Klassisches libpcap-Capture. Langsamer als AF_PACKET, "
                 "dafuer ueberall verfuegbar. Nur fuer IDS.",
         "phimantec_warning": "PCAP ist moeglich, aber fuer Docker-/VM-Lane-Last meist weniger geeignet als AF_PACKET."},
        {"path": "pcap[0].buffer-size", "type": "int",
         "label": "PCAP-Buffer-Size", "default": 0,
         "help": "0 = libpcap-Default. Sonst Bytes."},
        {"path": "pcap[0].checksum-checks", "type": "enum",
         "label": "PCAP-Checksum-Checks",
         "options": ["auto", "yes", "no"], "default": "auto"},
        {"path": "dpdk.eal-params.proc-type", "type": "enum",
         "label": "DPDK Process-Type",
         "options": ["", "primary", "secondary", "auto"], "default": "",
         "help": "Nur wenn DPDK-Backend genutzt wird. Erfordert DPDK-Build.",
         "phimantec_warning": "DPDK ist ein Spezialprofil und sollte vom Standard-Proxy-Lane-Kontext getrennt bleiben."},
    ],
}


# =========================================================================
# Gruppe 3: DETEKTION
# =========================================================================

_APP_LAYER = {
    "key": "app_layer",
    "group": "detection",
    "label": "App-Layer-Protokolle",
    "icon": "shield",
    "description": "Welche L7-Protokolle Suricata parsen soll. "
                   "Auswirkungen auf EVE-Output und Regel-Wirkung.",
    "fields": [
        {"path": "app-layer.protocols.http.enabled", "type": "bool",
         "label": "HTTP", "default": True},
        {"path": "app-layer.protocols.http.libhtp.default-config.request-body-limit", "type": "int",
         "label": "HTTP Request-Body-Limit", "default": 1048576,
         "min": 4096, "max": 16777216,
         "help": "Inspektionstiefe fuer PHIMANTEC-Lane-POSTs. Standard: 1 MiB, passend zum Proxy-Sample-Fenster."},
        {"path": "app-layer.protocols.http.libhtp.default-config.response-body-limit", "type": "int",
         "label": "HTTP Response-Body-Limit", "default": 1048576,
         "min": 4096, "max": 16777216,
         "help": "Symmetrischer HTTP-Body-Parser-Limitwert fuer Suricata."},
        {"path": "app-layer.protocols.http.libhtp.default-config.request-body-inspect-window", "type": "int",
         "label": "HTTP Request-Inspect-Window", "default": 1048576,
         "min": 4096, "max": 16777216,
         "help": "Sichtfenster fuer Pattern-Matching im HTTP-Body. Standard deckt das komplette PHIMANTEC-Sample ab."},
        {"path": "app-layer.protocols.tls.enabled", "type": "bool",
         "label": "TLS / SSL", "default": True},
        {"path": "app-layer.protocols.tls.ja3-fingerprints", "type": "bool",
         "label": "JA3-Fingerprints (TLS)", "default": True,
         "help": "Client-Fingerprint - wertvoll fuer Threat-Hunting."},
        {"path": "app-layer.protocols.dns.tcp.enabled", "type": "bool",
         "label": "DNS ueber TCP", "default": True},
        {"path": "app-layer.protocols.dns.udp.enabled", "type": "bool",
         "label": "DNS ueber UDP", "default": True},
        {"path": "app-layer.protocols.smtp.enabled", "type": "bool",
         "label": "SMTP", "default": True},
        {"path": "app-layer.protocols.imap.enabled", "type": "bool",
         "label": "IMAP", "default": False},
        {"path": "app-layer.protocols.ftp.enabled", "type": "bool",
         "label": "FTP", "default": True},
        {"path": "app-layer.protocols.ssh.enabled", "type": "bool",
         "label": "SSH", "default": True},
        {"path": "app-layer.protocols.smb.enabled", "type": "bool",
         "label": "SMB", "default": True},
        {"path": "app-layer.protocols.nfs.enabled", "type": "bool",
         "label": "NFS", "default": False},
        {"path": "app-layer.protocols.tftp.enabled", "type": "bool",
         "label": "TFTP", "default": False},
        {"path": "app-layer.protocols.dhcp.enabled", "type": "bool",
         "label": "DHCP", "default": True},
        {"path": "app-layer.protocols.snmp.enabled", "type": "bool",
         "label": "SNMP", "default": False},
        {"path": "app-layer.protocols.modbus.enabled", "type": "bool",
         "label": "Modbus", "default": False},
        {"path": "app-layer.protocols.dnp3.enabled", "type": "bool",
         "label": "DNP3", "default": False,
         "help": "SCADA-Protokoll. Nur aktivieren, wenn relevant."},
        {"path": "app-layer.protocols.enip.enabled", "type": "bool",
         "label": "EtherNet/IP (ENIP)", "default": False},
        {"path": "app-layer.protocols.krb5.enabled", "type": "bool",
         "label": "Kerberos (KRB5)", "default": True},
        {"path": "app-layer.protocols.rdp.enabled", "type": "bool",
         "label": "RDP", "default": True},
    ],
}

_OUTPUTS = {
    "key": "outputs",
    "group": "detection",
    "label": "Outputs (EVE-Log)",
    "icon": "file",
    "description": "EVE-Log ist Suricatas primaere strukturierte Logging-"
                   "Quelle. Hier waehlst du welche Event-Typen mit "
                   "hineingeschrieben werden.",
    "fields": [
        {"path": "outputs[eve-log].enabled", "type": "bool",
         "label": "EVE-Log aktivieren", "default": True,
         "help": "Schreibt eve.json mit Alerts + Flow-/HTTP-/TLS-Records.",
         "phimantec_warning": "Nicht deaktivieren, solange inspectord die Proxy-Verdicts aus EVE-Alerts ableitet."},
        {"path": "outputs[eve-log].filename", "type": "string",
         "label": "EVE-Filename", "default": "eve.json",
         "phimantec_warning": "Der inspectord erwartet im Standardprofil eve.json. Bei Aenderung muss phimantec-suricata-node.conf synchron angepasst werden."},
        {"path": "outputs[eve-log].rotate-interval", "type": "enum",
         "label": "Rotations-Intervall",
         "options": ["", "minute", "hour", "day"], "default": "",
         "help": "Leer = keine zeitbasierte Rotation (Logrotate uebernimmt)."},
        {"path": "outputs[eve-log].types[alert].enabled", "type": "bool",
         "label": "Type: alert", "default": True,
         "help": "Eigentliche Signature-Treffer.",
         "phimantec_warning": "Alert-Events sind der Kern der PHIMANTEC FlowBus-Entscheidung."},
        {"path": "outputs[eve-log].types[alert].payload-printable",
         "type": "bool", "label": "alert.payload-printable", "default": True,
         "phimantec_warning": "Erforderlich, damit X-Phim-Flow-Id fuer inspectord sichtbar bleibt."},
        {"path": "outputs[eve-log].types[alert].metadata", "type": "bool",
         "label": "alert.metadata", "default": True},
        {"path": "outputs[eve-log].types[http].enabled", "type": "bool",
         "label": "Type: http", "default": True},
        {"path": "outputs[eve-log].types[dns].enabled", "type": "bool",
         "label": "Type: dns", "default": True},
        {"path": "outputs[eve-log].types[tls].enabled", "type": "bool",
         "label": "Type: tls", "default": True},
        {"path": "outputs[eve-log].types[flow].enabled", "type": "bool",
         "label": "Type: flow", "default": True},
        {"path": "outputs[eve-log].types[netflow].enabled", "type": "bool",
         "label": "Type: netflow", "default": False},
        {"path": "outputs[eve-log].types[fileinfo].enabled", "type": "bool",
         "label": "Type: fileinfo", "default": False},
        {"path": "outputs[eve-log].types[ssh].enabled", "type": "bool",
         "label": "Type: ssh", "default": True},
        {"path": "outputs[eve-log].types[stats].enabled", "type": "bool",
         "label": "Type: stats", "default": False},
        {"path": "outputs[fast].enabled", "type": "bool",
         "label": "Fast-Log (Legacy)", "default": False,
         "help": "Einzeilige Alerts in fast.log. Modern: EVE nutzen."},
        {"path": "outputs[stats].enabled", "type": "bool",
         "label": "Stats-Log", "default": False,
         "help": "Periodische Service-Statistik in stats.log."},
    ],
}

_DETECTION = {
    "key": "detection",
    "group": "detection",
    "label": "Detection-Engine",
    "icon": "cpu",
    "description": "Performance- und Speicher-Tuning der Erkennungs-Engine.",
    "fields": [
        {"path": "mpm-algo", "type": "enum",
         "label": "Multi-Pattern-Matcher (MPM)",
         "options": ["hs", "auto", "ac", "ac-ks"], "default": "hs",
         "help": "hs = Hyperscan (am schnellsten, wenn verfuegbar). "
                 "ac-ks = Aho-Corasick (Default-Fallback).",
         "phimantec_warning": "Funktional ist ein Fallback moeglich; fuer hohe Proxy-Lane-Last bleibt hs/auto empfohlen."},
        {"path": "spm-algo", "type": "enum",
         "label": "Single-Pattern-Matcher (SPM)",
         "options": ["hs", "auto", "bm"], "default": "hs"},
        {"path": "detect.profile", "type": "enum",
         "label": "Detect-Profile",
         "options": ["low", "medium", "high", "custom"], "default": "medium",
         "help": "Speicher- und Performance-Profil der Detect-Engine."},
        {"path": "detect.sgh-mpm-context", "type": "enum",
         "label": "SGH-MPM-Context",
         "options": ["auto", "full", "single"], "default": "auto"},
        {"path": "detect.inspection-recursion-limit", "type": "int",
         "label": "Inspection-Recursion-Limit",
         "default": 3000, "min": 100, "max": 100000},
    ],
}


# =========================================================================
# Gruppe 4: PERFORMANCE
# =========================================================================

_THREADING = {
    "key": "threading",
    "group": "performance",
    "label": "Threading",
    "icon": "cpu",
    "description": "CPU-Affinity und Thread-Verteilung. Vorsicht: falsche "
                   "Werte koennen Performance massiv senken.",
    "fields": [
        {"path": "threading.set-cpu-affinity", "type": "bool",
         "label": "CPU-Affinity aktivieren", "default": False},
        {"path": "threading.detect-thread-ratio", "type": "range",
         "label": "Detect-Thread-Ratio", "default": 1.0,
         "min": 0.25, "max": 4.0, "step": 0.25,
         "help": "Wieviele Detect-Threads pro CPU-Kern. 1.0 = 1:1."},
    ],
}

_STREAM_FLOW = {
    "key": "stream_flow",
    "group": "performance",
    "label": "Stream & Flow",
    "icon": "activity",
    "description": "Stream-Reassembly und Flow-Tracking. Memcap-Werte "
                   "sollten zur RAM-Ausstattung passen.",
    "fields": [
        {"path": "flow.memcap", "type": "string",
         "label": "Flow Memcap", "default": "128mb",
         "help": "Speicher fuer Flow-Tabelle. Format: 128mb / 1gb."},
        {"path": "flow.hash-size", "type": "int",
         "label": "Flow Hash-Size", "default": 65536,
         "min": 1024, "max": 1048576},
        {"path": "flow.prealloc", "type": "int",
         "label": "Flow Prealloc", "default": 10000, "min": 100},
        {"path": "stream.memcap", "type": "string",
         "label": "Stream Memcap", "default": "64mb"},
        {"path": "stream.checksum-validation", "type": "bool",
         "label": "Stream-Checksum-Validation", "default": True,
         "help": "Bei NICs mit Hardware-Offload oft deaktivieren."},
        {"path": "stream.midstream", "type": "bool",
         "label": "Midstream-Pickup", "default": False,
         "help": "Flows mitten in der Session aufnehmen."},
        {"path": "stream.reassembly.memcap", "type": "string",
         "label": "Stream Reassembly Memcap", "default": "256mb"},
    ],
}

_LOGGING = {
    "key": "logging",
    "group": "performance",
    "label": "Logging (Engine-Logs)",
    "icon": "info",
    "description": "Wie Suricata seine eigenen Diagnose-Meldungen schreibt "
                   "(nicht zu verwechseln mit EVE).",
    "fields": [
        {"path": "logging.default-log-level", "type": "enum",
         "label": "Default-Log-Level",
         "options": ["error", "warning", "notice", "info", "debug"],
         "default": "notice"},
        {"path": "logging.default-log-format", "type": "string",
         "label": "Default-Log-Format",
         "default": "[%i] %t - (%f:%l) <%d> (%n) -- "},
    ],
}


# =========================================================================
# Schema-Komposition
# =========================================================================

SCHEMA = {
    "version": "1.3",
    "groups": [
        {"key": "general_group", "label": "Allgemein"},
        {"key": "modes",
         "label": "Betriebsmodus",
         "help": "Wie Pakete in Suricata kommen (Capture) und wie das "
                 "System mit ihnen umgeht (IDS=passiv / IPS=aktiv)."},
        {"key": "detection", "label": "Detektion"},
        {"key": "performance", "label": "Performance"},
    ],
    "sections": [
        _GENERAL,
        _VARS,
        _IDS_IPS,
        _CAPTURE_AFPACKET,
        _CAPTURE_NFQUEUE,
        _CAPTURE_OTHER,
        _APP_LAYER,
        _OUTPUTS,
        _DETECTION,
        _THREADING,
        _STREAM_FLOW,
        _LOGGING,
    ],
}


def get_schema():
    return SCHEMA


def collect_paths():
    out = []
    for section in SCHEMA["sections"]:
        for field in section["fields"]:
            if field["type"] == "list-of-objects":
                continue
            out.append(field["path"])
    return out
