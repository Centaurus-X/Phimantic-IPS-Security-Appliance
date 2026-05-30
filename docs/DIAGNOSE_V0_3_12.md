# PHIMANTEC v0.3.12 — Diagnose und Patch

## Ist-Zustand vor dem Patch

v0.3.11 stellte korrekt sicher, dass Suricata der einzige Blocking-Entscheider ist. Datei-basierte EICAR-Tests wie `eicar.com.txt` und `eicar_com.zip` wurden wieder geblockt. Text-/HTML-Seiten mit eingebettetem EICAR-String konnten jedoch durchkommen, wenn der Treffer erst hinter dem Suricata-Default-Sichtfenster lag.

## Root Cause

Der Proxy sendet entschluesselte Samples bis zu `tls.max_body_bytes = 1048576` an die PHIMANTEC-Lane. Suricata bekam diesen Lane-POST vollstaendig, der HTTP-App-Layer inspizierte aber nur das kleine Default-Fenster. Bei einer HTML-Seite von circa 40-50 KiB liegt der EICAR-String typischerweise weit hinter den ersten 4096 Bytes. Dadurch erzeugte Suricata keinen EVE-Alert und inspectord musste `suricata-no-alert` zurueckgeben.

```text
Proxy Sample:        bis 1 MiB
Suricata Sicht alt:  ca. 4 KiB Default
Treffer bei:         z.B. Byte 40000
Ergebnis alt:        kein Suricata-Alert -> ALLOW
```

Das war kein Hyperscan- oder ET-Ruleset-Problem. Die lokale HTTP-Layer-Regel `sid:900002` war ausreichend; Suricata sah nur nicht tief genug in den HTTP-Body des Lane-POSTs.

## Patch

`scripts/phimantec_suricata_yaml_patch.py` setzt nun die HTTP-Body-Inspection-Tiefe unter:

```yaml
app-layer:
  protocols:
    http:
      enabled: true
      libhtp:
        default-config:
          request-body-limit: 1048576
          response-body-limit: 1048576
          request-body-minimal-inspect-size: 1048576
          request-body-inspect-window: 1048576
          response-body-minimal-inspect-size: 1048576
          response-body-inspect-window: 1048576
```

Damit entspricht Suricatas Sichtfenster dem PHIMANTEC-Proxy-Sample-Fenster. Die Mustererkennung bleibt generisch: Plain Text, HTML, JSON, XML und andere entschluesselte Body-Daten werden durch dieselbe Suricata-Regel-/EVE-Kette verarbeitet. Der Proxy interpretiert keine Inhalte und entscheidet nicht lokal.

## Streaming-/Chunked-Fix

Bei chunked oder connection-close Responses konnte der Proxy vorher HTTP-200-Header weiterleiten, bevor der erste Body-Record von Suricata verdicted war. v0.3.12 liest deshalb bei `initial_body_len == 0` einen ersten Body-Record vor dem Header-Forwarding, sendet diesen an Suricata und leitet Header erst weiter, wenn Suricata erlaubt.

Wenn ein spaeter Body-Chunk nach bereits gesendeten Headern durch Suricata geblockt wird, injiziert der Proxy keine `HTTP/1.1 403`-Blockpage mehr in den laufenden `200 OK`-Body. Er beendet den Stream ohne verschachtelte Blockpage. Auch hier bleibt Suricata der einzige Entscheider.

## Defaults

```text
PHIMANTEC_SURICATA_MPM=hs
PHIMANTEC_SURICATA_SPM=hs
PHIMANTEC_SURICATA_RUNMODE=workers
PHIMANTEC_SURICATA_BODY_LIMIT=1048576
```

## Release-Gates

v0.3.12 ergaenzt Regressionstests fuer:

```text
- Suricata YAML HTTP body limit und Hyperscan-Defaults
- Proxy streaming guard fuer chunked/until-close Responses
- bestehende Flow-ID-Korrelation
- bestehendes Suricata-only Blocking-Gate
- bestehender EVE-Cursor/Web-UI-Pfad
```
