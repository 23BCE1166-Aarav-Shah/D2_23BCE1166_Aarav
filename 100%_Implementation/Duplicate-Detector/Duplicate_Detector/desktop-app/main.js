const { app, BrowserWindow, ipcMain, dialog, nativeImage, shell, protocol } = require('electron');
const path = require('path');
const os = require('os');
const { spawn, exec } = require('child_process');
const util = require('util');
const execPromise = util.promisify(exec);
const fs = require('fs/promises');

const WINDOW_WIDTH = 1440;
const WINDOW_HEIGHT = 920;
const BACKEND_HOST = '127.0.0.1';
const BACKEND_PORT = 8080;
const PREVIEW_CACHE_LIMIT = 600;

let lastTrashOperation = null;
let backendProcess = null;
const previewCache = new Map();

function backendBinaryPath() {
  if (process.env.DUPLICATE_BACKEND_BINARY) {
    return process.env.DUPLICATE_BACKEND_BINARY;
  }

  if (app.isPackaged) {
    return path.join(process.resourcesPath, 'backend', 'bin', 'duplicate_backend_service');
  }

  return path.join(__dirname, '..', 'dist', 'install', 'backend', 'bin', 'duplicate_backend_service');
}

async function isBackendReachable() {
  try {
    const response = await fetch(`http://${BACKEND_HOST}:${BACKEND_PORT}/status/health-check`);
    return response.ok || response.status === 404;
  } catch (error) {
    return false;
  }
}

async function ensureBackendRunning() {
  if (await isBackendReachable()) {
    return true;
  }

  const binary = backendBinaryPath();
  try {
    await fs.access(binary);
  } catch (error) {
    return false;
  }

  backendProcess = spawn(binary, ['--host', BACKEND_HOST, '--port', String(BACKEND_PORT)], {
    stdio: 'ignore',
    detached: false
  });

  backendProcess.on('exit', () => {
    backendProcess = null;
  });

  for (let attempt = 0; attempt < 20; attempt += 1) {
    if (await isBackendReachable()) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }

  return false;
}

function createMainWindow() {
  const window = new BrowserWindow({
    width: WINDOW_WIDTH,
    height: WINDOW_HEIGHT,
    minWidth: 1180,
    minHeight: 760,
    backgroundColor: '#0a0d12',
    titleBarStyle: 'hiddenInset',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  window.loadFile(path.join(__dirname, 'index.html'));
}

async function moveFile(sourcePath, destinationDirectory) {
  const destinationPath = path.join(destinationDirectory, path.basename(sourcePath));

  try {
    await fs.mkdir(destinationDirectory, { recursive: true });
    await fs.rename(sourcePath, destinationPath);
    return { ok: true, path: destinationPath };
  } catch (error) {
    if (error && error.code !== 'EXDEV') {
      return { ok: false, error: error.message };
    }
  }

  try {
    await fs.copyFile(sourcePath, destinationPath);
    await fs.unlink(sourcePath);
    return { ok: true, path: destinationPath };
  } catch (error) {
    return { ok: false, error: error.message };
  }
}

async function moveFileToPath(sourcePath, destinationPath) {
  try {
    await fs.mkdir(path.dirname(destinationPath), { recursive: true });
    await fs.rename(sourcePath, destinationPath);
    return { ok: true, path: destinationPath };
  } catch (error) {
    if (error && error.code !== 'EXDEV') {
      return { ok: false, error: error.message };
    }
  }

  try {
    await fs.copyFile(sourcePath, destinationPath);
    await fs.unlink(sourcePath);
    return { ok: true, path: destinationPath };
  } catch (error) {
    return { ok: false, error: error.message };
  }
}

function trashRootDirectory() {
  return path.join(app.getPath('userData'), 'trash', 'files');
}

function sanitizePathForTrash(filePath) {
  return String(filePath).replace(/[\\/:\s]+/g, '_');
}

async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch (error) {
    return false;
  }
}

async function uniqueTrashPath(filePath) {
  const root = trashRootDirectory();
  await fs.mkdir(root, { recursive: true });

  const extension = path.extname(filePath);
  const baseName = path.basename(filePath, extension);
  const safeName = sanitizePathForTrash(baseName || 'file');

  let candidate = path.join(root, `${Date.now()}_${safeName}${extension}`);
  let suffix = 1;

  while (await pathExists(candidate)) {
    candidate = path.join(root, `${Date.now()}_${safeName}_${suffix}${extension}`);
    suffix += 1;
  }

  return candidate;
}

async function getFileInfo(filePath) {
  try {
    const stats = await fs.stat(filePath);
    return {
      ok: true,
      path: filePath,
      size: stats.size
    };
  } catch (error) {
    return {
      ok: false,
      error: error.message
    };
  }
}

function quoteForShell(value) {
  return `"${String(value).replace(/(["\\$`])/g, '\\$1')}"`;
}

