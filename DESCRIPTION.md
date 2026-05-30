# Phimantic-IPS-Security-Appliance

Phimantic-IPS-Security-Appliance is a strict HTTPS inspection gateway for lab, research and controlled network environments. It decrypts browser traffic locally, normalizes it to HTTP/1.1, sends the plain HTTP flow to Suricata, and enforces only the final Suricata-derived verdict.

## Purpose

The project is designed to make a Suricata-based inspection chain usable for HTTPS traffic without moving the security decision into the proxy. The proxy is responsible for transport handling, TLS bumping, H1.1 normalization, flow tagging and enforcement. Suricata remains the only component that evaluates security rules and decides whether traffic is malicious or clean.

## Operating model

```text
Client browser
  -> TLS bump proxy
  -> Plain HTTP Lane
  -> Suricata HTTP app-layer and rules
  -> EVE output
  -> inspectord flow correlation
  -> FlowBus verdict
  -> proxy enforcement
```

Final verdict behavior:

```text
Suricata alert          -> block
Suricata no-alert final -> allow
pending / unavailable   -> fail-closed
```

## Key capabilities

- HTTPS TLS bump with generated local CA.
- HTTP/1.1-only inspection mode for deterministic page loading.
- Suricata EVE correlation with flow IDs.
- HMAC-protected FlowBus verdict protocol.
- Fail-closed proxy enforcement.
- Multi-stream page loading support with bounded inspection queueing.
- Web management for events, local rules, ET/Open rulesets and live ruleset reloads.
- EICAR matrix testing and release verification scripts.

## Non-goals

The system intentionally does not provide:

- fail-open behavior;
- domain, CDN, media or image bypasses;
- proxy-side malware heuristics;
- uninspected forwarding;
- a replacement for Suricata rule logic.

## Web UI attribution

The web management UI is based on and adapted from the `suriguard` UI template by SEc-123: <https://github.com/SEc-123>.
