#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/desktop-app"
BACKEND_INSTALL_DIR="${INSTALL_DIR:-$ROOT_DIR/dist/install/backend}"

echo "Checking staged backend shared libraries"
if ldd "$BACKEND_INSTALL_DIR/bin/duplicate_backend_service" | grep -q "not found"; then
  echo "Missing shared library detected in backend executable"
  exit 1
fi

echo "Checking packaged artifacts"
find "$APP_DIR/dist" -maxdepth 1 \( -name "*.AppImage" -o -name "*.deb" \) | sort

if ! find "$APP_DIR/dist" -maxdepth 1 -name "*.AppImage" | grep -q .; then
  echo "AppImage artifact missing"
  exit 1
fi

if ! find "$APP_DIR/dist" -maxdepth 1 -name "*.deb" | grep -q .; then
  echo ".deb artifact missing"
  exit 1
fi

echo "Checking for unexpected remote endpoints in packaged source"
if grep -R -nE 'https?://' "$APP_DIR"/main.js "$APP_DIR"/preload.js "$APP_DIR"/src "$APP_DIR"/index.html \
  | grep -v '127.0.0.1:8080' \
  | grep -v 'BACKEND_HOST' ; then
  echo "Unexpected non-local URL detected"
  exit 1
fi

echo "Offline package verification passed"
