#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${PHIMANTEC_VERSION:-0.3.18-monitor-offline-vendor}"
OUT_DIR="${PHIMANTEC_BUNDLE_OUT_DIR:-${ROOT_DIR}/dist}"
IMAGE_TAR="${OUT_DIR}/phimantec-${VERSION}-images.tar"
BUNDLE="${OUT_DIR}/phimantec-${VERSION}-runtime-bundle.tar.gz"

log() { printf '[docker-bundle] %s\n' "$*"; }

install -d -m 0755 "$OUT_DIR"
cd "$ROOT_DIR"

docker compose build

docker save \
  -o "$IMAGE_TAR" \
  "phimantec/proxy:${VERSION}" \
  "phimantec/suricata-node:${VERSION}"
sha256sum "$IMAGE_TAR" > "${IMAGE_TAR}.sha256"

find runtime-libs -type f -print0 2>/dev/null | sort -z | xargs -0 sha256sum > "$OUT_DIR/runtime-libs.SHA256SUMS" || true

tar -czf "$BUNDLE" \
  docker-compose.yml \
  .env.example \
  config/docker \
  config/suricata \
  config/suricata-monitor \
  docs \
  runtime-libs \
  "${IMAGE_TAR#${ROOT_DIR}/}" \
  "${IMAGE_TAR#${ROOT_DIR}/}.sha256"
sha256sum "$BUNDLE" > "${BUNDLE}.sha256"
log "bundle: ${BUNDLE}"
