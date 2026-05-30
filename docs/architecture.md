# Phimantic-IPS-Security-Appliance

## Architecture

This document summarizes the current appliance architecture for the public repository.

## Strict inspection chain

```text
Client browser
  -> Proxy TLS bump
  -> HTTP/1.1 normalization
  -> Plain HTTP Lane
  -> Suricata rules and EVE output
  -> inspectord Flow-ID correlation
  -> FlowBus HMAC verdict
  -> Proxy enforcement
```

The proxy does not decide whether content is malicious. It only enforces the final Suricata-derived verdict.

## Components

| Component | Responsibility |
|---|---|
| proxy | CONNECT parsing, TLS bump, HTTP/1.1 normalization, lane submission, verdict request and enforcement |
| lane-sink | accepts plain HTTP samples from the proxy and makes them visible to Suricata |
| Suricata | performs rule-based inspection and writes EVE output |
| inspectord | correlates EVE records and PHIMANTEC Flow-IDs, then produces verdicts |
| FlowBus | HMAC-protected local verdict transport |
| SuricataMonitor | web UI for events, rulesets, settings and runtime reload |

## Verdict behavior

```text
suricata-alert          -> block
suricata-no-alert-final -> allow
pending / timeout       -> fail-closed
unavailable             -> fail-closed
```

## Diagrams

- [Security chain SVG](diagrams/security-chain.svg)
- [Ruleset reload workflow SVG](diagrams/ruleset-reload-workflow.svg)
- [Mermaid architecture](diagrams/phimantic-ips-security-appliance-architecture.md)
