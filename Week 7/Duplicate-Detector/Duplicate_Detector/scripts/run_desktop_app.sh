#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -x "$ROOT_DIR/dist/install/backend/bin/duplicate_backend_service" ]]; then
  "$ROOT_DIR/scripts/build_backend.sh"
fi

cd "$ROOT_DIR/desktop-app"
npm start
