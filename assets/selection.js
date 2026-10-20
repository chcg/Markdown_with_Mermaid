/* Rendered selection snapshots and formatted clipboard, without network requests. */
(() => {
  'use strict';
  window.mwmSelection = (body, bridge, ready) => {
    const copyButton = document.getElementById('copy');
    const status = document.getElementById('copy-status');
    const properties = ['color','background-color','font-family','font-size','font-weight','font-style','text-decoration',
      'line-height','text-align','vertical-align','white-space','border-top','border-right','border-bottom','border-left',
      'border-collapse','padding-top','padding-right','padding-bottom','padding-left','margin-top','margin-bottom','list-style-type'];
    let resourceId = 0, busy = false, editorActive = false;
    function setEditorSelectionActive(value) { editorActive = !!value; }
    function previewActive() { if (editorActive) { editorActive = false; bridge('preview-active'); } }
    const resources = new Map();
    function resourceResult(id, data, pending) {
      const entry = resources.get(id); if (!entry) return;
      if (pending) setTimeout(() => { if (resources.has(id)) entry.retry(); }, 50);
      else { resources.delete(id); entry.done(data); }
    }
    function resource(url, scope) {
      return new Promise(resolve => {
        const id = resourceId++, timer = setTimeout(() => { resources.delete(id); resolve(''); }, 5000);
        const retry = () => bridge('clipboard-resource:' + scope + ':' + id + ':' + url);
        resources.set(id, { done: data => { clearTimeout(timer); resolve(data); }, retry }); retry();
      });
    }
    function ranges() {
      const selection = window.getSelection(), result = [];
      for (let i = 0; i < (selection?.rangeCount || 0); i++) {
        const range = selection.getRangeAt(i);
        if (range.collapsed || !range.intersectsNode(body)) continue;
        const clipped = range.cloneRange(), bounds = document.createRange(); bounds.selectNodeContents(body);
        if (clipped.compareBoundaryPoints(Range.START_TO_START, bounds) < 0) clipped.setStart(body, 0);
        if (clipped.compareBoundaryPoints(Range.END_TO_END, bounds) > 0) clipped.setEnd(body, body.childNodes.length);
        if (!clipped.collapsed) result.push(clipped);
      }
      if (!result.length && selection?.toString()) {
        const diagram = selection.anchorNode?.getRootNode()?.host?.closest('.mermaid-diagram');
        if (diagram && body.contains(diagram)) { const range = document.createRange(); range.selectNode(diagram); result.push(range); }
      }
      return result;
    }
    function nativeTextSelection() {
      // The filename/header belongs to the browser UI, outside the document
      // snapshot. Its selection must not turn into "copy whole document".
      return !!window.getSelection()?.toString() && !ranges().length;
    }
    const filename = document.getElementById('filename');
    const filenameButton = document.getElementById('filename-copy');
    let filenameFeedbackTimer, pendingFilename = '';
    function resetFilenameFeedback() {
      clearTimeout(filenameFeedbackTimer); pendingFilename = '';
      delete filenameButton.dataset.state;
      filenameButton.title = 'Copy filename as plain text';
      filenameButton.setAttribute('aria-label', filenameButton.title);
    }
    function filenameSelection() {
      const selection = window.getSelection(), parts = [];
      if (ranges().length && !filename.contains(selection?.anchorNode) && !filename.contains(selection?.focusNode)) return '';
      const bounds = document.createRange(); bounds.selectNodeContents(filename);
      for (let i = 0; i < (selection?.rangeCount || 0); ++i) {
        const range = selection.getRangeAt(i);
        if (range.collapsed || !range.intersectsNode(filename)) continue;
        // A browser line selection can extend beyond the span. Copy only
        // the filename, never an adjacent newline, toolbar or document text.
        const clipped = range.cloneRange();
        if (clipped.compareBoundaryPoints(Range.START_TO_START, bounds) < 0) clipped.setStart(filename, 0);
        if (clipped.compareBoundaryPoints(Range.END_TO_END, bounds) > 0) clipped.setEnd(filename, filename.childNodes.length);
        if (!clipped.collapsed) parts.push(clipped.toString());
      }
      return parts.join('');
    }
    function copyFilename(text = filenameSelection()) {
      if (!text) return false;
      if (!busy) {
        busy = true; copyButton.disabled = filenameButton.disabled = true; status.textContent = 'Copying…';
        bridge('clipboard-text:' + text);
      }
      return true;
    }
    filenameButton.addEventListener('click', () => {
      if (busy || !filename.textContent) return;
      resetFilenameFeedback(); pendingFilename = filename.textContent;
      copyFilename(pendingFilename);
    });
    filename.addEventListener('click', event => {
      if (event.detail < 3) return;
      const range = document.createRange(); range.selectNodeContents(filename);
      window.getSelection().removeAllRanges(); window.getSelection().addRange(range);
    });
    const filenameMenu = document.createElement('div');
    filenameMenu.id = 'filename-menu'; filenameMenu.setAttribute('role', 'menu'); filenameMenu.hidden = true;
    const filenameCopy = document.createElement('button');
    filenameCopy.type = 'button'; filenameCopy.textContent = 'Copy'; filenameCopy.setAttribute('role', 'menuitem');
    filenameCopy.title = 'Copy filename as plain text'; filenameMenu.append(filenameCopy); document.body.append(filenameMenu);
    let menuText = '';
    const closeFilenameMenu = () => {
      const wasOpen = !filenameMenu.hidden;
      filenameMenu.hidden = true; menuText = ''; return wasOpen;
    };
    filename.addEventListener('contextmenu', event => {
      event.preventDefault();
      menuText = filenameSelection() || filename.textContent;
      filenameMenu.hidden = false;
      filenameMenu.style.left = Math.max(0, Math.min(event.clientX, window.innerWidth - filenameMenu.offsetWidth)) + 'px';
      filenameMenu.style.top = Math.max(0, Math.min(event.clientY, window.innerHeight - filenameMenu.offsetHeight)) + 'px';
      filenameCopy.focus({ preventScroll: true });
    });
    filenameCopy.addEventListener('mousedown', event => event.preventDefault());
    filenameCopy.addEventListener('click', () => { copyFilename(menuText); closeFilenameMenu(); });
    document.addEventListener('pointerdown', event => { if (!filenameMenu.contains(event.target)) closeFilenameMenu(); });
    window.addEventListener('blur', closeFilenameMenu);
    function snapshot(selected = ranges(), styled = false) {
      const selectedNode = node => !selected.length || selected.some(range => range.intersectsNode(node));
      function clone(node, whole = false) {
        if (!whole && !selectedNode(node)) return null;
        if (node.nodeType === Node.TEXT_NODE) {
          if (whole || !selected.length) return node.cloneNode();
          const parts = selected.filter(r => r.intersectsNode(node)).map(r => node.data.slice(r.startContainer === node ? r.startOffset : 0, r.endContainer === node ? r.endOffset : node.length));
          return document.createTextNode(parts.join(''));
        }
        if (node.nodeType !== Node.ELEMENT_NODE || node.matches('.diagram-tools,.mermaid-status,.link-type-icon,script,style')) return null;
        if (node.matches('.mermaid-source') && !node.closest('[data-render-state="error"]')) return null;
        const result = node.cloneNode(false);
        if (node.tagName === 'INPUT' && node.type === 'checkbox') result.toggleAttribute('checked', node.checked);
        result.classList.remove('expanded'); result.removeAttribute('data-view-zoom');
        if (node === body) { result.removeAttribute('style'); }
        else if (styled) {
          const css = getComputedStyle(node);
          for (const property of properties) result.style.setProperty(property, css.getPropertyValue(property));
        }
        if (node.tagName === 'A' && !node.getAttribute('href')?.startsWith('#')) result.href = node.href;
        if (node.tagName === 'OL' && selected.length && !whole) {
          const items = [...node.children].filter(item => item.tagName === 'LI');
          const first = items.findIndex(item => selectedNode(item));
          if (first > 0) result.start = (node.start || 1) + first;
        }
        if (node.tagName === 'IMG') {
          result.src = node.currentSrc || node.src;
          // Use CSS pixels independent of the current Preview zoom.
          const css = getComputedStyle(node);
          result.setAttribute('width', String(parseFloat(css.width) || node.naturalWidth));
          result.setAttribute('height', String(parseFloat(css.height) || node.naturalHeight));
        }
        if (node.matches('.mermaid-graphic')) {
          const svg = node.shadowRoot?.querySelector('svg') || node.querySelector('svg');
          if (svg) result.append(svg.cloneNode(true));
          return result;
        }
        const atomic = whole || node.matches('.mermaid-diagram');
        for (const child of node.childNodes) { const copy = clone(child, atomic); if (copy) result.append(copy); }
        return result;
      }
      return clone(body, !selected.length);
    }
    async function embedImages(fragment, scope) {
      for (const img of fragment.querySelectorAll('img')) {
        if (img.src.startsWith('data:')) continue;
        const data = await resource(img.src, scope);
        if (data) img.src = data; else img.replaceWith(document.createTextNode('[Image unavailable: ' + (img.alt || 'Image') + ']'));
      }
    }
    async function requestExport(kind) {
      const selected = editorActive ? [] : ranges();
      if (!selected.length) { bridge(kind); return; }
      const scope = resourceId++; bridge('clipboard-begin:' + scope);
      try {
        const first = body.firstChild; await ready();
        if (body.firstChild !== first) throw new Error('The document changed. Please export again.');
        const fragment = snapshot(selected);
        if (!fragment.textContent.trim() && !fragment.querySelector('img,.mermaid-graphic svg,hr,input')) throw new Error('The selection has no content to export.');
        await embedImages(fragment, scope);
        const message = kind + '-fragment:' + fragment.innerHTML;
        if (message.length >= 32 * 1024 * 1024) throw new Error('The selection including images exceeds 32 MiB.');
        bridge(message);
      } catch (error) { status.textContent = 'Could not prepare the export: ' + String(error.message || error); status.classList.add('error'); }
      finally { bridge('clipboard-end:' + scope); }
    }
    const svgImage = async svg => {
      const clone = svg.cloneNode(true), box = clone.viewBox.baseVal;
      clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
      clone.setAttribute('width', String(Math.ceil(box.width || 680))); clone.setAttribute('height', String(Math.ceil(box.height || 400)));
      const source = 'data:image/svg+xml;base64,' + btoa([...new TextEncoder().encode(new XMLSerializer().serializeToString(clone))].map(c => String.fromCharCode(c)).join(''));
      const image = new Image();
      await new Promise((resolve, reject) => { image.onload = resolve; image.onerror = reject; image.src = source; });
      const factor = Math.min(1, 1200 / image.naturalWidth, 1600 / image.naturalHeight);
      const canvas = document.createElement('canvas'); canvas.width = Math.max(1, Math.round(image.naturalWidth * factor)); canvas.height = Math.max(1, Math.round(image.naturalHeight * factor));
      canvas.getContext('2d').drawImage(image, 0, 0, canvas.width, canvas.height);
      const result = document.createElement('img'); result.src = canvas.toDataURL('image/png'); result.alt = 'Mermaid diagram';
      const display = Math.min(1, 680 / canvas.width);
      result.width = Math.max(1, Math.round(canvas.width * display)); result.height = Math.max(1, Math.round(canvas.height * display)); result.style.maxWidth = '100%';
      return result;
    };
    async function copy() {
      if (busy) return;
      previewActive();
      busy = true; copyButton.disabled = filenameButton.disabled = true; status.textContent = 'Copying…';
      const scope = resourceId++; bridge('clipboard-begin:' + scope);
      try {
        // Freeze the range before waiting for pending Mermaid work. A changed
        // buffer must never silently substitute another document.
        const selected = ranges(), first = body.firstChild; await ready();
        if (body.firstChild !== first) throw new Error('The document changed. Please copy again.');
        const fragment = snapshot(selected, true);
        for (const svg of fragment.querySelectorAll('.mermaid-graphic svg')) {
          try { svg.replaceWith(await svgImage(svg)); } catch (_) { svg.replaceWith(document.createTextNode('[Mermaid diagram]')); }
        }
        await embedImages(fragment, scope);
        const css = getComputedStyle(body);
        fragment.style.cssText = 'font-family:Segoe UI,sans-serif;font-size:15px;line-height:1.6;';
        fragment.style.color = css.color; fragment.style.backgroundColor = getComputedStyle(document.body).backgroundColor;
        // innerText gives a readable fallback for plain-text paste targets.
        const holder = document.createElement('div'); holder.style.cssText = 'position:absolute;left:-100000px;top:0;';
        holder.append(fragment); document.body.append(holder);
        const text = fragment.innerText; holder.remove();
        const message = 'clipboard:' + text.length + ':' + text + fragment.outerHTML;
        if (message.length >= 64 * 1024 * 1024) throw new Error('The content including images exceeds 64 MiB.');
        bridge(message);
      } catch (error) { copyResult(false, 'Could not copy: ' + String(error.message || error)); }
      finally { bridge('clipboard-end:' + scope); }
    }
    function copyResult(success, message) {
      busy = false; copyButton.disabled = filenameButton.disabled = false;
      status.textContent = message || (success ? 'Copied with formatting' : 'The clipboard is busy. Please copy again.');
      status.classList.toggle('error', !success);
      if (pendingFilename && pendingFilename === filename.textContent) {
        filenameButton.dataset.state = success ? 'copied' : 'error';
        filenameButton.title = success ? 'Filename copied' : 'Copy failed. Try again.';
        filenameButton.setAttribute('aria-label', filenameButton.title);
        if (success) status.textContent = 'Filename copied';
        filenameFeedbackTimer = setTimeout(resetFilenameFeedback, 1400);
      }
      pendingFilename = '';
    }
    for (const id of ['filename-copy', 'copy', 'export', 'export-pdf', 'export-docx']) document.getElementById(id).addEventListener('mousedown', event => event.preventDefault());
    copyButton.addEventListener('click', copy);
    body.addEventListener('pointerdown', previewActive);
    body.addEventListener('keydown', previewActive);
    document.addEventListener('copy', event => {
      if (filenameSelection() && !document.activeElement?.matches('input,textarea,[contenteditable="true"]')) {
        event.preventDefault(); copyFilename(); return;
      }
      if (document.activeElement?.matches('input,textarea,[contenteditable="true"]') || nativeTextSelection()) return;
      event.preventDefault(); copy();
    });
    function copyFocused() {
      if (document.activeElement?.matches('input,textarea,[contenteditable="true"]')) document.execCommand('copy');
      else if (copyFilename()) return;
      else if (nativeTextSelection()) document.execCommand('copy'); else copy();
    }
    return { copy, copyFocused, copyResult, resourceResult, requestExport, snapshot, ranges, closeFilenameMenu, resetFilenameFeedback, setEditorSelectionActive, editorSelectionActive: () => editorActive };
  };
})();
