# Toolbar icon

Selected launcher: **Markdown with Mermaid badge**, design option 2, approved
by the user on 2026-10-18. `markdown-mermaid.svg` is the production vector source:
a dark Markdown badge with a white M/down arrow and a pink Mermaid emblem.
The Markdown shapes and composition were drawn for this project.

The Mermaid glyph comes from the official Mermaid logo, downloaded 2026-10-18:
https://raw.githubusercontent.com/mermaid-js/mermaid/develop/docs/public/favicon.svg

Project: https://github.com/mermaid-js/mermaid (MIT; see `vendor/LICENSE-Mermaid.txt`).
The original `mermaid.svg` is preserved separately for attribution and design
references. The glyph in the launcher is resized; the pink is Mermaid #ff3670.
`tools/build-toolbar-icon.cjs` converts `markdown-mermaid.svg` to Windows ICO
(16/20/24/32/48/64 px), classic 16 px BMP and a 64 px preview using sharp.
Icons are embedded in the DLL; existing resource filenames stay compatible.
