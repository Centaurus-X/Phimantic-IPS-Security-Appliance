# PHIMANTEC C11 Appliance v0.1.9-lab — Performance-Diagnose und Optimierungsentscheidungen

Stand: 2026-05-25  
Basis: `v0.1.7-lab-patch7-hyperscan`  
Zielrelease: `phimantec-c11-appliance-v0.1.9-lab`

## 1. Diagnose-Kurzfazit

Die Analyse des Codes, der bestehenden Lab-Dokumentation und der Patch-7-Architektur bestätigt den bisherigen Befund:

```text
Suricata ist im aktuellen Lab nicht der dominante Engpass.
Der dominante Optimierungsbereich ist der Proxy-Hot-Path.
```

Die wichtigsten Latenzquellen für langsamer ladende Webseiten sind:

```text
1. viele parallele Browser-CONNECTs moderner Webseiten,
2. dynamische Leaf-Zertifikatserzeugung je Zielhost,
3. RSA-2048-Keygeneration im Cache-Miss-Pfad,
4. Lane-TCP-Verbindung pro Inspection-Event,
5. EVE-Dateiscan pro Verdict-Anfrage,
6. kleine Heap-Allokationen im FlowBus-Frame-Hot-Path,
7. H1-only-Betrieb ohne H2-Multiplexing,
8. noch kein Upstream-Connection-Pool.
```

Die neue Version setzt bewusst nur Optimierungen um, die eine reale Beschleunigung erwarten lassen und die bestehende Sicherheitsarchitektur nicht aufweichen. Domain-/CDN-Bypass, Response-Body-Scan-Abschaltung oder Fail-Open als Performance-Mittel wurden nicht umgesetzt.

## 2. Harte Architektur-Guardrail

Für entschlüsselten Traffic bleibt genau eine Analyse-Route bestehen:

```text
phimantec-proxyd
  -> klare Lane TCP/18080
  -> phimantec-lane-sink
  -> Suricata IDS/IPS auf dem Suricata-Host
  -> EVE/Inspectord-Korrelation
  -> FlowBus Verdict zurück zum Proxy
```

Es wurde keine zweite Decrypted-Traffic-Route, kein Domain-Bypass und kein paralleler Schatten-Scanner ergänzt. Blocking bleibt im Proxy; Suricata bleibt Analyse- und Signallieferant.

## 3. Umgesetzte Optimierungen in v0.1.9-lab

### 3.1 ECDSA-P-256 Leaf-Zertifikate

Datei: `appliance/src/phim_cert.c`

Vorher:

```text
Leaf-Key pro Zielhost: RSA-2048
```

Jetzt:

```text
Leaf-Key pro Zielhost: ECDSA P-256
```

Nutzen:

```text
- deutlich geringere Keygeneration-Kosten bei Cache-Miss,
- weniger CPU-Peaks bei vielen neuen Zielhosts,
- unveränderte CA-Signatur über EVP_sha256(),
- passend für aktuelle Win11-/Browser-Lab-Clients.
```

Sicherheitsbewertung:

```text
Risiko: gering
Detection: unverändert
Architektur: unverändert
```

### 3.2 Downstream TLS Context Cache mit CA-Fingerprint-Key

Datei: `appliance/src/phim_tls.c`

Vorher:

```text
CONNECT -> Leaf-Zertifikat + SSL_CTX neu erzeugen
```

Jetzt:

```text
CONNECT -> Cache-Lookup nach hostname_lowercase + CA-SHA256-Fingerprint
        -> SSL_CTX_up_ref() bei Hit
        -> ECDSA-Leaf-Erzeugung nur bei Miss
```

Eigenschaften:

```text
Cache-Größe: 1024 Einträge
TTL:         7 Tage Prozesslaufzeit
Eviction:    LRU
CA-Wechsel:  eigener Cache-Key über CA-SHA256-Fingerprint
Threading:   mutex-geschützt, keine unsicheren Shared-Refs
```

Nutzen:

```text
- wiederholte Hosts vermeiden Zertifikatsarbeit vollständig,
- parallele Browser-Subrequests profitieren nach erstem Host-Miss,
- spürbar weniger CPU- und Latenzspitzen im TLS-Bump-Pfad.
```

