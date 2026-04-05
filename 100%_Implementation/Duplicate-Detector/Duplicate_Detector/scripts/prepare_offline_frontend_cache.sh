#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/desktop-app"
CACHE_DIR="$ROOT_DIR/offline-cache"
NPM_CACHE_DIR="$CACHE_DIR/npm"
ELECTRON_CACHE_DIR="$CACHE_DIR/electron"
ELECTRON_BUILDER_CACHE_DIR="$CACHE_DIR/electron-builder"

mkdir -p "$NPM_CACHE_DIR" "$ELECTRON_CACHE_DIR" "$ELECTRON_BUILDER_CACHE_DIR"

export npm_config_cache="$NPM_CACHE_DIR"
export ELECTRON_CACHE="$ELECTRON_CACHE_DIR"
export ELECTRON_BUILDER_CACHE="$ELECTRON_BUILDER_CACHE_DIR"

pushd "$APP_DIR" >/dev/null
  rm -rf node_modules package-lock.json
  npm install --package-lock-only
  npm ci
  ./node_modules/.bin/electron-builder --config electron-builder.yml --linux AppImage deb --publish never || true
popd >/dev/null

tar -czf "$CACHE_DIR/node_modules.tar.gz" -C "$APP_DIR" node_modules

echo "Offline frontend cache prepared in $CACHE_DIR"
echo "Transfer the following to the offline build machine:"
echo "  - offline-cache/"
echo "  - desktop-app/package-lock.json"
