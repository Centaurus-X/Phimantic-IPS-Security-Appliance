
### phimantec-c11-appliance-v0.3.3-offline-vendor

````md
# PHIMANTEC C11 Appliance — Offline Vendoring, Patch-Mechanismus und Third-Party-Build

## 1. Ziel

Dieses Dokument beschreibt ein sauberes, reproduzierbares und offline-fähiges Konzept, um externe C/C++-Bibliotheken, Suricata und weitere Runtime-Komponenten für die PHIMANTEC C11 Appliance bereitzustellen.

Ziel ist nicht, fremde Projekte manuell zu verändern, sondern:

```text
Original-Archive unverändert speichern
SHA256/PGP prüfen
Patches deterministisch anwenden
Third-Party-Komponenten reproduzierbar bauen
PHIMANTEC gegen einen lokalen Stage-Prefix linken
Docker-/Offline-Bundles aus reproduzierbaren Artefakten erzeugen
````

Dadurch bleibt das Projekt wartbar, auditierbar und air-gap-fähig.

---

## 2. Grundentscheidung

Externe Libraries und Suricata sollen nicht direkt im Source-Tree verändert werden.

Falsch:

```text
third_party/suricata/
third_party/openssl/
third_party/hyperscan/
```

und dort manuell Code ändern.

Richtig:

```text
third_party/sources/
third_party/patches/
third_party/build/
third_party/stage/
```

Ablauf:

```text
1. Original-Source-Archive liegen unter third_party/sources/
2. Hashes werden über manifest.lock geprüft
3. Archive werden nach third_party/build/ entpackt
4. Kleine Patch-Serien werden angewendet
5. Komponenten werden gebaut
6. Installation erfolgt nach third_party/stage/
7. PHIMANTEC wird gegen third_party/stage/ gebaut
```

---

## 3. Zielstruktur

```text
third_party/
  manifest.lock

  sources/
    openssl-3.5.6.tar.gz
    suricata-8.0.5.tar.gz
    hyperscan-5.4.2.tar.gz
    pcre2-10.47.tar.gz
    jansson-x.y.z.tar.gz
    libyaml-x.y.z.tar.gz
    libpcap-x.y.z.tar.gz
    libnet-x.y.z.tar.gz
    libcap-ng-x.y.z.tar.gz
    libhtp-x.y.z.tar.gz
    zlib-x.y.z.tar.gz

  patches/
    suricata/
      series
      0001-phimantec-suricata-defaults.patch
      0002-phimantec-eve-payload-defaults.patch

    openssl/
      series

    hyperscan/
      series
      0001-cmake-modern-compat.patch

    pcre2/
      series

    jansson/
      series

    libyaml/
      series

    libpcap/
      series

    libnet/
      series

    libcap-ng/
      series

    libhtp/
      series

  build/
    openssl-3.5.6/
    suricata-8.0.5/
    hyperscan-5.4.2/
    pcre2-10.47/
    ...

  stage/
    bin/
    lib/
    include/
    share/
    etc/
```

---

## 4. Build-Skripte

Empfohlene Skriptstruktur:

```text
scripts/vendor_fetch_manifest.sh
scripts/vendor_verify.sh
scripts/vendor_unpack.sh
scripts/vendor_apply_patches.sh
scripts/vendor_build.sh
scripts/vendor_install_stage.sh
scripts/build_phimantec_against_stage.sh
scripts/build_offline_bundle.sh
scripts/verify_offline_bundle.sh
```

### Bedeutung der Skripte

| Skript                             | Zweck                                                                       |
| ---------------------------------- | --------------------------------------------------------------------------- |
| `vendor_fetch_manifest.sh`         | lädt die in `manifest.lock` definierten Archive, nur im Online-Build        |
| `vendor_verify.sh`                 | prüft SHA256 und optional PGP-Signaturen                                    |
| `vendor_unpack.sh`                 | entpackt Archive deterministisch nach `third_party/build/`                  |
| `vendor_apply_patches.sh`          | wendet pro Paket die Patch-Serie aus `third_party/patches/<name>/series` an |
| `vendor_build.sh`                  | baut alle externen Bibliotheken in korrekter Reihenfolge                    |
| `vendor_install_stage.sh`          | installiert alle Artefakte nach `third_party/stage/`                        |
| `build_phimantec_against_stage.sh` | baut PHIMANTEC gegen die lokalen Stage-Libraries                            |
| `build_offline_bundle.sh`          | erzeugt Docker-Images, `docker save`, Checksums und Doku                    |
| `verify_offline_bundle.sh`         | prüft, dass das Bundle offline startbar und vollständig ist                 |

---

## 5. Beispiel `manifest.lock`

```yaml
schema_version: 1

