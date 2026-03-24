#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <cert-path>"
  exit 1
fi

CERT_PATH="$1"

openssl x509 -in "$CERT_PATH" -noout -fingerprint -sha256 \
  | sed 's/^.*=//' \
  | tr -d ':' \
  | tr 'A-Z' 'a-z'
