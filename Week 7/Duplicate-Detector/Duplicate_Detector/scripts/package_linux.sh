#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/desktop-app"
INSTALL_DIR="${INSTALL_DIR:-$ROOT_DIR/dist/install/backend}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/dist/packages}"
CACHE_DIR="$ROOT_DIR/offline-cache"
NPM_CACHE_DIR="$CACHE_DIR/npm"
ELECTRON_CACHE_DIR="$CACHE_DIR/electron"
ELECTRON_BUILDER_CACHE_DIR="$CACHE_DIR/electron-builder"

"$ROOT_DIR/scripts/build_backend.sh"
"$ROOT_DIR/scripts/install_frontend_offline.sh"

mkdir -p "$OUTPUT_DIR"

pushd "$APP_DIR" >/dev/null
  export DUPLICATE_BACKEND_INSTALL_DIR="$INSTALL_DIR"
  export npm_config_cache="$NPM_CACHE_DIR"
  export npm_config_offline="true"
  export ELECTRON_CACHE="$ELECTRON_CACHE_DIR"
  export ELECTRON_BUILDER_CACHE="$ELECTRON_BUILDER_CACHE_DIR"
  export ELECTRON_SKIP_BINARY_DOWNLOAD="0"
  export ELECTRON_BUILDER_OFFLINE="true"
  export CSC_IDENTITY_AUTO_DISCOVERY="false"
  ./node_modules/.bin/electron-builder \
    --config electron-builder.yml \
    --linux AppImage deb \
    --publish never \
    --projectDir "$APP_DIR"
popd >/dev/null

bash "$ROOT_DIR/scripts/verify_offline_package.sh"

echo "Linux packages are available under $APP_DIR/dist"