stage_prefix: "third_party/stage"

packages:
  openssl:
    version: "3.5.6"
    archive: "openssl-3.5.6.tar.gz"
    url: "https://www.openssl.org/source/openssl-3.5.6.tar.gz"
    sha256: "<sha256>"
    patches: []
    build_system: "openssl-configure"
    license: "Apache-2.0"

  pcre2:
    version: "10.47"
    archive: "pcre2-10.47.tar.gz"
    url: "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.gz"
    sha256: "<sha256>"
    patches: []
    build_system: "cmake"
    license: "BSD-3-Clause"

  hyperscan:
    version: "5.4.2"
    archive: "hyperscan-5.4.2.tar.gz"
    url: "https://github.com/intel/hyperscan/archive/refs/tags/v5.4.2.tar.gz"
    sha256: "<sha256>"
    patches:
      - "0001-cmake-modern-compat.patch"
    build_system: "cmake"
    license: "BSD-3-Clause"

  suricata:
    version: "8.0.5"
    archive: "suricata-8.0.5.tar.gz"
    url: "https://www.openinfosecfoundation.org/download/suricata-8.0.5.tar.gz"
    sha256: "<sha256>"
    patches:
      - "0001-phimantec-suricata-defaults.patch"
      - "0002-phimantec-eve-payload-defaults.patch"
    build_system: "autotools"
    license: "GPL-2.0-only"

  jansson:
    version: "x.y.z"
    archive: "jansson-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "cmake"
    license: "MIT"

  libyaml:
    version: "x.y.z"
    archive: "libyaml-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "autotools"
    license: "MIT"

  libpcap:
    version: "x.y.z"
    archive: "libpcap-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "autotools"
    license: "BSD"

  libnet:
    version: "x.y.z"
    archive: "libnet-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "autotools"
    license: "BSD"

  libcap-ng:
    version: "x.y.z"
    archive: "libcap-ng-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "autotools"
    license: "LGPL-2.1-or-later"

  libhtp:
    version: "x.y.z"
    archive: "libhtp-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "autotools"
    license: "BSD"

  zlib:
    version: "x.y.z"
    archive: "zlib-x.y.z.tar.gz"
    url: "<official-url>"
    sha256: "<sha256>"
    patches: []
    build_system: "cmake"
    license: "Zlib"
```

---

## 6. Patch-Policy

### 6.1 Grundregel

Patches dürfen nur klein, nachvollziehbar und begründet sein.

Erlaubt:

```text
Build-Kompatibilität
CMake-/Autotools-Fixes
Default-Konfiguration für Appliance
Suricata-EVE-Payload-Optionen
Hyperscan/PCRE Build-Integration
Compiler-Warning-Fixes, falls notwendig
```

Nicht erlaubt:

```text
große Third-Party-Forks
unreviewte Engine-Logikänderungen
Security-Verhalten fremder Komponenten unklar verändern
manuelle Änderungen im entpackten Source
Patches ohne Kommentar
```

### 6.2 Patch-Header

Jede Patch-Datei sollte einen klaren Header besitzen:

```patch
From: PHIMANTEC Build System
Subject: [PATCH] Enable PHIMANTEC Suricata lab defaults

Upstream-Version: suricata-8.0.5

Reason:
  PHIMANTEC lane inspection needs payload and payload_printable in EVE.
  Hyperscan should be enabled as the default MPM/SPM in the appliance image.

Risk:
  Configuration-only change. No Suricata engine logic changed.

Can-be-removed-when:
  Runtime config generator owns these settings entirely.
