#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_DIR="${INSTALL_DIR:-$ROOT_DIR/dist/install/backend}"
BIN="$INSTALL_DIR/bin/duplicate_backend_service"

if [[ ! -x "$BIN" ]]; then
  echo "Missing backend executable: $BIN"
  exit 1
fi

echo "Inspecting dynamic dependencies for $BIN"
ldd "$BIN"

if ldd "$BIN" | grep -q "not found"; then
  echo
  echo "Missing shared libraries detected."
  exit 1
fi

echo
echo "Checking bundled shared libraries under $INSTALL_DIR/lib"
find "$INSTALL_DIR/lib" -maxdepth 1 -type f | sort
