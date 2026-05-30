# IST/SOLL-Diagnose

## Zielbild

Die Zielarchitektur verlangt eine eigenständige C11-Appliance auf dem Proxy-Host und getrennte Suricata-Komponenten auf dem Suricata-Host.

```text
Proxy-Host:
  phimantec-proxyd
    listener :3128
    TLS-Bump
    H1 Engine
    Lane Client
    Verdict Core
    FlowBus Client
    Upstream TLS Proxy

Suricata-Host:
  phimantec-lane-sink :18080
  phimantec-inspectord :18090
  Suricata 8 AF_PACKET IDS
  eve.json
```

## IST aus hochgeladenen Unterlagen

| Kriterium | Befund |
|---|---|
| Sprache | C/C11 als Hauptlinie gefordert |
| Clientpfad | HTTP CONNECT Proxy, kein Docker-Port-Publishing, kein Envoy/Squid |
| Enforcement | Proxy blockt, nicht Suricata-Kernel-Drop in Phase 1 |
| Detection | Suricata 8 analysiert klare Lane |
| Verdict | FlowBus TCP + HMAC, kein Pflicht-mTLS |
| Host-Trennung | Proxy-Host und Suricata-Host getrennt |
| H1 | zuerst nativ |
| H2 | später nativ |
| H3 | initial adaptiv durch Alt-Svc-Stripping, später nativ |
| Lane | bewusst unverschlüsselt und nur isoliert betreiben |

## Umsetzung in diesem Paket

| Kriterium | Status `v0.1.0-lab` | Datei/Modul |
|---|---:|---|
| C11 Build | erfüllt | `CMakeLists.txt` |
| Proxy Listener `:3128` | erfüllt | `appliance/src/phim_proxy_worker.c` |
| CONNECT Parser | erfüllt | `appliance/src/phim_connect_parser.c` |
| TLS-Bump | erfüllt | `appliance/src/phim_tls.c`, `appliance/src/phim_cert.c` |
| dynamisches Leaf-Zertifikat | erfüllt | `phim_generate_leaf_cert()` |
| H1 Request Parser | erfüllt | `shared/src/phim_h1.c` |
| H1 Response Parser | erfüllt | `shared/src/phim_h1.c` |
| Alt-Svc Strip | erfüllt | `phim_h1_strip_alt_svc()` |
| Lane Client | erfüllt | `appliance/src/phim_lane.c` |
| Lane Sink | erfüllt | `suricata-node/src/lane_sink_main.c` |
| FlowBus Frame + HMAC | erfüllt | `shared/src/phim_flowbus.c` |
| Inspectord FlowBus Server | erfüllt | `suricata-node/src/inspectord_main.c` |
| EVE-Korrelation | MVP erfüllt | `shared/src/phim_eve.c` |
| Blockseite | erfüllt | `appliance/src/phim_blockpage.c` |
| Unit Tests | erfüllt | `tests/` |
| H2 nativ | geplant | nicht Teil von `v0.1.0-lab` |
| H3 nativ | geplant | nicht Teil von `v0.1.0-lab` |
| Kernel-Drop | bewusst nicht umgesetzt | Phase 1 Nicht-Ziel |

## Diagnose SSLsplit-Archiv

Analysiert wurde das hochgeladene Archiv `sslsplit-develop(1).zip`.

| Punkt | Befund |
|---|---|
| Dateien | 325 Dateien |
| C/H Umfang | ca. 211004 Zeilen über `.c/.h` nach `wc -l` |
| Lizenz | BSD 2-Clause laut `LICENSE` |
| Proxy-Modell | transparente TLS-Interception mit NAT/TPROXY/PF, nicht Browser-CONNECT-Proxy |
| Relevanz | technische Referenz für Zertifikats- und TLS-Bump-Konzepte |
| Nicht übernommen | kein Copy/Paste, keine Rebranding-Ableitung, keine Source-Dateien importiert |

Wesentliche Abweichung zum Zielprodukt: SSLsplit beschreibt sich als transparenter Proxy und nicht als HTTP/SOCKS-Proxy für Browser-Konfiguration. Die PHIMANTEC-Zielarchitektur verlangt dagegen explizit `HTTP CONNECT` vom Win11-Browser zum Proxy.

## Entscheidung

Die Appliance wurde als eigene C11-Linie umgesetzt. Das ist technisch sauberer, wartbarer und vermeidet Lizenz-/Provenance-Risiken. Konzepte wie CONNECT, TLS-Bump, dynamische Leaf-Zertifikate und Zertifikatscache sind Standardmuster; die konkrete Implementierung ist eigenständig.
