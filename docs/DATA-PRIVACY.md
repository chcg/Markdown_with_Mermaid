# Data, cache and privacy

The plugin uses the configuration directory returned by Notepad++, with a
`Markdown_with_Mermaid` subdirectory. An explicit `DataDirectory` override is
supported. Installation files and user data are separate.

## Persistent data

`settings.ini` stores the automatic-preview and editor-scroll options. Updating
or removing the plugin files does not intentionally remove these settings.
Your Markdown files, exported HTML/PDF/DOCX and ordinary editor configuration
are never targets of cache collection. Navigation history is kept in memory.

## Temporary sessions

Each editor process gets a browser profile named `webview2-<pid>`; an existing
directory is never silently reused and receives a random suffix instead.
Ownership metadata and an exclusive lease live separately in
`session-info/<profile-name>`. Metadata records process IDs, creation times and
incomplete initialization, without document contents or authentication data.
If Windows rejects controller creation, `last-webview-error.txt` contains its
short diagnostic and HRESULT; successful controller creation clears it.
Preview/export HTML is stored under `preview`, with the owning session name
prefixed to each new filename. Normal destruction removes the individual HTML.
Hiding the panel keeps its WebView2 instance available for quick reopening.

At the next Preview startup, the plugin collects marked sessions whose owner
and recorded browser processes and discoverable child processes have exited. Process creation times protect
against PID reuse. It removes that session's browser profile, matching orphaned
HTML and ownership metadata. This is deferred collection, not a promise to erase
all files immediately when the editor exits. No background cleanup executable
or scheduled task is installed.

Active leases, live or inaccessible processes, unknown/incomplete browser
initialization, invalid metadata and reparse points are retained conservatively.
The collector checks the absolute deletion boundary and never follows junctions
or symbolic links. Locked files can remain for a later startup. A crash during
unknown browser initialization may require manual cleanup.

Unmarked profiles and HTML left by older versions are not automatically removed:
their ownership cannot be established safely. For explicit manual cleanup, first
close all Notepad++ instances using this plugin and wait for their WebView2
processes to exit. Within this plugin's **data directory only**, temporary
`webview2-*`, `session-info` and `preview` data may then be removed. Preserve
`settings.ini`, user documents and exports unless you independently intend to
delete them. Do not remove another application's WebView2 data.

## Document privacy and network access

Temporary HTML can contain the Markdown source in plaintext. Browser storage
may contain fetched images or document-related resources. Treat the data
directory as private and exclude it from public repositories and shared backups.
This plugin does not encrypt these files or promise forensic secure deletion.

The plugin's own code does not upload Markdown documents or implement telemetry.
Bundled Markdown/Mermaid/DOCX code runs locally. Remote images embedded in a
document can contact their hosting servers; a fallback image loader may also
fetch missing resources. Clicking an external link opens its destination.
WebView2 Runtime is a Microsoft component with its own update and platform
behavior. Offline Mermaid does not make every external document resource offline.

The public source exporter omits private request history, context, research,
profiles, caches, downloaded comparison projects and personal documents. Release
screenshots use project test fixtures. Dependency copyright notices and public
upstream contact details are retained as required by their respective licenses.
