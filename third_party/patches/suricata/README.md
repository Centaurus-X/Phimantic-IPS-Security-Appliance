# third_party/patches/suricata/

Suricata-Engine-Code wird fuer PHIMANTEC nicht geforkt.

Die Standardintegration nutzt stattdessen:

```text
config/suricata/local.rules
config/suricata/phimantec-suricata.yaml.fragment
scripts/phimantec_suricata_yaml_patch.py
```

Damit bleiben die Aenderungen klein, auditierbar und rueckbaubar. Source-
Patches fuer Suricata sollten erst aktiviert werden, wenn eine konkrete
Upstream-kompatible Notwendigkeit besteht. Die `series` bleibt deshalb aktuell
leer.
