/* Markdown_with_Mermaid (MIT). Mermaid host adapted from Markdown View.
 * Source maps / slugification adapted from VS Code (MIT). See vendor licenses. */
(() => {
  'use strict';
  const initial = window.markdownViewData;
  const pdfMode = !!initial.pdfExport;
  document.documentElement.classList.toggle('pdf-export', pdfMode && !initial.htmlExport);
  delete window.markdownViewData;
  const bridge = message => window.chrome?.webview?.postMessage(message);
  const body = document.querySelector('.markdown-body');
  const backButton = document.getElementById('history-back'), forwardButton = document.getElementById('history-forward');
  const themeButton = document.getElementById('theme-button'), navigationStatus = document.getElementById('navigation-status');
  const themeLink = document.getElementById('highlight-theme');
  const assetBase = themeLink.href.slice(0, themeLink.href.lastIndexOf('/') + 1);
  const diagrams = [], positions = new Map();
  let current = null, revision = 0, queue = Promise.resolve(), dark = !!initial.dark;
  let scrollTimer, restoring = false;
  const zoomInput = document.getElementById('zoom-value'), zoomSelect = document.getElementById('zoom-presets');
  const zoomSteps = [10, 25, 50, 75, 100, 125, 150, 200, 400, 800, 1600, 2400, 3200, 6400];
  let zoomPercent = 100, zoomMode = 'percent';
  function applyZoom(preservePosition = true) {
    if (pdfMode) return;
    const previousScale = Number(body.style.zoom) || 1, oldTop = window.scrollY, oldLeft = window.scrollX;
    const viewport = document.documentElement.clientWidth, toolbarHeight = document.getElementById('toolbar').offsetHeight;
    let layoutWidth = Math.max(1, Math.min(1044, viewport));
    if (zoomMode === 'content') {
      // Fit wide code, tables and the intrinsic SVG, including diagrams currently scaled down to the panel.
      const padding = parseFloat(getComputedStyle(body).paddingLeft) + parseFloat(getComputedStyle(body).paddingRight);
      for (const item of body.querySelectorAll('pre,table')) layoutWidth = Math.max(layoutWidth, item.scrollWidth + padding);
      for (const item of body.querySelectorAll('.mermaid-graphic')) {
        const svg = item.shadowRoot?.querySelector('svg');
        const width = svg?.viewBox?.baseVal.width;
        if (Number.isFinite(width)) layoutWidth = Math.max(layoutWidth, width + padding + 34);
      }
      layoutWidth = Math.min(layoutWidth, Math.max(1044, viewport * 10));
    }
    if (zoomMode !== 'percent') zoomPercent = Math.max(10, Math.min(6400, Math.round(viewport / layoutWidth * 1000) / 10));
    const scale = zoomPercent / 100;
    body.dataset.viewZoom = String(zoomPercent);
    body.style.width = layoutWidth + 'px'; body.style.zoom = String(scale);
    body.style.marginLeft = body.style.marginRight = layoutWidth * scale <= viewport ? 'auto' : '0px';
    zoomInput.value = zoomPercent + '%';
    zoomSelect.value = zoomMode !== 'percent' ? zoomMode : zoomSteps.includes(zoomPercent) ? String(zoomPercent) : '';
    document.getElementById('zoom-out').disabled = zoomPercent <= 10;
    document.getElementById('zoom-in').disabled = zoomPercent >= 6400;
    if (preservePosition) window.scrollTo(oldLeft * scale / previousScale, Math.max(0, (oldTop - toolbarHeight) * scale / previousScale + toolbarHeight));
  }
  function setZoom(value) {
    if (value === 'width' || value === 'content') zoomMode = value;
    else {
      const match = String(value).match(/^\s*(\d+(?:[.,]\d+)?)\s*%?\s*$/);
      if (value !== 'actual' && !match) { zoomInput.value = zoomPercent + '%'; return; }
      zoomMode = 'percent'; zoomPercent = value === 'actual' ? 100 : Math.max(10, Math.min(6400, Math.round(Number(match[1].replace(',', '.')) * 10) / 10));
    }
    applyZoom();
  }
  function changeZoom(direction) {
    const next = direction > 0 ? zoomSteps.find(value => value > zoomPercent) : [...zoomSteps].reverse().find(value => value < zoomPercent);
    if (next !== undefined) setZoom(next);
  }
  const md = window.markdownit({
    html: true, linkify: true, typographer: false,
    highlight(code, language) {
      const aliases = { shell: 'sh', py3: 'python', tsx: 'jsx', typescriptreact: 'jsx', json5: 'json', jsonc: 'json' };
      language = aliases[language?.toLowerCase()] || language?.toLowerCase();
      if (language && window.hljs.getLanguage(language)) {
        try { return window.hljs.highlight(code, { language, ignoreIllegals: true }).value; } catch (_) { }
      }
      return md.utils.escapeHtml(code);
    }
  });
  md.linkify.set({ fuzzyLink: false });
  // VS Code's YAML preamble parser, using its codeBlock presentation mode.
  md.block.ruler.before('fence', 'front_matter', (state, startLine, endLine, silent) => {
    if (startLine !== 0 || state.tShift[startLine] !== 0) return false;
    const lineText = i => state.src.slice(state.bMarks[i], state.eMarks[i]).replace(/\s+$/, '');
    if (lineText(0) !== '---') return false;
    let end = 1;
    while (end < endLine && (state.tShift[end] !== 0 || lineText(end) !== '---')) ++end;
    if (end === endLine) return false;
    if (silent) return true;
    const token = state.push('fence', 'code', 0);
    token.block = true; token.info = 'yaml'; token.markup = '---'; token.map = [0, end + 1];
    token.content = state.src.slice(state.bMarks[1], state.bMarks[end]);
    token.attrJoin('class', 'frontmatter'); state.line = end + 1; return true;
  }, { alt: ['paragraph', 'reference', 'blockquote', 'list'] });
  md.core.ruler.push('source_map_data_attribute', state => {
    for (const token of state.tokens) {
      if (token.map && token.type !== 'inline') {
        token.attrSet('data-line', String(token.map[0])); token.attrSet('data-line-end', String(token.map[1]));
        if (token.type === 'fence' || token.type === 'code_block') token.attrSet('data-content-line', String(token.map[0] + (token.type === 'fence' ? 1 : 0)));
        token.attrJoin('class', 'code-line'); token.attrSet('dir', 'auto');
      }
    }
  });
  const htmlBlock = md.renderer.rules.html_block;
  md.renderer.rules.html_block = (tokens, i, options, env, self) =>
    '<div' + self.renderAttrs(tokens[i]) + '></div>\n' + htmlBlock(tokens, i, options, env, self);
  const fence = md.renderer.rules.fence;
  md.renderer.rules.fence = (tokens, i, options, env, self) => {
    const token = tokens[i];
    if (token.info.trim().split(/\s+/)[0].toLowerCase() !== 'mermaid') return fence(tokens, i, options, env, self);
    const diagram = { id: 'mwm-' + crypto.randomUUID(), source: token.content };
    diagrams.push(diagram);
    return '<div class="mermaid-diagram code-line" data-line="' + token.map[0] + '" data-line-end="' + token.map[1] + '" id="' + diagram.id + '"></div>';
  };
  const plainText = token => token.children ? token.children.map(plainText).join('') :
    ['text', 'emoji', 'code_inline'].includes(token.type) ? token.content : '';
  md.renderer.rules.heading_open = (tokens, i, options, env, self) => {
    tokens[i].attrSet('id', env.slugBuilder.add(plainText(tokens[i + 1])));
    return self.renderToken(tokens, i, options);
  };
  function tasksAndAlerts() {
    body.querySelectorAll('li').forEach(li => {
      const target = li.firstElementChild?.tagName === 'P' ? li.firstElementChild : li;
      const node = target.firstChild;
      const match = node?.nodeType === Node.TEXT_NODE && node.textContent.match(/^\[([ xX])\]\s/);
      if (!match) return;
      node.textContent = node.textContent.slice(match[0].length);
      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox'; checkbox.disabled = true; checkbox.checked = match[1] !== ' ';
      target.insertBefore(checkbox, node); li.classList.add('task-list-item');
    });
    body.querySelectorAll('blockquote > p:first-child').forEach(p => {
      const node = p.firstChild;
      const match = node?.nodeType === Node.TEXT_NODE && node.textContent.match(/^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\](?:\s|$)/);
      if (!match) return;
      node.textContent = node.textContent.slice(match[0].length);
      const label = document.createElement('strong'); label.className = 'alert-title'; label.textContent = match[1];
      p.prepend(label); p.parentElement.classList.add('markdown-alert', 'alert-' + match[1].toLowerCase());
    });
  }
  function mountDiagrams() {
    for (const diagram of diagrams) {
      diagram.host = document.getElementById(diagram.id); if (!diagram.host) continue;
      diagram.graphic = document.createElement('div'); diagram.graphic.className = 'mermaid-graphic';
      diagram.shadow = diagram.graphic.attachShadow({ mode: 'open' });
      diagram.status = document.createElement('p'); diagram.status.className = 'mermaid-status'; diagram.status.setAttribute('role', 'status');
      diagram.details = document.createElement('details'); diagram.details.className = 'mermaid-source';
      const summary = document.createElement('summary'); summary.textContent = 'Mermaid source'; summary.title = 'Show or hide Mermaid source';
      const pre = document.createElement('pre'), code = document.createElement('code');
      code.textContent = diagram.source; pre.append(code); diagram.details.append(summary, pre);
      const tools = document.createElement('div'); tools.className = 'diagram-tools';
      const expand = document.createElement('button'); expand.textContent = '⛶'; expand.title = 'Expand diagram';
      expand.setAttribute('aria-label', expand.title); expand.setAttribute('aria-expanded', 'false');
      expand.addEventListener('click', () => {
        const expanded = diagram.host.classList.toggle('expanded');
        expand.title = expanded ? 'Collapse diagram' : 'Expand diagram';
        expand.setAttribute('aria-label', expand.title); expand.setAttribute('aria-expanded', String(expanded));
      });
      tools.append(expand); diagram.host.replaceChildren(tools, diagram.graphic, diagram.status, diagram.details);
    }
  }
  function opaqueEdgeLabels(diagram) {
    const svg = diagram.shadow.querySelector('svg');
    // Mermaid's SVG label rectangles have opacity:.5 (and sometimes an RGBA
    // fill). Paint the actual surface opaquely so edges stop behind the text.
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 1;
    const context = canvas.getContext('2d');
    const luminance = channels => channels.reduce((sum, channel, i) => {
      const value = channel / 255;
      return sum + (value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4) * [.2126, .7152, .0722][i];
    }, 0);
    const surfaces = [...svg.querySelectorAll('rect,circle,ellipse,polygon,path')]
      .filter(shape => !shape.closest('defs,marker,clipPath,mask,pattern'))
      .map(shape => ({ shape, box: shape.getBoundingClientRect() }));
    const backgroundAt = (x, y, skipLabels = false) => {
      context.globalAlpha = 1; context.fillStyle = dark ? '#1e1e1e' : '#ffffff'; context.fillRect(0, 0, 1, 1);
      context.fillStyle = getComputedStyle(diagram.host).backgroundColor; context.fillRect(0, 0, 1, 1);
      context.fillStyle = getComputedStyle(svg).backgroundColor; context.fillRect(0, 0, 1, 1);
      for (const { shape, box } of surfaces) {
        const css = getComputedStyle(shape);
        if (css.fill === 'none' || x < box.left || x > box.right || y < box.top || y > box.bottom || !box.width || !box.height) continue;
        if (skipLabels && shape.matches('.edgeLabel rect,.classLabel rect.box')) continue;
        // Bounding rectangles alone are wrong near diamond/rounded corners.
        const matrix = shape.getScreenCTM();
        if (matrix && typeof shape.isPointInFill === 'function' && !shape.isPointInFill(new DOMPoint(x, y).matrixTransform(matrix.inverse()))) continue;
        let opacity = Number(css.fillOpacity);
        for (let parent = shape; parent && parent !== svg; parent = parent.parentElement) opacity *= Number(getComputedStyle(parent).opacity);
        context.globalAlpha = opacity; context.fillStyle = css.fill; context.fillRect(0, 0, 1, 1);
      }
      context.globalAlpha = 1;
      return [...context.getImageData(0, 0, 1, 1).data].slice(0, 3);
    };
    const contrast = (a, b) => (Math.max(a, b) + .05) / (Math.min(a, b) + .05);
    const ensureContrast = (text, background) => {
      const css = getComputedStyle(text), surface = luminance(background);
      context.globalAlpha = 1; context.fillStyle = `rgb(${background.join(',')})`; context.fillRect(0, 0, 1, 1);
      context.globalAlpha = Number(css.opacity) * Number(css.fillOpacity);
      context.fillStyle = css.fill; context.fillRect(0, 0, 1, 1); context.globalAlpha = 1;
      if (contrast(luminance([...context.getImageData(0, 0, 1, 1).data].slice(0, 3)), surface) >= 4.5) return;
      // Keep readable author colors; otherwise prefer normal dark text, then
      // white or black. Black/white cover midtones where dark gray cannot.
      const ink = contrast(luminance([36, 41, 47]), surface) >= 4.5 ? '#24292f' : contrast(1, surface) >= 4.5 ? '#ffffff' : '#000000';
      text.style.setProperty('fill', ink, 'important'); text.style.setProperty('color', ink, 'important');
      text.style.setProperty('fill-opacity', '1', 'important'); text.style.setProperty('opacity', '1', 'important');
    };
    const strokeContrast = (color, opacity, paper) => {
      context.globalAlpha = 1; context.fillStyle = `rgb(${paper.join(',')})`; context.fillRect(0, 0, 1, 1);
      context.globalAlpha = opacity; context.fillStyle = color; context.fillRect(0, 0, 1, 1); context.globalAlpha = 1;
      return contrast(luminance([...context.getImageData(0, 0, 1, 1).data].slice(0, 3)), luminance(paper));
    };
    let markerId = 0;
    for (const edge of svg.querySelectorAll('path,line,polyline')) {
      if (edge.closest('defs,marker,clipPath,mask,pattern,.node,.cluster,.edgeLabel,.classLabel,.label')) continue;
      const css = getComputedStyle(edge);
      if (css.stroke === 'none' || css.fill !== 'none' || !edge.getTotalLength) continue;
      if (!edge.matches('.flowchart-link,.transition,.relationshipLine,.messageLine0,.messageLine1') && !edge.closest('.edgePath') &&
          !['start', 'mid', 'end'].some(end => css.getPropertyValue('marker-' + end) !== 'none')) continue;
      const length = edge.getTotalLength(), matrix = edge.getScreenCTM();
      if (!length || !matrix) continue;
      const papers = [.03, .2, .5, .8, .97].map(fraction => {
        const point = edge.getPointAtLength(length * fraction).matrixTransform(matrix);
        return backgroundAt(point.x, point.y, true);
      });
      const score = color => Math.min(...papers.map(paper => strokeContrast(color, 1, paper)));
      let ink = css.stroke;
      if (Math.min(...papers.map(paper => strokeContrast(ink, Number(css.strokeOpacity) * Number(css.opacity), paper))) < 3) {
        ink = ['#24292f', '#ffffff', '#000000'].sort((a, b) => score(b) - score(a))[0];
        edge.style.setProperty('stroke', ink, 'important');
        edge.style.setProperty('stroke-opacity', '1', 'important'); edge.style.setProperty('opacity', '1', 'important');
        if (score(ink) < 3) {
          // A connector can cross both dark and light containers. A narrow
          // opposite-color underlay keeps its route visible on both surfaces.
          const halo = edge.cloneNode(false); halo.removeAttribute('id');
          for (const attribute of [...halo.attributes]) if (attribute.name.startsWith('data-')) halo.removeAttribute(attribute.name);
          halo.setAttribute('aria-hidden', 'true'); halo.style.pointerEvents = 'none';
          for (const end of ['start', 'mid', 'end']) halo.style.setProperty('marker-' + end, 'none', 'important');
          halo.style.setProperty('stroke', ink === '#ffffff' ? '#24292f' : '#ffffff', 'important');
          halo.style.setProperty('stroke-width', (parseFloat(css.strokeWidth) || 1) + 2 + 'px', 'important');
          edge.before(halo);
        }
      }
      for (const end of ['start', 'mid', 'end']) {
        const reference = css.getPropertyValue('marker-' + end), id = reference.match(/#([^\s"')]+)/)?.[1];
        const marker = id && svg.querySelector('[id="' + CSS.escape(id) + '"]');
        if (!marker) continue;
        const paper = papers[end === 'start' ? 0 : end === 'end' ? papers.length - 1 : 2];
        const replacement = marker.cloneNode(true); replacement.id = marker.id + '-contrast-' + markerId++;
        const originals = [...marker.querySelectorAll('path,polygon,polyline,line,circle,ellipse,rect')];
        const copies = [...replacement.querySelectorAll('path,polygon,polyline,line,circle,ellipse,rect')];
        let changed = false;
        originals.forEach((shape, i) => {
          const style = getComputedStyle(shape);
          for (const paint of ['stroke', 'fill']) {
            const color = style.getPropertyValue(paint);
            // Hollow UML markers retain their distinct fill; their outline
            // supplies the contrast. Solid arrowheads change with the edge.
            if (color === 'none' || (paint === 'fill' && color !== style.stroke && !shape.matches('.arrowMarkerPath'))) continue;
            if (strokeContrast(color, Number(style.getPropertyValue(paint + '-opacity')) * Number(style.opacity), paper) >= 3) continue;
            const colorInk = strokeContrast(ink, 1, paper) >= 3 ? ink : luminance(paper) > .18 ? '#24292f' : '#ffffff';
            copies[i].style.setProperty(paint, colorInk, 'important');
            copies[i].style.setProperty(paint + '-opacity', '1', 'important'); copies[i].style.setProperty('opacity', '1', 'important');
            changed = true;
          }
        });
        if (changed) {
          marker.after(replacement);
          edge.style.setProperty('marker-' + end, 'url(#' + replacement.id + ')', 'important');
        }
      }
    }
    for (const rect of svg.querySelectorAll('.edgeLabel rect,.classLabel rect.box')) {
      const box = rect.getBoundingClientRect(); if (!box.width || !box.height) continue;
      const x = box.left + box.width / 2, y = box.top + box.height / 2;
      const [r, g, b] = backgroundAt(x, y, true);
      rect.style.setProperty('fill', `rgb(${r}, ${g}, ${b})`, 'important');
      rect.style.setProperty('fill-opacity', '1', 'important');
      rect.style.setProperty('opacity', '1', 'important');
      for (const text of rect.parentElement.querySelectorAll('text,tspan')) ensureContrast(text, [r, g, b]);
      // A little breathing room also keeps diagonal strokes off the first/last glyph.
      rect.setAttribute('x', String(rect.x.baseVal.value - 2));
      rect.setAttribute('width', String(rect.width.baseVal.value + 4));
    }
    for (const text of svg.querySelectorAll('text,tspan')) {
      const box = text.getBoundingClientRect(); if (!box.width || !box.height || !text.textContent.trim()) continue;
      ensureContrast(text, backgroundAt(box.left + box.width / 2, box.top + box.height / 2));
    }
  }
  function renderDiagrams() {
    const generation = ++revision, batch = [...diagrams], theme = dark;
    window.markdownView.mermaidReady = false; document.getElementById('export').disabled = true;
    queue = queue.catch(() => {}).then(async () => {
      if (generation !== revision) return;
      if (batch.length && window.mermaid) window.mermaid.initialize({
        startOnLoad: false, securityLevel: 'strict', suppressErrorRendering: true,
        theme: theme ? 'dark' : 'default', darkMode: theme,
        fontFamily: 'Segoe UI, sans-serif', htmlLabels: false, flowchart: { htmlLabels: false }, maxTextSize: 50000, maxEdges: 500,
        secure: ['secure', 'securityLevel', 'startOnLoad', 'maxTextSize', 'maxEdges', 'suppressErrorRendering',
          'dompurifyConfig', 'htmlLabels', 'flowchart', 'theme', 'themeCSS', 'themeVariables', 'darkMode', 'fontFamily']
      });
      for (const diagram of batch) {
        if (generation !== revision) return;
        if (!diagram.host) continue;
        diagram.host.dataset.renderState = 'loading'; diagram.status.textContent = 'Rendering diagram…';
        diagram.status.hidden = false; diagram.status.classList.remove('mermaid-error');
        const scratch = document.createElement('div'); scratch.className = 'mermaid-measure'; document.body.append(scratch);
        try {
          if (!window.mermaid) throw new Error('The Mermaid library is not loaded.');
          if (diagram.source.length > 50000) throw new Error('The diagram exceeds the 50,000-character limit.');
          const { svg } = await window.mermaid.render(diagram.id + '-svg-' + generation, diagram.source, scratch);
          if (generation !== revision) return;
          const clean = window.DOMPurify.sanitize(svg, {
            USE_PROFILES: { svg: true, svgFilters: true }, RETURN_DOM_FRAGMENT: true, ADD_TAGS: ['style'],
            FORBID_TAGS: ['foreignObject', 'a', 'image', 'script', 'iframe']
          });
          const svgElement = clean.querySelector('svg'); if (!svgElement) throw new Error('Mermaid did not return an SVG diagram.');
          svgElement.setAttribute('role', 'img'); svgElement.setAttribute('aria-label', 'Mermaid diagram');
          const style = document.createElement('style');
          style.textContent = ':host{display:block}svg{display:block;max-width:100%;height:auto;margin:0 auto;font-family:Segoe UI,sans-serif}';
          diagram.shadow.replaceChildren(style, clean);
          opaqueEdgeLabels(diagram);
          diagram.host.dataset.renderState = 'ready'; diagram.host.dataset.theme = theme ? 'dark' : 'light'; diagram.status.hidden = true;
        } catch (error) {
          if (generation !== revision) return;
          diagram.shadow.replaceChildren(); diagram.host.dataset.renderState = 'error'; diagram.status.classList.add('mermaid-error');
          diagram.status.textContent = 'Could not render the Mermaid diagram: ' + String(error.message || error).slice(0, 600); diagram.details.open = true;
        } finally { scratch.remove(); }
      }
      if (generation === revision) {
        window.markdownView.mermaidReady = true; document.getElementById('export').disabled = false;
        document.getElementById('render-status').textContent = batch.length ? 'Mermaid 11.16.1 · ' + batch.length : '';
        applyZoom(false);
      }
    });
    return queue;
  }
  function scrollToLine(line) {
    if (!Number.isFinite(line) || line < 0) return;
    const items = [...body.querySelectorAll('[data-line]')];
    // Nested lists and blockquotes overlap their children's source ranges.
    // Use the smallest containing range, not the last node in DOM order.
    const containing = items.filter(item => +item.dataset.line <= line && line < +item.dataset.lineEnd);
    containing.sort((a, b) => (+a.dataset.lineEnd - +a.dataset.line) - (+b.dataset.lineEnd - +b.dataset.line) || +b.dataset.line - +a.dataset.line || (a.contains(b) ? 1 : b.contains(a) ? -1 : 0));
    const target = containing[0] || items.filter(item => +item.dataset.line <= line).sort((a, b) => +b.dataset.line - +a.dataset.line)[0] || items[0];
    if (!target) return;
    const start = +target.dataset.line, end = +target.dataset.lineEnd || start + 1;
    let top = target.getBoundingClientRect().top + window.scrollY;
    const isCode = target.matches('pre,pre > code');
    const text = isCode ? (target.matches('code') ? target : target.querySelector('code')) : target.matches('p,h1,h2,h3,h4,h5,h6') ? target : null;
    if (text) {
      // DOM ranges work through syntax highlighting and CSS wrapping without
      // inserting spans that would alter Copy or exported code formatting.
      const value = text.textContent, lines = value.split('\n');
      if (lines.length > 1 && !lines.at(-1)) lines.pop();
      const contentStart = target.hasAttribute('data-content-line') ? +target.dataset.contentLine : start;
      if (isCode || lines.length >= end - start) {
        const offsetLine = Math.max(0, line - contentStart), index = Math.min(Math.floor(offsetLine), lines.length - 1);
        let offset = 0;
        for (let i = 0; i < index; ++i) offset += lines[i].length + 1;
        offset += Math.min(Math.max(0, lines[index]?.length - 1 || 0), Math.floor((offsetLine - Math.floor(offsetLine)) * (lines[index]?.length || 0)));
        const walker = document.createTreeWalker(text, NodeFilter.SHOW_TEXT); let node;
        while ((node = walker.nextNode())) {
          if (offset < node.length) {
            const range = document.createRange();
            range.setStart(node, Math.min(offset, node.length)); range.setEnd(node, Math.min(offset + 1, node.length));
            const rect = range.getClientRects()[0];
            if (rect) top = rect.top + window.scrollY;
            break;
          }
          offset -= node.length;
        }
      } else top += target.getBoundingClientRect().height * Math.min(1, Math.max(0, (line - start) / (end - start)));
    } else {
      let height = target.getBoundingClientRect().height;
      if (!height) {
        const next = items.filter(item => +item.dataset.line >= end).sort((a,b) => +a.dataset.line - +b.dataset.line)[0];
        height = Math.max(0, (next || body).getBoundingClientRect()[next ? 'top' : 'bottom'] + window.scrollY - top);
      }
      top += height * Math.min(1, Math.max(0, (line - start) / (end - start)));
    }
    window.scrollTo(window.scrollX, Math.max(0, top - (document.getElementById('toolbar')?.offsetHeight || 0) - 12));
  }
  function buildOutline() {
    const outline = document.getElementById('outline'); outline.replaceChildren();
    for (const h of body.querySelectorAll('h1,h2,h3,h4,h5,h6')) {
      const a = document.createElement('a'); a.textContent = h.textContent; a.href = '#' + encodeURIComponent(h.id);
      a.style.paddingLeft = (8 + (+h.tagName.slice(1) - 1) * 12) + 'px';
      a.addEventListener('click', event => { event.preventDefault(); navigateAnchor(encodeURIComponent(h.id)); }); outline.append(a);
    }
  }
  function linkHints() {
    for (const link of body.querySelectorAll('a[href]')) {
      const raw = link.getAttribute('href');
      try {
        const url = new URL(raw, document.baseURI);
        let kind, destination, marker;
        if (raw.startsWith('#')) {
          kind = 'Section in this document'; destination = (current?.file || '') + decodeURI(raw);
        } else if (url.protocol === 'file:') {
          const markdown = /\.(md|markdown|mdown|mkd|mkdn)$/i.test(url.pathname);
          kind = markdown ? 'Markdown document' : 'Local file';
          if (markdown) marker = 'md';
          let path = decodeURIComponent(url.pathname);
          if (url.hostname) path = '\\\\' + url.hostname + path;
          else if (/^\/[a-z]:\//i.test(path)) path = path.slice(1);
          destination = path.replace(/\//g, '\\') + decodeURI(url.search + url.hash);
        } else if (['http:', 'https:', 'mailto:'].includes(url.protocol)) {
          kind = url.protocol === 'mailto:' ? 'Email' : 'External link';
          destination = url.href;
          if (url.protocol !== 'mailto:') marker = 'external';
        } else continue;
        const original = link.getAttribute('title');
        link.title = (original ? original + '\n\n' : '') + kind + '\n' + destination;
        // Keep image-only badges intact. Icons are trusted local geometry, not document SVG.
        if (marker && link.textContent.trim()) {
          const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
          svg.setAttribute('viewBox', '0 0 16 16'); svg.setAttribute('aria-hidden', 'true');
          svg.setAttribute('focusable', 'false'); svg.setAttribute('class', 'link-type-icon link-type-' + marker);
          const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
          path.setAttribute('d', marker === 'md' ? 'M9 1.5H3v13h10v-9L9 1.5v4h4M5 8h6M5 11h6' : 'M9 2h5v5M14 2L7 9M6 3H2v11h11v-4');
          svg.append(path);
          if (marker === 'md') link.prepend(svg); else link.append(svg);
        }
      } catch (_) { /* Leave malformed or unsupported URLs unchanged. */ }
    }
  }
  function sizeImages() {
    body.querySelectorAll('img').forEach(img => {
      const dimension = name => /^\d+(?:\.\d+)?$/.test(img.getAttribute(name) || '') ? Number(img.getAttribute(name)) : 0;
      const width = dimension('width'), height = dimension('height');
      if (width > 0) {
        img.style.width = width + 'px';
        if (height > 0) img.style.aspectRatio = width + ' / ' + height;
      } else if (height > 0) {
        // A general height:auto rule otherwise erases the HTML height hint.
        // Once decoded, constrain width and let CSS scale both axes together.
        img.style.height = height + 'px';
        const loaded = () => {
          if (!img.naturalHeight) return;
          img.style.width = height * img.naturalWidth / img.naturalHeight + 'px';
          img.style.height = 'auto';
        };
        if (img.complete && img.naturalHeight) loaded(); else img.addEventListener('load', loaded, { once: true });
      }
    });
  }
  function update(data) {
    backButton.disabled = !data.navigation?.back;
    forwardButton.disabled = !data.navigation?.forward;
    selection.setEditorSelectionActive(data.editorActive);
    const navigationChanged = current?.navigation?.token !== data.navigation?.token;
    if (navigationChanged) navigationStatus.hidden = true;
    if (current && current.source === data.source && current.file === data.file) {
      current = data;
      if (navigationChanged && data.navigation?.restore) {
        restoring = true; restoreAfterRender(data, () => restoreVisit(data.navigation));
      }
      return;
    }
    clearTimeout(scrollTimer); restoring = true;
    if (current) positions.set(current.file, window.scrollY);
    const sameFile = current?.file === data.file;
    const top = sameFile ? window.scrollY : positions.get(data.file) ?? data.scrollY ?? 0;
    current = data; diagrams.length = 0;
    if (data.base) document.querySelector('base').href = data.base;
    try {
      const fragment = window.DOMPurify.sanitize(data.fragment || md.render(data.source, { slugBuilder: window.mwmSlugBuilder() }), {
        USE_PROFILES: data.fragment ? { html: true, svg: true, svgFilters: true } : { html: true },
        FORBID_TAGS: [...(data.fragment ? [] : ['style', 'input']), 'form', 'button', 'textarea', 'select', 'iframe', 'object', 'embed', 'base', 'meta', 'link', 'foreignObject'],
        FORBID_ATTR: [...(data.fragment ? [] : ['style']), 'srcset', 'autofocus'], ADD_TAGS: data.fragment ? ['style'] : [],
        ALLOW_DATA_ATTR: false, ADD_ATTR: data.fragment ? ['data-line', 'data-line-end', 'data-content-line', 'data-render-state'] : ['data-line', 'data-line-end', 'data-content-line'], RETURN_DOM_FRAGMENT: !!initial.docxExport
      });
      if (initial.docxExport) {
        fragment.querySelectorAll('img[src]').forEach(img => { img.dataset.docxSource = img.getAttribute('src'); img.removeAttribute('src'); });
        body.replaceChildren(fragment);
      } else body.innerHTML = fragment;
      sizeImages();
      if (!data.fragment) { tasksAndAlerts(); mountDiagrams(); linkHints(); }
      buildOutline();
    } catch (error) { body.textContent = 'Could not render Markdown: ' + error.message; }
    document.getElementById('filename').textContent = data.file.split(/[\\/]/).pop();
    if (!sameFile) selection.resetFilenameFeedback();
    document.getElementById('filename').title = data.file;
    document.title = document.getElementById('filename').textContent + ' — Markdown_with_Mermaid';
    renderDiagrams();
    restoreAfterRender(data, () => {
      if (navigationChanged && data.navigation?.restore) restoreVisit(data.navigation);
      else if (data.line >= 0) scrollToLine(data.line); else window.scrollTo(0, top);
    });
  }
  async function restoreAfterRender(data, action) {
    let pending;
    do { pending = queue; await pending; } while (pending !== queue);
    if (current?.file !== data.file || current.source !== data.source || current.navigation?.token !== data.navigation?.token) return;
    action(); restoring = false; publishScroll();
  }
  function fragmentTarget(fragment) {
    try {
      const target = document.getElementById(decodeURIComponent(fragment));
      return target && body.contains(target) ? target : null;
    } catch (_) { return null; }
  }
  function goToFragment(fragment) {
    const target = fragmentTarget(fragment);
    if (!target) return false;
    window.scrollTo(0, Math.max(0, target.getBoundingClientRect().top + window.scrollY - document.getElementById('toolbar').offsetHeight - 12));
    return true;
  }
  function navigateAnchor(fragment) {
    if (!fragmentTarget(fragment)) return;
    if (!current?.navigation) { goToFragment(fragment); return; }
    publishScroll(); navigationStatus.hidden = true;
    bridge('anchor:' + current.navigation.token + ':' + fragment);
  }
  function restoreVisit(visit) {
    if (!visit.fragment || !goToFragment(visit.fragment)) window.scrollTo(visit.x, visit.y);
  }
  function publishScroll() {
    clearTimeout(scrollTimer);
    if (!pdfMode && !restoring) bridge('scroll:' + (current?.navigation?.token ?? 0) + ':' + Math.round(window.scrollX) + ':' + Math.round(window.scrollY));
  }
  function navigate(direction) {
    if ((direction === 'back' ? backButton : forwardButton).disabled) return;
    publishScroll(); navigationStatus.hidden = true; bridge('history:' + direction);
  }
  function navigationError() { navigationStatus.textContent = 'Could not open the document from history. The file may have been moved or deleted.'; navigationStatus.hidden = false; }
  function themeButtonState() {
    themeButton.setAttribute('aria-pressed', String(dark));
    themeButton.title = dark ? 'Switch to light theme' : 'Switch to dark theme';
    themeButton.setAttribute('aria-label', themeButton.title);
  }
  function setTheme(value) {
    dark = value; document.documentElement.classList.toggle('dark', dark);
    themeButtonState();
    if (!pdfMode) bridge(dark ? 'theme:dark' : 'theme:light');
    themeLink.href = assetBase + (dark ? 'highlight-dark.css' : 'highlight-light.css'); return renderDiagrams();
  }
  const findbar = document.getElementById('findbar'), query = document.getElementById('query');
  function openFind() { findbar.hidden = false; query.focus(); query.select(); }
  function find(text, backwards = false) {
    if (!text) return false;
    query.blur(); const found = window.find(text, false, backwards, true, false, false, false);
    if (found) { selection.setEditorSelectionActive(false); bridge('preview-active'); }
    document.getElementById('find-status').textContent = found ? '' : 'No matches found'; return found;
  }
  function escape() {
    if (selection.closeFilenameMenu()) return;
    if (document.activeElement === zoomInput) { zoomInput.value = zoomPercent + '%'; zoomInput.blur(); return; }
    const expanded = document.querySelector('.mermaid-diagram.expanded');
    if (expanded) expanded.classList.remove('expanded'); else if (!findbar.hidden) findbar.hidden = true; else bridge('close');
  }
  function toggleOutline() {
    const outline = document.getElementById('outline'); outline.hidden = !outline.hidden;
    document.getElementById('outline-button').setAttribute('aria-expanded', String(!outline.hidden));
  }
  async function exportHtml(selected = []) {
    const first = body.firstChild; await queue;
    if (body.firstChild !== first) throw new Error('The document changed. Please export HTML again.');
    const clone = selection.snapshot(selected);
    clone.removeAttribute('style'); clone.removeAttribute('data-view-zoom');
    clone.querySelectorAll('.diagram-tools,.mermaid-status').forEach(e => e.remove());
    clone.querySelectorAll('.expanded').forEach(e => e.classList.remove('expanded'));
    clone.querySelectorAll('img,a').forEach(e => { if (e.tagName === 'IMG') e.src = e.src; else if (!e.getAttribute('href')?.startsWith('#')) e.href = e.href; });
    const html = '<!doctype html><html lang="ru"' + (dark ? ' class="dark"' : '') + '><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>' + md.utils.escapeHtml(document.title) + '</title><!--MWM_EXPORT_STYLES--></head><body class="vscode-body">' + clone.outerHTML + '</body></html>';
    bridge('export:' + html); return html;
  }
  async function requestHtmlExport() {
    try {
      if (selection.editorSelectionActive()) { bridge('html'); return; }
      return await exportHtml(selection.ranges());
    } catch (error) { setHtmlStatus('Could not export HTML: ' + String(error.message || error), false, true); }
  }
  function setHtmlStatus(message, busy, error = false) {
    document.getElementById('export').disabled = busy;
    const status = document.getElementById('copy-status');
    status.textContent = message; status.classList.toggle('error', error);
  }
  function setPdfStatus(message, busy, error = false) {
    document.getElementById('export-pdf').disabled = busy;
    const status = document.getElementById('pdf-status');
    status.textContent = message; status.classList.toggle('error', error);
  }
  async function preparePdf(notify = true) {
    if (!pdfMode) return;
    try {
      await queue;
      await document.fonts.ready;
      let missing = 0;
      if (!initial.docxExport) await Promise.all([...body.querySelectorAll('img')].map(async img => {
        if (!img.complete) await new Promise(resolve => {
          const done = () => { clearTimeout(timer); img.removeEventListener('load', done); img.removeEventListener('error', done); resolve(); };
          const timer = setTimeout(done, 15000);
          img.addEventListener('load', done, { once: true }); img.addEventListener('error', done, { once: true });
        });
        if (!img.complete || !img.naturalWidth) {
          missing++;
          const warning = document.createElement('p'); warning.className = 'pdf-image-error';
          warning.textContent = 'Image unavailable: ' + (img.alt || img.getAttribute('src') || ''); img.replaceWith(warning);
        }
      }));
      for (const diagram of diagrams) {
        if (diagram.host.dataset.renderState === 'ready') {
          const graphic = document.createElement('div'); graphic.className = 'mermaid-graphic';
          graphic.append(diagram.shadow.querySelector('svg').cloneNode(true)); diagram.graphic.replaceWith(graphic);
          diagram.details.remove(); diagram.status.remove();
        } else { diagram.details.open = true; }
      }
      body.querySelectorAll('.diagram-tools').forEach(node => node.remove());
      body.removeAttribute('style'); body.removeAttribute('data-view-zoom');
      document.getElementById('toolbar').remove(); document.getElementById('findbar').remove();
      document.getElementById('outline').remove(); document.getElementById('render-status').remove(); document.getElementById('pdf-status').remove();
      window.scrollTo(0, 0);
      window.__pdfPrepared = true;
      // Allow the flattened SVGs to complete a layout pass, also in an offscreen controller.
      if (notify) setTimeout(() => bridge('pdf-ready:' + missing), 100);
      return missing;
    } catch (error) { if (!notify) throw error; bridge('pdf-error:' + String(error.message || error).slice(0, 600)); }
  }
  function setDocxStatus(message, busy, error = false) {
    document.getElementById('export-docx').disabled = busy;
    const status = document.getElementById('docx-status'); status.textContent = message; status.classList.toggle('error', error);
  }
  const docxResources = new Map(); let docxResourceId = 0, docxChunk;
  function docxResourceResult(id, data, error) { const resolve = docxResources.get(id); if (resolve) { docxResources.delete(id); resolve({ data, error }); } }
  function docxChunkAccepted(id) { if (docxChunk?.id === id) { const resolve = docxChunk.resolve; docxChunk = null; resolve(); } }
  function localScript(name) {
    return new Promise((resolve, reject) => {
      const script = document.createElement('script'); script.nonce = document.querySelector('script[nonce]').nonce;
      script.onload = resolve; script.onerror = () => reject(new Error('Missing assets/' + name));
      script.src = assetBase + name; document.head.append(script);
    });
  }
  async function prepareDocx() {
    try {
      const missing = await preparePdf(false);
      await localScript('docx.iife.js'); await localScript('docx-export.js');
      const result = await window.mwmBuildDocx(body, { title: current.file.split(/[\\/]/).pop(), missing,
        resource: url => new Promise(resolve => { const id = docxResourceId++; const timer = setTimeout(() => { docxResources.delete(id); resolve({ error: 'Image download timed out' }); }, 45000);
          docxResources.set(id, data => { clearTimeout(timer); resolve(data); }); bridge('docx-resource:' + id + ':' + url); }) });
      const data = new Uint8Array(await result.blob.arrayBuffer());
      for (let offset = 0, index = 0; offset < data.length; offset += 49152, index++) {
        const chunk = data.subarray(offset, offset + 49152); let binary = ''; for (const byte of chunk) binary += String.fromCharCode(byte);
        await new Promise(resolve => { docxChunk = { id: index, resolve }; bridge('docx-chunk:' + index + ':' + btoa(binary)); });
      }
      bridge('docx-done:' + result.missing);
    } catch (error) { bridge('docx-error:' + String(error.message || error).slice(0, 600)); }
  }
  const selection = window.mwmSelection(body, bridge, () => queue);
  window.markdownView = { update, setTheme, toggleTheme: () => setTheme(!dark), toggleOutline, scrollToLine, openFind, find, escape, exportHtml, requestHtmlExport, setHtmlStatus, setZoom, setPdfStatus, preparePdf, setDocxStatus, prepareDocx, docxResourceResult, docxChunkAccepted,
    copy: selection.copy, copyFocused: selection.copyFocused, copyResult: selection.copyResult, clipboardResourceResult: selection.resourceResult,
    requestExport: selection.requestExport, selectionSnapshot: selection.snapshot, navigationError, mermaidReady: false };
  window.markdownView.setEditorSelectionActive = selection.setEditorSelectionActive;
  document.documentElement.classList.toggle('dark', dark);
  themeLink.href = assetBase + (dark ? 'highlight-dark.css' : 'highlight-light.css');
  themeButtonState();
  backButton.addEventListener('click', () => navigate('back'));
  forwardButton.addEventListener('click', () => navigate('forward'));
  themeButton.addEventListener('click', () => setTheme(!dark));
  document.getElementById('outline-button').addEventListener('click', toggleOutline);
  document.getElementById('zoom-out').addEventListener('click', () => changeZoom(-1));
  document.getElementById('zoom-in').addEventListener('click', () => changeZoom(1));
  // Use document zoom for Ctrl+wheel; browser zoom would also resize the toolbar.
  document.addEventListener('wheel', event => {
    if (pdfMode || !event.ctrlKey) return;
    event.preventDefault();
    if (event.deltaY) changeZoom(event.deltaY < 0 ? 1 : -1);
  }, { passive: false });
  zoomSelect.addEventListener('change', () => setZoom(zoomSelect.value));
  zoomInput.addEventListener('change', () => setZoom(zoomInput.value));
  zoomInput.addEventListener('blur', () => { zoomInput.value = zoomPercent + '%'; });
  zoomInput.addEventListener('keydown', event => {
    if (event.key === 'Enter') { event.preventDefault(); setZoom(zoomInput.value); zoomInput.blur(); }
  });
  window.addEventListener('resize', () => applyZoom(false));
  document.getElementById('export').addEventListener('click', requestHtmlExport);
  document.getElementById('export-pdf').addEventListener('click', () => selection.requestExport('pdf'));
  document.getElementById('export-docx').addEventListener('click', () => selection.requestExport('docx'));
  findbar.addEventListener('submit', event => { event.preventDefault(); find(query.value); });
  document.getElementById('find-prev').addEventListener('click', () => find(query.value, true));
  document.getElementById('find-close').addEventListener('click', () => { findbar.hidden = true; });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') { event.preventDefault(); escape(); }
    else if (!pdfMode && event.ctrlKey && !event.altKey && ['+', '=', '-', '0'].includes(event.key)) {
      event.preventDefault();
      if (event.key === '0') setZoom('actual');
      else changeZoom(event.key === '-' ? -1 : 1);
    }
    else if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key.toLowerCase() === 'c' && !document.activeElement?.matches('input,textarea,[contenteditable="true"]')) { event.preventDefault(); selection.copyFocused(); }
    else if ((event.ctrlKey && event.key.toLowerCase() === 'f') || event.key === 'F7') { event.preventDefault(); openFind(); }
    else if (event.key === 'F5') { event.preventDefault(); bridge('reload'); }
  });
  body.addEventListener('dblclick', event => {
    const item = event.target.closest('[data-line]'); if (item && !event.target.closest('a,details')) bridge('line:' + item.dataset.line);
  });
  body.addEventListener('click', event => {
    const a = event.target.closest('a[href]'); if (!a) return;
    event.preventDefault(); const raw = a.getAttribute('href');
    if (raw.startsWith('#')) navigateAnchor(raw.slice(1));
    else { try { const url = new URL(raw, document.baseURI); if (['http:', 'https:', 'mailto:', 'file:'].includes(url.protocol)) { publishScroll(); bridge('link:' + url.href); } } catch (_) { } }
  });
  window.addEventListener('scroll', () => {
    clearTimeout(scrollTimer); if (!restoring) scrollTimer = setTimeout(publishScroll, 120);
  });
  if (!pdfMode) {
    const toolbar = document.getElementById('toolbar');
    const resizeHeader = () => document.documentElement.style.setProperty('--preview-header-height', toolbar.offsetHeight + 'px');
    new ResizeObserver(resizeHeader).observe(toolbar); resizeHeader();
  }
  update(initial); bridge('ready');
})();
