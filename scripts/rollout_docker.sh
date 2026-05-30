#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NO_CACHE=0
for arg in "$@"; do case "$arg" in --no-cache) NO_CACHE=1;; -h|--help) echo 'Usage: scripts/rollout_docker.sh [--no-cache]'; exit 0;; *) echo "[rollout-docker] ERROR: unknown option: $arg" >&2; exit 1;; esac; done
cd "$ROOT_DIR"
scripts/phimantec_compose.sh init-env
scripts/phimantec_compose.sh down
scripts/phimantec_compose.sh doctor
if [[ "$NO_CACHE" == 1 ]]; then scripts/phimantec_compose.sh build --no-cache; else scripts/phimantec_compose.sh build; fi
scripts/phimantec_compose.sh up
scripts/phimantec_compose.sh ps
scripts/phimantec_compose.sh smoke
