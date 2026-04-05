(function renderDesktopApp() {
  const {
    createElement,
    Fragment,
    useEffect,
    useMemo,
    useReducer,
    useRef,
    useState
  } = React;

  const RECENT_FOLDERS_KEY = 'duplicate-scanner-recent-folders';
  const GROUP_PAGE_SIZE = 24;
  const POLL_INTERVAL_MS = 700;

  function formatBytes(value) {
    if (!Number.isFinite(value) || value <= 0) {
      return '0 B';
    }

    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let size = value;
    let unitIndex = 0;

    while (size >= 1024 && unitIndex < units.length - 1) {
      size /= 1024;
      unitIndex += 1;
    }

    const digits = size >= 10 || unitIndex === 0 ? 0 : 1;
    return `${size.toFixed(digits)} ${units[unitIndex]}`;
  }

  function basename(filePath) {
    const normalized = String(filePath || '').replace(/\\/g, '/');
    const parts = normalized.split('/');
    return parts[parts.length - 1] || normalized;
  }

  function normalizeType(type) {
    if (type === 'image' || type === 'video' || type === 'audio') {
      return type;
    }
    return 'file';
  }

  function groupTitleFromType(type) {
    switch (type) {
      case 'image':
        return 'Similar Images';
      case 'video':
        return 'Similar Videos';
      case 'audio':
        return 'Similar Audio';
      default:
        return 'Exact Duplicates';
    }
  }

  function loadRecentFolders() {
    try {
      const raw = window.localStorage.getItem(RECENT_FOLDERS_KEY);
      const parsed = JSON.parse(raw || '[]');
      return Array.isArray(parsed) ? parsed.filter(Boolean).slice(0, 6) : [];
    } catch (error) {
      return [];
    }
  }

  function storeRecentFolders(folders) {
    try {
      window.localStorage.setItem(RECENT_FOLDERS_KEY, JSON.stringify(folders.slice(0, 6)));
    } catch (error) {
      // Ignore localStorage failures in hardened environments.
    }
  }

  function pushRecentFolder(recentFolders, folder) {
    return [folder].concat(recentFolders.filter((entry) => entry !== folder)).slice(0, 6);
  }

  const initialState = {
    selectedFolders: [],
    recentFolders: loadRecentFolders(),
    status: 'idle',
    progress: 0,
    filesScanned: 0,
    scanToken: 0,
    errorMessage: '',
    groups: [],
    selectedFileIds: [],
    confirmationState: null,
    lastOperation: null,
    visibleGroupCount: GROUP_PAGE_SIZE,
    infoMessage: 'Choose a folder to start a local duplicate scan.'
  };

  function reducer(state, action) {
    switch (action.type) {
      case 'select-folder': {
        const newSet = new Set([...state.selectedFolders, ...action.folders]);
        const folders = Array.from(newSet);
        return {
          ...state,
          selectedFolders: folders,
          recentFolders: action.recentFolders,
          errorMessage: '',
          infoMessage: folders.length > 0 ? 'Folders selected. Start a scan when you are ready.' : state.infoMessage
        };
      }
      case 'remove-folder': {
        const folders = state.selectedFolders.filter(f => f !== action.folder);
        return {
          ...state,
          selectedFolders: folders,
          infoMessage: folders.length === 0 ? 'Choose a folder to start a local duplicate scan.' : state.infoMessage
        };
      }
      case 'clear-folders':
        return {
          ...state,
          selectedFolders: [],
          infoMessage: 'Choose a folder to start a local duplicate scan.'
        };
      case 'scan-start':
        return {
          ...state,
          status: 'running',
          progress: 0,
          filesScanned: 0,
          errorMessage: '',
          groups: [],
          selectedFileIds: [],
          confirmationState: null,
          lastOperation: null,
          visibleGroupCount: GROUP_PAGE_SIZE,
          scanToken: state.scanToken + 1,
          infoMessage: 'Scanning files locally. No files are modified during this step.'
        };
      case 'scan-progress':
        return {
          ...state,
          progress: action.progress,
          filesScanned: action.filesScanned
        };
      case 'scan-complete':
        return {
          ...state,
          status: 'done',
          progress: 100,
          filesScanned: action.filesScanned,
          groups: action.groups,
          selectedFileIds: [],
          visibleGroupCount: GROUP_PAGE_SIZE,
          infoMessage: action.groups.length === 0
            ? 'Scan completed. No duplicates were found in this folder.'
            : `Scan completed. Review ${action.groups.length} duplicate groups below.`
        };
      case 'scan-cancelled':
        return {
          ...state,
          status: 'cancelled',
          errorMessage: '',
          infoMessage: 'Scan cancelled. Your files were not changed.'
        };
      case 'scan-failed':
        return {
          ...state,
          status: 'failed',
          errorMessage: action.message,
          infoMessage: 'Something went wrong while scanning.'
        };
      case 'toggle-file': {
        const alreadySelected = state.selectedFileIds.includes(action.fileId);
        return {
          ...state,
          selectedFileIds: alreadySelected
            ? state.selectedFileIds.filter((id) => id !== action.fileId)
            : state.selectedFileIds.concat(action.fileId)
        };
      }
      case 'replace-selection':
        return {
          ...state,
          selectedFileIds: action.fileIds
        };
      case 'show-confirmation':
        return {
          ...state,
          confirmationState: action.summary,
          errorMessage: ''
        };
      case 'hide-confirmation':
        return {
          ...state,
          confirmationState: null
        };
      case 'trash-complete':
        return {
          ...state,
          groups: action.groups,
          selectedFileIds: [],
          confirmationState: null,
          lastOperation: {
            count: action.count,
            totalBytes: action.totalBytes
          },
          infoMessage: `Moved ${action.count} files to Trash. You can undo the last action.`
        };
      case 'undo-complete':
        return {
          ...state,
          lastOperation: null,
          infoMessage: 'Last trash action restored successfully.'
        };
      case 'set-error':
        return {
          ...state,
          errorMessage: action.message
        };
      case 'show-more-groups':
        return {
          ...state,
          visibleGroupCount: Math.min(state.groups.length, state.visibleGroupCount + GROUP_PAGE_SIZE)
        };
      default:
        return state;
    }
  }

  async function enrichGroups(groups) {
    const enrichedGroups = [];

    for (let groupIndex = 0; groupIndex < groups.length; groupIndex += 1) {
      const group = groups[groupIndex];
      const files = [];

      for (let fileIndex = 0; fileIndex < (group.files || []).length; fileIndex += 1) {
        const filePath = group.files[fileIndex];
        const info = await window.desktopApi.getFileInfo(filePath);

        files.push({
          id: `${groupIndex}-${fileIndex}-${filePath}`,
          path: filePath,
          name: basename(filePath),
          size: info && info.ok ? Number(info.size || 0) : 0,
          type: normalizeType(group.type),
          previewType: normalizeType(group.type),
          previewUrl: null,
          meta: group.type ? group.type.toUpperCase() : 'FILE'
        });
      }

      let recommendedIndex = 0;
      let largestSize = -1;
      files.forEach((file, index) => {
        if (file.size > largestSize) {
          largestSize = file.size;
          recommendedIndex = index;
        }
      });

      enrichedGroups.push({
        id: `group-${groupIndex}`,
        title: groupTitleFromType(normalizeType(group.type)),
        type: normalizeType(group.type),
        files: files.map((file, index) => ({
          ...file,
          recommended: index === recommendedIndex
        }))
      });
    }

    return enrichedGroups;
  }

  function useLazyPreview(file) {
    const [previewUrl, setPreviewUrl] = useState(file.previewUrl || null);
    const [isVisible, setIsVisible] = useState(false);
    const hostRef = useRef(null);

    useEffect(() => {
      setPreviewUrl(file.previewUrl || null);
    }, [file.previewUrl, file.path]);

    useEffect(() => {
      const node = hostRef.current;
      if (!node || previewUrl) {
        return undefined;
      }

      const observer = new IntersectionObserver(
        (entries) => {
          if (entries.some((entry) => entry.isIntersecting)) {
            setIsVisible(true);
            observer.disconnect();
          }
        },
        {
          rootMargin: '240px 0px'
        }
      );

      observer.observe(node);
      return () => observer.disconnect();
    }, [previewUrl]);

    useEffect(() => {
      let active = true;

      if (!isVisible || previewUrl) {
        return undefined;
      }

      window.desktopApi.getThumbnail(file.path, 104, 104)
        .then((url) => {
          if (active && url) {
            setPreviewUrl(url);
          }
        })
        .catch(() => {});

      return () => {
        active = false;
      };
    }, [file.path, isVisible, previewUrl]);

    return { hostRef, previewUrl };
  }

  /* ═══ Design System Constants ═══ */

  var _b = 'inline-flex items-center justify-center rounded-lg text-sm font-medium transition-colors duration-150 cursor-pointer whitespace-nowrap disabled:opacity-40 disabled:cursor-not-allowed';
  var B_P = _b + ' bg-accent hover:bg-accent-light text-white px-4 py-2';
  var B_S = _b + ' bg-surface-2 hover:bg-surface-3 text-txt border border-edge-2 px-4 py-2';
  var B_G = _b + ' hover:bg-surface-2 text-txt-2 px-3 py-2';
  var B_D = _b + ' bg-err-dim text-err border border-err-border hover:bg-[rgba(239,111,118,0.16)] px-4 py-2';
  var CARD = 'bg-surface rounded-xl border border-edge';
  var LABEL = 'text-[11px] uppercase tracking-widest text-txt-3 font-medium';

  function statusBadge(status) {
    var base = 'inline-flex items-center h-6 px-2.5 rounded-md text-[11px] font-semibold uppercase tracking-wide';
    var colors = {
      idle: 'bg-surface-2 text-txt-3',
      starting: 'bg-surface-2 text-txt-3',
      running: 'bg-accent-dim text-accent-light',
      queued: 'bg-accent-dim text-accent-light',
      cancelling: 'bg-accent-dim text-accent-light',
      done: 'bg-ok-dim text-ok',
      cancelled: 'bg-err-dim text-err',
      failed: 'bg-err-dim text-err'
    };
    return base + ' ' + (colors[status] || colors.idle);
  }

  /* ═══ Components ═══ */

  function LogoMark() {
    return createElement(
      'div',
      { className: 'w-8 h-8 rounded-lg bg-gradient-to-br from-accent to-accent-light flex items-center justify-center shadow-[0_0_20px_rgba(124,110,246,0.3)]', 'aria-hidden': 'true' },
      createElement('div', { className: 'w-3 h-3 rounded-[3px] bg-white/90 shadow-sm' })
    );
  }

  function Sidebar(props) {
    const { selectedFolders, recentFolders, onSelectFolder, onOpenRecent, groups, selectedCount } = props;

    return createElement(
      'aside',
      { className: 'flex flex-col bg-surface border-r border-edge overflow-y-auto' },
      // Brand
      createElement(
        'div',
        { className: 'flex items-center gap-3 px-5 pt-5 pb-4' },
        createElement(LogoMark),
        createElement(
          'div',
          null,
          createElement('div', { className: 'text-[13px] font-semibold text-txt' }, 'Duplicate Scanner'),
          createElement('div', { className: 'text-[11px] text-txt-3 mt-0.5' }, 'Offline workspace')
        )
      ),
      createElement('div', { className: 'h-px bg-edge mx-4' }),
      // Workspace
      createElement(
        'div',
        { className: 'px-4 py-4' },
        createElement('div', { className: LABEL + ' mb-3' }, 'Workspace'),
        createElement(
          'div',
          { className: 'rounded-lg bg-surface-2 border border-edge p-3 flex flex-col gap-2' },
          createElement('div', { className: 'text-[12px] text-txt-3' }, selectedFolders.length > 0 ? `Active folders (${selectedFolders.length})` : 'No folder selected'),
          selectedFolders.length > 0 
            ? selectedFolders.slice(0, 3).map(f => createElement('div', { key: f, className: 'text-[13px] text-txt-2 break-words leading-relaxed' }, basename(f)))
            : createElement('div', { className: 'text-[13px] text-txt-2 mt-1 break-words leading-relaxed' }, 'Select folders to begin scanning.'),
          selectedFolders.length > 3 ? createElement('div', { className: 'text-[12px] text-txt-3' }, `+ ${selectedFolders.length - 3} more`) : null
        )
      ),
      // Recent Folders
      createElement(
        'div',
        { className: 'px-4 pb-4 flex-1 min-h-0' },
        createElement('div', { className: LABEL + ' mb-3' }, 'Recent'),
        recentFolders.length > 0
          ? createElement(
              'div',
              { className: 'flex flex-col gap-1' },
              recentFolders.map((folder) =>
                createElement(
                  'button',
                  {
                    key: folder,
                    className: 'flex flex-col gap-0.5 px-3 py-2.5 rounded-lg text-left cursor-pointer transition-colors duration-100 hover:bg-surface-2 border-0 bg-transparent',
                    onClick: () => onOpenRecent(folder)
                  },
                  createElement('div', { className: 'text-[13px] font-medium text-txt truncate' }, basename(folder)),
                  createElement('div', { className: 'text-[11px] text-txt-3 truncate' }, folder)
                )
              )
            )
          : createElement('div', { className: 'text-[12px] text-txt-3 leading-relaxed' }, 'Scanned folders will appear here.')
      ),
      createElement('div', { className: 'h-px bg-edge mx-4' }),
      // Stats
      createElement(
        'div',
        { className: 'px-4 py-4 grid grid-cols-2 gap-3' },
        createElement(
          'div',
          { className: 'rounded-lg bg-surface-2 border border-edge p-3 text-center' },
          createElement('div', { className: 'text-xl font-bold text-txt' }, groups.length),
          createElement('div', { className: 'text-[11px] text-txt-3 mt-1' }, 'Groups')
        ),
        createElement(
          'div',
          { className: 'rounded-lg bg-surface-2 border border-edge p-3 text-center' },
          createElement('div', { className: 'text-xl font-bold text-txt' }, selectedCount),
          createElement('div', { className: 'text-[11px] text-txt-3 mt-1' }, 'Selected')
        )
      )
    );
  }

  function TopBar(props) {
    const { status, progress, filesScanned, selectedFolders, onStartScan, onCancelScan, onSelectFolder, onClearFolders } = props;
    const isScanning = status === 'running';

    return createElement(
      'header',
      { className: 'flex flex-col md:flex-row md:items-center justify-between gap-3 px-6 py-4 border-b border-edge bg-surface/50' },
      createElement(
        'div',
        { className: 'flex items-center gap-3 min-w-0' },
        createElement('h1', { className: 'text-base font-semibold text-txt truncate' }, selectedFolders.length > 0 ? (selectedFolders.length === 1 ? basename(selectedFolders[0]) : `${selectedFolders.length} Folders Selected`) : 'Duplicate Scanner'),
        createElement('span', { className: statusBadge(status) }, status),
        createElement('span', { className: 'text-[12px] text-txt-3 hidden md:inline' }, isScanning ? `${progress}% \u00B7 Discovering...` : `${progress}% \u00B7 ${filesScanned} files`)
      ),
      createElement(
        'div',
        { className: 'flex items-center gap-2' },
        createElement(
          'button',
          { className: B_S, onClick: onSelectFolder },
          selectedFolders.length > 0 ? 'Add Folder' : 'Select Folder'
        ),
        selectedFolders.length > 0 ? createElement(
          'button',
          { className: B_G, onClick: onClearFolders },
          'Clear All'
        ) : null,
        createElement(
          'button',
          { className: B_G, onClick: onCancelScan, disabled: !isScanning },
          'Cancel'
        ),
        createElement(
          'button',
          { className: B_P, onClick: onStartScan, disabled: selectedFolders.length === 0 || isScanning },
          isScanning ? 'Scanning\u2026' : 'Start Scan'
        )
      )
    );
  }

  function FolderPicker(props) {
    const { selectedFolders, onSelectFolder, onStartScan, recentFolders, onOpenRecent, onRemoveFolder } = props;

    return createElement(
      'section',
      { className: CARD + ' p-6' },
      createElement('h2', { className: 'text-2xl font-bold text-txt tracking-tight' }, 'Review duplicates before you act'),
      createElement('p', { className: 'text-sm text-txt-2 mt-2 leading-relaxed max-w-xl' }, 'Pick local folders, scan them offline, then decide what to keep or move to Trash. Nothing is deleted automatically.'),
      createElement(
        'div',
        { className: 'mt-5 rounded-lg bg-surface-2 border border-edge flex flex-col overflow-hidden' },
        createElement('div', { className: 'px-4 py-2 border-b border-edge bg-surface-3/50 text-[11px] text-txt-3 uppercase tracking-wide font-medium' }, selectedFolders.length > 0 ? 'Selected Paths' : 'Selected Path'),
        createElement('div', { className: 'p-4 max-h-[160px] overflow-y-auto flex flex-col gap-2' }, 
          selectedFolders.length > 0
            ? selectedFolders.map((f) => 
                createElement('div', { key: f, className: 'flex items-center justify-between gap-3 group' },
                  createElement('div', { className: 'text-sm text-txt-2 break-words font-mono truncate', title: f }, f),
                  createElement('button', { 
                    className: 'opacity-0 group-hover:opacity-100 p-1 text-txt-3 hover:text-err transition-opacity duration-150 rounded',
                    onClick: () => onRemoveFolder(f),
                    title: 'Remove'
                  }, '\u2715')
                )
              )
            : createElement('div', { className: 'text-sm text-txt-2' }, 'No folders selected')
        )
      ),
      recentFolders.length > 0
        ? createElement(
            'div',
            { className: 'mt-5' },
            createElement('div', { className: LABEL + ' mb-2' }, 'Recent folders'),
            createElement(
              'div',
              { className: 'flex flex-wrap gap-2' },
              recentFolders.map((folder) =>
                createElement(
                  'button',
                  {
                    key: folder,
                    className: 'text-[12px] text-txt-2 border border-edge-2 bg-surface-2 hover:bg-surface-3 rounded-md px-3 py-1.5 cursor-pointer transition-colors duration-100',
                    onClick: () => onOpenRecent(folder)
                  },
                  basename(folder)
                )
              )
            )
          )
        : null
    );
  }

  function ScanProgress(props) {
    const { status, progress, filesScanned, onCancel } = props;
    const animated = status === 'running';

    return createElement(
      'section',
      { className: CARD + ' p-6' },
      createElement(
        'div',
        { className: 'flex items-start justify-between gap-4' },
        createElement(
          'div',
          null,
          createElement('div', { className: LABEL }, 'Scan Progress'),
          createElement('div', { className: 'text-lg font-semibold text-txt mt-1.5' }, animated ? 'Scanning your files…' : 'Ready when you are')
        ),
        animated ? createElement('button', { className: B_G + ' text-[12px]', onClick: onCancel }, 'Cancel') : null
      ),
      createElement(
        'div',
        { className: 'mt-4 h-1.5 rounded-full bg-surface-2 overflow-hidden' },
        createElement('div', {
          className: 'relative h-full rounded-full bg-gradient-to-r from-accent to-accent-light transition-[width] duration-300 ease-out' + (animated ? ' progress-shimmer' : ''),
          style: { width: `${Math.max(0, Math.min(progress, 100))}%` }
        })
      ),
      createElement(
        'div',
        { className: 'flex items-baseline justify-between mt-3' },
        createElement('span', { className: 'text-2xl font-bold text-txt tabular-nums' }, `${progress}%`),
        createElement('span', { className: 'text-[12px] text-txt-3' }, animated ? 'Discovering files...' : `${filesScanned} files scanned`)
      )
    );
  }

  function EmptyState(props) {
    const { title, copy, actionLabel, onAction } = props;
    return createElement(
      'section',
      { className: CARD + ' p-10 text-center' },
      createElement('div', { className: 'w-14 h-14 mx-auto mb-5 rounded-2xl bg-accent-dim border border-accent-border flex items-center justify-center' },
        createElement('div', { className: 'w-5 h-5 rounded-md bg-accent/40' })
      ),
      createElement('h2', { className: 'text-lg font-semibold text-txt' }, title),
      createElement('p', { className: 'text-sm text-txt-2 mt-2 max-w-sm mx-auto leading-relaxed' }, copy),
      actionLabel
        ? createElement('button', { className: B_S + ' mt-5', onClick: onAction }, actionLabel)
        : null
    );
  }

  function ErrorBanner(props) {
    const { message } = props;
    return createElement(
      'div',
      { className: 'rounded-xl bg-err-dim border border-err-border p-4 flex gap-3 items-start' },
      createElement('div', { className: 'w-6 h-6 shrink-0 rounded-full bg-err/20 text-err flex items-center justify-center text-xs font-bold' }, '!'),
      createElement(
        'div',
        { className: 'min-w-0' },
        createElement('div', { className: 'text-sm font-semibold text-err' }, 'Something needs attention'),
        createElement('div', { className: 'text-[13px] text-txt-2 mt-0.5 leading-relaxed' }, message)
      )
    );
  }

  function ConfirmationModal(props) {
    const { summary, onCancel, onConfirm } = props;
    if (!summary) {
      return null;
    }

    return createElement(
      'div',
      { className: 'fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center z-50 p-6' },
      createElement(
        'div',
        { className: 'w-full max-w-lg max-h-[80vh] overflow-auto bg-surface rounded-2xl border border-edge-2 shadow-2xl p-6' },
        createElement('h2', { className: 'text-lg font-semibold text-txt' }, 'Move to Trash?'),
        createElement('p', { className: 'text-sm text-txt-2 mt-2 leading-relaxed' }, `${summary.count} files (${summary.sizeLabel}) will be moved to Trash. You can undo this action.`),
        createElement(
          'div',
          { className: 'mt-4 rounded-lg bg-surface-2 border border-edge p-3 max-h-40 overflow-auto flex flex-col gap-1.5' },
          summary.files.map((filePath) =>
            createElement('div', { key: filePath, className: 'text-[12px] text-txt-3 break-words font-mono' }, filePath)
          )
        ),
        createElement(
          'div',
          { className: 'flex items-center justify-end gap-2 mt-5' },
          createElement('button', { className: B_S, onClick: onCancel }, 'Cancel'),
          createElement('button', { className: B_D, onClick: onConfirm }, 'Move to Trash')
        )
      )
    );
  }

  function ActionBar(props) {
    const {
      selectedCount,
      selectedBytes,
      onSelectRecommended,
      onKeepSelected,
      onMoveToTrash,
      onUndo,
      canUndo
    } = props;

    return createElement(
      'section',
      { className: CARD + ' px-5 py-4 flex flex-col md:flex-row md:items-center justify-between gap-4' },
      createElement(
        'div',
        { className: 'flex items-baseline gap-3' },
        createElement('span', { className: 'text-sm font-semibold text-txt' }, `${selectedCount} selected`),
        createElement('span', { className: 'text-[12px] text-txt-3' }, formatBytes(selectedBytes))
      ),
      createElement(
        'div',
        { className: 'flex items-center gap-2 flex-wrap' },
        createElement('button', { className: B_G + ' text-[13px]', onClick: onSelectRecommended }, 'Auto-select'),
        createElement('button', { className: B_S + ' text-[13px]', onClick: onKeepSelected, disabled: selectedCount === 0 }, 'Keep Selected'),
        createElement('button', { className: B_D + ' text-[13px]', onClick: onMoveToTrash, disabled: selectedCount === 0 }, 'Trash'),
        createElement('button', { className: B_G + ' text-[13px]', onClick: onUndo, disabled: !canUndo }, 'Undo')
      )
    );
  }

  /* ═══ App ═══ */

  function App() {
    const [state, dispatch] = useReducer(reducer, initialState);
    const [isCancelling, setIsCancelling] = useState(false);
    const scanMetricsRef = useRef({
      scanStartedAt: 0,
      filesScanned: 0
    });

    useEffect(() => {
      storeRecentFolders(state.recentFolders);
    }, [state.recentFolders]);

    const selectedIdSet = useMemo(() => new Set(state.selectedFileIds), [state.selectedFileIds]);
    const visibleGroups = useMemo(
      () => state.groups.slice(0, state.visibleGroupCount),
      [state.groups, state.visibleGroupCount]
    );
    const selectedFiles = useMemo(
      () => state.groups.flatMap((group) => group.files).filter((file) => selectedIdSet.has(file.id)),
      [state.groups, selectedIdSet]
    );

    const selectedBytes = useMemo(
      () => selectedFiles.reduce((sum, file) => sum + Number(file.size || 0), 0),
      [selectedFiles]
    );

    async function selectFolder() {
      const folders = await window.desktopApi.selectFolder();
      if (!folders || folders.length === 0) {
        return;
      }

      let nextRecent = state.recentFolders;
      folders.forEach((f) => {
        nextRecent = pushRecentFolder(nextRecent, f);
      });

      dispatch({
        type: 'select-folder',
        folders,
        recentFolders: nextRecent
      });
    }

    function openRecentFolder(folder) {
      dispatch({
        type: 'select-folder',
        folders: [folder],
        recentFolders: pushRecentFolder(state.recentFolders, folder)
      });
    }

    function removeFolder(folder) {
      dispatch({
        type: 'remove-folder',
        folder
      });
    }

    function clearFolders() {
      dispatch({
        type: 'clear-folders'
      });
    }

    async function startScan() {
      if (state.selectedFolders.length === 0) {
        dispatch({ type: 'set-error', message: 'Please select a folder before starting a scan.' });
        return;
      }

      dispatch({ type: 'scan-start' });
      scanMetricsRef.current.scanStartedAt = Date.now();
      scanMetricsRef.current.filesScanned = 0;

      let polling = true;
      let timer = null;

      function stopPolling() {
        polling = false;
        if (timer) {
          window.clearTimeout(timer);
          timer = null;
        }
      }

      function pollProgress() {
        if (!polling) {
          return;
        }

        try {
          const currentProgress = Number(window.desktopApi.getProgress() || 0);
          dispatch({
            type: 'scan-progress',
            progress: currentProgress,
            filesScanned: scanMetricsRef.current.filesScanned
          });

          if (currentProgress < 100 && polling) {
            timer = window.setTimeout(pollProgress, POLL_INTERVAL_MS);
          }
        } catch (error) {
          dispatch({ type: 'set-error', message: error.message });
        }
      }

      try {
        const scanPromise = window.desktopApi.scan(state.selectedFolders);
        timer = window.setTimeout(pollProgress, POLL_INTERVAL_MS);
        const resultGroups = await scanPromise;
        stopPolling();

        const enrichedGroups = await enrichGroups(resultGroups || []);
        const fileCount = enrichedGroups.reduce((sum, group) => sum + group.files.length, 0);
        scanMetricsRef.current.filesScanned = fileCount;

        dispatch({
          type: 'scan-complete',
          groups: enrichedGroups,
          filesScanned: fileCount
        });
      } catch (error) {
        stopPolling();
        if (String(error.message || '').toLowerCase().includes('cancel')) {
          dispatch({ type: 'scan-cancelled' });
        } else {
          dispatch({ type: 'scan-failed', message: error.message || 'Unable to complete scan.' });
        }
      } finally {
        setIsCancelling(false);
      }
    }

    async function cancelScan() {
      setIsCancelling(true);
      try {
        await window.desktopApi.cancelScan();
      } catch (error) {
        dispatch({ type: 'set-error', message: error.message || 'Unable to cancel the running scan.' });
      }
    }

    function toggleFile(fileId) {
      dispatch({ type: 'toggle-file', fileId });
    }

    function selectRecommended() {
      const recommendedIds = state.groups.flatMap((group) =>
        group.files.filter((file) => !file.recommended).map((file) => file.id)
      );
      dispatch({ type: 'replace-selection', fileIds: recommendedIds });
    }

    function keepSelected() {
      const selectedIds = new Set(state.selectedFileIds);
      const trimmedGroups = state.groups
        .map((group) => ({
          ...group,
          files: group.files.filter((file) => !selectedIds.has(file.id))
        }))
        .filter((group) => group.files.length > 1);

      dispatch({
        type: 'scan-complete',
        groups: trimmedGroups,
        filesScanned: trimmedGroups.reduce((sum, group) => sum + group.files.length, 0)
      });
    }

    function requestMoveToTrash() {
      if (selectedFiles.length === 0) {
        dispatch({ type: 'set-error', message: 'Select at least one file before moving to Trash.' });
        return;
      }

      dispatch({
        type: 'show-confirmation',
        summary: {
          files: selectedFiles.map((file) => file.path),
          count: selectedFiles.length,
          totalBytes: selectedBytes,
          sizeLabel: formatBytes(selectedBytes)
        }
      });
    }

    async function confirmMoveToTrash() {
      const summary = state.confirmationState;
      if (!summary) {
        return;
      }

      const result = await window.desktopApi.moveFilesToTrash(summary.files);
      if (!result.ok) {
        dispatch({ type: 'set-error', message: result.error || 'Unable to move files to Trash.' });
        return;
      }

      const removedPaths = new Set(summary.files);
      const groups = state.groups
        .map((group) => ({
          ...group,
          files: group.files.filter((file) => !removedPaths.has(file.path))
        }))
        .filter((group) => group.files.length > 1);

      dispatch({
        type: 'trash-complete',
        groups,
        count: result.movedCount,
        totalBytes: result.totalBytes
      });
    }

    async function undoLastOperation() {
      const result = await window.desktopApi.undoLastTrash();
      if (!result.ok) {
        dispatch({ type: 'set-error', message: result.error || 'Unable to undo the last operation.' });
        return;
      }

      dispatch({ type: 'undo-complete' });
    }

    async function openFileForReview(filePath) {
      const result = await window.desktopApi.openFile(filePath);
      if (!result.ok) {
        dispatch({ type: 'set-error', message: result.error || 'Unable to open the selected file.' });
      }
    }

    const shouldShowEmpty = state.status === 'done' && state.groups.length === 0;
    const shouldShowResults = state.groups.length > 0;

    return createElement(
      'div',
      { className: 'grid grid-cols-1 xl:grid-cols-[280px_1fr] w-full h-full min-h-full' },
      createElement(Sidebar, {
        selectedFolders: state.selectedFolders,
        recentFolders: state.recentFolders,
        onSelectFolder: selectFolder,
        onOpenRecent: openRecentFolder,
        groups: state.groups,
        selectedCount: state.selectedFileIds.length
      }),
      createElement(
        'main',
        { className: 'min-w-0 min-h-0 flex flex-col overflow-hidden' },
        createElement(TopBar, {
          status: isCancelling ? 'cancelling' : state.status,
          progress: state.progress,
          filesScanned: state.filesScanned,
          selectedFolders: state.selectedFolders,
          onStartScan: startScan,
          onCancelScan: cancelScan,
          onSelectFolder: selectFolder,
          onClearFolders: clearFolders
        }),
        createElement(
          'section',
          { className: 'flex-1 min-h-0 grid grid-cols-1 xl:grid-cols-[1fr_280px] overflow-hidden' },
          createElement(
            'div',
            { className: 'min-h-0 overflow-y-auto p-6 flex flex-col gap-5' },
            createElement(FolderPicker, {
              selectedFolders: state.selectedFolders,
              onSelectFolder: selectFolder,
              onStartScan: startScan,
              recentFolders: state.recentFolders,
              onOpenRecent: openRecentFolder,
              onRemoveFolder: removeFolder
            }),
            createElement(ScanProgress, {
              status: isCancelling ? 'running' : state.status,
              progress: state.progress,
              filesScanned: state.filesScanned,
              onCancel: cancelScan
            }),
            state.errorMessage ? createElement(ErrorBanner, { message: state.errorMessage }) : null,
            shouldShowResults
              ? createElement(ActionBar, {
                  selectedCount: state.selectedFileIds.length,
                  selectedBytes,
                  onSelectRecommended: selectRecommended,
                  onKeepSelected: keepSelected,
                  onMoveToTrash: requestMoveToTrash,
                  onUndo: undoLastOperation,
                  canUndo: Boolean(state.lastOperation)
                })
              : null,
            shouldShowResults
              ? createElement(window.DuplicateGroupsView, {
                  groups: visibleGroups,
                  totalGroups: state.groups.length,
                  selectedFileIds: selectedIdSet,
                  onToggleFile: toggleFile,
                  onOpenFile: openFileForReview,
                  onLoadMore: () => dispatch({ type: 'show-more-groups' })
                })
              : null,
            shouldShowEmpty
              ? createElement(EmptyState, {
                  title: 'No duplicates found',
                  copy: 'This folder looks clean. Try another location or scan a larger media collection.',
                  actionLabel: 'Choose Another Folder',
                  onAction: selectFolder
                })
              : null,
            state.status === 'idle' && state.selectedFolders.length === 0
              ? createElement(EmptyState, {
                  title: 'Start with a folder',
                  copy: 'Pick folders from your device to begin an offline scan for duplicates and visually similar files.',
                  actionLabel: 'Select Folders',
                  onAction: selectFolder
                })
              : null
          ),
          createElement(
            'aside',
            { className: 'min-h-0 overflow-y-auto border-l border-edge p-5 flex flex-col gap-4 hidden xl:flex' },
            createElement(
              'section',
              { className: 'rounded-xl bg-surface-2 border border-edge p-5' },
              createElement('div', { className: LABEL }, 'Workflow'),
              createElement('h3', { className: 'text-sm font-semibold text-txt mt-2' }, 'Folder → Scan → Review → Act'),
              createElement('p', { className: 'text-[13px] text-txt-2 mt-2 leading-relaxed' }, state.infoMessage),
              createElement(
                'div',
                { className: 'flex flex-col gap-1 mt-4' },
                ['Select folder', 'Run local scan', 'Review recommendations', 'Move to Trash or keep'].map((step, index) =>
                  createElement(
                    'div',
                    { key: step, className: 'flex items-center gap-3 py-2 border-b border-edge last:border-b-0' },
                    createElement('span', { className: 'w-6 h-6 rounded-md bg-surface-3 text-txt-3 flex items-center justify-center text-[11px] font-bold shrink-0' }, String(index + 1)),
                    createElement('span', { className: 'text-[13px] text-txt-2' }, step)
                  )
                )
              )
            ),
            state.lastOperation
              ? createElement(
                  'section',
                  { className: 'rounded-xl bg-ok-dim border border-[rgba(62,207,142,0.15)] p-5' },
                  createElement('div', { className: LABEL }, 'Undo Available'),
                  createElement('div', { className: 'text-sm font-semibold text-ok mt-2' }, `${state.lastOperation.count} files moved`),
                  createElement('div', { className: 'text-[12px] text-txt-3 mt-1' }, `Size: ${formatBytes(state.lastOperation.totalBytes)}`)
                )
              : null
          )
        ),
        createElement(ConfirmationModal, {
          summary: state.confirmationState,
          onCancel: () => dispatch({ type: 'hide-confirmation' }),
          onConfirm: confirmMoveToTrash
        })
      )
    );
  }

  const root = ReactDOM.createRoot(document.getElementById('root'));
  root.render(createElement(App));
})();