### 3.3 Host-spezifischer Upstream SSL_CTX Cache mit Client-Session-Cache

Dateien:

```text
appliance/include/phimantec/phim_tls.h
appliance/src/phim_tls.c
appliance/src/phim_proxy_out.c
```

Vorher:

```text
Jeder Upstream-Verbindungsaufbau bekam einen frischen SSL_CTX.
```

Jetzt:

```text
host + verify_policy -> wiederverwendeter SSL_CTX
SSL_SESS_CACHE_CLIENT aktiviert
Session-Cache-Größe 4096
Session-Timeout 300 Sekunden
```

Nutzen:

```text
- Grundlage für TLS-Session-Resumption,
- weniger OpenSSL-Kontextaufbau,
- host-isoliert, daher keine Cross-Host-Session-Vermischung.
```

Hinweis:

```text
Das ist noch kein vollständiger Upstream-Connection-Pool.
TCP/TLS-Verbindungen werden weiterhin nach dem Request geschlossen.
Ein echter Upstream-Pool bleibt bewusst eine eigene Folgeoptimierung.
```

### 3.4 Persistenter Lane-Client-Pool

Dateien:

```text
appliance/src/phim_lane.c
suricata-node/src/lane_sink_main.c
shared/src/phim_config.c
shared/include/phimantec/phim_config.h
```

Vorher:

```text
phim_lane_send() -> TCP connect -> POST -> close
```

Jetzt:

```text
phim_lane_send() -> Pool-Slot -> persistente TCP-Verbindung -> POST -> 204 lesen -> reuse
```

Eigenschaften:

```text
Default-Poolgröße: 4
Max-Poolgröße:     8
Konfiguration:     [lane] pool_size = 4
Reconnect:         automatisch bei Write-/Read-Fehler
Backwards-Toleranz: bei Connection: close wird sauber geschlossen und später neu verbunden
```

Der Lane-Sink verarbeitet nun mehrere sequenzielle HTTP/1.1-POSTs über dieselbe Verbindung. Damit entfällt ein großer Teil der TCP-Handshakes zwischen Proxy-Host und Suricata-Host.

### 3.5 Inkrementeller EVE-Index statt Full-File-Scan pro Lookup

Datei: `shared/src/phim_eve.c`

Vorher:

```text
phim_eve_find_alert_for_flow() öffnet eve.json und scannt die ganze Datei pro Verdict.
```

Jetzt:

```text
pro Prozess ein mutex-geschützter Index:
- stat() erkennt Rotation/Truncation/Pfadwechsel,
- nur neue Bytes seit letztem Offset werden gelesen,
- Flow-ID -> sid/msg wird in einem 4096er Ring/LRU-Index gehalten,
- Lookup ist im Normalfall O(1)-ähnlich statt O(Dateigröße).
```

Eigenschaften:

```text
Indexgröße: 4096 Alert-Einträge
TTL:        120 Sekunden
Rotation:   dev/ino/size-basiert erkannt
Fallback:   bei fehlender Datei bleibt Verhalten PHIM_ERR wie vorher
```

Nutzen:

```text
- deutlich weniger CPU- und I/O-Last im inspectord bei wachsender eve.json,
- niedrigere Verdict-Latenz unter Browser-Last,
- keine Änderung der Verdict-Policy.
```

### 3.6 FlowBus-Frame-Write ohne Heap im kleinen Hot-Path

Dateien:

```text
shared/src/phim_flowbus.c
CMakeLists.txt
tests/test_flowbus_write_read.c
```

Vorher:

```text
phim_flowbus_write_frame() -> phim_flowbus_build_frame() -> malloc/free pro Frame
```

Jetzt:

```text
Frames bis 768 Byte werden auf dem Stack gebaut.
Heap-Fallback bleibt für größere Frames erhalten.
```

Nutzen:

```text
- weniger malloc/free im Verdict-Hot-Path,
- weniger Allocator-Lock-Pressure bei parallelen Browser-Requests,
- identisches Wire-Format und HMAC-Verhalten.
```

### 3.7 Konfigurierbarer FlowBus-Pool

Dateien:

