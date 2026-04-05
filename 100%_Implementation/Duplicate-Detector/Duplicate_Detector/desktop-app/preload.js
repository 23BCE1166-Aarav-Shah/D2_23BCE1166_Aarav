const { contextBridge, ipcRenderer } = require('electron');
const path = require('path');

const BACKEND_BASE_URL = process.env.DUPLICATE_BACKEND_URL || 'http://127.0.0.1:8080';

function loadDuplicateEngineAddon() {
  try {
    if (process.env.DUPLICATE_ENGINE_ADDON_PATH) {
      return require(process.env.DUPLICATE_ENGINE_ADDON_PATH);
    }

    return require(path.join(__dirname, '..', 'native-addon'));
  } catch (error) {
    return null;
  }
}

const duplicateEngine = loadDuplicateEngineAddon();
let currentProgress = 0;
let currentScanMode = duplicateEngine ? 'native-addon' : 'rest-fallback';
let currentJobId = null;

async function postJson(url, body) {
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(body)
  });

  const payload = await response.json();
  if (!response.ok) {
    throw new Error(payload.error || `Request failed: ${response.status}`);
  }
  return payload;
}

async function getJson(url) {
  const response = await fetch(url);
  const payload = await response.json();
  if (!response.ok) {
    throw new Error(payload.error || `Request failed: ${response.status}`);
  }
  return payload;
}

async function scanViaBackend(scanPaths) {
  currentProgress = 0;
  currentScanMode = 'rest-fallback';
  const start = await postJson(`${BACKEND_BASE_URL}/scan`, { paths: scanPaths });
  const jobId = start.id;
  currentJobId = jobId;

  while (true) {
    const status = await getJson(`${BACKEND_BASE_URL}/status/${jobId}`);
    currentProgress = Number(status.progress || 0);

    if (status.status === 'done') {
      currentProgress = 100;
      const results = await getJson(`${BACKEND_BASE_URL}/results/${jobId}`);
      currentJobId = null;
      return results.result || [];
    }

    if (status.status === 'failed' || status.status === 'cancelled') {
      currentJobId = null;
      throw new Error(`Scan ${status.status}`);
    }

    await new Promise((resolve) => setTimeout(resolve, 500));
  }
}

contextBridge.exposeInMainWorld('desktopApi', {
  scan: (scanPaths) => {
    currentScanMode = duplicateEngine ? 'native-addon' : 'rest-fallback';
    return duplicateEngine ? duplicateEngine.scan(scanPaths) : scanViaBackend(scanPaths);
  },
  cancelScan: async () => {
    if (duplicateEngine) {
      return false;
    }

    if (!currentJobId) {
      return false;
    }

    await postJson(`${BACKEND_BASE_URL}/cancel/${currentJobId}`, {});
    return true;
  },
  getProgress: () => duplicateEngine ? duplicateEngine.getProgress() : currentProgress,
  mode: () => currentScanMode,
  selectFolder: () => ipcRenderer.invoke('dialog:select-folder'),
  getThumbnail: (filePath, width, height) =>
    ipcRenderer.invoke('file:get-thumbnail', filePath, width, height),
  getPreview: (filePath, options) =>
    ipcRenderer.invoke('file:get-preview', filePath, options || {}),
  getFileInfo: (filePath) =>
    ipcRenderer.invoke('file:get-info', filePath),
  openFile: (filePath) =>
    ipcRenderer.invoke('file:open', filePath),
  moveFilesToTrash: (filePaths) =>
    ipcRenderer.invoke('files:move-to-trash', filePaths),
  undoLastTrash: () =>
    ipcRenderer.invoke('files:undo-last-trash')
});
