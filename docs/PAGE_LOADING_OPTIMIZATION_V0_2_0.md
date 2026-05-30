# PHIMANTEC C11 Appliance — Page-Loading-Optimierung v0.2.0-lab

**Bezugsbasis:** `phimantec-c11-appliance-v0.1.9-lab`  
**Neuer Stand:** `phimantec-c11-appliance-v0.2.0-lab`  
**Ziel:** YouTube/Amazon/wetter.com minimal-invasiv schneller laden, ohne Security-Bypass und ohne H2/H3-Grossumbau.

---

## 1. Diagnose

Die Browser-Konsole zeigte bei Amazon und YouTube vor allem Timeouts, `NetworkError`-Meldungen, verzögertes Preload-Verhalten und CORS-/Fetch-Fehler bei Subresources. Viele weitere Zeilen waren Site- oder Browser-Rauschen. Das relevante Muster war: mehrere Subresources kamen zu spät oder wurden durch Verbindungsschluss/Timeouts erneut angefordert.

Die Codeanalyse von v0.1.9 bestaetigte den Hot-Path:

```text
CONNECT lesen
TLS zum Client terminieren
einen H1 Request lesen
Lane senden
Verdict holen
frischen Upstream TCP/TLS Tunnel aufbauen
Request senden
eine Response forwarden
Tunnel schliessen
```

Damit galt in v0.1.9 faktisch:

```text
1 CONNECT = 1 H1 Request = 1 Upstream TCP/TLS Aufbau
```

Bei modernen Seiten mit 80 bis 150 Subrequests erzeugt das massive Handshake-Latenz. H2/H3 sind langfristig wichtig, aber der minimale Soforthebel liegt darunter: H1 Keep-Alive und Upstream-Reuse.

---

## 2. Umgesetzte v0.2.0-Optimierungen

### 2.1 H1 Keep-Alive im CONNECT-Tunnel

`phimantec-proxyd` liest nach erfolgreichem CONNECT und Client-TLS-Handshake nun mehrere sequenzielle HTTP/1.1 Requests aus demselben TLS-Tunnel.

Eigenschaften:

```text
- pro CONNECT mehrere H1 Requests moeglich
- pro Request eigene abgeleitete Flow-ID
- pro Request Lane-Send zu Suricata
- pro Request Verdict-Pruefung ueber FlowBus
- Block-Verdict beendet den Tunnel konservativ
- Connection: close wird respektiert
- Idle-Timeout: 30 Sekunden
- Request-Limit pro Tunnel: 1000
```

### 2.2 Tunnel-lokaler Upstream-TLS-Reuse

Innerhalb desselben Client-CONNECT-Tunnels wird die Upstream-TLS-Verbindung zum Origin wiederverwendet, solange sie laut Response-Grenzen und Headern wiederverwendbar ist.

Das reduziert:

```text
- TCP Connects zum Origin
- Upstream TLS Handshakes
- Session-Setup-Latenz bei CDN-lastigen Seiten
```

Kein Cross-Host-Pooling wurde eingebaut. Die Wiederverwendung bleibt auf denselben CONNECT-Zielhost und denselben Worker-Kontext begrenzt.

### 2.3 Exakte Response-Boundaries

Damit Upstream-Reuse sicher moeglich ist, wurde `phim_forward_response()` neu strukturiert:

```text
- Content-Length: exakt n Bytes forwarden und inspecten
- Transfer-Encoding: chunked: bis 0-Chunk inklusive Trailer lesen
- close-delimited: bis Upstream-Close lesen, danach keine Wiederverwendung
- HEAD/1xx/204/304: Body korrekt unterdruecken
```

### 2.4 Chunked-Decoder

Neues Modul im H1-Core:

```text
phim_h1_chunked_decoder_init()
phim_h1_chunked_decoder_feed()
phim_h1_chunked_decoder_complete()
```

Der Decoder erkennt Chunk-Extensions, Trailer und fehlerhafte Chunk-Sizes. Neuer CTest: `h1_chunked_decoder`.

---

## 3. Sicherheitsbewertung

### Beibehaltene Security-Pipeline

Jeder H1 Request im Keep-Alive-Tunnel durchlaeuft weiterhin:

```text
1. H1 Parser und Begrenzungen
2. Lane-Send zu Suricata
3. FlowBus Verdict
4. Response-Body-Inspection vor/waehrend Auslieferung
5. Blockpage bei BLOCK
```

Suricata bleibt das primaere Security-Gate fuer den entschluesselten Traffic. Es gibt weiterhin keine zweite Route fuer decrypted Traffic.

### Bewusst nicht umgesetzt

```text
- kein Domain-/CDN-Bypass
- keine Abschaltung der Response-Body-Inspection
- kein Sample-only-Modus
- kein Verdict-Cache fuer statische Assets in diesem Release
- kein H2/H3-nativer Proxy-Pfad
- kein Cross-Host-Upstream-Pool
```