```

---

## 7. Patch-Series

Jedes Paket besitzt optional eine `series`-Datei.

Beispiel:

```text
third_party/patches/suricata/series
```

Inhalt:

```text
0001-phimantec-suricata-defaults.patch
0002-phimantec-eve-payload-defaults.patch
```

Leere Zeilen und Kommentare sind erlaubt:

```text
# PHIMANTEC Suricata config defaults
0001-phimantec-suricata-defaults.patch

# EVE payload visibility for lane correlation
0002-phimantec-eve-payload-defaults.patch
```

Wenn ein Paket keine Patches benötigt, kann die `series` leer sein oder fehlen.

---

## 8. Beispiel `vendor_apply_patches.sh`

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

apply_patch_series() {
  local name="$1"
  local src_dir="$2"
  local patch_dir="third_party/patches/${name}"
  local series="${patch_dir}/series"

  if [ ! -d "$src_dir" ]; then
    echo "[vendor] source directory missing for ${name}: ${src_dir}" >&2
    return 1
  fi

  if [ ! -f "$series" ]; then
    echo "[vendor] no patch series for ${name}"
    return 0
  fi

  while IFS= read -r patch_name; do
    case "$patch_name" in
      ""|\#*)
        continue
        ;;
    esac

    if [ ! -f "${patch_dir}/${patch_name}" ]; then
      echo "[vendor] patch missing: ${patch_dir}/${patch_name}" >&2
      return 1
    fi

    echo "[vendor] apply ${name}: ${patch_name}"
    patch -d "$src_dir" -p1 < "${patch_dir}/${patch_name}"
  done < "$series"
}

apply_patch_series "openssl"   "third_party/build/openssl-3.5.6"
apply_patch_series "pcre2"     "third_party/build/pcre2-10.47"
apply_patch_series "hyperscan" "third_party/build/hyperscan-5.4.2"
apply_patch_series "suricata"  "third_party/build/suricata-8.0.5"

echo "[vendor] patch application complete"
```

---

## 9. Beispiel `vendor_verify.sh`

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

verify_sha256() {
  local archive="$1"
  local expected="$2"

  if [ ! -f "$archive" ]; then
    echo "[vendor] missing archive: $archive" >&2
    return 1
  fi

  local actual
  actual="$(sha256sum "$archive" | awk '{print $1}')"

  if [ "$actual" != "$expected" ]; then
    echo "[vendor] SHA256 mismatch for $archive" >&2
    echo "[vendor] expected: $expected" >&2
    echo "[vendor] actual:   $actual" >&2
    return 1
  fi

  echo "[vendor] SHA256 OK: $archive"
}

# Beispielhaft; später aus manifest.lock automatisch generieren.
verify_sha256 "third_party/sources/openssl-3.5.6.tar.gz"   "<sha256>"
verify_sha256 "third_party/sources/pcre2-10.47.tar.gz"     "<sha256>"
verify_sha256 "third_party/sources/hyperscan-5.4.2.tar.gz" "<sha256>"
verify_sha256 "third_party/sources/suricata-8.0.5.tar.gz"  "<sha256>"

echo "[vendor] verification complete"
```

---

## 10. Beispiel `vendor_unpack.sh`

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

rm -rf third_party/build
mkdir -p third_party/build

unpack_archive() {
  local archive="$1"

  if [ ! -f "$archive" ]; then
    echo "[vendor] missing archive: $archive" >&2
    return 1
  fi

  echo "[vendor] unpack: $archive"
  tar -xzf "$archive" -C third_party/build
}

unpack_archive "third_party/sources/openssl-3.5.6.tar.gz"
unpack_archive "third_party/sources/pcre2-10.47.tar.gz"
unpack_archive "third_party/sources/hyperscan-5.4.2.tar.gz"
unpack_archive "third_party/sources/suricata-8.0.5.tar.gz"

echo "[vendor] unpack complete"
```

---

## 11. Stage-Prefix

Alle Third-Party-Komponenten sollen in einen lokalen Prefix installiert werden:

```text
third_party/stage/
```

Typische Inhalte:

