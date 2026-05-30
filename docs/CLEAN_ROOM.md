# Clean-Room- und Lizenzdiagnose

## Grundsatz

Die neue PHIMANTEC Appliance ist keine umbenannte SSLsplit-Version. Das Ziel ist nicht, Herkunft zu verschleiern, sondern eine eigenständige Implementierung zu bauen, deren Architektur, Modulgrenzen, Protokolle und Hot Path zum PHIMANTEC-Use-Case passen.

## Analyse der Referenz

Das hochgeladene SSLsplit-Archiv enthält eine BSD-2-Clause-Lizenz und eine transparente Proxy-Architektur. SSLsplit ist als technische Referenz nützlich für folgende allgemeine Konzepte:

- TLS-Interception als legitimer Enterprise-/Lab-Baustein
- dynamische Leaf-Zertifikate
- SNI-/Zertifikatslogik
- getrennte Client- und Upstream-TLS-Sessions

## Nicht übernommen

- keine SSLsplit-Dateien im Projektbaum
- keine Funktionsnamen aus SSLsplit als API-Schicht
- keine SSLSplit-Datenstrukturen
- kein NAT-/TPROXY-Architekturmodell
- keine Log-/Mirror-/Cache-Implementierung aus SSLsplit

## Eigene Produktabgrenzung

PHIMANTEC implementiert:

- expliziten Browser-CONNECT-Proxy
- interne Verdict-API
- FlowBus TCP + HMAC
- Suricata-Lane mit `X-Phim-*` Headern
- Inspectord/EVE-Korrelation
- Proxy-Enforcement mit Blockpage

Damit ist die technische Kernlinie klar eigenständig.
