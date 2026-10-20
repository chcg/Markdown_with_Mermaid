# License review

Reviewed on 21 October 2026 against the pinned files shipped with the local
1.4.0 candidate. This is a source and notice audit, not a legal opinion.

## Decision

Project-authored code and documentation may remain under MIT. The author can
license the rights they own; this does not replace the licenses of copied code,
SDK headers, bundled libraries or the loader DLL. Retain their copyright notices,
license texts and applicable redistribution conditions.

**An unconditional claim that the complete plugin is MIT-only is not cleared.**
The build includes GPL-3.0-or-later Notepad++ SDK headers. Their effect on the
compiled plugin and its distribution needs resolution before publication.
The existing MIT LICENSE is retained for project-authored material; no third-party
file has been relicensed by this review.

## Components checked

| Component | Pinned terms / distribution action |
| --- | --- |
| NppMarkdownPanel, Markdown View, VS Code CSS/slug logic, markdown-it, Mermaid, docx | MIT; retain original copyright and permission notices. |
| highlight.js and BSD dependencies | BSD-2-Clause/BSD-3-Clause as supplied; retain notices/disclaimers and respect endorsement restrictions. |
| ISC dependencies | Retain copyright and permission notices. |
| DOMPurify | Choose the Apache-2.0 branch of its MPL-2.0 OR Apache-2.0 license; retain the Apache text and notices. |
| JSZip bundled in docx | Choose its MIT branch, rather than the alternative GPL branch; retain supplied notices. |
| pako bundled in docx | MIT AND Zlib: retain both sets of applicable notices and modification/origin conditions. |
| argparse bundled in Mermaid | Python-2.0; retain the PSF text and notices collected from the pinned package. |
| Scintilla interface headers | Retain the permissive Scintilla copyright/license notice. |
| WebView2 SDK/loader | The pinned SDK's LICENSE.txt permits source/binary redistribution subject to its notice and non-endorsement conditions. Its LICENSE.txt and NOTICE.txt are retained. This does not license the separately installed browser Runtime under MIT. |
| Zig/MinGW/LLVM runtime code | Retain the shipped Zig, MinGW, libc++, libc++abi and libunwind notices; LLVM files include their LLVM exception. The MinGW overview contains exceptions for separately marked files, so it is not a claim that every toolchain file has one license. |
| Notepad++ SDK headers | GPL-3.0-or-later; keep their headers and GPL text. They are not MIT files. See the unresolved issue below. |

The JavaScript inventory contains **123 entries**, including conservative notices
for supporting packages; it is not a claim that 123 runtime packages are all
loaded separately. Terms recorded in `vendor/bundled-dependencies.json`:
78 MIT, 34 ISC, 6 BSD-3-Clause, and one each of BSD-2-Clause, Python-2.0,
MPL-2.0 OR Apache-2.0, MIT OR GPL-3.0-or-later, and MIT AND Zlib.
Full collected texts are in `vendor/NOTICE-BundledDependencies.txt`. Original
inline bundle notices remain intact. The inventory explicitly identifies where
an exact bundled revision has not been established.

## Notepad++ SDK issue

`src/plugin.cpp` includes `PluginInterface.h` and `Docking.h`; the SDK also contains
GPL-marked `Notepad_plus_msgs.h`. This is a build-time
use of GPL files, not merely a separate license text placed beside the plugin.
No plugin-specific exception was found in the pinned license/header texts.

MIT is GPL-compatible, but compatibility does not turn GPL material into MIT.
The FSF's plugin guidance distinguishes separate works from combined programs
based on how they communicate; it calls for GPL-compatible licensing and
GPL-compliant source distribution where they form a combined work. This is
guidance from the license steward, not a court ruling about this plugin.
[GNU FAQ: plugins](https://www.gnu.org/licenses/gpl-faq.en.html#GPLAndPlugins),
[GNU FAQ: compatibility](https://www.gnu.org/licenses/gpl-faq.en.html#WhatDoesCompatMean).

Before a public release, obtain a qualified assessment or an applicable exception
from the relevant rightsholders, or establish a distribution/source arrangement
that complies with the GPL requirements that apply. Replacing SDK headers is not
assumed to settle the host/plugin question by itself. Publishing source alone is
also not treated here as a complete compliance assessment. Do not remove the GPL
notices or promise recipients MIT-only rights over third-party material.

## Retained notices and release checks

`vendor/NOTICE-ThirdParty.txt` maps components to their license files. Release ZIPs
copy those files, the bundled dependency inventory and provenance into `licenses`.
The root MIT LICENSE remains present. The WebView2 license/notice copies match the
pinned SDK. The document review does not change library versions or native code.

The remaining GPL distribution question is an explicit release condition,
separate from the functional test results. The current local candidate is not
approved by this document for an MIT-only public distribution.

Primary terms: [MIT](https://opensource.org/license/mit),
[Apache-2.0, section 4](https://www.apache.org/licenses/LICENSE-2.0),
[WebView2 distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution).
The pinned license files shipped with each component take precedence over a
general description or a later upstream release.