Der in `PAGE_LOADING_OPTIMIZATION_v0_1_9.md` diskutierte Verdict-Asset-Cache wurde bewusst nicht umgesetzt, weil v0.2.0 maximal konservativ bleiben soll: Suricata/FlowBus entscheiden weiterhin pro Request.

### Limits gegen Long-Lived-Tunnel-Missbrauch

```text
PHIM_MAX_REQUESTS_PER_TUNNEL = 1000
PHIM_TUNNEL_IDLE_TIMEOUT_MS  = 30000
```

Diese Grenzen verhindern endlose H1-Tunnel und reduzieren Slowloris-artige Risiken.

---

## 4. Erwartete Wirkung

Die groesste Verbesserung entsteht nicht durch H2-Konvertierung, sondern dadurch, dass Browser- und Origin-TLS-Verbindungen nicht fuer jede Subresource neu aufgebaut werden muessen.

Typisches Verhalten:

```text
v0.1.9: viele CONNECTs, viele Client-TLS-Handshakes, viele Upstream-TLS-Handshakes
v0.2.0: weniger CONNECTs, mehrere H1-Requests pro Tunnel, wiederverwendeter Upstream-TLS-Tunnel
```

Fuer YouTube/Amazon ist vor allem die Reduktion der Upstream-TLS-Handshakes relevant. Die Anzahl aktiver Upstream-Verbindungen beim Page-Load sollte deutlich sinken.

Messung auf dem Proxy-Host waehrend eines Amazon-/YouTube-Ladevorgangs:

```bash
sudo ss -tn dst :443 | wc -l
```

Erwartung:

```text
v0.1.9: oft sehr viele kurzlebige :443 Verbindungen
v0.2.0: deutlich weniger Verbindungsaufbau-Churn
```

---

## 5. H2/H3 Einordnung

H2 nativ bleibt die strategische Loesung fuer Multiplexing pro Host. Fuer v0.2.0 wurde es nicht implementiert, weil H2 eine groessere Architekturänderung bedeutet:

```text
- ALPN h2
- HPACK
- Stream-State
- Flow-Control
- Verdict pro Stream
- Lane-Normalisierung pro Stream
```

H3/QUIC laeuft ueber UDP und passt nicht direkt in den aktuellen TCP-CONNECT-Proxy. Fuer Lab-Messungen kann es sinnvoll sein, Alt-Svc/HTTP3 im Browser zu deaktivieren, damit keine zusaetzlichen H3-Fallback-Latenzen in die Messung einfliessen.

Firefox-Lab-Mitigation:

```text
network.http.http3.enable = false
network.http.altsvc.enabled = false
```

---

## 6. Verifikation

Durchgefuehrt im Release-Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Ergebnis:

```text
7/7 Tests bestanden
```

ASAN/UBSAN:

```bash
bash scripts/build_asan.sh
```

Ergebnis:

```text
7/7 Tests bestanden
kein ASAN/UBSAN-Befund in den Tests
```

Zusätzliche lokale Integrations-Smokes:

```text
- zwei sequenzielle GETs ueber einen CONNECT-Tunnel: PASS
- tunnel-lokale Upstream-TLS-Wiederverwendung: PASS
- chunked Response mit Trailer ueber Keep-Alive: PASS
```

---

## 7. Regression-Gates fuer das Zwei-Host-Lab

Proxy-Host:

```bash
CA="/home/philo/phimantec-bump-ca.crt"

curl --http1.1 -sS --noproxy ""   --proxy http://127.0.0.1:3128   --cacert "$CA"   -o /tmp/phimantec-allow.html   -w "allow_http_code=%{http_code}
"   https://127.0.0.1:8443/

curl --http1.1 -sS --noproxy ""   --proxy http://127.0.0.1:3128   --cacert "$CA"   -o /tmp/phimantec-suricata-block.html   -w "suricata_block_http_code=%{http_code}
"   https://127.0.0.1:8443/suricata-block || true
```

Suricata-Host:

```bash
sudo grep -aE "PHIMANTEC EICAR TEST FILE|PHIMANTEC SURICATA PATH BLOCK|PHIMANTEC BODY BLOCK"   /var/log/suricata/eve.json | tail -n 30

sudo journalctl -u phimantec-inspectord --no-pager -n 120
```

---

## 8. Naechste sinnvolle Schritte

```text
1. Live-Benchmark auf Win11 mit Amazon/Youtube vor/nach v0.2.0
2. Metrics fuer Tunnel-Reuse und Upstream-Reuse zaehlen
3. Optionaler host-lokaler Upstream-Connection-Pool ueber CONNECT-Grenzen hinweg
4. Erst danach H2 nativ evaluieren
```
