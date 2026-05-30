#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${PHIMANTEC_ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"
OUT_DIR="${PHIMANTEC_OFFLINE_DEBS_DIR:-${ROOT_DIR}/offline-debs/${ARCH}}"
APT_GET="${APT_GET:-apt-get}"

packages=(
  ca-certificates
  openssl
  libssl-dev
  zlib1g
  zlib1g-dev
  libpcre2-8-0
  libpcre2-dev
  libyaml-0-2
  libyaml-dev
  libjansson4
  libjansson-dev
  libpcap0.8
  libpcap0.8-dev
  libnet1
  libnet1-dev
  libcap-ng0
  libcap-ng-dev
  libmagic1
  libmagic-dev
  libnspr4
  libnss3
  suricata
  suricata-update
  python3
  python3-flask
  python3-yaml
)

log() { printf '[offline-debs] %s\n' "$*"; }
fail() { printf '[offline-debs] ERROR: %s\n' "$*" >&2; exit 1; }

install -d -m 0755 "$OUT_DIR"
printf '%s\n' "${packages[@]}" > "$OUT_DIR/packages.requested.txt"

if [[ "${PHIMANTEC_OFFLINE_DEBS_DRY_RUN:-0}" == "1" ]]; then
  log "dry-run package list written to ${OUT_DIR}/packages.requested.txt"
  exit 0
fi

(
  cd "$OUT_DIR"
  for pkg in "${packages[@]}"; do
    log "download ${pkg}"
    apt download "$pkg" || fail "apt download failed for ${pkg}"
  done
  find . -maxdepth 1 -type f -name '*.deb' -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)

log "complete: ${OUT_DIR}"
