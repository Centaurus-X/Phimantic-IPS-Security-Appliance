# PHIMANTEC C11 Appliance v0.3.9 Diagnose

## Ausgangslage

v0.3.7 konnte den Docker-Stack bereits starten und v0.3.8 enthielt die richtigen
Konzepte fuer CA-Validierung, host-network Runtime und Suricata 8. Der letzte
praktische Release-Blocker war der Docker-Build des `suricata-node`-Images:

```text
dpkg: error processing archive ... suricata-update_1.3.0-2_amd64.deb
trying to overwrite '/usr/bin/suricata-update', which is also in package suricata 1:8.0.5-0ubuntu0
```

## Root Cause Suricata 8

Das OISF-PPA liefert Suricata 8 bereits inklusive `/usr/bin/suricata-update`.
Die v0.3.8-Dockerfile-Zeile installierte aber gleichzeitig das Ubuntu-Paket
`suricata-update`. Dadurch kollidierten zwei Pakete auf derselben Datei und
`dpkg` brach den Image-Build ab.

## Patch v0.3.9

Der Suricata-Node installiert jetzt zuerst nur das OISF-`suricata`-Paket plus
Runtime-Abhaengigkeiten. Danach wird `suricata-update` nur noch nachinstalliert,
wenn das Binary nicht schon vom Suricata-Paket bereitgestellt wurde.

```text
suricata tini
command -v suricata-update || apt install suricata-update
suricata --build-info muss Version >= 8 zeigen
```

Damit bleibt der Suricata-8-Gate fail-fast, ohne den bekannten OISF/Ubuntu
Dateikonflikt erneut auszulösen.

## Zertifikatsdiagnose

Docker-CA und exportierte CA wurden bereits in v0.3.8 mit CA-Erweiterungen
validiert. v0.3.9 schliesst die restliche Luecke im VM-Installer: bestehende
oder neu erzeugte VM-CAs werden jetzt ebenfalls auf `CA:TRUE`, `keyCertSign`,
`cRLSign`, Ablaufzeit und Key/Cert-Match geprueft. Nicht browserfaehige CAs
werden automatisch neu erzeugt.

Die Leaf-Zertifikate werden weiterhin mit 128-bit Seriennummer, kritischen
`CA:FALSE` BasicConstraints, `digitalSignature`, `serverAuth`, SKI, AKI und
korrektem DNS/IP-SAN erzeugt. Neu ist ein C-Unit-Test, der DNS- und IP-Leafs
gegen die erzeugte CA validiert.

## Neue Release-Gates

- Dockerfile darf `suricata` und `suricata-update` nicht mehr gemeinsam in einem
  APT-Aufruf installieren.
- Dockerfile muss ein `command -v suricata-update`-Fallback besitzen.
- `scripts/gen_ca.sh` wird im Release-Verify gegen CA-Extensions und
  `openssl verify` getestet.
- `tests/test_cert_generation.c` validiert DNS- und IP-Leaf-Zertifikate.
- VM-Installer regeneriert nicht browserfaehige Alt-CAs.

## Erwartung auf dem Zielhost

```bash
scripts/phimantec_compose.sh ps
sudo ss -ltnp | grep -E ':(3128|5050|18080|18090)\b'
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject -issuer -text | grep -E 'CA:TRUE|Certificate Sign|CRL Sign'
scripts/phimantec_compose.sh compose exec -T suricata-node suricata --build-info | grep 'This is Suricata version'
scripts/phimantec_smoke.sh
```

Erwartet:

```text
Suricata version >= 8
proxy_ca_chain_ok
ca_export_ok
suricata8_ok
[smoke-docker] complete
```
