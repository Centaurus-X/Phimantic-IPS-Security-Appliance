# third_party/sources/

Hier liegen nur Original-Source-Archive fuer echte Air-Gap-Rebuilds.

Wichtig:

```text
Suricata Source wird vorerst NICHT im Repository mitgeliefert.
third_party/sources/suricata-8.0.5.tar.gz kann manuell abgelegt werden.
```

Erwartete optionale Archive:

```text
openssl-3.5.6.tar.gz
pcre2-10.47.tar.gz
hyperscan-5.4.2.tar.gz
zlib-*.tar.gz
libyaml-*.tar.gz
jansson-*.tar.gz
libpcap-*.tar.gz
libnet-*.tar.gz
libcap-ng-*.tar.gz
libhtp-*.tar.gz
suricata-8.0.5.tar.gz      # manuell, nicht Teil des schlanken Repos
```

`third_party/manifest.lock` beschreibt die erwarteten Namen, Bundle-Stufen und
Lizenz-/Build-Informationen. Hashwerte muessen nach dem Ablegen der Archive mit
`scripts/vendor_verify.sh --update-template` bzw. manuell gepflegt werden.
