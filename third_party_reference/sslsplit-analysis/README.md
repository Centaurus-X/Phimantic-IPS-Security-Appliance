# SSLsplit Archive Analysis

Input archive: `sslsplit-develop(1).zip`

## Observed facts

| Item | Finding |
|---|---|
| Files | 325 |
| C/H line count | 211004 total by `wc -l` |
| License | BSD 2-Clause in `LICENSE` |
| Product model | transparent SSL/TLS interception |
| Explicit browser proxy | README states SSLsplit cannot act as an HTTP or SOCKS proxy configured in a browser |

## Engineering diagnosis

SSLsplit is a useful reference for general TLS-interception concepts, but it is not a fit as a direct product base for this architecture because the requested PHIMANTEC appliance is an explicit HTTP CONNECT proxy with a verdict API, FlowBus HMAC, Suricata clear lane and proxy-side enforcement.

## Clean-room decision

No SSLsplit source files were copied into this project. The PHIMANTEC project uses its own module names, protocols and implementation.
