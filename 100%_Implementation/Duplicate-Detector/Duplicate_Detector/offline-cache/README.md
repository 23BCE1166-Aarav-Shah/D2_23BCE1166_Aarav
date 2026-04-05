# Offline Cache Layout

This directory stores all frontend build artifacts required for a fully offline packaging run.

Expected contents:

- `npm/`
  - npm content-addressable cache used by `npm ci --offline`
- `electron/`
  - Electron binary cache consumed during `electron` install
- `electron-builder/`
  - `electron-builder` cache for AppImage tooling and helper binaries
- `node_modules.tar.gz` (optional)
  - Prebuilt `desktop-app/node_modules` archive for faster deterministic restores

The packaging pipeline prefers `node_modules.tar.gz` when present and otherwise falls back to:

```bash
npm ci --offline
```

with the local cache directories above.
