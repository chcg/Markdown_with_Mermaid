/* Markdown_with_Mermaid semantic DOCX adapter. docx 9.12.0 is shipped locally. */
(() => {
  'use strict';
  window.mwmBuildDocx = async (body, options) => {
    const d = window.docx;
    const WIDTH = 680, HEIGHT = 850, TWIPS = 10224;
    const bookmarks = new Map(), numbering = [];
    let missing = options.missing || 0, listId = 0, imageId = 0;
    body.querySelectorAll('h1[id],h2[id],h3[id],h4[id],h5[id],h6[id]').forEach((h, i) => bookmarks.set(h.id, 'mwm_heading_' + i));
    const textRun = (text, style = {}) => new d.TextRun({ text, ...style });
    const paragraph = (children = [], properties = {}) => new d.Paragraph({ children, ...properties });
    const decode = text => { try { return decodeURIComponent(text); } catch (_) { return text; } };
    const bytes = dataUri => Uint8Array.from(atob(dataUri.slice(dataUri.indexOf(',') + 1)), c => c.charCodeAt(0));
    const svgUri = svg => 'data:image/svg+xml;base64,' + btoa([...new TextEncoder().encode(svg)].map(c => String.fromCharCode(c)).join(''));
    const loadImage = src => new Promise((resolve, reject) => {
      const image = new Image(), timer = setTimeout(() => reject(new Error('Image decoding timed out')), 15000);
      if (/^https?:/i.test(src)) image.crossOrigin = 'anonymous';
      image.onload = () => { clearTimeout(timer); resolve(image); };
      image.onerror = () => { clearTimeout(timer); reject(new Error('Could not decode the image')); };
      image.src = src;
    });
    async function imageRun(node, maxWidth = WIDTH, diagram = false) {
      const alt = diagram ? 'Mermaid diagram' : node.alt || 'Image';
      try {
        if (++imageId > 256) throw new Error('image limit');
        let src, svg;
        if (diagram) {
          const clone = node.cloneNode(true), viewBox = clone.viewBox.baseVal;
          if (!viewBox.width || !viewBox.height) throw new Error('invalid SVG bounds');
          // Word/LibreOffice do not implement Chromium's complete SVG CSS cascade.
          // Freeze resolved styles as presentation attributes (including HSL -> RGB).
          const properties = ['fill','fill-opacity','stroke','stroke-width','stroke-opacity','stroke-linecap','stroke-linejoin','stroke-dasharray','stroke-dashoffset','opacity','font-family','font-size','font-weight','font-style','text-anchor','dominant-baseline','alignment-baseline','paint-order','color','letter-spacing','text-decoration'];
          const originals = [node, ...node.querySelectorAll('*')], copies = [clone, ...clone.querySelectorAll('*')];
          for (let i = 0; i < originals.length; i++) {
            if (originals[i].tagName.toLowerCase() === 'style') continue;
            const computed = getComputedStyle(originals[i]); copies[i].removeAttribute('style');
            for (const property of properties) {
              let value = computed.getPropertyValue(property);
              if (!value) continue;
              if (['fill','stroke','color'].includes(property) && /^rgb\(/.test(value)) {
                const channels = value.match(/[\d.]+/g); value = '#' + channels.slice(0, 3).map(v => Math.round(Number(v)).toString(16).padStart(2, '0')).join('');
              }
              copies[i].setAttribute(property, value);
            }
            // Several Office SVG readers stroke actor labels using the shape's
            // inherited outline. Mermaid text must stay filled, without outlines.
            if (['text', 'tspan'].includes(originals[i].tagName.toLowerCase())) copies[i].setAttribute('stroke', 'none');
            if (originals[i].tagName.toLowerCase() === 'text') {
              const firstSpan = originals[i].querySelector('tspan');
              if (firstSpan) copies[i].setAttribute('fill', getComputedStyle(firstSpan).fill);
            }
          }
          clone.querySelectorAll('style').forEach(style => style.remove());
          clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
          clone.setAttribute('width', String(Math.ceil(viewBox.width))); clone.setAttribute('height', String(Math.ceil(viewBox.height)));
          clone.style.maxWidth = 'none'; clone.style.width = ''; clone.style.height = '';
          svg = new XMLSerializer().serializeToString(clone); src = svgUri(svg);
        } else {
          const original = node.dataset.docxSource;
          src = original ? new URL(original, document.baseURI).href : node.src;
          if (/^(?:file|https?):/i.test(src)) {
            const resource = await options.resource(src);
            if (!resource.data) throw new Error(resource.error || 'Could not download the image');
            src = resource.data;
          }
          if (!src) throw new Error('Missing image URL');
        }
        const image = await loadImage(src);
        let width = image.naturalWidth, height = image.naturalHeight;
        if (!diagram) {
          const dimension = name => /^\d+(?:\.\d+)?$/.test(node.getAttribute(name) || '') ? Number(node.getAttribute(name)) : 0;
          const authoredWidth = dimension('width'), authoredHeight = dimension('height');
          if (authoredWidth > 0) { width = authoredWidth; height = authoredHeight || image.naturalHeight * width / image.naturalWidth; }
          else if (authoredHeight > 0) { height = authoredHeight; width = image.naturalWidth * height / image.naturalHeight; }
        }
        const factor = Math.min(1, maxWidth / width, HEIGHT / height);
        width = Math.max(1, Math.round(width * factor)); height = Math.max(1, Math.round(height * factor));
        const density = Math.min(2, Math.sqrt(12000000 / (width * height)));
        const canvas = document.createElement('canvas'); canvas.width = Math.max(1, Math.ceil(width * density)); canvas.height = Math.max(1, Math.ceil(height * density));
        const context = canvas.getContext('2d'); context.fillStyle = '#ffffff'; context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        const png = bytes(canvas.toDataURL('image/png'));
        const common = { transformation: { width, height }, altText: { name: alt, title: alt, description: alt } };
        return svg ? new d.ImageRun({ type: 'svg', data: new TextEncoder().encode(svg), fallback: { type: 'png', data: png }, ...common }) :
          new d.ImageRun({ type: 'png', data: png, ...common });
      } catch (error) {
        missing++;
        return textRun('[Image unavailable: ' + alt + '. ' + String(error.message || 'Image processing error').slice(0, 600) + ']', { color: 'A11A1A' });
      }
    }
    async function inline(node, style = {}, width = WIDTH) {
      if (node.nodeType === Node.TEXT_NODE) return node.textContent ? [textRun(node.textContent.replace(/[\r\n\t]+/g, ' '), style)] : [];
      if (node.nodeType !== Node.ELEMENT_NODE || node.matches('.link-type-icon,summary,.diagram-tools,script,style')) return [];
      const tag = node.tagName;
      if (tag === 'BR') return [new d.TextRun({ break: 1 })];
      if (tag === 'IMG') return [await imageRun(node, width)];
      if (tag === 'PRE') return node.textContent.replace(/\n$/, '').split('\n').flatMap((line, i) => [i ? new d.TextRun({ break: 1 }) : textRun(''), textRun(line, { font: 'Consolas', size: 18 })]);
      if (tag === 'INPUT' && node.type === 'checkbox') return [textRun(node.checked ? '☑ ' : '☐ ', { ...style, font: 'Segoe UI Symbol' })];
      const next = { ...style };
      if (['STRONG', 'B'].includes(tag)) next.bold = true;
      if (['EM', 'I'].includes(tag)) next.italics = true;
      if (['S', 'DEL', 'STRIKE'].includes(tag)) next.strike = true;
      if (tag === 'U') next.underline = {};
      if (tag === 'SUB') next.subScript = true;
      if (tag === 'SUP') next.superScript = true;
      if (tag === 'CODE') { next.font = 'Consolas'; next.size = 19; }
      if (node.className?.toString().includes('hljs-')) {
        const color = getComputedStyle(node).color.match(/\d+/g);
        if (color?.length >= 3) next.color = color.slice(0, 3).map(c => Number(c).toString(16).padStart(2, '0')).join('');
      }
      if (tag === 'A') { next.color = '0969DA'; next.underline = {}; }
      const children = [];
      for (const child of node.childNodes) children.push(...await inline(child, next, width));
      if (tag === 'P' && node.nextElementSibling) children.push(new d.TextRun({ break: 1 }));
      if (tag === 'A' && node.hasAttribute('href')) {
        const href = node.getAttribute('href');
        if (href.startsWith('#') && bookmarks.has(decode(href.slice(1)))) return [new d.InternalHyperlink({ anchor: bookmarks.get(decode(href.slice(1))), children })];
        try { const url = new URL(href, document.baseURI); if (['https:', 'http:', 'mailto:', 'file:'].includes(url.protocol)) return [new d.ExternalHyperlink({ link: url.href, children })]; } catch (_) { }
      }
      return children;
    }
    async function inlines(nodes, style = {}, width = WIDTH) {
      const result = []; for (const node of nodes) result.push(...await inline(node, style, width)); return result;
    }
    async function list(node, depth, width, indent) {
      const result = [], ordered = node.tagName === 'OL', level = Math.min(depth, 8), reference = 'mwm_list_' + listId++;
      if (ordered) numbering.push({ reference, levels: Array.from({ length: 9 }, (_, i) => ({ level: i, format: d.LevelFormat.DECIMAL, text: '%' + (i + 1) + '.', start: i === level ? Number(node.start) || 1 : 1,
        alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: indent + 360 * (i + 1), hanging: 240 } } } })) });
      for (const item of node.children) {
        if (item.tagName !== 'LI') continue;
        const content = [], nested = [];
        for (const child of item.childNodes) {
          if (child.nodeType === Node.ELEMENT_NODE && ['UL', 'OL'].includes(child.tagName)) nested.push(child); else content.push(child);
        }
        const task = item.querySelector(':scope > input[type=checkbox],:scope > p > input[type=checkbox]');
        const runs = await inlines(content, {}, Math.max(100, width - (level + 1) * 24));
        const properties = { spacing: { before: 0, after: 80 }, indent: { left: indent + 360 * (level + 1), hanging: 240 } };
        if (!task) { if (ordered) properties.numbering = { reference, level }; else properties.bullet = { level }; }
        result.push(paragraph(runs, properties));
        for (const child of nested) result.push(...await list(child, depth + 1, width, indent));
      }
      return result;
    }
    async function table(node, width) {
      const rows = [...node.rows], count = Math.max(1, ...rows.map(row => [...row.cells].reduce((n, cell) => n + (cell.colSpan || 1), 0)));
      const weights = Array(count).fill(4);
      rows.forEach(row => { let i = 0; for (const cell of row.cells) { if (cell.colSpan === 1) weights[i] = Math.max(weights[i], Math.min(36, cell.textContent.length)); i += cell.colSpan || 1; } });
      const sum = weights.reduce((a, b) => a + b, 0), total = Math.round(TWIPS * width / WIDTH), columns = weights.map(weight => Math.round(total * weight / sum));
      const border = { style: d.BorderStyle.SINGLE, size: 4, color: 'D9D9D9' }, borders = { top: border, bottom: border, left: border, right: border };
      const converted = [];
      for (const [rowIndex, row] of rows.entries()) {
        let column = 0; const cells = [];
        for (const cell of row.cells) {
          const span = cell.colSpan || 1, cellWidth = columns.slice(column, column + span).reduce((a, b) => a + b, 0); column += span;
          const children = await blocks(cell, Math.max(80, cellWidth / 15 - 12), 0, { size: 20, bold: cell.tagName === 'TH' });
          if (!children.length || children[children.length - 1] instanceof d.Table) children.push(paragraph());
          cells.push(new d.TableCell({ children, width: { size: cellWidth, type: d.WidthType.DXA }, columnSpan: span, rowSpan: cell.rowSpan || 1,
            borders, margins: { top: 85, bottom: 85, left: 100, right: 100 }, verticalAlign: d.VerticalAlign.CENTER,
            shading: { fill: cell.tagName === 'TH' ? 'E8EEF5' : rowIndex % 2 === 0 ? 'F6F8FA' : 'FFFFFF' } }));
        }
        converted.push(new d.TableRow({ children: cells, tableHeader: row.parentElement.tagName === 'THEAD', cantSplit: true }));
      }
      return new d.Table({ rows: converted, width: { size: total, type: d.WidthType.DXA }, columnWidths: columns, layout: d.TableLayoutType.FIXED,
        borders: { ...borders, insideHorizontal: border, insideVertical: border } });
    }
    const blockTags = new Set(['P','H1','H2','H3','H4','H5','H6','PRE','UL','OL','TABLE','BLOCKQUOTE','DIV','SECTION','ARTICLE','DETAILS','HR']);
    async function blocks(parent, width = WIDTH, indent = 0, baseStyle = {}) {
      const result = []; let pending = [];
      const flush = async () => { if (pending.some(n => n.textContent.trim() || n.nodeType === Node.ELEMENT_NODE && n.tagName === 'IMG')) result.push(paragraph(await inlines(pending, baseStyle, width), { indent: { left: indent }, ...(baseStyle.size ? { spacing: { before: 0, after: 0, line: 240 } } : {}) })); pending = []; };
      for (const node of parent.childNodes) {
        if (node.nodeType !== Node.ELEMENT_NODE || !blockTags.has(node.tagName)) { pending.push(node); continue; }
        await flush(); const tag = node.tagName;
        if (/^H[1-6]$/.test(tag)) {
          const children = await inlines(node.childNodes, {}, width), bookmark = bookmarks.get(node.id);
          result.push(paragraph(bookmark ? [new d.Bookmark({ id: bookmark, children })] : children, { heading: d.HeadingLevel['HEADING_' + tag.slice(1)] }));
        } else if (tag === 'P') {
          result.push(paragraph(await inlines(node.childNodes, node.classList.contains('mermaid-error') || node.classList.contains('pdf-image-error') ? { ...baseStyle, color: 'A11A1A' } : baseStyle, width), { indent: { left: indent }, ...(baseStyle.size ? { spacing: { before: 0, after: 0, line: 240 } } : {}) }));
        } else if (tag === 'PRE') {
          // One paragraph per source line allows arbitrary-length fences to paginate normally.
          const lines = node.textContent.replace(/\r/g, '').replace(/\n$/, '').split('\n');
          for (const line of lines) result.push(paragraph([textRun(line || ' ', { font: 'Consolas', size: 18 })], { style: 'CodeBlock', indent: { left: indent }, wordWrap: true }));
        } else if (tag === 'UL' || tag === 'OL') result.push(...await list(node, 0, width, indent));
        else if (tag === 'TABLE') { result.push(await table(node, width)); result.push(paragraph([], { spacing: { after: 100 } })); }
        else if (tag === 'BLOCKQUOTE') result.push(...await blocks(node, Math.max(100, width - 20), indent + 240));
        else if (tag === 'HR') result.push(paragraph([], { border: { bottom: { style: d.BorderStyle.SINGLE, color: 'D9D9D9', size: 4 } }, spacing: { before: 140, after: 140 } }));
        else if (node.matches('.mermaid-diagram[data-render-state=ready]')) {
          result.push(paragraph([await imageRun(node.querySelector('.mermaid-graphic svg'), width, true)], { alignment: d.AlignmentType.CENTER, spacing: { before: 100, after: 180 } }));
        } else result.push(...await blocks(node, width, indent));
      }
      await flush(); return result;
    }
    const children = await blocks(body);
    const heading = size => ({ run: { font: 'Arial', size, bold: true, color: '000000' }, paragraph: { spacing: { before: 220, after: 120 }, keepNext: true, keepLines: true } });
    const wordDocument = new d.Document({ title: options.title || 'Markdown document', creator: 'Markdown_with_Mermaid', lastModifiedBy: 'Markdown_with_Mermaid',
      styles: { default: { document: { run: { font: 'Arial', size: 22, color: '000000' }, paragraph: { spacing: { after: 140, line: 276 }, widowControl: true } },
        heading1: heading(38), heading2: heading(30), heading3: heading(26), heading4: heading(24), heading5: heading(22), heading6: heading(22) },
        paragraphStyles: [{ id: 'CodeBlock', name: 'Code block', basedOn: 'Normal', run: { font: 'Consolas', size: 18, color: '24292F' }, paragraph: { spacing: { before: 0, after: 0, line: 240 }, keepNext: false, keepLines: false, shading: { fill: 'F6F8FA' } } }] },
      numbering: { config: numbering },
      sections: [{ properties: { page: { size: { width: 12240, height: 15840 }, margin: { top: 1008, bottom: 1008, left: 1008, right: 1008 } } }, children: children.length ? children : [paragraph()] }] });
    return { blob: await d.Packer.toBlob(wordDocument), missing };
  };
})();
