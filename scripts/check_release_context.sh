#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_VERSION="0.3.18-monitor-offline-vendor"
EXPECTED_RELEASE="phimantec-c11-appliance-v0.3.18-monitor-offline-vendor"
fail(){ printf '[release-context] ERROR: %s\n' "$*" >&2; exit 64; }
base="$(basename "$ROOT_DIR")"
[[ "$base" == "$EXPECTED_RELEASE" || "${PHIMANTEC_ALLOW_RENAMED_ROOT:-0}" == "1" ]] || fail "falsches Verzeichnis: ${ROOT_DIR}; erwartet ${EXPECTED_RELEASE}. Setze PHIMANTEC_ALLOW_RENAMED_ROOT=1 nur bewusst."
[[ -f "$ROOT_DIR/docker-compose.yml" ]] || fail "docker-compose.yml fehlt"
[[ -f "$ROOT_DIR/.env.example" ]] || fail ".env.example fehlt"
[[ -f "$ROOT_DIR/BUILD_INFO.txt" ]] || fail "BUILD_INFO.txt fehlt"
grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" "$ROOT_DIR/.env.example" || fail ".env.example passt nicht zu ${EXPECTED_VERSION}"
grep -q "PHIMANTEC_VERSION=${EXPECTED_VERSION}" "$ROOT_DIR/BUILD_INFO.txt" || fail "BUILD_INFO.txt passt nicht zu ${EXPECTED_VERSION}"
! grep -Eq "^[[:space:]]*container_name:" "$ROOT_DIR/docker-compose.yml" || fail "fixed container_name ist in v0.3.4 verboten"
printf '[release-context] OK: %s\n' "$ROOT_DIR"
