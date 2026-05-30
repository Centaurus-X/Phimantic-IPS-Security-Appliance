# v0.2.0 Page-Loading-Hotpath

v0.2.0 behebt den groessten noch minimal-invasiv adressierbaren Page-Loading-Hebel aus v0.1.9: `1 CONNECT = 1 Request`.

Neue Hot-Path-Eigenschaften:

```text
- ein CONNECT-Tunnel kann mehrere sequenzielle H1 Requests tragen
- jeder Request erhaelt eine eigene Flow-ID
- jeder Request geht weiterhin ueber Lane + Suricata + FlowBus Verdict
- die Upstream-TLS-Verbindung wird innerhalb dieses Tunnels wiederverwendet
- chunked Responses werden bis zum 0-Chunk inklusive Trailer begrenzt
```

Dadurch sinkt bei Amazon/YouTube besonders die Zahl kurzlebiger Client- und Upstream-TLS-Handshakes.

Nicht umgesetzt:

```text
- H2/H3 nativ
- Domain-Bypass
- Response-Inspection abschalten
- Verdict-Cache
```

---

# PHIMANTEC Lab Performance Tuning

## v0.1.9-lab

Dieses Release setzt die naechste risikoarme Performance-Stufe um. Der Schwerpunkt liegt weiterhin im Proxy-Hot-Path, weil Suricata im Lab nicht der primaere Engpass ist. Die Security-Architektur bleibt unveraendert: entschluesselter Traffic wird nur ueber die Lane zu Suricata IDS/IPS analysiert; Blocking erfolgt im Proxy.

### Geaendert

1. Leaf-Zertifikate nutzen ECDSA P-256 statt RSA-2048.
   - Cache-Misses sind deutlich billiger.
   - Moderne Win11-/Browser-Lab-Clients unterstuetzen diesen Pfad.

2. Downstream TLS Context Cache.
   - Cache-Key: normalisierter Hostname + CA-SHA256-Fingerprint.
   - 1024 Eintraege, LRU, 7 Tage TTL pro Prozesslaufzeit.
   - Wiederholte CONNECTs fuer denselben Host vermeiden Leaf-Cert-/SSL_CTX-Erzeugung.

3. Host-spezifischer Upstream SSL_CTX Cache.
   - Pro Host und Verify-Policy wird ein SSL_CTX wiederverwendet.
   - OpenSSL Client-Session-Cache ist aktiviert.
   - Noch kein echter Upstream-Connection-Pool.

4. Lane nutzt einen persistenten Client-Pool.
   - `phim_lane_send()` oeffnet nicht mehr zwingend pro Event eine neue TCP-Verbindung.
   - Der Lane-Sink unterstuetzt HTTP/1.1 Keep-Alive fuer sequenzielle POSTs.
   - Default: `[lane] pool_size = 4`, Cap: 8.

5. Inspectord/EVE-Korrelation ist inkrementell.
   - Neue Bytes aus `eve.json` werden indexiert.
   - Flow-ID Lookup erfolgt aus einem 4096er Alert-Index.
   - Rotation und Truncation werden erkannt.

6. FlowBus-Frame-Write reduziert Heap-Arbeit.
   - Kleine Frames werden auf dem Stack gebaut.
   - Das HMAC-geschuetzte Wire-Format bleibt identisch.

7. FlowBus-Pool ist konfigurierbar.
   - Default: `[flowbus] pool_size = 4`, Cap: 8.

### Erwartete Wirkung

Komplexe Webseiten mit vielen Subresources profitieren vor allem durch weniger Zertifikatsarbeit und weniger Lane-TCP-Handshakes. Bei wachsender `eve.json` sinkt zusaetzlich die Inspectord-I/O-Last. Die Verbesserung ist besonders sichtbar bei wiederholten Seitenaufrufen und bei Hosts, die mehrfach innerhalb einer Browser-Session auftauchen.

### Unverändert bewusst sicher

- EICAR muss weiterhin blocken.
- `/suricata-block` muss weiterhin blocken.
- Response-Body-Inspection bleibt aktiv.
- Keine ungeprueften Domain-/CDN-Bypaesse.
- Keine zweite Analyse-Route fuer decrypted Traffic.

### Noch offene Performance-Themen

