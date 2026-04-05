# Linux Build And Packaging

This repository packages the application as:

- `AppImage`
- `.deb`

The packaged desktop app is fully local-only:

- Electron frontend
- Bundled C++ backend service bound to `127.0.0.1:8080`
- No cloud or internet runtime calls

## Prerequisites

Build on Ubuntu 20.04 or newer with these available locally:

- `cmake` 3.16+
- `g++` with C++17 support
- `pkg-config`
- OpenCV development files
- FFmpeg development files:
  - `libavformat`
  - `libavcodec`
  - `libavutil`
  - `libswresample`
- Chromaprint development files
- Node.js 22+
- npm
- Electron dependencies already available in a local npm cache or vendored `node_modules`

Recommended Ubuntu packages:

```bash
sudo apt install build-essential cmake pkg-config \
  libopencv-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libchromaprint-dev
```

For Electron packaging, make sure `desktop-app/node_modules` contains:

- `electron`
- `electron-builder`
- `react`
- `react-dom`

## Backend Build

Build and install the backend into a staging directory:

```bash
chmod +x scripts/build_backend.sh
./scripts/build_backend.sh
```

This creates:

- `build/backend`
- `dist/install/backend/bin/duplicate_backend_service`
- `dist/install/backend/lib/*.so*`

The CMake install step also copies runtime shared libraries into the install tree so the packaged app does not depend on the target machine having matching OpenCV, FFmpeg, or Chromaprint runtime libraries.

## Runtime Dependency Check

Verify the staged backend before packaging:

```bash
chmod +x scripts/verify_runtime_deps.sh
./scripts/verify_runtime_deps.sh
```

## Frontend Packaging

Package the Electron app together with the staged backend:

```bash
chmod +x scripts/package_linux.sh
./scripts/package_linux.sh
```

Artifacts are emitted under:

```text
desktop-app/dist/
```

Expected outputs:

- `*.AppImage`
- `*.deb`

## Notes

- The Electron shell starts the bundled backend executable automatically.
- The backend service always binds to `127.0.0.1`.
- Packaging assumes an offline-capable build environment with dependencies already installed or cached locally.
