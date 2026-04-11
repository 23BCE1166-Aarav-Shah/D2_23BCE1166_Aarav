# Duplicate Scanner

Offline duplicate and similar-file detector for Linux desktops.

This project combines:
- a C++ duplicate detection engine
- an optional local C++ backend service
- an Electron + React desktop application
- offline packaging for `.deb` and `AppImage`

It is designed to work fully offline:
- no cloud APIs
- no internet calls at runtime
- local filesystem processing only

## Features

- Exact duplicate detection for binary files
- Similar image and video grouping with perceptual hashing
- Audio grouping with local fingerprinting
- SQLite-based caching for faster rescans
- Local preview and review workflow
- Safe actions only:
  - move to Trash
  - undo last Trash action
  - no automatic deletion
- Desktop UI with:
  - folder selection
  - scan progress
  - grouped results
  - per-file selection
  - open file in default system app
- Linux packaging:
  - `.deb`
  - `AppImage`

## Project Structure

```text
.
├── include/duplicate_finder/   # public C++ library headers
├── src/                        # core C++ library implementation
├── desktop-app/                # Electron + React desktop app
├── native-addon/               # optional Node native addon wrapper
├── scripts/                    # build, run, package, test scripts
├── tests/                      # unit and integration tests
├── cmake/                      # runtime dependency bundling helpers
├── offline-cache/              # offline npm / Electron cache layout
├── CMakeLists.txt              # backend build config
└── config.example.json         # example runtime config
```

## Architecture

### Core modules

- `file_scanner`
  Walks folders and classifies files.
- `hash_engine`
  Computes:
  - sampled and full `xxhash` for binaries
  - visual perceptual hash for images and videos
  - audio fingerprint for audio files
- `duplicate_engine`
  Main scan orchestration and grouping entrypoint.
- `database_cache`
  SQLite cache for path, size, modified time, and hashes.
- `duplicate_detector`
  Internal grouping logic.
- `preview_engine`
  Local preview generation support.
- `file_manager`
  Safe move-to-trash and undo logic.
- `watcher_service`
  Linux `inotify` watcher for incremental rescans.

### Desktop runtime

The Electron app uses this order:
1. Native addon if available
2. Local REST backend fallback on `127.0.0.1:8080`

The packaged app bundles the backend locally and stays offline.

## Detection Model

### Binary files

Binary duplicate detection uses a staged pipeline:
1. size bucketing
2. sampled `xxhash`
3. full `xxhash`
4. byte-for-byte verification

This keeps large scans faster and reduces unnecessary I/O.

### Images and videos

Images and videos are grouped with perceptual visual hashing and fuzzy comparison.

### Audio

Audio is grouped with local fingerprinting.

## Safety Model

- Files are never auto-deleted
- Destructive actions require explicit user action
- Default cleanup path is Trash
- Undo is supported for the last Trash operation

## Requirements

### Build-time

- Linux
- CMake 3.16+
- C++17 compiler
- OpenCV
- FFmpeg development libraries
- Chromaprint development libraries
- Node.js + npm for the desktop app

### Runtime

The backend bundles its own major shared libraries, but Electron still expects normal Linux desktop libraries to exist on the target machine.

Common required base packages:
- `libgtk-3-0`
- `libnotify4`
- `libnss3`
- `libxss1`
- `libxtst6`
- `xdg-utils`

## Build The Backend

```bash
cd /path/to/project
./scripts/build_backend.sh
```

Manual equivalent:

```bash
cmake -S . -B build/backend -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/dist/install/backend"
cmake --build build/backend --parallel
cmake --install build/backend
```

## Run The Desktop App

```bash
cd /path/to/project
./scripts/run_desktop_app.sh
```

This will:
- build the backend if needed
- start the Electron app

## Native Addon

The desktop app can use the native addon directly if it is available.

If you have local addon dependencies ready:

```bash
cd /path/to/project/native-addon
npm run build
```

Then run the app with:

```bash
cd /path/to/project
export DUPLICATE_ENGINE_ADDON_PATH="$PWD/native-addon/build/Release/duplicate_engine_addon.node"
./scripts/run_desktop_app.sh
```

## Offline Frontend Setup

If you are building with no internet access, prepare:
- `offline-cache/npm/`
- `offline-cache/electron/`
- `offline-cache/electron-builder/`

Then:

```bash
cd /path/to/project
./scripts/install_frontend_offline.sh
```

If `desktop-app/node_modules` already exists locally, the script can use it as a fallback.

More details:
- [OFFLINE_BUILD.md](./OFFLINE_BUILD.md)
- [BUILDING_LINUX.md](./BUILDING_LINUX.md)

## Package For Linux

Create both `.deb` and `AppImage`:

```bash
cd /path/to/project
./scripts/package_linux.sh
```

Artifacts are written to:

```text
desktop-app/dist/
```

Typical outputs:
- `Duplicate Scanner-1.0.0-amd64.deb`
- `Duplicate Scanner-1.0.0-x86_64.AppImage`

## Run On Another Linux Machine

### `.deb`

```bash
sudo dpkg -i "Duplicate Scanner-1.0.0-amd64.deb"
sudo apt-get install -f
duplicate-scanner
```

### `AppImage`

```bash
chmod +x "Duplicate Scanner-1.0.0-x86_64.AppImage"
./"Duplicate Scanner-1.0.0-x86_64.AppImage"
```

## Testing

### Integration test

```bash
cd /path/to/project
./scripts/integration/test_full_scan_pipeline.sh
```

This validates:
- binary duplicate grouping
- image/video grouping
- mixed file types
- scan pipeline behavior

### Dependency verification

```bash
./scripts/verify_runtime_deps.sh
bash ./scripts/verify_offline_package.sh
```

## Current Limitations

- Audio grouping can still be overly permissive on very small synthetic clips
- The project currently targets Linux first
- Offline frontend packaging is easiest when local `node_modules` or prepared offline caches are already available

## License

Currently marked as `UNLICENSED` in the desktop package metadata.

If you plan to make the repository public, choose and add an explicit license before publishing.
