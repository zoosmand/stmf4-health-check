#!/usr/bin/env bash

set -euo pipefail

dns_name="${1:-health-check.local}"
ip_address="${2:-192.168.0.50}"
validity_days="${3:-3650}"
output_directory="TLS/Private"
private_key="${output_directory}/management_server.key.pem"
certificate="${output_directory}/management_server.crt.pem"

if [[ ! "${dns_name}" =~ ^[A-Za-z0-9.-]+$ ]]; then
  echo "Invalid DNS name: ${dns_name}" >&2
  exit 1
fi

if [[ ! "${ip_address}" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]]; then
  echo "Invalid IPv4 address: ${ip_address}" >&2
  exit 1
fi

if [[ ! "${validity_days}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Validity must be a positive number of days." >&2
  exit 1
fi

mkdir -p "${output_directory}"
umask 077

openssl req \
  -x509 \
  -new \
  -nodes \
  -newkey ec \
  -pkeyopt ec_paramgen_curve:P-256 \
  -pkeyopt ec_param_enc:named_curve \
  -sha256 \
  -days "${validity_days}" \
  -subj "/CN=${dns_name}/O=STM32 Health Check" \
  -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
  -addext "keyUsage=critical,digitalSignature,keyCertSign" \
  -addext "extendedKeyUsage=serverAuth" \
  -addext "subjectAltName=DNS:${dns_name},IP:${ip_address}" \
  -keyout "${private_key}" \
  -out "${certificate}"

chmod 600 "${private_key}"
chmod 644 "${certificate}"
python3 tools/embed_server_credentials.py

echo "Private key: ${private_key}"
echo "Certificate: ${certificate}"
echo "Embedded header: ${output_directory}/management_server_credentials.h"
openssl x509 \
  -in "${certificate}" \
  -noout \
  -subject \
  -issuer \
  -dates \
  -fingerprint \
  -sha256
openssl x509 -in "${certificate}" -noout -text \
  | awk '
      /X509v3 Subject Alternative Name/ {
        getline
        sub(/^[[:space:]]+/, "")
        print "subjectAltName=" $0
      }
    '
