# CA-Verzeichnis

Dieses Verzeichnis enthaelt im Release-ZIP bewusst keine private CA.

Fuer lokale Lab-Tests erzeugt `scripts/gen_ca.sh` eine Root-CA mit den fuer
Browser-Vertrauen notwendigen Erweiterungen:

- `basicConstraints = critical, CA:true, pathlen:0`
- `keyUsage = critical, keyCertSign, cRLSign`
- `subjectKeyIdentifier` und `authorityKeyIdentifier`

```bash
scripts/gen_ca.sh
openssl x509 -in config/ca/phimantec-bump-ca.crt -noout -text \
  | grep -E 'CA:TRUE|Certificate Sign|CRL Sign'
```

Im Docker-Appliance-Betrieb wird die aktive CA aus dem Proxy-Container nach
`$HOME/phimantec-bump-ca.crt` exportiert. Fuer Firefox/Windows muss genau dieses
exportierte Zertifikat als Zertifizierungsstelle importiert werden. Alte
PHIMANTEC-Test-CAs sollten vorher aus dem Browser-Zertifikatsspeicher entfernt
werden, damit kein Issuer-/Fingerprint-Mismatch entsteht.

Fuer Produktion muss hier eine kontrolliert erzeugte interne Unternehmens-CA
oder ein dedizierter PHIMANTEC-Sub-CA-Schluessel mit sicheren Dateirechten
hinterlegt werden.
