# User guide

[Back to the overview](../README.md) · [Русский](USAGE.ru.md)

## Get started

Follow the [installation steps](../README.md#installation), then open
[the sample document](screenshots/demo.en.md). The preview opens automatically
for Markdown files. Use **Ctrl+Alt+M** to show or hide it.

## Update or remove

Save your documents and close all Notepad++ windows before replacing the plugin
folder with the contents of a newer ZIP of the same architecture. Keep your
configuration folder: it stores your preferences separately from the DLL.
For a manual removal, close Notepad++ and remove only its
`plugins/Markdown_with_Mermaid` folder. Your documents and saved preferences remain.
The plugin is not yet in the public Plugins Admin catalog.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No plugin menu entry | The ZIP architecture must match Notepad++; the DLL belongs directly inside `plugins/Markdown_with_Mermaid`, not another nested folder. Restart the editor. |
| Preview cannot start | Install Microsoft Edge WebView2 Runtime and keep the ZIP's `assets` and loader subfolders together with the DLL. |
| An export contains the whole document | Select the desired content in Preview or the editor immediately before exporting. A filename selection is not a document selection. |
| An export contains only a fragment | Clear selections in both Preview and the editor before exporting the whole document. |
| A diagram displays an error | Check the Mermaid source shown below it; unsupported diagram extensions are listed under Limits. |
| Auto-show or scroll sync is off | Enable the corresponding item in the plugin menu. Saved preferences take precedence over first-run defaults. |

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

## Panel, navigation and theme

The Markdown/Mermaid toolbar icon and **Plugins → Markdown_with_Mermaid** open or
hide the dockable panel. Preview updates approximately **400 ms** after editing,
including unsaved changes. Double-clicking preview content moves its source line to the top of the editor
and aligns the Preview target below its toolbar. In long code blocks and multiline
paragraphs, the clicked physical source line is used instead of the block start. Scroll synchronization runs from the editor to Preview, including positions
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

## HTML export

**HTML** exports the rendered document with static Mermaid SVG and embedded
styles. Its tooltip is **Export to HTML**. Mermaid/JavaScript is not required to
view the exported diagrams. HTML exports the selection in the last active
Preview/editor area; without a selection it exports the whole document.
Selected editor Markdown is rendered separately, including Mermaid. The snapshot
is frozen before the save dialog; preview theme and zoom remain unchanged.
Local images retain absolute references to their original files; external images
can still require network access. Keep the referenced resources available.

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
an existing destination file intact. Details: [PDF export](PDF-EXPORT.md).

## DOCX export

**DOCX** or **Export DOCX...** creates a Word document using bundled **docx 9.12.0**,
loaded only when needed. Tooltip: **Export to DOCX**. Full-document and selection
rules match PDF. Text, headings, lists, tables and links remain editable;
Mermaid is embedded as SVG with a PNG fallback.

DOCX uses **Letter**, **0.7 inch** margins and a light theme. Already displayed
images reuse preview bytes. Images not yet available can use a background native
loader without browser CORS restrictions. GitHub image-page URLs are converted
to direct image URLs only for fetching the image. Unavailable resources are
reported in the document. Details: [DOCX export](DOCX-EXPORT.md).

HTML `width`/`height` image dimensions are honored by preview and export; oversized
images shrink proportionally to the available width. Examples:
`Mermaid-examples.md` in the plugin ZIP and `Mermaid-examples.md` in source.

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
see [data and privacy](DATA-PRIVACY.md). The source-tree `install.ps1` is a development helper that explicitly
keeps working data in the workspace `runtime` directory.

## Limits and document security

Markdown buffers are limited to **32 MiB**; Mermaid to **50,000 characters** and
**500 edges** per diagram. Document HTML is sanitized with DOMPurify and a CSP;
document scripts, Mermaid callbacks and SVG links are disabled. Diagram styles
are isolated. External Markdown images may use the network.

KaTeX, external ELK/ZenUML plugins and VS Code Extension API integration are not
provided. PDF/DOCX page settings are fixed. Navigation is session-only and has
no history dropdown or persistent journal.