async function openWithSystemDefault(filePath) {
  if (process.platform === 'linux') {
    await execPromise(`xdg-open ${quoteForShell(filePath)}`);
    return;
  }

  if (process.platform === 'darwin') {
    await execPromise(`open ${quoteForShell(filePath)}`);
    return;
  }

  if (process.platform === 'win32') {
    await execPromise(`cmd /c start "" ${quoteForShell(filePath)}`);
    return;
  }

  const result = await shell.openPath(filePath);
  if (result) {
    throw new Error(result);
  }
}

function previewCacheKey(filePath, width, height, kind) {
  return `${kind || 'file'}|${width || 0}|${height || 0}|${filePath}`;
}

function setPreviewCache(key, value) {
  if (previewCache.has(key)) {
    previewCache.delete(key);
  }
  previewCache.set(key, value);
  if (previewCache.size > PREVIEW_CACHE_LIMIT) {
    const oldestKey = previewCache.keys().next().value;
    if (oldestKey) {
      previewCache.delete(oldestKey);
    }
  }
}

async function createPreviewPayload(filePath, width, height, kindHint) {
  const previewWidth = Math.max(64, Number(width || 160));
  const previewHeight = Math.max(64, Number(height || 120));
  const kind = String(kindHint || '').toLowerCase();
  const cacheKey = previewCacheKey(filePath, previewWidth, previewHeight, kind);

  if (previewCache.has(cacheKey)) {
    return previewCache.get(cacheKey);
  }

  let payload = {
    ok: false,
    url: null,
    kind,
    source: 'none'
  };

  try {
    const thumbnail = await nativeImage.createThumbnailFromPath(filePath, {
      width: previewWidth,
      height: previewHeight
    });

    if (!thumbnail.isEmpty()) {
      payload = {
        ok: true,
        url: thumbnail.toDataURL(),
        kind,
        source: 'thumbnail'
      };
      setPreviewCache(cacheKey, payload);
      return payload;
    }
  } catch (error) {
    // Fall through to local image or icon fallback.
  }

  const extension = path.extname(filePath).toLowerCase();
  const imageExtensions = new Set(['.jpg', '.jpeg', '.png', '.bmp', '.webp', '.gif', '.tiff']);
  if (kind === 'image' || imageExtensions.has(extension)) {
    payload = {
      ok: true,
      url: 'local://' + encodeURI(filePath.replace(/#/g, '%23')),
      kind: 'image',
      source: 'file'
    };
    setPreviewCache(cacheKey, payload);
    return payload;
  }

  try {
    const icon = await app.getFileIcon(filePath, { size: 'large' });
    if (!icon.isEmpty()) {
      payload = {
        ok: true,
        url: icon.toDataURL(),
        kind,
        source: 'icon'
      };
      setPreviewCache(cacheKey, payload);
      return payload;
    }
  } catch (error) {
    // Fall through.
  }

  setPreviewCache(cacheKey, payload);
  return payload;
}

async function moveFilesToTrash(filePaths) {
  const operation = {
    id: `${Date.now()}`,
    items: [],
    totalBytes: 0
  };

  for (const filePath of filePaths) {
    const sourceInfo = await getFileInfo(filePath);
    if (!sourceInfo.ok) {
      return { ok: false, error: sourceInfo.error };
    }

    const trashPath = await uniqueTrashPath(filePath);
    const moveResult = await moveFileToPath(filePath, trashPath);
    if (!moveResult.ok) {
      return { ok: false, error: moveResult.error || 'Unable to move file to trash.' };
    }

    operation.items.push({
      originalPath: filePath,
      trashPath,
      size: sourceInfo.size
    });
    operation.totalBytes += sourceInfo.size;
  }

  lastTrashOperation = operation;
  return {
    ok: true,
    operationId: operation.id,
    movedCount: operation.items.length,
    totalBytes: operation.totalBytes
  };
}

async function undoLastTrashOperation() {
  if (!lastTrashOperation || !Array.isArray(lastTrashOperation.items) || lastTrashOperation.items.length === 0) {
    return { ok: false, error: 'No trash operation available to undo.' };
  }

  for (const item of lastTrashOperation.items) {
    try {
      await fs.mkdir(path.dirname(item.originalPath), { recursive: true });
      await fs.rename(item.trashPath, item.originalPath);
    } catch (error) {
      return { ok: false, error: error.message };
    }
  }

  const restoredOperation = lastTrashOperation;
  lastTrashOperation = null;
  return {
    ok: true,
    restoredCount: restoredOperation.items.length,
    totalBytes: restoredOperation.totalBytes
  };
}

ipcMain.handle('dialog:select-folder', async () => {
  if (process.platform === 'linux') {
    try {
      const { stdout } = await execPromise('zenity --file-selection --directory --multiple --separator="|" --title="Select Folders to Scan"');
      const paths = stdout.trim().split('|').filter(p => p.length > 0);
      if (paths.length > 0) {
        return paths;
      }
    } catch (err) {
      if (err.code === 1) {
        return null; // Zenity returns 1 when the user cancels the dialog
      }
      console.warn('Zenity fallback failed, attempting native dialog:', err);
    }
  }

  const result = await dialog.showOpenDialog({
    properties: ['openDirectory', 'multiSelections']
  });

  if (result.canceled || result.filePaths.length === 0) {
    return null;
  }

  return result.filePaths;
});

ipcMain.handle('file:get-thumbnail', async (_event, filePath, width, height) => {
  try {
    const thumbnail = await nativeImage.createThumbnailFromPath(filePath, {
      width: width || 96,
      height: height || 96
    });

    if (!thumbnail.isEmpty()) {
      return thumbnail.toDataURL();
    }
  } catch (error) {
    // Fall through to file icon fallback.
  }

  try {
    const icon = await app.getFileIcon(filePath, { size: 'normal' });
    if (!icon.isEmpty()) {
      return icon.toDataURL();
    }
  } catch (error) {
    return null;
  }

  return null;
});

ipcMain.handle('file:get-preview', async (_event, filePath, options) => {
  const settings = options && typeof options === 'object' ? options : {};
  return createPreviewPayload(
    filePath,
    settings.width,
    settings.height,
    settings.kind
  );
});

ipcMain.handle('file:get-info', async (_event, filePath) => {
  return getFileInfo(filePath);
});

ipcMain.handle('file:open', async (_event, filePath) => {
  try {
    await openWithSystemDefault(filePath);
    return { ok: true };
  } catch (error) {
    return { ok: false, error: error.message };
  }
});

ipcMain.handle('files:move-to-trash', async (_event, filePaths) => {
  return moveFilesToTrash(Array.isArray(filePaths) ? filePaths : []);
});

ipcMain.handle('files:undo-last-trash', async () => {
  return undoLastTrashOperation();
});

app.whenReady().then(async () => {
  protocol.registerFileProtocol('local', (request, callback) => {
    const url = request.url.replace(/^local:\/\//, '');
    callback({ path: decodeURI(url) });
  });

  ensureBackendRunning().finally(() => {
    createMainWindow();
  });

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createMainWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (backendProcess && !backendProcess.killed) {
    backendProcess.kill('SIGTERM');
  }
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