```text
third_party/stage/bin/
  openssl
  suricata
  suricata-update

third_party/stage/lib/
  libssl.so
  libcrypto.so
  libpcre2-8.so
  libhs.so
  libjansson.so
  libyaml.so
  libpcap.so
  libnet.so
  libcap-ng.so
  libhtp.so

third_party/stage/include/
  openssl/
  pcre2.h
  hs/
  yaml.h
  jansson.h
  pcap/
  ...

third_party/stage/share/
  suricata/
  licenses/
```

Der Stage-Prefix ist die spätere Quelle für:

```text
Docker Runtime Image
Offline Bundle
SBOM
Library Audit
PHIMANTEC Build
```

---

## 12. Build-Reihenfolge

Die Reihenfolge ist wichtig, weil Suricata von mehreren Libraries abhängt.

Empfohlen:

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
12. PHIMANTEC
```

Je nach Suricata-Build können weitere Dependencies hinzukommen:

```text
NSS/nspr
Lua
libmagic
Rust/Cargo für Suricata-Rust-Komponenten
```

Diese sollten später ebenfalls in `manifest.lock` erfasst werden.

---

## 13. Beispiel `vendor_build.sh`

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(pwd)"
STAGE_DIR="${ROOT_DIR}/third_party/stage"
BUILD_DIR="${ROOT_DIR}/third_party/build"

mkdir -p "$STAGE_DIR"

build_openssl() {
  cd "${BUILD_DIR}/openssl-3.5.6"

  ./Configure linux-x86_64 \
    --prefix="$STAGE_DIR" \
    --openssldir="$STAGE_DIR/ssl" \
    shared \
    no-tests \
    no-docs

  make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
  make install_sw
}

build_pcre2() {
  cmake -S "${BUILD_DIR}/pcre2-10.47" -B "${BUILD_DIR}/pcre2-10.47-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$STAGE_DIR" \
    -DBUILD_SHARED_LIBS=ON \
    -DPCRE2_BUILD_PCRE2_8=ON \
    -DPCRE2_BUILD_TESTS=OFF

  cmake --build "${BUILD_DIR}/pcre2-10.47-build" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
  cmake --install "${BUILD_DIR}/pcre2-10.47-build"
}

build_hyperscan() {
  cmake -S "${BUILD_DIR}/hyperscan-5.4.2" -B "${BUILD_DIR}/hyperscan-5.4.2-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$STAGE_DIR" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_AND_SHARED=ON

  cmake --build "${BUILD_DIR}/hyperscan-5.4.2-build" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
  cmake --install "${BUILD_DIR}/hyperscan-5.4.2-build"
}

build_suricata() {
  cd "${BUILD_DIR}/suricata-8.0.5"

  ./configure \
    --prefix="$STAGE_DIR" \
    --with-libpcap-includes="$STAGE_DIR/include" \
    --with-libpcap-libraries="$STAGE_DIR/lib" \
    --with-libpcre-includes="$STAGE_DIR/include" \
    --with-libpcre-libraries="$STAGE_DIR/lib" \
    --with-libyaml-includes="$STAGE_DIR/include" \
    --with-libyaml-libraries="$STAGE_DIR/lib" \
    --with-jansson-includes="$STAGE_DIR/include" \
    --with-jansson-libraries="$STAGE_DIR/lib" \
    --enable-af-packet \
    --enable-hyperscan

  make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
  make install
}

build_openssl
build_pcre2
build_hyperscan

# Weitere Libs hier ergänzen:
# build_zlib
# build_libyaml
# build_jansson
# build_libpcap
# build_libnet
# build_libcap_ng
# build_libhtp

build_suricata

echo "[vendor] build complete"
```

Hinweis: Dieses Beispiel zeigt das Prinzip. Die exakten Configure-Flags müssen gegen die tatsächlich gewählten Versionen validiert werden.

---

## 14. PHIMANTEC gegen Stage bauen

Beispiel `build_phimantec_against_stage.sh`:

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(pwd)"
STAGE_DIR="${ROOT_DIR}/third_party/stage"
BUILD_DIR="${ROOT_DIR}/build-vendor"

rm -rf "$BUILD_DIR"

