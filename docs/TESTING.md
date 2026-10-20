# Testing and compatibility

Release 1.4.0 validation is in progress. Windows 10 build 19045 is the available
test platform. Windows 11 is a target platform but has not been tested separately.
The first release may ship with that limitation explicitly stated; this does not
replace the remaining Windows 10 and native Notepad++ acceptance checks.
The supported Notepad++ minimum will be set after API review and actual startup
checks against the current release and an earlier candidate minimum.

## Local 1.4.0 candidate results

Status as of 21 October 2026, on Windows 10 build 19045 with WebView2 Runtime
155.0.4283.62 and native Notepad++ 8.9.8.1:

- Both x64 and x86 passed **251 full integration checks** and **127 focused
  checks** each, with no failures; all six output checkers passed. These use real
  WebView2 with a modeled editor API. The focused runs overlap the full suite;
  the counts are not a total of unique tests.
- Native x86 functional acceptance and the final settings/width restart check
  passed with the owner. Native x64 base functions were checked earlier; the
  latest defaults, About, English hints, themes and zoom also passed. Final x64
  filename-copy, diagram-contrast, interior-scroll, docking and restart checks
  remain pending. The owner waived repetition of the editor-selection boundary
  test; that waiver is not a new manual pass.
- The bilingual README review, six English example diagrams in both themes and
  five refreshed screenshots passed documentation review. Documentation-only
  packaging reuses the tested DLLs and checks source/assets against their tested
  snapshot; it does not claim another full integration run.
- The complete plugin's GPL SDK distribution question remains open. See
  [license review](LICENSING.md); successful tests do not settle licensing.

Navigation checks cover A → B → C, scroll restoration, unsaved open buffers,
outline and inline section jumps within one file, cross-document fragments,
forward-branch replacement, failed opening/retry,
light/dark Mermaid, host theme notifications and toolbar wrapping at 320 px.
History is capped at 100 session visits; refreshed content does not add visits.
The left-hand control order is Outline → Back → Forward → Copy.

An additional lifecycle suite exercises inactive-session collection, live leases,
browser processes, junction rejection, settings/document preservation and an
unavailable Runtime using the real loader with an empty process-local browser
folder. It checks failure cleanup for PDF/DOCX without uninstalling the machine
Runtime. Run it **without administrator rights**:

```powershell
./test.ps1 -Architecture x64 -Suite Lifecycle
./test.ps1 -Architecture x86 -Suite Lifecycle
```

The earlier candidate passed 14 lifecycle checks per architecture. Private
fixtures also exercised manual installation, binary replacement, restart,
removal and reinstallation in portable Notepad++ 8.4.1 x64/x86, preserving
settings and a synthetic document; a browser crash was found during that earlier
work. Those historical results do not establish a supported minimum or close
the final candidate's installation/update/removal acceptance. The owner has
confirmed working Preview/diagrams in Program Files with the older zoom-fix
build. The latest x64 candidate has since been installed there and all 63 files
verified; its post-installation GUI check remains pending.
Merely seeing a loaded DLL does not count as a rendering pass.

The Codex sandbox can stall WebView2 script callbacks even in an unchanged prior
test binary. Normal-runtime regression results must be distinguished from the
non-admin token checks. A callback deadline allows cold startup and CPU contention;
timeouts remain failures. See [data and privacy](DATA-PRIVACY.md) for collection rules.

An isolated build from the curated public source succeeded for both architectures
with the pinned toolchain. All PE sections matched the workspace DLLs; file
headers differed, so byte-for-byte reproducible binaries are not claimed.
Release ZIP/version checks passed; a Development build was rejected by packaging.
The supplied GitHub Actions template is not active yet: publishing credentials
lack workflow permission. Remote build CI remains pending and does not replace
desktop acceptance. See [building](BUILDING.md) for activation instructions.

## Automated checks

Build Release mode first to generate version/resource headers. The integration
host uses real WebView2 and a small Notepad++/Scintilla API model. Run it in a
Windows desktop session with WebView2 Runtime available:

```powershell
./test.ps1 -Architecture x64
./test.ps1 -Architecture x86
```

The source tree includes the fixtures needed by these standard runs. The host
checks preview rendering, Mermaid, toolbar resources, themes, zoom, formatted
clipboard data, editor focus, selections and export output. It also exercises
the Notepad++ config directory, Unicode/spaces, portable separation and explicit
absolute/relative data-directory overrides.

Python is used only for developer-side inspection of produced documents. A tested
QA environment uses lxml 6.1.1, Pillow 12.3.0, pypdf 6.10.0 and pdfplumber 0.11.9:

```powershell
python tests/check-pdf.py
python tests/check-docx.py
python tests/check-image-sizes.py
python tests/check-selection.py
python tests/check-edge-labels.py
python tests/check-docx-images.py
```

Node.js is used by `tests/check-binaries.cjs`. Word, LibreOffice and these QA tools
are not required on end-user machines. Rendered-page inspection can supplement
the structural/geometry checks when layout changes.

## HTTP image cache fixtures

In a separate terminal, run `python tests/image-server.py`. It listens only on
127.0.0.1, chooses a port and records the URL in `build/image-server.json`.
Before running each architecture's integration test:

```powershell
$env:MWM_TEST_IMAGE_BASE = (Get-Content build/image-server.json -Raw | ConvertFrom-Json).url
```

The fixture includes an image that succeeds only once. Preview, formatted copy
and subsequent exports must reuse it; the request log must show one GET for
each test image and no forwarded Authorization/Cookie headers.
Stop the fixture server after testing.

`tests/check-docx-images.py` checks these outputs and optionally an externally
supplied README export. `test.ps1` accepts optional `-PdfRegressionMarkdown` and
`-DocxImageMarkdown` paths for additional documents. Such documents are not
prerequisites and are not included with the public source tree.

Run `python tests/check-docx-images.py` after the HTTP fixture runs. Add
`--include-readme` only when both optional README exports were freshly generated;
old exports left in a development directory are not part of the standard check.

Some constrained environments prevent WebView2 JavaScript callbacks. Use a normal
desktop process for those tests. GPU-related issues can be investigated with
process-local `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu`; do not treat
an unexecuted or skipped integration test as a pass.

## Actual Notepad++ and distribution

The API model does not validate the real plugin manager. Before final release,
check both architectures in clean installed/portable Notepad++ environments:
DLL loading, toolbar/menu, preview, selection focus, copy, export, restart and
settings placement. Include a normal user account with a read-only installation
directory. Record the exact Windows, Notepad++ and Runtime versions.

For Plugins Admin, use the official debug Notepad++/WinGUp procedure with a local
catalog, then verify the public download URLs, install, update and removal.
The ZIP/resource checker is useful preparation, but does not replace that test.

Anonymous download validation and the official catalog validator have not run
against published binary assets. The supplied build CI template is separate
from native acceptance and distribution checks; remote execution remains pending.
The minimum supported
Notepad++ version remains unconfirmed; Windows 11 remains untested. The local
candidate is prepared for review, not approved for publication.
