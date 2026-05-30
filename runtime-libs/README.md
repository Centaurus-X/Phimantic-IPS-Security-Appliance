# runtime-libs/

Dieses Verzeichnis ist die schlanke Runtime-Stufe des Offline-Bundles.

Zweck:

```text
runtime-libs/
  <arch>/
    bin/      optionale Runtime-Binaries, z.B. suricata, openssl
    lib/      fertige .so Runtime-Libraries
    licenses/ Lizenztexte, falls separat gesammelt
    SHA256SUMS
    MANIFEST.runtime.txt
```

Empfohlen fuer normale Admin-Deployments:

```text
Docker Runtime Image + runtime-libs + SBOM + SHA256SUMS
```

Nicht Ziel dieses Ordners:

```text
Compiler
Header/Dev-Pakete
vollstaendige Ubuntu/glibc-Systembasis
Third-Party-Source-Archive
```

Die kleinen Libraries wie `zlib`, `libjansson`, `libyaml`, `libcap-ng`,
`libnet`, `PCRE2`, `libpcap` und `libhtp` sind platzmaessig unkritisch. Die
wirklich grossen Bestandteile bleiben Suricata samt Runtime-Abhaengigkeiten,
Hyperscan, NSS/nspr, Rulesets, Ubuntu/glibc-Basis und Builder-/Dev-Pakete.

Befuellen:

```bash
scripts/collect_runtime_libs.sh /usr/bin/suricata
```

Das Skript kopiert nur explizit erlaubte Suricata-/PHIMANTEC-nahe Runtime-
Bibliotheken und erzeugt Checksums.
