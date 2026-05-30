#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
printf 'Start first in two terminals:\n  scripts/run_suricata_node_lab.sh\n  scripts/run_proxy_lab.sh\n\n'
printf 'Allow request:\n'
curl -v --noproxy "" --proxy http://127.0.0.1:3128 --cacert config/ca/phimantec-bump-ca.crt https://example.org/
printf '\nBlock request through inspectord test policy:\n'
curl -v --noproxy "" --proxy http://127.0.0.1:3128 --cacert config/ca/phimantec-bump-ca.crt https://example.org/block || true
