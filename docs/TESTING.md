# Testing and compatibility

[Overview](../README.md) · [Build instructions](BUILDING.md)

Status as of **21 October 2026** for **1.4.1**. Both Release packages passed
archive/version/architecture checks and **26 fresh startup/API/runtime checks
per architecture** in real Notepad++ 8.4.1, with Preview payload creation,
WebView2 connection and clean shutdown verified. SHA-256 checksums accompany
the release downloads.

The broader functional results below were obtained for **1.4.0 RC2, local.2**;
its runtime implementation and bundled dependencies are carried into 1.4.1
unchanged. The full suite is not claimed as a new 1.4.1 run.
The test platform is Windows 10 build 19045 with WebView2 Runtime 155.0.4283.62.

## Accepted runtime baseline

| Area | x64 | x86 | Scope |
| --- | --- | --- | --- |
| Full integration suite | 291 passed | 291 passed | Real WebView2 with a modeled Notepad++/Scintilla API. |
| Focused regressions | 82 passed | 82 passed | Interior-line navigation and diagram outlines; overlaps the full suite. |
| Output inspection | Passed | Passed | Six checkers cover PDF, DOCX, selections, image sizes, edge labels and image reuse. |
| Native functional acceptance | Passed | Passed | Owner-operated Notepad++ 8.9.8.1: Preview, copy/export, themes, zoom, navigation, docking and settings. |
| Minimum-version smoke | Passed | Passed | Real Notepad++ 8.4.1; 26 startup/API/runtime checks per architecture at clean launch and restart. |
| Local Plugins Admin lifecycle | Passed | Passed | Install, update, remove and reinstall using official debug tools and a private local catalog. |

The declared minimum is **Notepad++ 8.4.1**. Its automated smoke checks cover
the real DLL, native API interactions, commands, configuration, generated Preview
payload and browser connection. They do not claim visual/export acceptance on
8.4.1; native visual checks were performed on 8.9.8.1.

The latest short native checks covered filename copying, Mermaid text/arrows/
borders in both themes, synchronization inside long and wrapped blocks, exact
double-click navigation, docking, width and preferences after restart.
The owner waived repeating the editor-selection boundary test in the final
short pass; this is not counted as another manual pass.

## Plugins Admin lifecycle

Both architectures completed the actual Plugins Admin flow in separate portable
Notepad++ 8.9.8.1 debug copies with official debug WinGUp 5.4.3:
install a controlled older baseline → update to 1.4.0 → remove → reinstall 1.4.0.
All **64 files** in each current test package matched the ZIP. Settings, a synthetic
document and a data sentinel remained byte-identical. Removal of the plugin folder
and its later restoration were independently observed.

The old 1.3.2 fixture lacked Windows VERSIONINFO. Test copies received a truthful
1.3.2.0 resource so Plugins Admin could compare numeric versions; non-resource
PE sections stayed unchanged. A test-only data-directory override aligned its
configuration path. This establishes a controlled 1.3.2.0 → 1.4.0.0 update,
not an unchanged historical-release upgrade.

The catalog and downloads were local. The signed public catalog and the future
release URLs were not exercised. After each final exit, 31 seconds of observation
found no related Notepad++, WinGUp or WebView2 processes and no new dumps.
Successful UI steps were confirmed by the owner; file/hash/process checks were
performed separately. They are not presented as automated screenshot proof.

## Remaining Plugins Admin checks

- Run the official nppPluginList schema/validator against the final entries and
  check installation using the final public download URLs.
- Submit the x64/x86 catalog entries for maintainer review.

Windows 11 has not been independently tested. There is no ARM64 package or
developer-owned code signature. The supplied GitHub Actions file is an inactive
template; no remote CI pass is claimed. Source builds succeeded on the local
pinned toolchain; byte-for-byte reproducible DLLs are not claimed.

Documentation-only changes do not rerun or supersede the binary acceptance.
New media are captured from the actual viewer in a separate WebView2 host;
[capture details](screenshots/README.md) distinguish them from native editor tests.

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