- H2 nativ ist noch nicht implementiert.
- CONNECT-Tunnel wird im Lab-Pfad weiterhin pro H1-Request geschlossen.
- Echter Upstream-Connection-Pool ist noch nicht implementiert.
- Content-Encoding-Normalisierung ist noch nicht implementiert.
- Kleine Responses mit Content-Length werden weiterhin vor Auslieferung gepuffert, um Malware-Block vor Client-Auslieferung zu garantieren.

### v0.1.9 Release-Gate

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
bash scripts/build_asan.sh
```

Ergebnis im Erzeugungslauf:

```text
RelWithDebInfo: 6/6 Tests bestanden
ASAN/UBSAN:    6/6 Tests bestanden
Lane KeepAlive: PASS
```

Weitere Details: `docs/PERFORMANCE_DIAGNOSIS_V0_1_9.md`.


## v0.1.7-lab-patch7

Dieser Patch reduziert den Hot-Path-Overhead minimal-invasiv, ohne die EICAR-/Suricata-Blockerkennung zu schwächen.

### Geändert

1. Response-Body wird nicht mehr doppelt an Lane/FlowBus geschickt.
   - Vorher: `response/headers` enthielt bereits den Body-Sample und danach folgte nochmals `response/body`.
   - Jetzt: Body wird vor Auslieferung genau einmal geprüft.

2. FlowBus nutzt einen kleinen Client-Pool mit 4 persistenten TCP/HMAC-Verbindungen.
   - Vorher: ein globaler FlowBus-Client mit einem Mutex serialisierte alle parallelen Browser-Requests.
   - Jetzt: Requests werden round-robin auf 4 persistente FlowBus-Verbindungen verteilt.

3. Suricata nutzt ab v0.3.3 `auto` als kompatiblen Default.
   - Hyperscan bleibt moeglich, wird aber nur noch bei explizitem `PHIMANTEC_SURICATA_MPM=hs` / `PHIMANTEC_SURICATA_SPM=hs` erzwungen.
   - Falls explizites `hs` fehlschlaegt, erfolgt maximal ein einmaliger Fallback auf `mpm=ac-ks`, `spm=auto`.

### Nicht geändert

- EICAR wird weiterhin vor Client-Auslieferung geblockt.
- Hyperscan veraendert nur die Pattern-Matching-Engine in Suricata, nicht die Blocklogik im Proxy.
- `/suricata-block` wird weiterhin deterministisch geblockt.
- Blocking bleibt im Proxy.
- Suricata sieht weiterhin die klare Lane auf TCP/18080.
- FlowBus bleibt HMAC-geschützt.

### Erwartete Wirkung

Komplexe Webseiten mit vielen parallelen Requests, z. B. Amazon oder Wetterseiten, erzeugen deutlich weniger Head-of-Line-Blocking im Verdict-Pfad. Zusätzlich sinkt die Anzahl der Lane-/FlowBus-Events pro Response.

### Noch offene Performance-Themen

- H2 nativ ist noch nicht implementiert.
- CONNECT-Tunnel wird im Lab-MVP pro Request geschlossen.
- Lane nutzt noch pro Event eine TCP-Verbindung.
- Kleine Responses mit Content-Length werden weiterhin vor Auslieferung gepuffert, um Malware-Block vor Client-Auslieferung zu garantieren.

## Hyperscan Default

Auf der Suricata-VM kann die aktive Einstellung nach dem Rollout direkt mit dem mitgelieferten Check-Skript geprueft werden:

```bash
PROXY_IP=192.168.0.232 bash scripts/check_suricata_hyperscan_vm.sh
```

Oder manuell:

```bash
suricata --build-info | grep -iE 'Hyperscan|AF_PACKET|NFQ|PCRE'
grep -nE '^[[:space:]]*(mpm-algo|spm-algo):' /etc/suricata/suricata.yaml
sudo suricata -T   -c /etc/suricata/suricata.yaml   --af-packet=ens18   -S /etc/suricata/rules/phimantec-local.rules   -l /var/log/suricata   -k none
```

Default:

```bash
PHIMANTEC_SURICATA_MPM=hs
PHIMANTEC_SURICATA_SPM=hs
```

Erzwingen ohne Fallback:

```bash
PHIMANTEC_REQUIRE_HYPERSCAN=1 PROXY_IP=192.168.0.232 bash scripts/repair_suricata_rules_vm.sh
```
