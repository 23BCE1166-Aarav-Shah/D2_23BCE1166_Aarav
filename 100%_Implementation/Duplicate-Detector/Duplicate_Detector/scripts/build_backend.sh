#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/backend}"
INSTALL_DIR="${INSTALL_DIR:-$ROOT_DIR/dist/install/backend}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
cmake --install "$BUILD_DIR"

echo "Backend built and installed to $INSTALL_DIR"