export PKG_CONFIG_PATH="${STAGE_DIR}/lib/pkgconfig:${STAGE_DIR}/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
export CFLAGS="-I${STAGE_DIR}/include ${CFLAGS:-}"
export LDFLAGS="-L${STAGE_DIR}/lib -Wl,-rpath,${STAGE_DIR}/lib ${LDFLAGS:-}"
export LD_LIBRARY_PATH="${STAGE_DIR}/lib:${LD_LIBRARY_PATH:-}"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPHIMANTEC_ASAN=OFF \
  -DCMAKE_PREFIX_PATH="$STAGE_DIR"

cmake --build "$BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "[phimantec] build against vendor stage complete"
```

---

## 15. Offline-Bundle-Strategie

Es gibt drei Stufen.

### Stufe A — Runtime-Offline-Bundle

Enthält:

```text
fertige Docker Images
docker-compose.yml
.env.example
Config Templates
Rules
Dokumentation
SHA256SUMS
SBOM
```

Vorteil:

```text
einfachster Betrieb
kein Compiler auf Zielsystem nötig
kein Internet auf Zielsystem nötig
```

Start offline:

```bash
docker load -i phimantec-v0.2.0-images.tar
cp .env.example .env
docker compose up -d
```

### Stufe B — Runtime + Builder-Image

Zusätzlich:

```text
phimantec/builder image
gcc
cmake
make
headers
third_party/sources
third_party/patches
third_party/stage optional
```

Vorteil:

```text
offline rebuild möglich
```

### Stufe C — Vollständiges Source-Vendoring

Zusätzlich:

```text
alle originalen Third-Party-Source-Archive
alle Patches
alle Lizenzdateien
alle Build-Skripte
lokales APT-Repo optional
```

Vorteil:

```text
maximale Air-Gap-Kontrolle
vollständig nachvollziehbarer Build
```

Nachteil:

```text
mehr Pflegeaufwand
größeres Paket
Security-Updates müssen aktiv gepflegt werden
```

---

## 16. Docker-Offline-Bundle

Build auf Online-Host:

```bash
docker build \
  -f docker/proxy/Dockerfile \
  -t phimantec/proxy:0.2.0-lab \
  .

docker build \
  -f docker/suricata-node/Dockerfile \
  -t phimantec/suricata-node:0.2.0-lab \
  .

docker save \
  -o phimantec-v0.2.0-lab-images.tar \
  phimantec/proxy:0.2.0-lab \
  phimantec/suricata-node:0.2.0-lab

sha256sum phimantec-v0.2.0-lab-images.tar > phimantec-v0.2.0-lab-images.tar.sha256
```

Bundle erzeugen:

```bash
tar -czf phimantec-v0.2.0-lab-offline-bundle.tar.gz \
  docker-compose.yml \
  .env.example \
  config \
  docs \
  scripts \
  third_party/manifest.lock \
  third_party/patches \
  phimantec-v0.2.0-lab-images.tar \
  phimantec-v0.2.0-lab-images.tar.sha256

sha256sum phimantec-v0.2.0-lab-offline-bundle.tar.gz \
  > phimantec-v0.2.0-lab-offline-bundle.tar.gz.sha256
