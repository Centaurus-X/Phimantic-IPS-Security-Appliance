# Phimantic-IPS-Security-Appliance

## Security Policy

## Security model

Phimantic-IPS-Security-Appliance is designed as a strict Suricata-only inspection gateway.

Security invariants:

```text
No final Suricata allow verdict -> no forwarding.
Final Suricata alert verdict    -> block.
Pending / timeout / unavailable -> fail-closed.
```

The proxy must not become a separate malware decision engine. It handles TLS, HTTP normalization, flow IDs and enforcement. Suricata remains the only inspection decision source.

## Supported code line

The public repository line is the maintained code line for this appliance. Security fixes should target the current default branch unless maintainers explicitly define a maintenance branch.

## Reporting vulnerabilities

Do not open a public issue for a suspected vulnerability.

Use one of the private channels configured by the repository owner:

1. GitHub Security Advisories, if enabled for the repository.
2. A private maintainer contact channel listed by the repository owner.

Include:

- affected component;
- exact reproduction steps;
- logs and configuration details without secrets;
- expected and observed behavior;
- whether EICAR or another safe test artifact was used;
- potential impact on the fail-closed chain.

## High-priority vulnerability classes

Please report privately if you find:

- any fail-open path;
- uninspected forwarding;
- FlowBus HMAC bypass;
- ruleset reload that activates invalid rules;
- authentication or authorization bypass in the web UI;
- CA private key leakage;
- token leakage;
- path traversal or arbitrary file write in the management backend;
- a way to disable Suricata inspection without an explicit administrative action.

## Handling rules

Maintainers should acknowledge reports, reproduce them, prepare a minimal fix, run EICAR and smoke tests, and publish a clear advisory after the fix is available. Public advisories should avoid unnecessary exploit detail until users have had time to update.

## Operational hardening

- Change default UI credentials.
- Change shared FlowBus tokens before production exposure.
- Keep the management UI on a trusted network.
- Import only the current generated CA into test browsers.
- Keep fail policy closed.
- Validate rules with `suricata -T` before live reload.
