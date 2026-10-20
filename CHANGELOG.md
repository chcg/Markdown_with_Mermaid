# Changelog

## 1.4.0 — release candidate

- Copy the complete filename as plain text with the header's file icon button;
  show English tooltips, press feedback and a brief success checkmark.
- Keep Mermaid connector strokes and arrowheads visible on custom container
  backgrounds in both themes, including diagrams with mixed light/dark fills.

- Keep Mermaid container titles, node labels and edge labels readable against
  their actual fills in both themes, while preserving readable author colors.

- Synchronize editor scrolling inside long code blocks, paragraphs and table
  rows, including wrapped source lines and Preview zoom.

- Use English throughout Preview controls, status messages, export results,
  and error diagnostics; preserve authored content and file paths.

- Copy the selected Preview filename as plain text via Ctrl+C or its English
  context menu; triple-click selects the name without copying it.

- Preserve normal Ctrl+C copying for the selected filename in the Preview header.

- Open a new docked preview at a readable, DPI-aware width while preserving
  existing docking layouts and subsequent manual resizing.

- Fix editor-selection exports losing the final byte with modern Scintilla;
  retain single-character selections and complete UTF-8 characters.

- Clarify Markdown preview and Mermaid diagram rendering in About; keep
  third-party attribution in the distributed license and notice files.

- Give every Preview button an English tooltip, including search and diagram
  controls; update theme and diagram hints to match the available action.

- Enable editor-scroll synchronization and Markdown auto-show by default on
  first use; preserve saved preferences and derive panel visibility normally.

- Add HTML export of the Preview or editor selection, with static Mermaid;
  export the full document when there is no selection.

- Fix PDF/DOCX selection export after Notepad++ restores editor focus: preserve
  the Preview selection scope until actual editor input or a document change.

- Fix Ctrl+mouse-wheel zoom: scale preview content without resizing the toolbar,
  keep percentage/presets synchronized and share zoom steps with buttons.
  Route Ctrl+plus/minus and Ctrl+0 through the same document zoom controls.

- Add conservative collection of marked inactive browser profiles and orphaned
  preview HTML, with ownership leases, process creation times and junction checks.
- Improve unavailable WebView2 Runtime errors; test failed PDF/DOCX cleanup.
- Document temporary document data, network access and manual legacy-cache cleanup.

- Add native-style Back/Forward preview navigation with scroll restoration,
  outline/section jumps, cross-document anchors, forward-branch replacement and
  open-buffer preservation. Order controls as Outline, Back, Forward, Copy.
- Restore the preview light/dark toolbar button, including Mermaid and code colors.
- Align English/Russian README content, highlight practical advantages and explain
  the VS Code-inspired preview; add structural/factual documentation checks.
- Prepare x64 and x86 packages for independent distribution and Plugins Admin.
- Put the plugin DLL at ZIP root and include bundled dependency notices.
- Generate Windows VERSIONINFO, About and HTTP User-Agent from VERSION.
- Default data storage to the configuration directory supplied by Notepad++.
- Keep explicit development data-directory overrides and separate Release/Development builds.
- Handle UTF-8-escaped Unicode preview paths when validating the owned page URL.
- Prepare English/Russian user documentation and self-contained test fixtures.

Publication and compatibility validation are still in progress.

## Earlier local versions

- **1.3.2:** selected Markdown toolbar icon with a Mermaid badge.
- **1.3.1:** opaque, readable Mermaid edge labels in preview and exported content.
- **1.3.0:** formatted copy, focus-aware source copying and selection PDF/DOCX export.
- **1.2.2:** honor authored image dimensions in preview and PDF.
- **1.2.x:** DOCX export and reuse of images already loaded in preview.
- **1.1.x:** WebView2 PDF export and long-code pagination fixes.
- **1.0.x:** native Notepad++ Markdown preview with offline Mermaid 11.16.1.
