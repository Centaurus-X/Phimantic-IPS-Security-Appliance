#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${PHIMANTEC_ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"
OUT_DIR="${PHIMANTEC_RUNTIME_LIBS_DIR:-${ROOT_DIR}/runtime-libs/${ARCH}}"
INCLUDE_SYSTEM="${PHIMANTEC_VENDOR_INCLUDE_SYSTEM_LIBS:-0}"

log() { printf '[runtime-libs] %s\n' "$*"; }
fail() { printf '[runtime-libs] ERROR: %s\n' "$*" >&2; exit 1; }

if [[ $# -lt 1 ]]; then
  cat >&2 <<USAGE
Usage: scripts/collect_runtime_libs.sh /path/to/suricata [/path/to/phimantec-binary ...]

Copies a whitelist of runtime libraries referenced by ldd into runtime-libs/<arch>/.
Set PHIMANTEC_VENDOR_INCLUDE_SYSTEM_LIBS=1 to include libc/glibc/system base libs.
USAGE
  exit 2
fi

install -d -m 0755 "$OUT_DIR/bin" "$OUT_DIR/lib" "$OUT_DIR/licenses"
: > "$OUT_DIR/MANIFEST.runtime.txt"

allow_lib() {
  local base="$1"
  case "$base" in
    libssl.so*|libcrypto.so*|libpcre2-*.so*|libhs.so*|libhs_runtime.so*|libyaml*.so*|libjansson.so*|libpcap.so*|libnet.so*|libcap-ng.so*|libhtp*.so*|libz.so*|libmagic.so*|libnss*.so*|libnspr*.so*|libplc*.so*|libplds*.so*) return 0 ;;
    libc.so*|ld-linux*.so*|libpthread.so*|libm.so*|libdl.so*|libgcc_s.so*|libstdc++.so*) [[ "$INCLUDE_SYSTEM" == "1" ]] && return 0 || return 1 ;;
    *) return 1 ;;
  esac
}

copy_unique() {
  local src="$1"
  local dst_dir="$2"
  [[ -e "$src" ]] || return 0
  local base
  base="$(basename "$src")"
  if [[ -e "${dst_dir}/${base}" ]]; then
    return 0
  fi
  cp -aL "$src" "${dst_dir}/${base}"
  printf '%s\n' "${dst_dir}/${base}" >> "$OUT_DIR/MANIFEST.runtime.txt"
}

for bin in "$@"; do
  [[ -x "$bin" ]] || fail "not executable: ${bin}"
  copy_unique "$bin" "$OUT_DIR/bin"
  while IFS= read -r lib; do
    [[ -n "$lib" ]] || continue
    base="$(basename "$lib")"
    if allow_lib "$base"; then
      log "copy ${base}"
      copy_unique "$lib" "$OUT_DIR/lib"
    fi
  done < <(ldd "$bin" | awk '/=> \/|^\// { for (i=1;i<=NF;i++) if ($i ~ /^\//) print $i }' | sort -u)
done

(
  cd "$OUT_DIR"
  find bin lib licenses -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
log "complete: ${OUT_DIR}"
