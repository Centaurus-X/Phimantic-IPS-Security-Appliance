#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${ROOT_DIR}/scripts/check_release_context.sh"
"${ROOT_DIR}/scripts/preflight_docker.sh" --all
"${ROOT_DIR}/scripts/phimantec_compose.sh" ps || true
