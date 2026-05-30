#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCES_DIR="${PHIMANTEC_VENDOR_SOURCES_DIR:-${ROOT_DIR}/third_party/sources}"
BUILD_DIR="${PHIMANTEC_VENDOR_BUILD_DIR:-${ROOT_DIR}/third_party/build}"

log() { printf '[vendor-unpack] %s\n' "$*"; }
fail() { printf '[vendor-unpack] ERROR: %s\n' "$*" >&2; exit 1; }

rm -rf "$BUILD_DIR"
install -d -m 0755 "$BUILD_DIR"

found=0
while IFS= read -r -d '' archive; do
  found=1
  log "unpack ${archive}"
  case "$archive" in
    *.tar.gz|*.tgz) tar -xzf "$archive" -C "$BUILD_DIR" ;;
    *.tar.xz) tar -xJf "$archive" -C "$BUILD_DIR" ;;
    *.zip) unzip -q "$archive" -d "$BUILD_DIR" ;;
    *) fail "unsupported archive: ${archive}" ;;
  esac
done < <(find "$SOURCES_DIR" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name '*.tgz' -o -name '*.tar.xz' -o -name '*.zip' \) -print0 | sort -z)

if [[ "$found" == "0" ]]; then
  log "no source archives found; nothing unpacked"
fi
log "complete"
