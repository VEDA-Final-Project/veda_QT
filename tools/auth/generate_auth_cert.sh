#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <output-dir> <host-or-ip>"
  exit 1
fi

OUT_DIR="$1"
HOST_NAME="$2"

mkdir -p "$OUT_DIR"

CRT_PATH="$OUT_DIR/server.crt"
KEY_PATH="$OUT_DIR/server.key"

openssl req -x509 -newkey rsa:2048 -sha256 -nodes \
  -keyout "$KEY_PATH" \
  -out "$CRT_PATH" \
  -days 825 \
  -subj "/CN=$HOST_NAME" \
  -addext "subjectAltName=DNS:$HOST_NAME,IP:$HOST_NAME"

chmod 600 "$KEY_PATH"
chmod 644 "$CRT_PATH"

echo "Generated:"
echo "  cert: $CRT_PATH"
echo "  key : $KEY_PATH"
