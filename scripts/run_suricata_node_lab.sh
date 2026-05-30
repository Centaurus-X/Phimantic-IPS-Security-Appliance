#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BUILD_DIR:=build}"
mkdir -p /tmp
"$BUILD_DIR/phimantec-lane-sink" -c config/phimantec-suricata-node.conf -v &
LANE_PID=$!
"$BUILD_DIR/phimantec-inspectord" -c config/phimantec-suricata-node.conf -v &
INSPECT_PID=$!
trap 'kill "$LANE_PID" "$INSPECT_PID" 2>/dev/null || true' INT TERM EXIT
wait
