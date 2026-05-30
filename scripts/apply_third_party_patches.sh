#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PHIMANTEC_VENDOR_BUILD_DIR:-${ROOT_DIR}/third_party/build}"
PATCH_ROOT="${PHIMANTEC_VENDOR_PATCH_DIR:-${ROOT_DIR}/third_party/patches}"

log() { printf '[vendor-patch] %s\n' "$*"; }
fail() { printf '[vendor-patch] ERROR: %s\n' "$*" >&2; exit 1; }

find_source_dir() {
  local name="$1"
  local direct="${BUILD_DIR}/${name}"
  if [[ -d "$direct" ]]; then
    printf '%s\n' "$direct"
    return 0
  fi
  local found
  found="$(find "$BUILD_DIR" -maxdepth 1 -mindepth 1 -type d -name "${name}-*" | sort | head -n 1 || true)"
  if [[ -n "$found" ]]; then
    printf '%s\n' "$found"
    return 0
  fi
  return 1
}

apply_patch_series() {
  local name="$1"
  local src_dir
  local patch_dir="${PATCH_ROOT}/${name}"
  local series="${patch_dir}/series"

  if ! src_dir="$(find_source_dir "$name")"; then
    log "skip ${name}: source directory missing in ${BUILD_DIR}"
    return 0
  fi
  if [[ ! -f "$series" ]]; then
    log "skip ${name}: no series file"
    return 0
  fi

  while IFS= read -r patch_name || [[ -n "$patch_name" ]]; do
    patch_name="${patch_name%%#*}"
    patch_name="$(printf '%s' "$patch_name" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//')"
    [[ -z "$patch_name" ]] && continue
    [[ -f "${patch_dir}/${patch_name}" ]] || fail "patch missing: ${patch_dir}/${patch_name}"
    log "apply ${name}: ${patch_name}"
    patch -d "$src_dir" -p1 < "${patch_dir}/${patch_name}"
  done < "$series"
}

packages=(zlib libyaml jansson pcre2 hyperscan libpcap libnet libcap-ng openssl libhtp suricata)
if [[ $# -gt 0 ]]; then
  packages=("$@")
fi

for pkg in "${packages[@]}"; do
  apply_patch_series "$pkg"
done

log "complete"
