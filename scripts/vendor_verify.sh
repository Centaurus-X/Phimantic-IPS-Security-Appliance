#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCES_DIR="${PHIMANTEC_VENDOR_SOURCES_DIR:-${ROOT_DIR}/third_party/sources}"
CHECKSUM_FILE="${PHIMANTEC_VENDOR_SHA256_FILE:-${SOURCES_DIR}/SHA256SUMS}"

log() { printf '[vendor-verify] %s\n' "$*"; }
fail() { printf '[vendor-verify] ERROR: %s\n' "$*" >&2; exit 1; }

if [[ "${1:-}" == "--write" || "${1:-}" == "--update-template" ]]; then
  find "$SOURCES_DIR" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name '*.tgz' -o -name '*.tar.xz' -o -name '*.zip' \) \
    -print0 | sort -z | xargs -0 sha256sum > "$CHECKSUM_FILE"
  log "wrote ${CHECKSUM_FILE}"
  exit 0
fi

[[ -f "$CHECKSUM_FILE" ]] || fail "missing checksum file: ${CHECKSUM_FILE}; run scripts/vendor_verify.sh --write after adding archives"
(
  cd "$SOURCES_DIR"
  sha256sum -c "$(basename "$CHECKSUM_FILE")"
)
log "complete"
