# PHIMANTEC C11 Appliance v0.3.8 Diagnose

## Ausgangslage

v0.3.7 behebt die externe Erreichbarkeit durch `network_mode: host`. Der
Windows-Test gegen `192.168.0.232:5050` und `192.168.0.232:3128` kann dadurch
bestanden werden. Danach blieben zwei reale Probleme:

1. Firefox meldete fuer HTTPS-Seiten einen Zertifikats-/MITM-Fehler, obwohl ein
   PHIMANTEC-Zertifikat importiert wurde.
2. Der Docker-Container installierte ueber die Ubuntu-24.04-Standardpakete
   weiterhin Suricata 7.0.3, nicht Suricata 8.

## Root Cause Zertifikat

Die Proxy-CA wurde bisher nur erzeugt, wenn im Docker-Volume keine CA vorhanden
war. Dadurch konnten alte oder nicht browserfaehig erzeugte CA-Dateien weiter
verwendet werden. Zusaetzlich wurde im Smoke-Test nur geprueft, ob die exportierte
Datei ein X.509-Zertifikat ist; es wurde nicht geprueft, ob diese CA wirklich die
aktuell vom Proxy genutzte CA ist und ob ein Browser eine durch den Proxy erzeugte
Leaf-Kette mit dieser CA validieren kann.

## Zertifikats-Fix

v0.3.8 erzeugt die Root-CA mit expliziten CA-Erweiterungen:

```text
basicConstraints = critical, CA:true, pathlen:0
keyUsage         = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
```

Beim Containerstart wird eine vorhandene CA validiert. Bei unpassendem Subject,
fehlendem CA-Flag, fehlender keyCertSign-Verwendung, abgelaufener CA oder nicht
passendem Private Key wird die alte CA gesichert und eine neue browserfaehige CA
erzeugt. Die Leaf-Zertifikate erhalten 128-bit Seriennummern, kritische
BasicConstraints `CA:FALSE`, kritische KeyUsage `digitalSignature`, EKU
`serverAuth`, SKI, AKI und SAN fuer DNS- oder IP-Ziele.

## Root Cause Suricata-Version

Ubuntu 24.04 liefert ueber die Standard-Repositories Suricata 7.0.3. Fuer
Suricata 8 muss das OISF-PPA genutzt werden. v0.3.8 installiert Suricata im
suricata-node-Container aus `ppa:oisf/suricata-stable` und blockiert Build und
Runtime, wenn keine Version >= 8 erkannt wird.

## Neue Release-Gates

`scripts/phimantec_smoke.sh` prueft jetzt zusaetzlich:

- exportierte CA ist eine echte CA (`CA:TRUE`, `keyCertSign`)
- exportierte CA-Fingerprint == Proxy-Container-CA-Fingerprint
- lokaler HTTPS-Test ueber den PHIMANTEC-Proxy mit `curl --cacert ~/phimantec-bump-ca.crt`
- Suricata-Version im Container ist >= 8

## Erwartung auf dem Zielhost

```bash
scripts/phimantec_compose.sh ps
sudo ss -ltnp | grep -E ':(3128|5050|18080|18090)\b'
openssl x509 -in "$HOME/phimantec-bump-ca.crt" -noout -subject -issuer -text | grep -E 'CA:TRUE|Certificate Sign'
scripts/phimantec_compose.sh compose exec -T suricata-node suricata --build-info | grep 'This is Suricata version'
scripts/phimantec_smoke.sh
```

Erwartet:

```text
TcpTestSucceeded : True fuer 5050 und 3128
Suricata version >= 8
proxy_ca_chain_ok
ca_export_ok
suricata8_ok
```