```text
appliance/include/phimantec/phim_verdict.h
appliance/src/phim_verdict.c
shared/src/phim_config.c
config/*.conf
scripts/*.sh
```

Vorher:

```text
FlowBus-Pool fest 4 Slots
```

Jetzt:

```text
[flowbus]
pool_size = 4
```

Der interne Maximalwert liegt bei 8. Werte darüber werden sicher gekappt und geloggt.

## 4. Bewusst nicht umgesetzt

### H2 nativ

Nicht umgesetzt in v0.1.9-lab, weil es eine große Protokolländerung mit Stream-State, HPACK, Flow-Control und pro-Stream-Verdicts ist. Das bringt perspektivisch sehr viel, ist aber kein risikoarmer Hotfix.

### Upstream Connection Pool

Nicht umgesetzt in v0.1.9-lab, weil sichere Wiederverwendung von TLS-Verbindungen saubere H1-Response-Grenzen, Connection-State, Fehlerzustände und Connection: close berücksichtigen muss. Der vorbereitende Host-spezifische SSL_CTX-/Session-Cache ist enthalten.

### Domain-/CDN-Bypass

Nicht umgesetzt. Ein Bypass wäre schnell, verletzt aber das Security-Ziel, weil Malware und aktive Inhalte auch über CDNs ausgeliefert werden können.

### Response-Body-Inspection deaktivieren

Nicht umgesetzt. EICAR und echte Malware-Downloads müssen weiterhin vor oder kontrolliert während der Auslieferung erkannt werden.

## 5. Erwartete Wirkung auf Surf-Verhalten

Die stärkste reale Verbesserung ist für wiederkehrende und komplexe Seiten zu erwarten:

```text
- weniger Leaf-Zertifikatserzeugung,
- deutlich weniger RSA-Kosten durch ECDSA,
- weniger Lane-TCP-Handshakes,
- niedrigere inspectord-Latenz bei wachsender eve.json,
- weniger Heap-Noise im FlowBus-Frame-Write.
```

Die Seiten, die weiterhin länger laden, werden voraussichtlich durch diese noch offenen Faktoren begrenzt:

```text
- H1-only statt H2-Multiplexing,
- pro CONNECT nur ein H1-Request im aktuellen Lab-Pfad,
- kein echter Upstream-Connection-Pool,
- Sicherheits-Pufferung kleiner Response-Bodies vor Client-Auslieferung.
```

## 6. Regression- und Release-Gates

Durchgeführt im Erzeugungslauf:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Ergebnis:

```text
6/6 Tests bestanden
0 Compiler-Warnings unter -Werror
```

Zusätzlich:

```bash
bash scripts/build_asan.sh
```

Ergebnis:

```text
6/6 ASAN/UBSAN-Tests bestanden
kein Leak/UB-Befund in den Tests
```

Zusätzlich wurde der Lane-Sink mit zwei sequenziellen POSTs über eine einzige TCP-Verbindung geprüft:

```text
lane_keepalive=PASS
flow-a request /one
flow-b response /two
```

## 7. Neue/erweiterte Tests

```text
connect_parser        unverändert
h1_parser             unverändert
flowbus_hmac          unverändert
flowbus_write_read    neu: Stack-/Write-Frame über socketpair
eve_correlation       erweitert: inkrementelles Append+Lookup
verdict_timeout       unverändert
```

## 8. Nächste sinnvolle Schritte

Priorität für die nächste Version:

```text
1. echte Proxy-Metrics/Histogramme exportieren,
2. H1 Keep-Alive im CONNECT-Tunnel sauber mit request_id einführen,
3. Upstream-Connection-Pool nur nach zusätzlichen H1-Grenztests,
4. Content-Encoding-Normalisierung für sichere spätere Streaming-Optimierung,
5. H2 nativ via nghttp2 als separater Architektur-Sprint.
```


## Nachtrag v0.2.0-lab

Die in dieser Diagnose genannten naechsten Low-Risk-Schritte `H1 Keep-Alive im CONNECT-Tunnel` und `Upstream Connection Reuse innerhalb desselben Zielhosts` wurden in v0.2.0 minimal-invasiv umgesetzt. H2/H3 bleibt bewusst ein spaeterer Architektur-Sprint.
