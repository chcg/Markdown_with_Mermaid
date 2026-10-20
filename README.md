# Markdown_with_Mermaid

[Русская документация](README.ru.md)

Markdown Preview for Notepad++ with **VS Code-inspired rendering**, offline
**Mermaid 11.16.1**, formatted copy and built-in HTML, PDF and DOCX export.
Native Windows plugin for **x64 and x86**.

Author: [Artyom Nikiforov (rhyots)](https://github.com/rhyots).
**1.4.0 is a prerelease candidate.**
Native Notepad++ acceptance is in progress: [validation status](docs/TESTING.md).

<!-- section:advantages -->
## Features

- **Familiar preview:** markdown-it and styling adapted from VS Code, with tables,
  tasks, highlighted code, heading anchors and GitHub alerts.
- **Offline diagrams:** Mermaid 11.16.1 is included; diagrams need no CDN.
- **Fast work inside Notepad++:** dockable preview, unsaved changes, outline and
  source-line navigation without switching editors.
- **Automatic opening for Markdown:** opening an `.md` file shows Preview;
  **Auto-show for Markdown files** is enabled by default on first use.
- **Source-scroll synchronization:** Preview follows scrolling inside code blocks,
  paragraphs and wrapped source lines. **Synchronize with editor scrolling** is
  enabled by default on first use; both options can be changed in the plugin menu.
- **Document and section navigation:** Back/Forward restore Markdown documents,
  outline jumps and scroll positions while preserving modified open buffers.
- **Formatted copy:** copy the selected preview fragment or the whole document,
  including tables and images, into applications that accept HTML.
- **Three export formats:** HTML, PDF and editable DOCX; all three can export a
  selection. No Office, LibreOffice or Pandoc installation is needed.
- **Reuse displayed images:** PDF/DOCX and formatted copy reuse available preview
  image bytes; exports do not download successfully cached images again.

<!-- section:vscode -->
## Preview behavior inspired by VS Code

The plugin follows the familiar VS Code Markdown Preview approach: **markdown-it**
parses the document, adapted **VS Code Markdown CSS** styles it, and adapted
heading-slug logic handles Unicode and duplicate headings. YAML front matter is
shown as highlighted code, corresponding to VS Code's `codeBlock` mode.

Supported content includes tables, task lists, strikethrough, fenced code with
syntax highlighting, relative images and GitHub alerts. Mermaid blocks render
as diagrams with isolated styling; edge-label backgrounds keep lines from
showing through the text. A broken diagram shows its source and an error while
other diagrams continue rendering.

This is a native Notepad++ implementation. VS Code extensions and every VS Code
preview setting are outside its compatibility scope; pixel-identical rendering
is not promised.

<!-- section:requirements -->
## Requirements

Use the plugin architecture matching **Notepad++**, regardless of Windows architecture.
Targets: **Windows 10 and 11**. **Windows 11 has not been independently tested.**
The minimum supported Notepad++ version is still being established during 1.4.0
acceptance; see [testing](docs/TESTING.md).

[Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)
is required. The archive includes its matching loader DLL. Preview and export
require no Word, LibreOffice, Pandoc, Node.js, .NET or separate Visual C++
Redistributable. External images and external links may require internet access.
The candidate DLLs do not have a developer-owned code signature.

<!-- section:installation -->
## Installation

1. Download the x64 or x86 prerelease plugin ZIP from
   [Releases](https://github.com/rhyots/Markdown_with_Mermaid/releases).
2. Save your work and close Notepad++.
3. Create `plugins/Markdown_with_Mermaid` inside the Notepad++ installation directory.
4. Extract the **contents** of the ZIP into that folder, keeping every subfolder.
5. Start Notepad++, open Markdown and press **Ctrl+Alt+M**.

```text
plugins/Markdown_with_Mermaid/
  Markdown_with_Mermaid.dll
  assets/
  x64/WebView2Loader.dll   (or x86/WebView2Loader.dll)
  licenses/
  docs/
```

Copying into Program Files may require administrator rights. Normal plugin use
stores data in the configuration area supplied by Notepad++. Plugins Admin
listing is a separate future step; this candidate has not been submitted.

<!-- section:navigation -->
## Panel, navigation and theme

The Markdown/Mermaid toolbar icon and **Plugins → Markdown_with_Mermaid** open or
hide the dockable panel. Preview updates approximately **400 ms** after editing,
including unsaved changes. Double-clicking preview content goes to the source
line. Scroll synchronization runs from the editor to Preview, including positions
inside long blocks and wrapped source lines. Editor-scroll synchronization and automatic opening for `.md`, `.markdown`,
`.mdown`, `.mkd` and `.mkdn` are enabled by default on first use and can be
disabled in the plugin menu. Saved preferences are preserved. The show/hide
checkmark reflects whether the panel is currently visible.

On first use the right dock opens at a readable width, adjusted to the window
and DPI. Drag its divider to resize; your saved docking layout is retained.

The left-hand controls are ordered **Outline → Back → Forward → Copy**.
Local Markdown links open in Notepad++; links to a section in another document
also scroll to that section. **Back** and **Forward** restore documents and saved
scroll positions. The session keeps up to **100 visits**, including Markdown tab
changes shown in preview, outline jumps and in-document section links.
Repeated rendering does not create duplicates. A new
visit after Back replaces the old forward branch. A failed history opening
keeps the current document and shows a message. History is not saved across restarts.
External links do not create history visits.

The **light/dark button** beside zoom changes preview, syntax highlighting and
Mermaid together. The initial theme follows Notepad++; the manual choice survives
file transitions. A subsequent Notepad++ theme change updates preview again.
The button does not change the editor's theme, and its choice is not saved across restarts.

The toolbar also provides outline and zoom from **10% to 6400%**, custom
percentages, actual size, fit width and fit content. Controls wrap in a narrow panel.
**Ctrl+wheel** zooms the document while the toolbar keeps its size; the percentage
updates with it. **Ctrl+0** returns to actual size.
**Ctrl+F** opens preview search. Hovering a link shows its type and destination,
while preserving its authored title. Local Markdown links have a document icon;
external HTTP/HTTPS links have an outgoing-arrow icon. Anchors and image-only
badges keep their appearance. External HTTP/HTTPS/mailto links open in the system application.

<!-- section:copy -->
## Formatted copy

The **Copy** icon and **Ctrl+C in Preview** copy the selected fragment with
formatting; without selection they copy the rendered document. The clipboard
contains HTML and Unicode text. Lists, links and tables retain their structure;
images are embedded and Mermaid becomes PNG. The receiving application's HTML
support determines the pasted appearance.

**Ctrl+C and Notepad++ Copy with editor focus** keep ordinary source-text copying.

Select the filename in the Preview header and press **Ctrl+C** to copy just
the selected name or part of it.
The filename's right-click **Copy** menu also copies plain text. A triple-click
selects the whole name; it does not copy until you choose Copy or press Ctrl+C.
Toolbar text fields retain their normal copy behavior.
Click the file icon beside the name to copy the **complete filename as plain
text**, without selecting it. Its tooltip is **Copy filename as plain text**;
a brief checkmark and **Filename copied** confirm success.

<!-- section:html -->
## HTML export

**HTML** exports the rendered document with static Mermaid SVG and embedded
styles. Its tooltip is **Export to HTML**. Mermaid/JavaScript is not required to
view the exported diagrams. HTML exports the selection in the last active
Preview/editor area; without a selection it exports the whole document.
Selected editor Markdown is rendered separately, including Mermaid. The snapshot
is frozen before the save dialog; preview theme and zoom remain unchanged.
Local images retain absolute references to their original files; external images
can still require network access. Keep the referenced resources available.

<!-- section:pdf -->
## PDF export

**PDF** or **Export PDF...** uses WebView2 **PrintToPdf**. Tooltip: **Export to PDF**.
It exports the document, including unsaved changes, or the selection in the last
active preview/editor area. Preview selection is captured as rendered content;
selected Markdown in the editor is rendered separately. No selection exports the
whole document. The selection is captured before the save dialog opens.

PDF uses **A4**, portrait, **15 mm** margins, a light theme and **100%** export scale;
preview theme and zoom remain unchanged. Large diagrams shrink to the page,
long tables split by rows and long code spans pages. No printer is needed.
Unavailable images and invalid diagrams receive messages. A failed export keeps
an existing destination file intact. Details: [PDF export](docs/PDF-EXPORT.md).

<!-- section:docx -->
## DOCX export

**DOCX** or **Export DOCX...** creates a Word document using bundled **docx 9.12.0**,
loaded only when needed. Tooltip: **Export to DOCX**. Full-document and selection
rules match PDF. Text, headings, lists, tables and links remain editable;
Mermaid is embedded as SVG with a PNG fallback.

DOCX uses **Letter**, **0.7 inch** margins and a light theme. Already displayed
images reuse preview bytes. Images not yet available can use a background native
loader without browser CORS restrictions. GitHub image-page URLs are converted
to direct image URLs only for fetching the image. Unavailable resources are
reported in the document. Details: [DOCX export](docs/DOCX-EXPORT.md).

HTML `width`/`height` image dimensions are honored by preview and export; oversized
images shrink proportionally to the available width. Examples:
`Mermaid-examples.md` in the plugin ZIP and `docs/Mermaid-examples.md` in source.

<!-- section:settings -->
## Settings and working files

The plugin asks Notepad++ for its plugin configuration directory and uses a
`Markdown_with_Mermaid` subfolder, for installed and portable layouts. Settings,
WebView2 profiles and temporary preview files go there. Write access beside
the installed DLL is not required for normal operation.

An optional `Markdown_with_Mermaid.ini` beside the DLL can override the location:

```ini
[Paths]
DataDirectory=D:\MarkdownPreviewData
```

A relative override resolves beside the DLL; choose a writable folder.
`settings.ini` in that data folder stores automatic-opening and scroll-sync
options. Temporary preview HTML is removed on shutdown. At the next Preview
startup, marked inactive browser profiles and orphaned HTML are collected;
active, uncertain and unmarked legacy sessions are retained safely. Updates and
plugin removal preserve settings. Temporary files may contain document text:
see [data and privacy](docs/DATA-PRIVACY.md). The source-tree `install.ps1` is a development helper that explicitly
keeps working data in the workspace `runtime` directory.

<!-- section:limits -->
## Limits and document security

Markdown buffers are limited to **32 MiB**; Mermaid to **50,000 characters** and
**500 edges** per diagram. Document HTML is sanitized with DOMPurify and a CSP;
document scripts, Mermaid callbacks and SVG links are disabled. Diagram styles
are isolated. External Markdown images may use the network.

KaTeX, external ELK/ZenUML plugins and VS Code Extension API integration are not
provided. PDF/DOCX page settings are fixed. Navigation is session-only and has
no history dropdown or persistent journal.

<!-- section:screenshots -->
## Preview screenshots

Captured from the real WebView2 integration host, not a completed native-editor
acceptance test. This README shows an English sample; the Russian README shows
the equivalent Russian sample to demonstrate Unicode support.
[Capture details](docs/screenshots/README.md).

<details>
<summary>Light and dark preview</summary>

![Light preview](docs/screenshots/preview-light.en.png)

![Dark preview](docs/screenshots/preview-dark.en.png)

</details>

<!-- section:development -->
## Build and test

Pinned tools: **Zig 0.13.0**, **WebView2 SDK 1.0.2903.40**. Bundled engines:
**Mermaid 11.16.1**, **docx 9.12.0**. Build both architectures on Windows:

```powershell
./bootstrap.ps1
./build.ps1 -Profile Release
./test.ps1 -Architecture x64
./test.ps1 -Architecture x86
```

Tools stay in `tools`; caches and generated files in `build`; packages in `dist`.
The tests use real WebView2 with a modeled Notepad++ API. Native editor acceptance
is separate. QA tools are developer requirements, not end-user dependencies.
Details: [building](docs/BUILDING.md), [testing](docs/TESTING.md),
[changelog](CHANGELOG.md). The bilingual README checker verifies section structure,
links and technical examples; translations also require a semantic review.

<!-- section:support -->
## Support, author and licenses

Use [GitHub Issues](https://github.com/rhyots/Markdown_with_Mermaid/issues). Include Notepad++ version/architecture, Windows version and a small
reproducible example.

Project-authored code: **MIT**, Copyright (c) 2026 **Artyom Nikiforov (rhyots)**.
[Author profile](https://github.com/rhyots) · [LICENSE](LICENSE).
Third-party files retain their own terms, including Notepad++ SDK headers.
The source keeps notices in `vendor`; release ZIPs include them in `licenses`.
Origins include NppMarkdownPanel, Markdown View, VS Code, markdown-it, Mermaid,
DOMPurify, highlight.js, docx, WebView2 and Scintilla.

MIT applies to project-authored code; it does not relicense dependencies.
The GPL-licensed Notepad++ SDK headers require a distribution review before a
claim that the complete plugin is MIT-only. See the [license review](docs/LICENSING.md)
for the component terms, retained notices and this unresolved release condition.
