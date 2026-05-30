# Phimantic-IPS-Security-Appliance

## Troubleshooting

## Fast status overview

```bash
scripts/phimantec_compose.sh ps
scripts/phimantec_compose.sh logs proxy
scripts/phimantec_compose.sh logs suricata-node
```

## Proxy or monitor port is busy

```bash
ss -ltnp | grep -E ':(3128|5050)\b' || true
docker ps -a --filter "name=phimantec"
```

Remove stale containers:

```bash
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
```

## Deployment guard rejects the directory

A GitHub clone usually has a repository directory name rather than the historical release directory name.

```bash
export PHIMANTEC_ALLOW_RENAMED_ROOT=1
```

Then rerun the deployment command.

## Browser certificate warning

Re-export the current CA:

```bash
scripts/export_docker_ca.sh
ls -l ~/phimantec-bump-ca.crt
```

Remove older PHIMANTEC test CAs from the browser, import the current certificate, and enable trust for websites.

## Inspection Unavailable

This is expected fail-closed behavior when no final Suricata allow verdict is available. Inspect the detailed reason:

```bash
scripts/phimantec_compose.sh compose logs --no-color --tail=1000 proxy suricata-node   | grep -E 'Inspection unavailable|inspection-|FlowBus|verdict|lane|timeout|queue|suricata' || true
```

Run FlowBus probes:

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node   /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-suricata-node.conf -t 1000

scripts/phimantec_compose.sh compose exec -T proxy   /usr/local/bin/phimantec-flowbus-probe -c /etc/phimantec/phimantec-proxyd.conf -t 1000
```

## EICAR is not blocked

Stop testing normal browsing and inspect the security chain:

```bash
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh
scripts/phimantec_compose.sh compose logs --no-color --tail=1000 proxy suricata-node
```

Check that:

```text
Suricata is running.
phimantec-local.rules is loaded.
eve.json is writable and updated.
inspectord is running.
FlowBus tokens match.
Proxy fail policy is closed.
```

## Rules reload fails

Validate and query Suricata directly:

```bash
scripts/phimantec_compose.sh compose exec -T suricata-node sh -lc '
set -e
suricata -T -c /etc/suricata/suricata.yaml -v
suricatasc -c ruleset-stats /var/run/suricata/suricata-command.socket || true
suricatasc -c ruleset-failed-rules /var/run/suricata/suricata-command.socket || true
'
```

A failed `suricata -T` means the running engine should not be reloaded. The old valid engine remains active.

## Pages are slow under many tabs

Check queue and inflight logs:

```bash
scripts/phimantec_compose.sh compose logs --no-color --tail=1000 proxy   | grep -E 'queue_ms|inflight|host_inflight|client_inflight|verdict_budget|timeout' || true
```

Conservative tuning values are in `.env.example`. Increasing pool and queue values can improve multi-tab loading on stronger hosts without changing the security model.

## Reset to a clean deployment

```bash
docker ps -a --filter "name=phimantec" -q | xargs -r docker rm -f
rm -f .env
cp .env.example .env
PHIMANTEC_ALLOW_RENAMED_ROOT=1 PHIMANTEC_AUTO_STOP_LEGACY_HOST_SERVICES=1 scripts/phimantec_deploy.sh --no-cache
```
