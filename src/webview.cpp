// WebView2 host adapted from Markdown View (MIT). See vendor/LICENSE-MarkdownView.txt.
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <WebView2.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <map>
#include <set>
#include <commdlg.h>
#include <commctrl.h>
#include <winhttp.h>
#include "runtime-path.h"
#include "plugin-version.h"

static bool editorContent(std::string&, std::wstring&, std::wstring&);
static bool editorExportSelection(std::string&);
static void hidePanel();
static void jumpToLine(int);
static void saveHtml(const std::wstring&, const std::wstring& = L"");
static bool syncScroll = false;
static double editorLine();
static std::wstring runtimeDirectory();

static HINSTANCE instance;
static const wchar_t* windowClass = L"Markdown_with_Mermaid.Panel.1";
static constexpr UINT reloadMessage = WM_APP + 71;
static constexpr UINT pdfRequestMessage = WM_APP + 72;
static constexpr UINT pdfFinishMessage = WM_APP + 73;
static constexpr UINT_PTR pdfTimeoutTimer = 72;
static constexpr UINT docxRequestMessage = WM_APP + 74;
static constexpr UINT docxFinishMessage = WM_APP + 75;
static constexpr UINT_PTR docxTimeoutTimer = 74;
static constexpr UINT_PTR docxImageTimer = 75;
static constexpr UINT htmlRequestMessage = WM_APP + 76;
static constexpr UINT htmlFinishMessage = WM_APP + 77;
static constexpr UINT_PTR htmlTimeoutTimer = 76;
struct Viewer;
static void chooseHtml(const std::shared_ptr<Viewer>&);
static void htmlFailed(const std::shared_ptr<Viewer>&, const std::wstring&);
static void htmlReady(const std::shared_ptr<Viewer>&, const std::wstring&);
static void finishHtml(const std::shared_ptr<Viewer>&);
static void cancelHtml();
static void choosePdf(const std::shared_ptr<Viewer>&);
static void pdfDocumentReady(const std::shared_ptr<Viewer>&, int);
static void pdfFailed(const std::shared_ptr<Viewer>&, const std::wstring&);
static void finishPdf(const std::shared_ptr<Viewer>&);
static void cancelPdf();
static void chooseDocx(const std::shared_ptr<Viewer>&);
static void docxFailed(const std::shared_ptr<Viewer>&, const std::wstring&);
static void docxMessage(const std::shared_ptr<Viewer>&, const std::wstring&);
static void finishDocx(const std::shared_ptr<Viewer>&);
static void cancelDocx();
static void pollDocxImage(const std::shared_ptr<Viewer>&);
static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
static void initializeWebView(const std::shared_ptr<Viewer>&);
template<class I> const IID& callbackIid();
#define CALLBACK_IID(Type) template<> const IID& callbackIid<Type>() { return IID_##Type; }
CALLBACK_IID(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
CALLBACK_IID(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
CALLBACK_IID(ICoreWebView2NavigationStartingEventHandler)
CALLBACK_IID(ICoreWebView2WebMessageReceivedEventHandler)
CALLBACK_IID(ICoreWebView2NewWindowRequestedEventHandler)
CALLBACK_IID(ICoreWebView2PermissionRequestedEventHandler)
CALLBACK_IID(ICoreWebView2AcceleratorKeyPressedEventHandler)
CALLBACK_IID(ICoreWebView2ExecuteScriptCompletedHandler)
CALLBACK_IID(ICoreWebView2PrintToPdfCompletedHandler)
CALLBACK_IID(ICoreWebView2WebResourceResponseReceivedEventHandler)
CALLBACK_IID(ICoreWebView2WebResourceResponseViewGetContentCompletedHandler)

template<class T> class ComPtr {
    T* p = nullptr;
public:
    ~ComPtr() { reset(); }
    T* get() const { return p; }
    T* operator->() const { return p; }
    T** put() { reset(); return &p; }
    void reset(T* value = nullptr) { if (p) p->Release(); p = value; }
    explicit operator bool() const { return p != nullptr; }
};

// Small COM callback implementation; no dependency on WRL or the VC runtime.
template<class I, class... A> class Callback final : public I {
    std::atomic<ULONG> refs{1};
    std::function<HRESULT(A...)> fn;
public:
    explicit Callback(std::function<HRESULT(A...)> f) : fn(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == callbackIid<I>()) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};
template<class I, class... A, class F> I* cb(F f) { return new Callback<I, A...>(std::move(f)); }

static std::wstring moduleDirectory() {
    wchar_t path[32768];
    GetModuleFileNameW(instance, path, 32768);
    PathRemoveFileSpecW(path);
    return path;
}
static std::wstring fromBytes(const std::string& bytes, UINT cp, DWORD flags = 0) {
    if (bytes.empty()) return {};
    int n = MultiByteToWideChar(cp, flags, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (!n) return {};
    std::wstring result(n, L'\0');
    MultiByteToWideChar(cp, flags, bytes.data(), (int)bytes.size(), result.data(), n);
    return result;
}
static std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n, nullptr, nullptr);
    return result;
}
static bool readBytes(const std::wstring& path, std::string& data, size_t limit = 32 * 1024 * 1024) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && (ULONGLONG)size.QuadPart <= limit;
    if (ok) {
        data.resize((size_t)size.QuadPart);
        DWORD actual = 0;
        ok = ReadFile(file, data.data(), (DWORD)data.size(), &actual, nullptr) && actual == data.size();
    }
    CloseHandle(file);
    return ok;
}
static std::string decodeMarkdown(const std::string& data) {
    if (data.size() >= 2 && ((unsigned char)data[0] == 0xff && (unsigned char)data[1] == 0xfe)) {
        std::wstring text;
        for (size_t i = 2; i + 1 < data.size(); i += 2)
            text += (wchar_t)((unsigned char)data[i] | ((unsigned char)data[i + 1] << 8));
        return utf8(text);
    }
    if (data.size() >= 2 && ((unsigned char)data[0] == 0xfe && (unsigned char)data[1] == 0xff)) {
        std::wstring text;
        for (size_t i = 2; i + 1 < data.size(); i += 2)
            text += (wchar_t)(((unsigned char)data[i] << 8) | (unsigned char)data[i + 1]);
        return utf8(text);
    }
    auto content = data;
    if (content.compare(0, 3, "\xef\xbb\xbf") == 0) content.erase(0, 3);
    if (content.empty() || !fromBytes(content, CP_UTF8, MB_ERR_INVALID_CHARS).empty()) return content;
    return utf8(fromBytes(content, CP_ACP));
}
static std::string jsonString(const std::string& text) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32 || c == '<' || c == '>' || c == '&') {
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else result += c;
    }
    return result + '"';
}
static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) { s.replace(at, from.size(), to); at += to.size(); }
}
static std::wstring fileUrl(const std::wstring& path) {
    wchar_t url[32768]; DWORD n = 32768;
    if (FAILED(UrlCreateFromPathW(path.c_str(), url, &n, 0))) return {};
    return url;
}
static bool isPageUrl(const std::wstring& url, const std::wstring& page) {
    // Chromium percent-encodes Unicode as UTF-8. PathCreateFromUrlW does not
    // decode that representation reliably, so compare escaped own-page URLs.
    auto escaped = [](const std::wstring& value) -> std::wstring {
        if (value.empty()) return {};
        wchar_t buffer[32768]; DWORD length = 32768;
        if (FAILED(UrlEscapeW(value.c_str(), buffer, &length, URL_ESCAPE_AS_UTF8))) return {};
        return buffer;
    };
    auto clean = url.substr(0, url.find_first_of(L"#?"));
    auto actual = escaped(clean), expected = escaped(fileUrl(page));
    return !actual.empty() && !expected.empty() && !_wcsicmp(actual.c_str(), expected.c_str());
}
static bool pathFromFileUrl(const std::wstring& url, std::wstring& path) {
    path.clear();
    if (_wcsnicmp(url.c_str(), L"file:", 5)) return false;
    const auto clean = url.substr(0, url.find_first_of(L"#?"));
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    auto byteAt = [&](size_t i) -> int {
        if (i + 2 >= clean.size() || clean[i] != L'%') return -1;
        const auto high = hex(clean[i + 1]), low = hex(clean[i + 2]);
        return high < 0 || low < 0 ? -1 : high * 16 + low;
    };
    std::wstring decoded;
    for (size_t i = 0; i < clean.size();) {
        if (byteAt(i) < 128) { decoded += clean[i++]; continue; }
        std::string bytes;
        while (byteAt(i) >= 128) { bytes += static_cast<char>(byteAt(i)); i += 3; }
        const auto text = fromBytes(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (text.empty()) return false;
        decoded += text;
    }
    // Decode non-ASCII UTF-8 first, leaving ASCII escapes to the path API.
    // This avoids decoding a literal filename such as "%41.md" twice.
    wchar_t buffer[32768]; DWORD length = 32768;
    if (FAILED(PathCreateFromUrlW(decoded.c_str(), buffer, &length, 0))) return false;
    path = buffer; return true;
}
static bool isMarkdown(const std::wstring& path) {
    auto ext = PathFindExtensionW(path.c_str());
    return !_wcsicmp(ext, L".md") || !_wcsicmp(ext, L".markdown") || !_wcsicmp(ext, L".mdown") || !_wcsicmp(ext, L".mkd") || !_wcsicmp(ext, L".mkdn");
}

#include "navigation.h"
#include "session-data.h"

struct PreviewImage {
    std::string data, error;
    bool complete = false;
    bool contentRequested = false;
};
struct Viewer : std::enable_shared_from_this<Viewer> {
    HWND hwnd{};
    bool oleInitialized{};
    bool dark{};
    bool pdfMode = false;
    bool docxMode = false;
    bool htmlMode = false;
    bool selectionOnly = false;
    bool editorSelectionActive = true;
    std::wstring source, page, error = L"Markdown View: loading...";
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    ComPtr<ICoreWebView2Environment> environment;
    void fail(const std::wstring& text) { error = L"Markdown_with_Mermaid\n\n" + text; if (htmlMode) htmlFailed(shared_from_this(), text); else if (docxMode) docxFailed(shared_from_this(), text); else if (pdfMode) pdfFailed(shared_from_this(), text); else InvalidateRect(hwnd, nullptr, TRUE); }
    void resize() { if (controller) { RECT r; GetClientRect(hwnd, &r); controller->put_Bounds(r); } }
    void script(const std::wstring& text) { if (webview) webview->ExecuteScript(text.c_str(), nullptr); }
    bool ready = false;
    std::wstring payload;
    std::wstring fragmentHtml;
    std::shared_ptr<Viewer> pendingPdf, pendingDocx;
    std::map<std::wstring, int> scrollPositions;
    PreviewHistory history;
    std::wstring pendingFragment;
    bool pendingAnchorVisit = false;
    std::map<std::wstring, std::shared_ptr<PreviewImage>> images;
    std::map<std::wstring, std::map<std::wstring, std::shared_ptr<PreviewImage>>> clipboardImages;
    bool captureContent() {
        if (pdfMode && !payload.empty()) return true; // Immutable export snapshot.
        std::string content; std::wstring name, problem;
        if (!editorContent(content, name, problem)) { if (pdfMode) { error = problem; return false; } content = "# Markdown_with_Mermaid\n\n" + utf8(problem); }
        if (selectionOnly) editorExportSelection(content);
        if (!pdfMode && source != name) images.clear();
        source = name;
        if (!pdfMode && isMarkdown(name)) {
            history.observe(name, scrollPositions[name]);
            if (!pendingFragment.empty()) { history.anchor(pendingFragment, pendingAnchorVisit); pendingFragment.clear(); pendingAnchorVisit = false; }
        }
        const bool navigationEnabled = !pdfMode && history.matches(name);
        std::string navigation = "null";
        if (navigationEnabled) {
            const auto& visit = history.visits[history.index];
            navigation = "{\"back\":" + std::string(history.canBack() ? "true" : "false") +
                ",\"forward\":" + (history.canForward() ? "true" : "false") +
                ",\"token\":" + std::to_string(history.revision) + ",\"x\":" + std::to_string(visit.x) +
                ",\"y\":" + std::to_string(visit.y) + ",\"restore\":" + (history.restore ? "true" : "false") +
                ",\"fragment\":" + jsonString(utf8(visit.fragment)) + "}";
        }
        auto dir = name.substr(0, name.find_last_of(L"\\/") + 1);
        if (dir.empty() || PathIsRelativeW(name.c_str())) dir = moduleDirectory() + L"\\";
        payload = fromBytes("{\"source\":" + jsonString(content) + ",\"file\":" + jsonString(utf8(name)) +
            ",\"base\":" + jsonString(utf8(fileUrl(dir))) + ",\"dark\":" + (dark ? "true" : "false") +
            ",\"scrollY\":" + std::to_string(scrollPositions[source]) +
            ",\"navigation\":" + navigation +
            ",\"editorActive\":" + (editorSelectionActive ? "true" : "false") +
            ",\"line\":" + std::to_string(!pdfMode && syncScroll ? editorLine() : -1) + ",\"pdfExport\":" + (pdfMode ? "true" : "false") + ",\"docxExport\":" + (docxMode ? "true" : "false") +
            ",\"htmlExport\":" + (htmlMode ? "true" : "false") + ",\"fragment\":" + jsonString(utf8(fragmentHtml)) + "}", CP_UTF8);
        return true;
    }
    bool render() {
        if (!captureContent()) return false;
        std::string html;
        if (!readBytes(moduleDirectory() + L"\\assets\\viewer.html", html, 1024 * 1024)) {
            fail(L"assets/viewer.html is missing. Extract the complete plugin archive."); return false;
        }
        GUID guid; CoCreateGuid(&guid); wchar_t token[40]; StringFromGUID2(guid, token, 40);
        std::wstring nonce; for (wchar_t c : std::wstring(token)) if (iswalnum(c)) nonce += c;
        replaceAll(html, "__MV_ASSET_BASE__", utf8(fileUrl(moduleDirectory() + L"\\assets\\")));
        replaceAll(html, "__MV_DOCUMENT_BASE__", utf8(fileUrl(moduleDirectory() + L"\\")));
        replaceAll(html, "__MV_NONCE__", utf8(nonce));
        // DOCX obtains image bytes from the frozen preview cache or the native
        // fallback. Parsing the hidden page must never trigger another GET.
        if (docxMode) replaceAll(html, "img-src file: https: http: data:;", "img-src data:;");
        // Replace payload last: Markdown cannot introduce template substitutions.
        replaceAll(html, "__MV_PAYLOAD__", utf8(payload));
        HANDLE file = CreateFileW(page.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { fail(L"Cannot write the preview file:\n" + page + L"\nCheck the data directory and access permissions."); return false; }
        DWORD n = 0; bool ok = WriteFile(file, html.data(), (DWORD)html.size(), &n, nullptr) && n == html.size();
        CloseHandle(file); return ok;
    }
    void load() {
        if (ready && webview) { captureContent(); script(L"window.markdownView.update(" + payload + L")"); }
        else if (render() && webview) webview->Navigate(fileUrl(page).c_str());
    }
};

#include "pdf-export.cpp"
#include "html-export.cpp"
#include "docx-export.cpp"
#include "preview-images.cpp"
#include "clipboard.cpp"

static bool openMarkdownFile(const wchar_t*);

static void navigateHistory(const std::shared_ptr<Viewer>& s, bool forward) {
    if (!s || s->pdfMode || !s->history.matches(s->source)) return;
    auto& history = s->history;
    if (forward ? !history.canForward() : !history.canBack()) return;
    // Set the destination before NPPM_DOOPEN: a real host may notify us
    // synchronously while activating the tab. Keep unsaved editor buffers intact.
    const auto previous = history;
    history.index = forward ? history.index + 1 : history.index - 1;
    ++history.revision; history.restore = true;
    const auto targetFile = history.visits[history.index].file;
    if (!openMarkdownFile(targetFile.c_str())) {
        history = previous;
        s->script(L"window.markdownView.navigationError()");
    }
    s->load();
}

static void openLink(const std::shared_ptr<Viewer>& s, const std::wstring& url) {
    if (url.find(L"https://") == 0 || url.find(L"http://") == 0 || url.find(L"mailto:") == 0) {
        ShellExecuteW(s->hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (url.find(L"file:///") == 0) {
        std::wstring path;
        if (pathFromFileUrl(url, path) && isMarkdown(path.c_str())) {
            std::string probe;
            const bool sameFile = s->history.matches(path);
            if (readBytes(path, probe) && openMarkdownFile(path.c_str())) {
                const auto hash = url.find(L'#');
                s->pendingFragment = hash == std::wstring::npos || url.size() - hash > 4097 ? L"" : url.substr(hash + 1);
                s->pendingAnchorVisit = sameFile && !s->pendingFragment.empty();
                s->load();
            }
        }
    }
}

static void initializeWebView(const std::shared_ptr<Viewer>& s) {
    using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
#ifdef _WIN64
    const auto loaderPath = moduleDirectory() + L"\\x64\\WebView2Loader.dll";
#else
    const auto loaderPath = moduleDirectory() + L"\\x86\\WebView2Loader.dll";
#endif
    // Keep the loader loaded while asynchronous callbacks can still reference it.
    static HMODULE loader = LoadLibraryExW(loaderPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = loader ? (CreateEnvironment)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions") : nullptr;
    if (!create) { s->fail(L"WebView2Loader.dll is missing. Extract the complete plugin archive."); return; }
    auto dataDirectory = runtimeDirectory();
    if (dataDirectory.empty()) { s->fail(L"Cannot locate a user data directory for WebView2."); return; }
    auto session = SessionData::get(dataDirectory);
    if (session->lock == INVALID_HANDLE_VALUE) { s->fail(L"Cannot create a temporary session directory. Check data directory access permissions."); return; }
    const auto profile = session->profile;
    session->starting();
    std::weak_ptr<Viewer> weak = s;
    auto envDone = cb<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [weak, session](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            auto s = weak.lock(); if (!s || !s->hwnd) return S_OK;
            if (FAILED(hr) || !env) { session->failedBeforeEnvironment(); s->fail(L"Microsoft Edge WebView2 Runtime could not be started. Install or repair the Runtime:\nhttps://developer.microsoft.com/microsoft-edge/webview2/"); return S_OK; }
            env->AddRef(); s->environment.reset(env);
            auto controllerDone = cb<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                [weak, session](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    auto s = weak.lock();
                    if (!s || !s->hwnd) { if (controller) controller->Close(); return S_OK; }
                    if (FAILED(result) || !controller) {
                        wchar_t code[40]; swprintf(code,40,L" (0x%08lX)",static_cast<unsigned long>(result));
                        const auto message=std::wstring(L"Cannot create the WebView2 viewer")+code+L".";
                        const auto log=runtimeDirectory()+L"\\last-webview-error.txt";
                        const auto bytes=utf8(message);
                        HANDLE file=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                        if(file!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr);CloseHandle(file);}
                        s->fail(message); return S_OK;
                    }
                    controller->AddRef(); s->controller.reset(controller);
                    controller->put_ZoomFactor(1.0);
                    controller->get_CoreWebView2(s->webview.put());
                    if (!s->webview) { s->fail(L"Cannot initialize the HTML viewer."); return S_OK; }
                    DeleteFileW((runtimeDirectory()+L"\\last-webview-error.txt").c_str());
                    UINT browserPid = 0; if (SUCCEEDED(s->webview->get_BrowserProcessId(&browserPid))) session->connected(browserPid);
                    ComPtr<ICoreWebView2Settings> settings;
                    s->webview->get_Settings(settings.put());
                    if (settings) {
                        // The viewer scales only document content and owns its zoom indicator.
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_AreHostObjectsAllowed(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_IsWebMessageEnabled(TRUE);
                    }
                    EventRegistrationToken token;
                    if (!s->pdfMode) watchPreviewImages(s);
                    auto nav = cb<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                        [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* e) -> HRESULT {
                            auto s = weak.lock(); if (!s) { e->put_Cancel(TRUE); return S_OK; }
                            LPWSTR uri = nullptr; e->get_Uri(&uri);
                            std::wstring url = uri ? uri : L""; CoTaskMemFree(uri);
                            if (!isPageUrl(url, s->page)) e->put_Cancel(TRUE);
                            return S_OK;
                        });
                    s->webview->add_NavigationStarting(nav, &token); nav->Release();
                    auto message = cb<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                        [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* e) -> HRESULT {
                            auto s = weak.lock(); if (!s || !s->hwnd) return S_OK;
                            LPWSTR origin = nullptr; e->get_Source(&origin);
                            std::wstring originUrl = origin ? origin : L""; CoTaskMemFree(origin);
                            if (!isPageUrl(originUrl, s->page)) return S_OK;
                            LPWSTR raw = nullptr;
                            if (SUCCEEDED(e->TryGetWebMessageAsString(&raw)) && raw) {
                                std::wstring text(raw); CoTaskMemFree(raw);
                                if (text == L"reload") PostMessageW(s->hwnd, reloadMessage, 0, 0);
                                else if (text == L"history:back" && !s->pdfMode) navigateHistory(s, false);
                                else if (text == L"history:forward" && !s->pdfMode) navigateHistory(s, true);
                                else if (!s->pdfMode && text.rfind(L"anchor:", 0) == 0 && text.size() < 4200) {
                                    const auto separator = text.find(L':', 7);
                                    if (separator != std::wstring::npos && s->history.matches(s->source) &&
                                        wcstoul(text.c_str() + 7, nullptr, 10) == s->history.revision && separator + 1 < text.size()) {
                                        s->history.anchor(text.substr(separator + 1), true); s->load();
                                    }
                                }
                                else if (text == L"theme:dark" && !s->pdfMode) s->dark = true;
                                else if (text == L"theme:light" && !s->pdfMode) s->dark = false;
                                else if (text == L"close") hidePanel();
                                else if (text == L"preview-active" && !s->pdfMode) s->editorSelectionActive = false;
                                else if (text == L"ready") { s->ready = true; if (s->htmlMode) s->script(L"window.markdownView.exportHtml().catch(e=>window.chrome.webview.postMessage('html-error:'+e.message))"); else if (s->docxMode) s->script(L"window.markdownView.prepareDocx()"); else if (s->pdfMode) s->script(L"window.markdownView.preparePdf()"); else s->load(); }
                                else if (text == L"html" && !s->pdfMode) PostMessageW(s->hwnd, htmlRequestMessage, 0, 0);
                                else if (s->htmlMode && text.rfind(L"html-error:", 0) == 0) htmlFailed(s, text.substr(11, 1000));
                                else if (text == L"pdf" && !s->pdfMode) PostMessageW(s->hwnd, pdfRequestMessage, 0, 0);
                                else if (text == L"docx" && !s->pdfMode) PostMessageW(s->hwnd, docxRequestMessage, 0, 0);
                                else if (!s->pdfMode && (text.rfind(L"pdf-fragment:", 0) == 0 || text.rfind(L"docx-fragment:", 0) == 0) && text.size() < 32*1024*1024) {
                                    const bool word = text[0] == L'd';
                                    auto snapshot = std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = word;
                                    snapshot->fragmentHtml = text.substr(word ? 14 : 13);
                                    if (snapshot->captureContent()) {
                                        if (s->source == snapshot->source) snapshot->images = s->images;
                                        (word ? s->pendingDocx : s->pendingPdf) = snapshot;
                                        PostMessageW(s->hwnd, word ? docxRequestMessage : pdfRequestMessage, 0, 0);
                                    }
                                }
                                else if (!s->pdfMode && text.rfind(L"clipboard", 0) == 0 && text.size() < 64*1024*1024) clipboardMessage(s, text);
                                else if (text.rfind(L"docx-", 0) == 0 && s->docxMode) docxMessage(s, text);
                                else if (text.rfind(L"pdf-ready:", 0) == 0 && s->pdfMode) pdfDocumentReady(s, _wtoi(text.c_str() + 10));
                                else if (text.rfind(L"pdf-error:", 0) == 0 && s->pdfMode) pdfFailed(s, text.substr(10, 1000));
                                else if (!s->pdfMode && text.rfind(L"scroll:", 0) == 0) {
                                    unsigned token; int x, y;
                                    if (swscanf(text.c_str() + 7, L"%u:%d:%d", &token, &x, &y) == 3 && token == s->history.revision) {
                                        x = std::max(0, std::min(100000000, x)); y = std::max(0, std::min(100000000, y));
                                        s->scrollPositions[s->source] = y; s->history.remember(token, x, y);
                                    }
                                }
                                else if (text.rfind(L"line:", 0) == 0) jumpToLine(_wtoi(text.c_str() + 5));
                                else if (text.rfind(L"export:", 0) == 0) { if (s->htmlMode) htmlReady(s, text.substr(7)); else saveHtml(text.substr(7)); }
                                else if (text.rfind(L"link:", 0) == 0) openLink(s, text.substr(5));
                            }
                            return S_OK;
                        });
                    s->webview->add_WebMessageReceived(message, &token); message->Release();
                    auto popup = cb<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* e) -> HRESULT { e->put_Handled(TRUE); return S_OK; });
                    s->webview->add_NewWindowRequested(popup, &token); popup->Release();
                    auto permission = cb<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
                        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* e) -> HRESULT { e->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; });
                    s->webview->add_PermissionRequested(permission, &token); permission->Release();
                    auto keys = cb<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>(
                        [weak](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* e) -> HRESULT {
                            auto s = weak.lock(); if (!s) return S_OK;
                            UINT key; COREWEBVIEW2_KEY_EVENT_KIND kind;
                            e->get_VirtualKey(&key); e->get_KeyEventKind(&kind);
                            if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                if (key == VK_ESCAPE) { e->put_Handled(TRUE); s->script(L"window.markdownView.escape()"); }
                                else if (key == VK_F5) { e->put_Handled(TRUE); PostMessageW(s->hwnd, reloadMessage, 0, 0); }
                                else if (key == VK_F7 || ((GetKeyState(VK_CONTROL) & 0x8000) && key == 'F')) {
                                    e->put_Handled(TRUE); s->script(L"window.markdownView.openFind()");
                                }
                            }
                            return S_OK;
                        });
                    controller->add_AcceleratorKeyPressed(keys, &token); keys->Release();
                    s->resize(); controller->put_IsVisible(s->pdfMode ? FALSE : TRUE);
                    const auto navigated = s->webview->Navigate(fileUrl(s->page).c_str());
                    if (FAILED(navigated)) s->fail(L"Cannot navigate to the preview file. Check the temporary data path.");
                    if (GetFocus() == s->hwnd) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                    return S_OK;
                });
            HRESULT result = env->CreateCoreWebView2Controller(s->hwnd, controllerDone);
            controllerDone->Release();
            if (FAILED(result)) s->fail(L"Cannot start the HTML viewer.");
            return S_OK;
        });
    HRESULT hr = create(nullptr, profile.c_str(), nullptr, envDone);
    envDone->Release();
    if (FAILED(hr)) { session->failedBeforeEnvironment(); s->fail(L"Cannot start Microsoft Edge WebView2 Runtime. Install or repair it:\nhttps://developer.microsoft.com/microsoft-edge/webview2/"); }
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto holder = reinterpret_cast<std::shared_ptr<Viewer>*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto s = holder ? *holder : std::shared_ptr<Viewer>();
    if (msg == WM_NCCREATE) {
        holder = static_cast<std::shared_ptr<Viewer>*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder));
        (*holder)->hwnd = hwnd;
        return TRUE;
    }
    if (s) switch (msg) {
    case WM_NOTIFY: { auto hdr = reinterpret_cast<NMHDR*>(lp); if (hdr && (hdr->code & 0xffff) == 1051) hidePanel(); break; }
    case WM_SIZE: s->resize(); return 0;
    case WM_SETFOCUS: if (s->controller) s->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r);
        FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
        r.left += 24; r.top += 24;
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        auto old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc, s->error.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK);
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
    }
    case WM_TIMER:
        if (wp == htmlTimeoutTimer) { htmlFailed(s, L"HTML export timed out."); return 0; }
        if (wp == docxImageTimer) { pollDocxImage(s); return 0; }
        KillTimer(hwnd, wp); if (wp == pdfTimeoutTimer) pdfFailed(s, L"PDF export timed out."); else if (wp == docxTimeoutTimer) docxFailed(s, L"DOCX export timed out."); else s->load(); return 0;
    case WM_SHOWWINDOW: if (s->controller) s->controller->put_IsVisible((BOOL)wp); break;
    case reloadMessage: s->load(); return 0;
    case pdfRequestMessage: choosePdf(s); return 0;
    case pdfFinishMessage: finishPdf(s); return 0;
    case docxRequestMessage: chooseDocx(s); return 0;
    case docxFinishMessage: finishDocx(s); return 0;
    case htmlRequestMessage: chooseHtml(s); return 0;
    case htmlFinishMessage: finishHtml(s); return 0;
    case WM_NCDESTROY:
        s->hwnd = nullptr;
        if (s->controller) s->controller->Close();
        s->webview.reset(); s->controller.reset(); s->environment.reset();
        DeleteFileW(s->page.c_str());
        if (s->oleInitialized) OleUninitialize();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete holder; break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