```

Start auf Offline-Host:

```bash
tar -xzf phimantec-v0.2.0-lab-offline-bundle.tar.gz
sha256sum -c phimantec-v0.2.0-lab-images.tar.sha256
docker load -i phimantec-v0.2.0-lab-images.tar
cp .env.example .env
docker compose up -d
docker compose ps
```

---

## 17. Welche Komponenten sind Libraries und welche Tools?

| Komponente            |                Kategorie |                    C/C++ Library? |        Runtime nötig? | Build nötig? |
| --------------------- | -----------------------: | --------------------------------: | --------------------: | -----------: |
| `gcc`                 |                 Compiler |                              Nein |                  Nein |           Ja |
| `make`                |               Build-Tool |                              Nein |                  Nein |           Ja |
| `cmake`               |             Build-System |                              Nein |                  Nein |           Ja |
| `pkg-config`          |          Build-Hilfstool |                              Nein |                  Nein |           Ja |
| `openssl`             |                 CLI Tool | Nein, aber nutzt libssl/libcrypto |      Ja, für CA/Debug |     optional |
| `libssl`              |          OpenSSL Runtime |                                Ja |                    Ja |           Ja |
| `libcrypto`           |          OpenSSL Runtime |                                Ja |                    Ja |           Ja |
| `libssl-dev`          |         Header/Dev-Paket |                           Ja, Dev |                  Nein |           Ja |
| `suricata`            |           IDS/IPS Binary |                 C/C++/Rust Binary |                    Ja |     optional |
| `hyperscan` / `libhs` |         Pattern Matching |                   Ja, C++ Library |                    Ja |           Ja |
| `pcre2`               |             Regex Engine |                     Ja, C Library |                    Ja |           Ja |
| `libyaml`             |              YAML Parser |                     Ja, C Library |                    Ja |           Ja |
| `jansson`             |              JSON Parser |                     Ja, C Library |                    Ja |           Ja |
| `libpcap`             |           Packet Capture |                     Ja, C Library |                    Ja |           Ja |
| `libnet`              |      Packet Construction |                     Ja, C Library |                    Ja |           Ja |
| `libcap-ng`           |       Linux Capabilities |                     Ja, C Library |                    Ja |           Ja |
| `libhtp`              | HTTP Parser für Suricata |                     Ja, C Library |                    Ja |           Ja |
| `zlib`                |              Compression |                     Ja, C Library |                    Ja |           Ja |
| `NSS/nspr`            |       Crypto/System Libs |                   Ja, C Libraries | abhängig von Suricata |     abhängig |
| `libmagic`            |      File Type Detection |                     Ja, C Library |              abhängig |     abhängig |
| `ca-certificates`     |               Datenpaket |                              Nein |                    Ja |         Nein |
| `jq`                  |                 CLI Tool |                              Nein |              optional |         Nein |
| `netcat-openbsd`      |                 CLI Tool |                              Nein |              optional |         Nein |
| `tini`                |           Container Init |                              Nein |             empfohlen |         Nein |
| `ubuntu:24.04`        |               Base Image |          enthält glibc/Systemlibs |                    Ja |         Nein |

---

## 18. Was ist `tini`?

`tini` ist ein kleines Init-Programm für Container.

Es läuft als PID 1 und sorgt dafür, dass:

```text
Signale sauber weitergegeben werden
Zombie-Prozesse eingesammelt werden
Container sauber stoppen
```

`tini` ist keine C-Bibliothek für PHIMANTEC und kein Compiler-Tool.

Es ist besonders sinnvoll, wenn ein Container mehrere Prozesse startet, zum Beispiel:

```text
suricata
phimantec-lane-sink
phimantec-inspectord
```

---

## 19. Was sollte möglichst nicht gepatcht werden?

### OpenSSL

OpenSSL sollte möglichst nicht gepatcht werden.

Besser:

```text
Build-Flags setzen
Version pinnen
SHA256/PGP prüfen
```

### PCRE2, libyaml, jansson, libpcap, libnet, libcap-ng

Diese Libraries sollten möglichst unverändert gebaut werden.

Patches nur bei:

```text
Build-Kompatibilität
Security-Fix vor Upstream-Release
Compiler-Kompatibilität
```

### Suricata

Suricata-Engine-Code möglichst nicht patchen.

Besser:

```text
eigene suricata.yaml
eigene local.rules
eigene Runtime-Config
eigener Container-Entrypoint
```

Suricata-Source-Patches nur, wenn es technisch wirklich notwendig ist.

---

## 20. Was sollte gepatcht werden?

Realistische PHIMANTEC-Patches:

```text
Suricata Config Defaults
EVE payload visibility
Hyperscan default config
CMake compatibility for older/newer compilers
Container-friendly paths
Install profile for PHIMANTEC rules
```

Beispiel Suricata-Konfigurationsziele:

```yaml
mpm-algo: hs
spm-algo: hs

pcre:
  jit: true

eve-log:
  enabled: yes
  types:
    - alert:
        payload: yes
        payload-printable: yes
