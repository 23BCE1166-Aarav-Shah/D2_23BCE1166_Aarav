(function registerDuplicateGroupsView() {
  const { createElement, useState } = React;

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

  function fileTypeLabel(type) {
    switch (type) {
      case 'image':
        return 'Images';
      case 'audio':
        return 'Audio Files';
      default:
        return 'Binary Files';
    }
  }

  function iconPalette(type) {
    switch (type) {
      case 'image':
        return 'from-[rgba(124,110,246,0.18)] to-[rgba(62,207,142,0.08)] text-[rgba(206,201,255,0.94)]';
      case 'audio':
        return 'from-[rgba(62,207,142,0.18)] to-[rgba(59,130,246,0.06)] text-[rgba(186,248,217,0.95)]';
      default:
        return 'from-[rgba(148,163,184,0.16)] to-[rgba(71,85,105,0.06)] text-[rgba(226,232,240,0.92)]';
    }
  }

  function FileTypeIcon(props) {
    const { type } = props;
    const svgClass = 'w-11 h-11';

    if (type === 'image') {
      return createElement(
        'svg',
        { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', strokeWidth: '1.7', className: svgClass, 'aria-hidden': 'true' },
        createElement('rect', { x: '3.5', y: '4.5', width: '17', height: '15', rx: '2.5' }),
        createElement('circle', { cx: '9', cy: '10', r: '1.5' }),
        createElement('path', { d: 'M6 17l4-4 2.5 2.5L15 13l3 4' })
      );
    }

    if (type === 'audio') {
      return createElement(
        'svg',
        { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', strokeWidth: '1.7', className: svgClass, 'aria-hidden': 'true' },
        createElement('path', { d: 'M4 14h2l1.5-4 3 8 2.5-6 1.5 2H20' }),
        createElement('path', { d: 'M6 6h12' })
      );
    }

    return createElement(
      'svg',
      { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', strokeWidth: '1.7', className: svgClass, 'aria-hidden': 'true' },
      createElement('path', { d: 'M7.5 3.5h6l4 4v11a2 2 0 0 1-2 2h-8a2 2 0 0 1-2-2v-13a2 2 0 0 1 2-2z' }),
      createElement('path', { d: 'M13.5 3.5v4h4' }),
      createElement('path', { d: 'M8.5 12.5h7' }),
      createElement('path', { d: 'M8.5 15.5h5' })
    );
  }

  function FileCard(props) {
    const { file, checked, onToggle, onOpenFile } = props;
    const [copied, setCopied] = useState(false);

    const handleCopy = (e) => {
      e.preventDefault();
      e.stopPropagation();
      navigator.clipboard.writeText(file.path);
      setCopied(true);
      setTimeout(() => { setCopied(false); }, 1500);
    };

    var border = checked
      ? ' border-accent-border shadow-[0_0_0_1px_rgba(124,110,246,0.15)]'
      : file.recommended
        ? ' border-[rgba(62,207,142,0.25)] shadow-[0_0_0_1px_rgba(62,207,142,0.1)]'
        : ' border-edge';

    return createElement(
      'label',
      {
        className: 'group flex flex-col min-w-0 rounded-xl bg-surface border transition-all duration-150 cursor-pointer hover:bg-surface-2' + border,
      },
      // Preview
      createElement(
        'div',
        { className: 'aspect-[1.2/1] rounded-t-xl overflow-hidden grid place-items-center relative bg-gradient-to-br ' + iconPalette(file.previewType || file.type) },
        createElement(
          'button',
          {
            type: 'button',
            className: 'file-card-open absolute top-2 right-2 inline-flex items-center justify-center w-8 h-8 rounded-lg border border-white/10 bg-black/30 text-white/80 backdrop-blur-sm hover:bg-black/45 hover:text-white transition-all duration-150',
            title: 'Open file',
            onClick: (event) => {
              event.preventDefault();
              event.stopPropagation();
              onOpenFile(file.path);
            }
          },
          createElement(
            'svg',
            { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', strokeWidth: '1.8', className: 'w-4 h-4', 'aria-hidden': 'true' },
            createElement('path', { d: 'M14 5h5v5' }),
            createElement('path', { d: 'M10 14L19 5' }),
            createElement('path', { d: 'M19 13v4a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2h4' })
          )
        ),
        createElement(
          'div',
          { className: 'w-full h-full flex flex-col items-center justify-center gap-3 px-4 text-center' },
          createElement(
            'div',
            { className: 'w-18 h-18 rounded-2xl bg-black/20 border border-white/8 flex items-center justify-center shadow-[inset_0_1px_0_rgba(255,255,255,0.06)]' },
            createElement(FileTypeIcon, { type: file.previewType || file.type })
          ),
          createElement('div', { className: 'text-[11px] uppercase tracking-[0.24em] font-semibold text-current/80' }, fileTypeLabel(file.previewType || file.type))
        )
      ),
      // Info
      createElement(
        'div',
        { className: 'p-3.5 flex flex-col gap-2' },
        createElement(
          'div',
          { className: 'flex items-center justify-between gap-2' },
          createElement('input', {
            type: 'checkbox',
            className: 'w-4 h-4 accent-accent rounded cursor-pointer shrink-0',
            checked,
            onChange: () => onToggle(file.id)
          }),
          file.recommended
            ? createElement('span', { className: 'text-[10px] uppercase tracking-wider font-semibold text-ok bg-ok-dim rounded px-1.5 py-0.5' }, 'Keep')
            : null
        ),
        createElement('div', { className: 'text-[13px] font-medium text-txt truncate' }, file.name || file.path),
        createElement('div', { 
            className: 'text-[11px] leading-[1.4] line-clamp-2 cursor-pointer transition-colors duration-150 ' + (copied ? 'text-ok' : 'text-txt-3 hover:text-txt'),
            title: 'Click to copy path',
            onClick: handleCopy
        }, copied ? 'Copied to clipboard!' : file.path),
        createElement(
          'div',
          { className: 'flex items-center gap-2 text-[11px] text-txt-3' },
          createElement('span', null, formatBytes(Number(file.size || 0))),
          file.meta ? createElement('span', { className: 'bg-surface-2 rounded px-1.5 py-0.5 text-[10px] font-medium' }, file.meta) : null
        )
      )
    );
  }

  function DuplicateGroup(props) {
    const { group, selectedIds, onToggleFile, onOpenFile } = props;
    const totalSize = group.files.reduce((sum, file) => sum + Number(file.size || 0), 0);

    return createElement(
      'section',
      { className: 'bg-surface rounded-xl border border-edge' },
      // Header
      createElement(
        'div',
        { className: 'px-5 pt-5 pb-4 border-b border-edge' },
        createElement(
          'div',
          { className: 'flex items-center gap-2' },
          createElement('h3', { className: 'text-sm font-semibold text-txt' }, group.title || 'Duplicate Group'),
          createElement('span', { className: 'text-[11px] text-txt-3 bg-surface-2 rounded-md px-2 py-0.5 font-medium' }, `${group.files.length} files`),
          createElement('span', { className: 'text-[11px] text-txt-3' }, '·'),
          createElement('span', { className: 'text-[11px] text-txt-3' }, formatBytes(totalSize))
        )
      ),
      // Grid
      createElement(
        'div',
        { className: 'p-4 grid grid-cols-1 sm:grid-cols-2 md:grid-cols-[repeat(auto-fill,minmax(200px,1fr))] gap-3' },
        group.files.map((file) =>
          createElement(FileCard, {
            key: file.id,
            file,
            checked: selectedIds.has(file.id),
            onToggle: onToggleFile,
            onOpenFile
          })
        )
      )
    );
  }

  function DuplicateGroupsView(props) {
    const {
      groups,
      totalGroups,
      selectedFileIds,
      onToggleFile,
      onLoadMore,
      onOpenFile
    } = props;

    const selectedIds = selectedFileIds instanceof Set
      ? selectedFileIds
      : new Set(selectedFileIds || []);

    if (!Array.isArray(groups) || groups.length === 0) {
      return null;
    }

    const hasMore = Number(totalGroups || groups.length) > groups.length;

    return createElement(
      'section',
      { className: 'flex flex-col gap-4' },
      createElement(
        'div',
        { className: 'flex items-end justify-between' },
        createElement('h2', { className: 'text-base font-semibold text-txt' }, 'Duplicate Groups'),
        createElement('span', { className: 'text-[12px] text-txt-3' }, `${totalGroups || groups.length} groups`)
      ),
      createElement(
        'div',
        { className: 'flex flex-col gap-4' },
        groups.map((group) =>
          createElement(DuplicateGroup, {
            key: group.id,
            group,
            selectedIds,
            onToggleFile,
            onOpenFile
          })
        )
      ),
      hasMore
        ? createElement(
            'div',
            { className: 'flex justify-center pt-2' },
            createElement(
              'button',
              { className: 'inline-flex items-center justify-center rounded-lg text-sm font-medium transition-colors duration-150 cursor-pointer bg-surface-2 hover:bg-surface-3 text-txt border border-edge-2 px-5 py-2', onClick: onLoadMore },
              'Load More'
            )
          )
        : null
    );
  }

  window.FileCard = FileCard;
  window.DuplicateGroup = DuplicateGroup;
  window.DuplicateGroupsView = DuplicateGroupsView;
})();
