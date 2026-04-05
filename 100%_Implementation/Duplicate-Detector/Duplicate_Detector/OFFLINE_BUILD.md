# Fully Offline Linux Packaging

The repository supports a fully offline Linux packaging flow for Ubuntu 20.04+.

One-command offline build:

```bash
./scripts/package_linux.sh
```

This works only after the offline frontend cache has been prepared on a connected machine and copied into this repository.

## Required Directory Structure

```text
offline-cache/
  npm/
  electron/
  electron-builder/
  node_modules.tar.gz        optional but recommended
desktop-app/
  package.json
  package-lock.json
scripts/
  build_backend.sh
  install_frontend_offline.sh
  package_linux.sh
  prepare_offline_frontend_cache.sh
  verify_offline_package.sh
```

## Connected Machine Preparation

Use a machine with internet access and the same target architecture as the offline builder.

1. Install Node.js 22.x and npm 10.9.3.
2. From the repository root, run:

```bash
chmod +x scripts/prepare_offline_frontend_cache.sh
./scripts/prepare_offline_frontend_cache.sh
```

This will:

- generate `desktop-app/package-lock.json`
- populate `offline-cache/npm/`
- populate `offline-cache/electron/`
- populate `offline-cache/electron-builder/`
- create `offline-cache/node_modules.tar.gz`

3. Copy these onto the offline build machine:

```text
offline-cache/
desktop-app/package-lock.json
```

## Exact Manual Commands For Cache Preparation

If you prefer to run the steps manually on the connected machine:

```bash
export npm_config_cache="$PWD/offline-cache/npm"
export ELECTRON_CACHE="$PWD/offline-cache/electron"
export ELECTRON_BUILDER_CACHE="$PWD/offline-cache/electron-builder"

mkdir -p "$npm_config_cache" "$ELECTRON_CACHE" "$ELECTRON_BUILDER_CACHE"

cd desktop-app
rm -rf node_modules package-lock.json
npm install --package-lock-only
npm ci
./node_modules/.bin/electron-builder --config electron-builder.yml --linux AppImage deb --publish never || true
tar -czf ../offline-cache/node_modules.tar.gz node_modules
```

The `electron-builder` command is intentionally allowed to fail because its purpose here is to warm the local cache with AppImage and helper binaries. After it runs once on the connected machine, those cached artifacts are transferred offline.

## Offline Build Machine

After copying the cache artifacts into the repository:

```bash
chmod +x scripts/build_backend.sh
chmod +x scripts/install_frontend_offline.sh
chmod +x scripts/package_linux.sh
chmod +x scripts/verify_offline_package.sh

./scripts/package_linux.sh
```

The script will:

1. build the C++ backend with CMake
2. bundle runtime shared libraries into `dist/install/backend/lib`
3. restore or install `desktop-app/node_modules` using only local offline cache
4. run `electron-builder`
5. verify output artifacts and backend shared libraries

## Reproducibility Notes

- `desktop-app/package.json` uses exact dependency versions.
- `desktop-app/package-lock.json` must be generated once and kept under version control or transferred with the cache.
- `npm ci --offline` is used for deterministic installs.
- Electron caches are supplied locally through:
  - `ELECTRON_CACHE`
  - `ELECTRON_BUILDER_CACHE`
- No internet access is required during the actual build.