```

Noch besser: Diese Einstellungen nicht als Source-Patch, sondern als eigene `suricata.yaml` im PHIMANTEC-Image ausliefern.

---

## 21. Update-Mechanismus

Updates sollen nicht automatisch produktiv übernommen werden.

Empfohlen:

```bash
make vendor-check-updates
make vendor-update PACKAGE=suricata VERSION=8.0.6
```

### `vendor-check-updates`

```text
prüft offizielle Quellen
zeigt neuere Versionen
ändert nichts
erzeugt Report
```

### `vendor-update`

```text
lädt neue Source
prüft SHA256/PGP
entpackt
wendet bestehende Patch-Serie an
baut Paket
führt Tests aus
erzeugt Review-Diff
```

Erst danach:

```text
Review
Security-Test
Performance-Test
Release-Entscheidung
```

---

## 22. CI-/Release-Gates

Jede Änderung an Third-Party-Komponenten muss diese Gates bestehen:

```bash
scripts/vendor_verify.sh
scripts/vendor_unpack.sh
scripts/vendor_apply_patches.sh
scripts/vendor_build.sh
scripts/build_phimantec_against_stage.sh
ctest --test-dir build-vendor --output-on-failure
```

Zusätzlich:

```bash
suricata --build-info | grep -iE 'Hyperscan|AF_PACKET|PCRE|JIT'
```

Security-Smokes:

```bash
allow_http_code=200
suricata_block_http_code=403
eicar_http_code=403
body_block_http_code=403
```

Offline-Bundle-Gate:

```bash
docker load -i phimantec-v0.2.0-lab-images.tar
docker compose up -d
docker compose ps
```

Keine Netzwerknachladung während Offline-Test erlaubt.

---

## 23. SBOM und Checksums

Jedes Offline-Bundle sollte enthalten:

```text
SHA256SUMS
SBOM.spdx.json
THIRD_PARTY_LICENSES/
third_party/manifest.lock
BUILD_INFO.txt
```

`BUILD_INFO.txt` Beispiel:

```text
PHIMANTEC_VERSION=v0.2.0-lab
SURICATA_VERSION=8.0.5
OPENSSL_VERSION=3.5.6
PCRE2_VERSION=10.47
HYPERSCAN_VERSION=5.4.2
BUILD_DATE=2026-xx-xx
BUILD_MODE=offline-vendor
COMPILER=gcc-x.y.z
```

---

## 24. Empfehlung für PHIMANTEC v0.2.1

Empfohlener nächster Release-Schritt:

```text
phimantec-c11-appliance-v0.2.1-offline-vendor
```

Inhalt:

```text
third_party/manifest.lock
third_party/sources/*.tar.gz
third_party/patches/*/series
scripts/vendor_verify.sh
scripts/vendor_unpack.sh
scripts/vendor_apply_patches.sh
scripts/vendor_build.sh
scripts/build_phimantec_against_stage.sh
docs/THIRD_PARTY_VENDORING.md
docs/PATCH_POLICY.md
docs/OFFLINE_BUILD.md
docs/DOCKER_OFFLINE_DEPLOYMENT.md
```

Umsetzung in Stufen:

```text
Phase A:
  manifest.lock + offline Docker Images + SBOM

Phase B:
  offline-debs/ ergänzen

Phase C:
  Suricata Source + OpenSSL Source vendorn

Phase D:
  Hyperscan, PCRE2, libyaml, jansson, libpcap, libnet, libcap-ng, libhtp vendorn

Phase E:
  vollständiger offline source build
```

---

## 25. Kurzfazit

Der richtige Mechanismus ist:

```text
Original-Archive + manifest.lock + SHA256/PGP + Patch-Series + Stage-Build
```

Nicht:

```text
manuell veränderte Third-Party-Source-Trees
```

Für PHIMANTEC ist die beste Strategie:

```text
1. Docker Runtime Offline Bundle für einfache Admin-Nutzung
2. Builder Image für offline Rebuilds
3. Vendored Third-Party Sources für harte Air-Gap-Umgebungen
4. Kleine, reviewbare Patches statt Forks
5. SBOM + SHA256SUMS + Build Info für Nachvollziehbarkeit
```

Damit liegen Suricata und alle relevanten C/C++-Bibliotheken vollständig kontrolliert beim Projekt, ohne dass die Wartbarkeit verloren geht.

```
```
