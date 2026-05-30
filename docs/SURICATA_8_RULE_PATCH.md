# Suricata 8 Rule Patch

## Problem

Suricata 8 rejects the old lab rules when legacy HTTP content modifiers are used in the wrong context, for example:

```text
"http_header" keyword found inside the rule without a content context
"http_client_body" keyword found inside the rule without a content context
```

## Fix

The lab rules now use Suricata 8 sticky buffers:

```text
http.request_header; content:"...";
http.request_body; content:"...";
```

`http.request_body` replaces the old `http_client_body` spelling. The generic lane marker alert was removed because the Phase-1 inspectord treats every Suricata alert containing the PHIMANTEC flow ID as a blocking signal.

## VM repair

Run this on the Suricata VM:

```bash
cd /home/philo/phimantec-c11-appliance
PROXY_IP=192.168.0.232 scripts/repair_suricata_rules_vm.sh
```
