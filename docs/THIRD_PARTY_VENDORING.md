# PHIMANTEC C11 — Third-Party Vendoring und Patch-Mechanik

## Prinzip

Fremde Projekte werden nicht direkt im Source-Tree bearbeitet.

```text
Original-Archive -> SHA256 -> unpack -> patch series -> build -> stage
```

Struktur:

```text
third_party/
  manifest.lock
  sources/
  patches/<package>/series
  build/
  stage/
```

## Aktive Skripte

```text
scripts/vendor_verify.sh
scripts/vendor_unpack.sh
scripts/apply_third_party_patches.sh
scripts/build_third_party.sh
scripts/collect_runtime_libs.sh
scripts/collect_offline_debs.sh
```

## Reihenfolge

```text
1. zlib
2. libyaml
3. jansson
4. pcre2
5. hyperscan
6. libpcap
7. libnet
8. libcap-ng
9. OpenSSL
10. libhtp
11. Suricata
```

Suricata Source `suricata-8.0.5.tar.gz` wird aktuell bewusst nicht im
Repository mitgeliefert. Das Build-Skript ueberspringt Suricata, wenn der
Source fehlt.

## Patch-Policy

Erlaubt:

```text
Build-Kompatibilitaet
kleine CMake-/Autotools-Fixes
PHIMANTEC Default-Konfiguration, falls nicht runtime-seitig loesbar
Compiler-Warning-Fixes
```

Nicht erlaubt:

```text
grosse Third-Party-Forks
unklare Security-Verhaltensaenderungen
manuelle Aenderungen im entpackten Source-Tree
Patches ohne Review-Kommentar
```

Aktuell bleibt die Suricata-Patch-Serie leer, weil PHIMANTEC die benoetigten
Defaults runtime-seitig ueber `scripts/phimantec_suricata_yaml_patch.py`,
`config/suricata/local.rules` und die vorhandenen Suricata-Fragmente setzt.
Das ist wartbarer als ein nicht verifizierter Suricata-Source-Patch.

## Minimaler Workflow

```bash
# Quellen manuell oder per kontrolliertem Prozess ablegen
ls third_party/sources/

# Hashdatei erzeugen/pruefen
scripts/vendor_verify.sh --write
scripts/vendor_verify.sh

# Entpacken, patchen, bauen
scripts/vendor_unpack.sh
scripts/apply_third_party_patches.sh
scripts/build_third_party.sh
```
