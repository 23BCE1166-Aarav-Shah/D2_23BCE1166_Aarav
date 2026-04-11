#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/desktop-app"
CACHE_DIR="$ROOT_DIR/offline-cache"
NPM_CACHE_DIR="$CACHE_DIR/npm"
ELECTRON_CACHE_DIR="$CACHE_DIR/electron"
ELECTRON_BUILDER_CACHE_DIR="$CACHE_DIR/electron-builder"
NODE_MODULES_ARCHIVE="$CACHE_DIR/node_modules.tar.gz"

export npm_config_cache="$NPM_CACHE_DIR"
export npm_config_offline="true"
export npm_config_audit="false"
export npm_config_fund="false"
export npm_config_update_notifier="false"
export ELECTRON_CACHE="$ELECTRON_CACHE_DIR"
export ELECTRON_BUILDER_CACHE="$ELECTRON_BUILDER_CACHE_DIR"

mkdir -p "$NPM_CACHE_DIR" "$ELECTRON_CACHE_DIR" "$ELECTRON_BUILDER_CACHE_DIR"

have_local_frontend_deps() {
  [[ -x "$APP_DIR/node_modules/.bin/electron-builder" ]] && [[ -d "$APP_DIR/node_modules/electron" ]]
}

if [[ -f "$NODE_MODULES_ARCHIVE" ]]; then
  echo "Restoring desktop-app/node_modules from offline archive"
  rm -rf "$APP_DIR/node_modules"
  tar -xzf "$NODE_MODULES_ARCHIVE" -C "$APP_DIR"
else
  if [[ ! -f "$APP_DIR/package-lock.json" ]]; then
    if have_local_frontend_deps; then
      echo "Using existing desktop-app/node_modules (package-lock.json is missing)"
    else
      echo "Missing $APP_DIR/package-lock.json"
      echo "Prepare the offline frontend cache on a connected machine first."
      exit 1
    fi
  else
    echo "Installing frontend dependencies from offline npm cache"
    pushd "$APP_DIR" >/dev/null
      npm ci --offline --ignore-scripts=false
    popd >/dev/null
  fi
fi

if [[ ! -x "$APP_DIR/node_modules/.bin/electron-builder" ]]; then
  echo "electron-builder is not available after offline install"
  exit 1
fi

if [[ ! -d "$APP_DIR/node_modules/electron" ]]; then
  echo "electron package is missing after offline install"
  exit 1
fi

echo "Offline frontend dependencies are ready"
