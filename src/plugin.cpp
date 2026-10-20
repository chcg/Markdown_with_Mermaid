// Markdown_with_Mermaid. Docking/update behavior ported from NppMarkdownPanel (MIT).
// See vendor/NOTICE-ThirdParty.txt and vendor/LICENSE-NppMarkdownPanel.txt.
#include "webview.cpp"
#include "PluginInterface.h"
#include "Docking.h"
#include "resource.h"
#include <cctype>

static NppData npp;
static FuncItem commands[11];
static ShortcutKey toggleShortcut{true, true, false, 'M'};
static std::shared_ptr<Viewer> panel;
static tTbData docking;
static bool automatic = false, shuttingDown = false;
static toolbarIconsWithDarkMode toolbarImages{};
static void activateEditorSelection() {
    if (!panel) return;
    panel->editorSelectionActive = true;
    panel->script(L"window.markdownView.setEditorSelectionActive(true)");
}
// Notepad++ can restore Scintilla focus while the application is reactivated,
// before delivering a click to WebView2. That focus notification alone does not
// mean the user abandoned the still-visible Preview selection.
static LRESULT CALLBACK editorInteractionProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    const bool editorKey = message == WM_KEYDOWN && wp != VK_SHIFT && wp != VK_CONTROL && wp != VK_MENU;
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_RBUTTONDOWN ||
        editorKey || message == WM_CHAR) activateEditorSelection();
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, editorInteractionProc, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}
// Notepad++'s Edit > Copy may be dispatched by the main accelerator table.
// Route it to WebView2 only while this panel owns focus; Scintilla keeps its copy.
static LRESULT CALLBACK copyCommandProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    constexpr UINT editCopy = 42002; // Notepad++ menuCmdID.h: IDM_EDIT_COPY
    if (message == WM_COMMAND && LOWORD(wp) == editCopy && panel && panel->hwnd &&
        IsWindowVisible(panel->hwnd) && (GetFocus() == panel->hwnd || IsChild(panel->hwnd, GetFocus()))) {
        panel->script(L"window.markdownView.copyFocused()"); return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, copyCommandProc, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}

static void registerToolbarButton() {
    // Notepad++ retains these handles when it rebuilds the toolbar / switches themes.
    if (!toolbarImages.hToolbarBmp) toolbarImages.hToolbarBmp = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_MERMAID));
    if (!toolbarImages.hToolbarIcon) toolbarImages.hToolbarIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_MERMAID), IMAGE_ICON, 32, 32, 0);
    if (!toolbarImages.hToolbarIconDarkMode) toolbarImages.hToolbarIconDarkMode = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_MERMAID), IMAGE_ICON, 32, 32, 0);
    if (toolbarImages.hToolbarBmp && toolbarImages.hToolbarIcon && toolbarImages.hToolbarIconDarkMode) {
        SendMessageW(npp._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, commands[0]._cmdID, (LPARAM)&toolbarImages);
    }
}
static void releaseToolbarImages() {
    if (toolbarImages.hToolbarBmp) DeleteObject(toolbarImages.hToolbarBmp);
    if (toolbarImages.hToolbarIcon) DestroyIcon(toolbarImages.hToolbarIcon);
    if (toolbarImages.hToolbarIconDarkMode) DestroyIcon(toolbarImages.hToolbarIconDarkMode);
    toolbarImages = {};
}

static std::wstring nppConfigDirectory() {
    if (!npp._nppHandle) return {};
    const auto length = SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0);
    if (length <= 0 || length >= 32768) return {};
    std::vector<wchar_t> path(static_cast<size_t>(length) + 1, L'\0');
    if (!SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, path.size(), reinterpret_cast<LPARAM>(path.data()))) return {};
    return path.data();
}
static std::wstring runtimeDirectory() {
    wchar_t path[32768]{};
    auto ini = moduleDirectory() + L"\\Markdown_with_Mermaid.ini";
    // An explicit override keeps development data in the user's workspace.
    GetPrivateProfileStringW(L"Paths", L"DataDirectory", L"", path, 32768, ini.c_str());
    std::wstring result(path);
    if (result.empty()) {
#ifdef MWM_DEVELOPMENT_RUNTIME_DIRECTORY
        result = MWM_DEVELOPMENT_RUNTIME_DIRECTORY;
#else
        auto configuration = nppConfigDirectory();
        if (!configuration.empty()) {
            result = configuration + L"\\Markdown_with_Mermaid";
        } else {
            // If the host cannot supply a config path, isolate the fallback
            // by installation. Never default to writing beside a Program Files DLL.
            wchar_t local[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, local))) {
                result = local;
            } else {
                wchar_t temporary[32768]{};
                const auto length = GetTempPathW(32768, temporary);
                if (length > 0 && length < 32768) result = temporary;
            }
            if (!result.empty()) {
                unsigned long long installation = 14695981039346656037ULL;
                for (wchar_t c : moduleDirectory()) { installation ^= static_cast<unsigned>(towlower(c)); installation *= 1099511628211ULL; }
                result += L"\\Markdown_with_Mermaid\\" + std::to_wstring(installation);
            }
        }
