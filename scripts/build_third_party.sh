#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PHIMANTEC_VENDOR_BUILD_DIR:-${ROOT_DIR}/third_party/build}"
STAGE_DIR="${PHIMANTEC_VENDOR_STAGE_DIR:-${ROOT_DIR}/third_party/stage}"
JOBS="${PHIMANTEC_VENDOR_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

log() { printf '[vendor-build] %s\n' "$*"; }
fail() { printf '[vendor-build] ERROR: %s\n' "$*" >&2; exit 1; }

install -d -m 0755 "$STAGE_DIR"

find_dir() {
  local pattern="$1"
  find "$BUILD_DIR" -maxdepth 1 -mindepth 1 -type d -name "$pattern" | sort | head -n 1
}

cmake_build_install() {
  local src="$1"
  local name="$2"
  shift 2
  local bld="${BUILD_DIR}/${name}-cmake-build"
  cmake -S "$src" -B "$bld" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$STAGE_DIR" \
    -DBUILD_SHARED_LIBS=ON \
    "$@"
  cmake --build "$bld" -j"$JOBS"
  cmake --install "$bld"
}

configure_make_install() {
  local src="$1"
  shift
  (
    cd "$src"
    ./configure --prefix="$STAGE_DIR" "$@"
    make -j"$JOBS"
    make install
  )
}

build_zlib() {
  local src
  src="$(find_dir 'zlib-*')"
  [[ -n "$src" ]] || { log 'skip zlib: source missing'; return 0; }
  configure_make_install "$src"
}

build_libyaml() {
  local src
  src="$(find_dir 'yaml-*')"
  [[ -n "$src" ]] || src="$(find_dir 'libyaml-*')"
  [[ -n "$src" ]] || { log 'skip libyaml: source missing'; return 0; }
  configure_make_install "$src"
}

build_jansson() {
  local src
  src="$(find_dir 'jansson-*')"
  [[ -n "$src" ]] || { log 'skip jansson: source missing'; return 0; }
  cmake_build_install "$src" jansson -DJANSSON_BUILD_DOCS=OFF -DJANSSON_WITHOUT_TESTS=ON
}

build_pcre2() {
  local src
  src="$(find_dir 'pcre2-*')"
  [[ -n "$src" ]] || { log 'skip pcre2: source missing'; return 0; }
  cmake_build_install "$src" pcre2 \
    -DPCRE2_BUILD_PCRE2_8=ON \
    -DPCRE2_BUILD_TESTS=OFF \
    -DPCRE2_BUILD_PCRE2GREP=OFF
}

build_hyperscan() {
  local src
  src="$(find_dir 'hyperscan-*')"
  [[ -n "$src" ]] || { log 'skip hyperscan: source missing'; return 0; }
  cmake_build_install "$src" hyperscan -DBUILD_SHARED_LIBS=ON
}

build_libpcap() {
  local src
  src="$(find_dir 'libpcap-*')"
  [[ -n "$src" ]] || { log 'skip libpcap: source missing'; return 0; }
  configure_make_install "$src"
}

build_libnet() {
  local src
  src="$(find_dir 'libnet-*')"
  [[ -n "$src" ]] || { log 'skip libnet: source missing'; return 0; }
  configure_make_install "$src"
}

build_libcap_ng() {
  local src
  src="$(find_dir 'libcap-ng-*')"
  [[ -n "$src" ]] || { log 'skip libcap-ng: source missing'; return 0; }
  configure_make_install "$src" --without-python
}

build_openssl() {
  local src
  src="$(find_dir 'openssl-*')"
  [[ -n "$src" ]] || { log 'skip openssl: source missing'; return 0; }
  (
    cd "$src"
    ./Configure linux-x86_64 --prefix="$STAGE_DIR" --openssldir="$STAGE_DIR/ssl" shared no-tests no-docs
    make -j"$JOBS"
    make install_sw
  )
}

build_libhtp() {
  local src
  src="$(find_dir 'libhtp-*')"
  [[ -n "$src" ]] || { log 'skip libhtp: source missing'; return 0; }
  if [[ -x "${src}/autogen.sh" && ! -x "${src}/configure" ]]; then
    (cd "$src" && ./autogen.sh)
  fi
  configure_make_install "$src"
}

build_suricata() {
  local src
  src="$(find_dir 'suricata-*')"
  [[ -n "$src" ]] || { log 'skip suricata: source missing by policy'; return 0; }
  (
    cd "$src"
    export PKG_CONFIG_PATH="${STAGE_DIR}/lib/pkgconfig:${STAGE_DIR}/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
    export CFLAGS="-I${STAGE_DIR}/include ${CFLAGS:-}"
    export LDFLAGS="-L${STAGE_DIR}/lib -Wl,-rpath,${STAGE_DIR}/lib ${LDFLAGS:-}"
    ./configure \
      --prefix="$STAGE_DIR" \
      --enable-af-packet \
      --enable-hyperscan \
      --with-libpcap-includes="$STAGE_DIR/include" \
      --with-libpcap-libraries="$STAGE_DIR/lib" \
      --with-libpcre-includes="$STAGE_DIR/include" \
      --with-libpcre-libraries="$STAGE_DIR/lib" \
      --with-libyaml-includes="$STAGE_DIR/include" \
      --with-libyaml-libraries="$STAGE_DIR/lib" \
      --with-jansson-includes="$STAGE_DIR/include" \
      --with-jansson-libraries="$STAGE_DIR/lib"
    make -j"$JOBS"
    make install
  )
}

export PKG_CONFIG_PATH="${STAGE_DIR}/lib/pkgconfig:${STAGE_DIR}/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${STAGE_DIR}/lib:${STAGE_DIR}/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

build_zlib
build_libyaml
build_jansson
build_pcre2
build_hyperscan
build_libpcap
build_libnet
build_libcap_ng
build_openssl
build_libhtp
build_suricata

log "complete: ${STAGE_DIR}"
