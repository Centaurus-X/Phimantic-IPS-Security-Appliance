#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BUILD_DIR:=build}"
if [[ ! -f config/ca/phimantec-bump-ca.crt || ! -f config/ca/phimantec-bump-ca.key ]]; then
  scripts/gen_ca.sh
fi
"$BUILD_DIR/phimantec-proxyd" -c config/phimantec-proxyd.conf -v