#endif
    }
    if (result.empty()) return {};
    if (PathIsRelativeW(result.c_str())) result = moduleDirectory() + L"\\" + result;
    SHCreateDirectoryExW(nullptr, result.c_str(), nullptr);
    return result;
}
static std::wstring settingsFile() { auto dir = runtimeDirectory(); return dir.empty() ? L"" : dir + L"\\settings.ini"; }
static HWND editor() {
    int view = 0;
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&view);
    return view ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
}
static double editorLine() {
    HWND sci = editor();
    auto visible = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
    const auto line = SendMessageW(sci, SCI_DOCLINEFROMVISIBLE, visible, 0);
    const auto first = SendMessageW(sci, SCI_VISIBLEFROMDOCLINE, line, 0);
    const auto wraps = std::max<LRESULT>(1, SendMessageW(sci, SCI_WRAPCOUNT, line, 0));
    return static_cast<double>(line) + static_cast<double>(std::clamp<LRESULT>(visible - first, 0, wraps - 1)) / wraps;
}
static void jumpToLine(int line) {
    if (line < 0) return;
    HWND sci = editor();
    auto count = SendMessageW(sci, SCI_GETLINECOUNT, 0, 0);
    if (line >= count) return;
    SendMessageW(sci, SCI_GOTOLINE, line, 0); SetFocus(sci);
}
static std::wstring currentPath() {
    wchar_t name[32768]{};
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, 32768, (LPARAM)name);
    return name;
}
static bool editorContent(std::string& content, std::wstring& name, std::wstring& error) {
    name = currentPath();
    HWND sci = editor();
    auto length = SendMessageW(sci, SCI_GETLENGTH, 0, 0);
    if (length < 0 || length > 32 * 1024 * 1024) { error = L"Document exceeds the 32 MiB preview limit."; return false; }
    std::string bytes((size_t)length + 1, '\0');
    SendMessageW(sci, SCI_GETTEXT, bytes.size(), (LPARAM)bytes.data());
    bytes.resize(length);
    auto codepage = (UINT)SendMessageW(sci, SCI_GETCODEPAGE, 0, 0);
    content = codepage == CP_UTF8 ? bytes : utf8(fromBytes(bytes, codepage ? codepage : CP_ACP));
    return true;
}
static bool openMarkdownFile(const wchar_t* file) { return SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)file) != 0; }
static bool editorExportSelection(std::string& content) {
    HWND sci = editor();
    auto length = SendMessageW(sci, SCI_GETSELTEXT, 0, 0);
    // Scintilla 5.1.5+ reports bytes without NUL, but writes one after the text.
    if (length <= 0 || length > 32*1024*1024) return false;
    std::string bytes((size_t)length + 1, '\0'); SendMessageW(sci, SCI_GETSELTEXT, 0, (LPARAM)bytes.data()); bytes.resize(length);
    auto codepage = (UINT)SendMessageW(sci, SCI_GETCODEPAGE, 0, 0);
    content = codepage == CP_UTF8 ? bytes : utf8(fromBytes(bytes, codepage ? codepage : CP_ACP)); return true;
}
static bool visible() { return panel && panel->hwnd && IsWindowVisible(panel->hwnd); }
static void checkMenu(int index, bool value) {
    SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, commands[index]._cmdID, value);
}
static void hidePanel() {
    if (panel && panel->hwnd) SendMessageW(npp._nppHandle, NPPM_DMMHIDE, 0, (LPARAM)panel->hwnd);
    checkMenu(0, false);
}
static bool savedPanelLayout(const std::string& xml) {
    for (size_t start = 0; (start = xml.find("<PluginDlg", start)) != std::string::npos; ++start) {
        const auto end = xml.find('>', start); if (end == std::string::npos) break;
        const auto tag = xml.substr(start, end - start);
        auto attribute = tag.find("pluginName"); if (attribute == std::string::npos) continue;
        attribute += 10;
        while (attribute < tag.size() && std::isspace(static_cast<unsigned char>(tag[attribute]))) ++attribute;
        if (attribute == tag.size() || tag[attribute++] != '=') continue;
        while (attribute < tag.size() && std::isspace(static_cast<unsigned char>(tag[attribute]))) ++attribute;
        if (attribute == tag.size() || (tag[attribute] != '\'' && tag[attribute] != '"')) continue;
        const char quote = tag[attribute++]; const auto last = tag.find(quote, attribute);
        if (last != std::string::npos && !_stricmp(tag.substr(attribute, last - attribute).c_str(), "Markdown_with_Mermaid.dll")) return true;
    }
    return false;
}
static bool restoredPanelLayout() {
    auto directory = nppConfigDirectory();
    // Notepad++ supplies <configuration root>\plugins\Config. Read its saved
    // docking record only; the host owns config.xml and must remain its writer.
    for (int i = 0; i < 2; ++i) {
        const auto separator = directory.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return true;
        directory.resize(separator);
    }
    const auto path = directory + L"\\config.xml"; std::string xml;
    if (!readBytes(path, xml, 8 * 1024 * 1024)) return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    return savedPanelLayout(xml);
}
static int initialPanelWidth(int windowWidth, int editorWidth, int panelWidth, UINT dpi) {
    const auto scaled = [dpi](int value) { return MulDiv(value, dpi ? dpi : 96, 96); };
    const int desired = std::max(scaled(420), std::min(scaled(720), windowWidth / 2));
    return std::max(panelWidth, std::min(desired, panelWidth + editorWidth - scaled(320)));
}
static bool initializePanelWidth(HWND hwnd) {
    const auto preferences = settingsFile();
    if (GetPrivateProfileIntW(L"Layout", L"InitialWidthApplied", 0, preferences.c_str()) || restoredPanelLayout()) return false;
    HWND container = GetParent(hwnd);
    while (container && GetParent(container) != npp._nppHandle) container = GetParent(container);
    if (!container || !IsChild(npp._nppHandle, container)) return false;
    HWND manager = FindWindowExW(npp._nppHandle, nullptr, L"dockingManager", nullptr);
    if (!manager) return false;
    RECT frame{}, dock{}, source{};
    if (!GetClientRect(npp._nppHandle, &frame) || !GetWindowRect(container, &dock) || !GetWindowRect(editor(), &source)) return false;
    POINT right{frame.right, 0}; ClientToScreen(npp._nppHandle, &right);
    if (abs(dock.right - right.x) > 8) return false; // only the initial right dock
    HWND splitter = nullptr;
    while ((splitter = FindWindowExW(npp._nppHandle, splitter, L"wedockspliter", nullptr))) {
        RECT edge{};
        if (IsWindowVisible(splitter) && GetWindowRect(splitter, &edge) && abs(edge.right - dock.left) <= 8 &&
            edge.top < dock.bottom && edge.bottom > dock.top) break;
    }
    if (!splitter) return false;
    const auto getDpi = reinterpret_cast<UINT (WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = getDpi ? getDpi(npp._nppHandle) : 96;
    const int current = dock.right - dock.left;
    const int desired = initialPanelWidth(frame.right, source.right - source.left, current, dpi);
    // Notepad++ has no public dock-width setter. Use its existing splitter
    // message after validating both native windows; never change host memory.
    // dockingResource.h / DockingManager.cpp: DMM_MOVE_SPLITTER = 0x5000 + 11.
    if (desired > current) {
        SendMessageW(manager, 0x5000 + 11, desired - current, reinterpret_cast<LPARAM>(splitter));
        RECT resized{};
        if (!GetWindowRect(container, &resized) || resized.right - resized.left <= current) return false;
    }
    WritePrivateProfileStringW(L"Layout", L"InitialWidthApplied", L"1", preferences.c_str());
    return true;
}
static void showPanel() {
    auto dataDirectory = runtimeDirectory();
    if (dataDirectory.empty()) { MessageBoxW(npp._nppHandle, L"Cannot locate a writable user data directory.", L"Markdown_with_Mermaid", MB_ICONERROR); return; }
    if (!panel) {
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&showPanel), &pinned);
        if (FAILED(OleInitialize(nullptr))) { MessageBoxW(npp._nppHandle, L"Cannot initialize COM for WebView2.", L"Markdown_with_Mermaid", MB_ICONERROR); return; }
        WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
        wc.lpszClassName = windowClass; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        auto state = std::make_shared<Viewer>(); state->oleInitialized = true;
        state->dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
        auto dir = previewDirectory();
        if (dir.empty()) { OleUninitialize(); MessageBoxW(npp._nppHandle,L"Cannot create a temporary preview directory. Check the data directory permissions.",L"Markdown_with_Mermaid",MB_ICONERROR); return; }
        GUID guid; CoCreateGuid(&guid); wchar_t id[40]; StringFromGUID2(guid, id, 40);
        state->page = dir + L"\\" + previewName(std::wstring(id) + L".html");
        auto holder = new std::shared_ptr<Viewer>(state);
        HWND hwnd = CreateWindowExW(0, windowClass, L"Markdown_with_Mermaid", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 650, 700, npp._nppHandle, nullptr, instance, holder);
        if (!hwnd) { delete holder; OleUninitialize(); return; }
        panel = state;
        SetWindowSubclass(npp._nppHandle, copyCommandProc, reinterpret_cast<UINT_PTR>(instance), 0);
        SetWindowSubclass(npp._scintillaMainHandle, editorInteractionProc, reinterpret_cast<UINT_PTR>(instance), 0);
        SetWindowSubclass(npp._scintillaSecondHandle, editorInteractionProc, reinterpret_cast<UINT_PTR>(instance), 0);
        docking.hClient = hwnd; docking.pszName = L"Markdown_with_Mermaid";
        docking.dlgID = 0; docking.uMask = DWS_DF_CONT_RIGHT | DWS_USEOWNDARKMODE;
        docking.pszModuleName = L"Markdown_with_Mermaid.dll";
        SendMessageW(npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&docking);
        SendMessageW(npp._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)hwnd);
        initializePanelWidth(hwnd);
        if (state->render()) initializeWebView(state);
    } else {
        SendMessageW(npp._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)panel->hwnd);
        initializePanelWidth(panel->hwnd); // retry if the host was not laid out during startup
        panel->load();
    }
    checkMenu(0, true);
    SetFocus(editor());
}
static void togglePanel() { if (visible()) hidePanel(); else showPanel(); }
static void refresh() { if (!visible()) showPanel(); else panel->load(); }
static void toggleSync() {
    syncScroll = !syncScroll; checkMenu(2, syncScroll);
    WritePrivateProfileStringW(L"Options", L"SyncScroll", syncScroll ? L"1" : L"0", settingsFile().c_str());
}
static void toggleAutomatic() {
    automatic = !automatic; checkMenu(3, automatic);
    WritePrivateProfileStringW(L"Options", L"AutoShow", automatic ? L"1" : L"0", settingsFile().c_str());
}
static void outline() { if (!visible()) showPanel(); if (panel) panel->script(L"window.markdownView.toggleOutline()"); }
static void theme() { if (!visible()) showPanel(); if (panel) panel->script(L"window.markdownView.toggleTheme()"); }
static void exportHtml() { if (panel && panel->ready) { if (GetFocus() == editor()) chooseHtml(panel); else panel->script(L"window.markdownView.requestHtmlExport()"); } }
static void exportDocument(bool word) {
    bool fromEditor = GetFocus() == editor();
    if (!visible()) showPanel();
    if (!panel) return;
    if (fromEditor) {
        auto snapshot = std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = word; snapshot->selectionOnly = true;
        if (!snapshot->captureContent()) return;
        if (snapshot->source == panel->source) snapshot->images = panel->images;
        (word ? panel->pendingDocx : panel->pendingPdf) = snapshot;
        PostMessageW(panel->hwnd, word ? docxRequestMessage : pdfRequestMessage, 0, 0);
    } else panel->script(word ? L"window.markdownView.requestExport('docx')" : L"window.markdownView.requestExport('pdf')");
}
static void exportPdf() { exportDocument(false); }
static void exportDocx() { exportDocument(true); }
static void saveHtml(const std::wstring& html, const std::wstring& source) {
    if (html.size() > 64 * 1024 * 1024) return;
    wchar_t path[32768]{};
    auto name = source.empty() ? currentPath() : source; auto filename = PathFindFileNameW(name.c_str());
    wcsncpy(path, filename, 32000); PathRemoveExtensionW(path); wcscat(path, L".html");
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = npp._nppHandle; ofn.lpstrFile = path; ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"HTML document (*.html)\0*.html\0\0"; ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string styles;
    for (auto css : {L"vscode-markdown.css", L"viewer.css", L"highlight-light.css", L"highlight-dark.css"}) {
        std::string part; if (readBytes(moduleDirectory() + L"\\assets\\" + css, part)) styles += part + "\n";
    }
    auto output = utf8(html); replaceAll(output, "<!--MWM_EXPORT_STYLES-->", "<style>" + styles + "</style>");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, output.data(), (DWORD)output.size(), &written, nullptr) && written == output.size();
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ok) MessageBoxW(npp._nppHandle, L"Could not save HTML document.", L"Markdown_with_Mermaid", MB_ICONERROR);
}
static void about() {
    MessageBoxW(npp._nppHandle,
        L"Markdown_with_Mermaid " MWM_VERSION_W L"\n\nArtyom Nikiforov (rhyots)\nhttps://github.com/rhyots\n\nMarkdown preview and Mermaid diagram rendering\nOffline Mermaid 11.16.1\nmarkdown-it + VS Code Markdown Preview CSS\nWebView2 PDF and DOCX export\n\nCtrl+Alt+M: show/hide preview.",
        L"Markdown_with_Mermaid", MB_OK | MB_ICONINFORMATION);
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"Markdown_with_Mermaid"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    npp = data;
    syncScroll = GetPrivateProfileIntW(L"Options", L"SyncScroll", 1, settingsFile().c_str()) != 0;
    automatic = GetPrivateProfileIntW(L"Options", L"AutoShow", 1, settingsFile().c_str()) != 0;
    const wchar_t* names[] = {L"Markdown_with_Mermaid: show/hide preview", L"Refresh preview", L"Synchronize with editor scrolling", L"Auto-show for Markdown files", L"Toggle outline", L"Toggle light/dark theme", L"Export rendered HTML...", L"Export PDF...", L"Export DOCX...", L"---", L"About Markdown_with_Mermaid"};
    PFUNCPLUGINCMD actions[] = {togglePanel, refresh, toggleSync, toggleAutomatic, outline, theme, exportHtml, exportPdf, exportDocx, nullptr, about};
    for (int i = 0; i < 11; ++i) { wcsncpy(commands[i]._itemName, names[i], 63); commands[i]._pFunc = actions[i]; }
    commands[0]._pShKey = &toggleShortcut; commands[2]._init2Check = syncScroll; commands[3]._init2Check = automatic;
}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) { *count = 11; return commands; }
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (!notification || shuttingDown) return;
    auto code = notification->nmhdr.code;
    if (code == NPPN_TBMODIFICATION) { registerToolbarButton(); return; }
    if (code == NPPN_SHUTDOWN) {
        shuttingDown = true;
        RemoveWindowSubclass(npp._nppHandle, copyCommandProc, reinterpret_cast<UINT_PTR>(instance));
        RemoveWindowSubclass(npp._scintillaMainHandle, editorInteractionProc, reinterpret_cast<UINT_PTR>(instance));
        RemoveWindowSubclass(npp._scintillaSecondHandle, editorInteractionProc, reinterpret_cast<UINT_PTR>(instance));
        cancelPdf();
        cancelHtml();
        cancelDocx();
        if (panel && panel->hwnd) DestroyWindow(panel->hwnd);
        panel.reset(); releaseToolbarImages(); return;
    }
    if (code == NPPN_READY || code == NPPN_BUFFERACTIVATED || code == NPPN_FILESAVED || code == NPPN_FILERENAMED) {
        if (code == NPPN_BUFFERACTIVATED && panel && panel->source != currentPath()) activateEditorSelection();
        if (automatic) { auto path = currentPath(); if (isMarkdown(path)) showPanel(); else hidePanel(); }
        if (visible()) { KillTimer(panel->hwnd, 1); SetTimer(panel->hwnd, 1, 80, nullptr); }
    } else if (code == NPPN_DARKMODECHANGED && panel) {
        panel->dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
        panel->script(panel->dark ? L"window.markdownView.setTheme(true)" : L"window.markdownView.setTheme(false)");
    } else if (code == SCN_MODIFIED && visible() && notification->nmhdr.hwndFrom == editor() &&
        (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        KillTimer(panel->hwnd, 1); SetTimer(panel->hwnd, 1, 400, nullptr);
    } else if (code == SCN_UPDATEUI && visible() && syncScroll && (notification->updated & SC_UPDATE_V_SCROLL)) {
        panel->script(L"window.markdownView.scrollToLine(" + std::to_wstring(editorLine()) + L")");
    }
}
BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { instance = dll; DisableThreadLibraryCalls(dll); }
    return TRUE;
}
