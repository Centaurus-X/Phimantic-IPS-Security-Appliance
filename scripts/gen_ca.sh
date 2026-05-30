#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CA_DIR="${PHIMANTEC_CA_DIR:-${ROOT_DIR}/config/ca}"
CA_CERT="${CA_DIR}/phimantec-bump-ca.crt"
CA_KEY="${CA_DIR}/phimantec-bump-ca.key"
CA_CN="${PHIMANTEC_CA_CN:-PHIMANTEC C11 Docker Lab Root CA}"
CA_ORG="${PHIMANTEC_CA_ORG:-PHIMANTEC}"
CA_OU="${PHIMANTEC_CA_OU:-C11 Docker Appliance}"
CA_DAYS="${PHIMANTEC_CA_DAYS:-3650}"
CONF="${CA_DIR}/phimantec-root-ca.openssl.cnf"

install -d -m 0750 "$CA_DIR"
cat > "$CONF" <<EOF_CONF
[ req ]
default_bits = 4096
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = v3_ca

[ dn ]
CN = ${CA_CN}
O = ${CA_ORG}
OU = ${CA_OU}

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true, pathlen:0
keyUsage = critical, keyCertSign, cRLSign
EOF_CONF

openssl req -x509 -newkey rsa:4096 -sha256 -days "$CA_DAYS" -nodes \
  -config "$CONF" -extensions v3_ca \
  -keyout "$CA_KEY" \
  -out "$CA_CERT"
chmod 0600 "$CA_KEY"
chmod 0644 "$CA_CERT"
openssl x509 -in "$CA_CERT" -noout -subject -issuer -fingerprint -sha256
printf 'CA generated:\n  %s\n  %s\n' "$CA_CERT" "$CA_KEY"
