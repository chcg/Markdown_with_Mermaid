// Real WebView2 integration tests with a small Notepad++/Scintilla API host.
#include "../src/plugin.cpp"
#include <cstdio>

// Set both window sizes explicitly; do not rely on an executable icon cached by Explorer.
static void setTestWindowIcons(HWND hwnd) {
    for (const auto size : {ICON_SMALL, ICON_BIG}) {
        const auto width = GetSystemMetrics(size == ICON_SMALL ? SM_CXSMICON : SM_CXICON);
        const auto height = GetSystemMetrics(size == ICON_SMALL ? SM_CYSMICON : SM_CYICON);
        const auto icon = LoadImageW(instance, MAKEINTRESOURCEW(IDI_MERMAID), IMAGE_ICON, width, height, LR_SHARED);
        SendMessageW(hwnd, WM_SETICON, size, reinterpret_cast<LPARAM>(icon));
    }
}

static std::string textBuffer;
static std::wstring documentPath;
static int activeView = 0, firstLine = 0, jumpedLine = -1;
static int wrappedDocumentLine = -1, firstWrappedDisplayLine = 0, displayedWrapCount = 1;
static int ensuredLine = -1;
static UINT textCodepage = CP_UTF8;
static std::string editorSelection;
static int editorCopyCommands = 0;
static int toolbarRegistrations = 0;
static WPARAM toolbarCommand = 0;
static toolbarIconsWithDarkMode registeredImages{};
static std::wstring hostConfigDirectory;
static std::map<std::wstring, std::string> openedBuffers;
static std::wstring refusedOpenPath;
static void pump(unsigned ms) {
    ULONGLONG end = GetTickCount64() + ms; MSG message;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
    }
}
static LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case NPPM_GETPLUGINSCONFIGDIR:
        if (!lp) return hostConfigDirectory.size();
        if (wp <= hostConfigDirectory.size()) return FALSE;
        wcscpy(reinterpret_cast<wchar_t*>(lp), hostConfigDirectory.c_str()); return TRUE;
    case WM_COMMAND: if (LOWORD(wp) == 42002) { ++editorCopyCommands; return TRUE; } break;
    case NPPM_ADDTOOLBARICON_FORDARKMODE: toolbarCommand = wp; registeredImages = *(toolbarIconsWithDarkMode*)lp; ++toolbarRegistrations; return TRUE;
    case NPPM_GETCURRENTSCINTILLA: *(int*)lp = activeView; return TRUE;
    case NPPM_GETFULLCURRENTPATH: wcsncpy((wchar_t*)lp, documentPath.c_str(), wp - 1); return TRUE;
    case NPPM_DMMREGASDCKDLG: { auto data = (tTbData*)lp; MoveWindow(data->hClient, 0, 0, 1000, 820, TRUE); return TRUE; }
    case NPPM_DMMSHOW: ShowWindow((HWND)lp, SW_SHOW); return TRUE;
    case NPPM_DMMHIDE: ShowWindow((HWND)lp, SW_HIDE); return TRUE;
    case NPPM_DOOPEN: {
        std::wstring target = (const wchar_t*)lp;
        if (target == refusedOpenPath) return FALSE;
        if (target == documentPath) return TRUE;
        std::string content;
        auto cached = openedBuffers.find(target);
        if (cached != openedBuffers.end()) content = cached->second;
        else if (!readBytes(target, content)) return FALSE;
        openedBuffers[documentPath] = textBuffer;
        documentPath = target; textBuffer = content; return TRUE;
    }
    case SCI_GETLENGTH: return textBuffer.size();
    case SCI_GETTEXT: memcpy((void*)lp, textBuffer.c_str(), std::min((size_t)wp, textBuffer.size() + 1)); return textBuffer.size();
    case SCI_GETCODEPAGE: return textCodepage;
    // Scintilla 5.1.5+ excludes the terminating NUL from the returned length.
    case SCI_GETSELTEXT: if (lp) memcpy((void*)lp, editorSelection.c_str(), editorSelection.size() + 1); return editorSelection.size();
    case SCI_GETFIRSTVISIBLELINE: return firstLine;
    case SCI_SETFIRSTVISIBLELINE: firstLine = (int)wp; return 0;
    case SCI_ENSUREVISIBLE: ensuredLine = (int)wp; return 0;
    case SCI_DOCLINEFROMVISIBLE: return wrappedDocumentLine >= 0 ? wrappedDocumentLine : wp;
    case SCI_VISIBLEFROMDOCLINE: return wrappedDocumentLine >= 0 ? firstWrappedDisplayLine : wp;
    case SCI_WRAPCOUNT: return displayedWrapCount;
    case SCI_GETLINECOUNT: return 10000;
    case SCI_GOTOLINE: jumpedLine = (int)wp; return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
static std::wstring eval(const wchar_t* expression) {
    bool done = false; std::wstring result;
    auto callback = cb<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
        [&](HRESULT hr, LPCWSTR json) -> HRESULT { result = SUCCEEDED(hr) && json ? json : L"ERROR"; done = true; return S_OK; });
    panel->webview->ExecuteScript(expression, callback); callback->Release();
    for (int i = 0; !done && i < 3000; ++i) pump(10);
    if (!done) {
        puts("FAIL: JavaScript callback timeout");
        LPWSTR location = nullptr; panel->webview->get_Source(&location);
        std::wprintf(L"Browser source: %ls\nPreview file: %ls\n", location ? location : L"", panel->page.c_str());
        CoTaskMemFree(location); ExitProcess(3);
    }
    return result;
}
static bool waitFor(const wchar_t* expression) {
    for (int i = 0; i < 300; ++i) { pump(100); if (panel && panel->webview && eval(expression) == L"true") return true; }
    std::wprintf(L"FAIL timeout: %ls\n", expression);
    if (panel) std::wprintf(L"Host: %ls\n", panel->error.c_str());
    return false;
}
static bool check(bool ok, const char* label) { printf("%s: %s\n", ok ? "PASS" : "FAIL", label); fflush(stdout); return ok; }
static void notify(UINT code, int modification = 0) {
    SCNotification notification{}; notification.nmhdr.code = code; notification.nmhdr.hwndFrom = editor();
    notification.modificationType = modification; beNotified(&notification);
}
static void loadFixture(const std::wstring& path) { documentPath = path; readBytes(path, textBuffer); notify(NPPN_BUFFERACTIVATED); }
CALLBACK_IID(ICoreWebView2CapturePreviewCompletedHandler)
CALLBACK_IID(ICoreWebView2DevToolsProtocolEventReceivedEventHandler)
CALLBACK_IID(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler)
static bool capture(const std::wstring& path) {
    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, stream.put()))) return false;
    bool done = false; HRESULT result = E_FAIL;
    auto callback = cb<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([&](HRESULT hr) -> HRESULT { result = hr; done = true; return S_OK; });
    auto started = panel->webview->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.get(), callback); callback->Release();
    if (FAILED(started)) return false;
    for (int i = 0; !done && i < 500; ++i) pump(10);
    if (!done) ExitProcess(3);
    return SUCCEEDED(result);
}
static bool pdfTest(const std::wstring& path, bool mutate = false, bool expectSuccess = true) {
    bool done = false, success = false; int calls = 0;
    bool started = startPdfExport(panel, path, [&](bool result, const std::wstring& message) {
        success = result; done = true; ++calls; if (!result && expectSuccess) std::wprintf(L"PDF error: %ls\n", message.c_str());
    });
    bool duplicateRejected = !pdfExport || !startPdfExport(panel, path + L"-duplicate.pdf");
    if (pdfExport) { panel->pendingPdf = std::make_shared<Viewer>(); choosePdf(panel); duplicateRejected &= !panel->pendingPdf; }
    if (mutate) { textBuffer = "# CHANGED_AFTER_PDF_SNAPSHOT\n"; panel->load(); }
    for (int i = 0; !done && i < 600; ++i) pump(100);
    if (!done) cancelPdf();
    std::string bytes;
    bool pdfHeader = readBytes(path, bytes, 64 * 1024 * 1024) && bytes.compare(0, 5, "%PDF-") == 0;
    return started && done && calls == 1 && success == expectSuccess && (!expectSuccess || (pdfHeader && duplicateRejected)) && !pdfExport;
}
static bool docxTest(const std::wstring& path, bool mutate = false, bool expectSuccess = true) {
    bool done = false, success = false; int calls = 0;
    bool started = startDocxExport(panel, path, [&](bool result, const std::wstring& message) {
        success = result; done = true; ++calls; if (!result && expectSuccess) printf("DOCX error: %s\n", utf8(message).c_str());
    });
    bool duplicateRejected = !docxExport || !startDocxExport(panel, path + L"-duplicate.docx");
    if (docxExport) { panel->pendingDocx = std::make_shared<Viewer>(); chooseDocx(panel); duplicateRejected &= !panel->pendingDocx; }
    if (mutate) { textBuffer = "# CHANGED_AFTER_DOCX_SNAPSHOT\n"; panel->load(); }
    for (int i = 0; !done && i < 1200; ++i) pump(100);
    if (!done) cancelDocx();
    std::string bytes; bool zipHeader = readBytes(path, bytes, 128*1024*1024) && bytes.compare(0, 4, "PK\3\4") == 0;
    return started && done && calls == 1 && success == expectSuccess && (!expectSuccess || (zipHeader && duplicateRejected)) && !docxExport;
}
static bool imageSizeTests(const std::wstring& fixtures, const std::wstring& prefix) {
    bool ok = true;
    loadFixture(fixtures + L"\\image-sizes.md");
    ok &= check(waitFor(L"document.querySelectorAll('.markdown-body img').length===6 && [...document.querySelectorAll('.markdown-body img')].every(i=>i.complete&&i.naturalWidth>0)"), "image size fixture loads with the official Mermaid logo");
    const wchar_t* dimensions = LR"JS((()=>{
      const size=(alt,w,h)=>{const r=document.querySelector('img[alt="'+alt+'"]').getBoundingClientRect();return Math.abs(r.width-w)<1&&Math.abs(r.height-h)<1;};
      return size('logo-height',150,150)&&size('height-only',120,24)&&size('width-only',200,40)&&size('both-dimensions',160,64)&&size('intrinsic',480,96);
    })())JS";
    ok &= check(eval(dimensions) == L"true", "preview respects height width both dimensions and intrinsic image sizes");
    capture(prefix + L"-image-sizes.png");
    eval(L"window.markdownView.setZoom(200)");
    ok &= check(eval(L"Math.abs(document.querySelector('img[alt=logo-height]').getBoundingClientRect().height-300)<1") == L"true", "image size follows the selected preview zoom");
    eval(L"window.markdownView.setZoom(100)");
    MoveWindow(panel->hwnd, 0, 0, 320, 820, TRUE); pump(300);
    ok &= check(eval(L"(()=>{const i=document.querySelector('img[alt=responsive-height]'),r=i.getBoundingClientRect();return r.width<=document.documentElement.clientWidth&&Math.abs(r.width/r.height-5)<0.02&&r.height<200;})()") == L"true", "height-only image shrinks proportionally in a narrow preview");
    ok &= check(eval(L"(()=>{const r=document.querySelector('img[alt=both-dimensions]').getBoundingClientRect();return Math.abs(r.width/r.height-2.5)<0.02;})()") == L"true", "explicit width and height keep the authored aspect ratio");
    MoveWindow(panel->hwnd, 0, 0, 1000, 820, TRUE); pump(300);
    ok &= check(pdfTest(prefix + L"-pdf-image-sizes.pdf"), "PDF exports the same authored image dimensions");
    return ok;
}
static std::string readHtmlClipboard() {
    std::string value;
    if (!OpenClipboard(nullptr)) return value;
    auto data = GetClipboardData(RegisterClipboardFormatW(L"HTML Format"));
    if (data) { auto text = (const char*)GlobalLock(data); if (text) { value = text; GlobalUnlock(data); } }
    CloseClipboard(); return value;
}
static std::wstring readTextClipboard() {
    std::wstring value;
    if (!OpenClipboard(nullptr)) return value;
    auto data = GetClipboardData(CF_UNICODETEXT);
    if (data) { auto text = (const wchar_t*)GlobalLock(data); if (text) { value = text; GlobalUnlock(data); } }
    CloseClipboard(); return value;
}
static bool selectedExport(const std::wstring& path, bool word, const std::shared_ptr<Viewer>& snapshot) {
    bool done = false, success = false;
    auto complete = [&](bool result, const std::wstring& message) { done = true; success = result; if (!result) printf("Selection export error: %s\n", utf8(message).c_str()); };
    bool started = word ? startDocxExport(panel, path, complete, snapshot) : startPdfExport(panel, path, complete, snapshot);
    for (int i = 0; !done && i < 1200; ++i) pump(100);
    if (!done) { if (word) cancelDocx(); else cancelPdf(); }
    return started && done && success;
}
static std::wstring captureSelectionFragment(bool word = false) {
    std::wstring fragment; bool completed = false;
    const std::wstring prefix = word ? L"probe:docx-fragment:" : L"probe:pdf-fragment:";
    auto receiver = cb<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
        [&](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            LPWSTR raw = nullptr; if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) { std::wstring text(raw); if (text.rfind(L"probe:", 0) == 0) { completed = true; if (text.rfind(prefix, 0) == 0) fragment = text.substr(prefix.size()); } CoTaskMemFree(raw); } return S_OK;
        });
    EventRegistrationToken token; panel->webview->add_WebMessageReceived(receiver, &token); receiver->Release();
    eval(L"window.__selectionPost=window.chrome.webview.postMessage;window.chrome.webview.postMessage=m=>/^(pdf|docx)(-fragment:|$)/.test(m)?window.__selectionPost('probe:'+m):window.__selectionPost(m)");
    eval(word ? L"document.getElementById('export-docx').click()" : L"document.getElementById('export-pdf').click()");
    for (int i = 0; !completed && i < 100; ++i) pump(100);
    panel->webview->remove_WebMessageReceived(token); eval(L"window.chrome.webview.postMessage=window.__selectionPost");
    return fragment;
}
static bool filenameCopyTests() {
    bool ok = true;
    const std::wstring filename = PathFindFileNameW(documentPath.c_str());
    const auto selectName = [](bool partial = false) {
        eval(partial ?
            L"document.activeElement?.blur();(()=>{const t=document.getElementById('filename').firstChild,r=document.createRange();r.setStart(t,t.length-3);r.setEnd(t,t.length);window.getSelection().removeAllRanges();window.getSelection().addRange(r)})()" :
            L"document.activeElement?.blur();(()=>{const r=document.createRange();r.selectNodeContents(document.getElementById('filename'));window.getSelection().removeAllRanges();window.getSelection().addRange(r)})()");
    };
    const auto copied = [](const std::wstring& expected) {
        for (int i = 0; i < 100; ++i) { pump(30); if (readTextClipboard() == expected && readHtmlClipboard().empty() &&
            !IsClipboardFormatAvailable(RegisterClipboardFormatW(L"Rich Text Format"))) return true; }
        return false;
    };
    selectName(); writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'c',ctrlKey:true,bubbles:true}))");
    ok &= check(copied(filename), "Ctrl+C on the selected header filename copies only its Unicode name");
    waitFor(L"!document.getElementById('copy').disabled");
    selectName(true); writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'c',ctrlKey:true,bubbles:true}))");
    ok &= check(copied(L".md"), "Ctrl+C on part of the header filename copies only that selected part");
    waitFor(L"!document.getElementById('copy').disabled");
    // Establish native focus before making the browser selection, as a real
    // click on the filename does. MoveFocus can restore a previous DOM focus.
    SetFocus(panel->hwnd); pump(100); selectName();
    writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    SendMessageW(npp._nppHandle, WM_COMMAND, 42002, 0);
    ok &= check(copied(filename), "Notepad++ Copy accelerator preserves a selected header filename");
    waitFor(L"!document.getElementById('copy').disabled");
    selectName(); writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    ok &= check(eval(L"(()=>{const e=new Event('copy',{bubbles:true,cancelable:true});document.dispatchEvent(e);return e.defaultPrevented})()") == L"true" && copied(filename),
        "the browser copy event writes only plain filename text without HTML or RTF");
    waitFor(L"!document.getElementById('copy').disabled");
    eval(L"window.getSelection().removeAllRanges()");
    writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    eval(L"document.getElementById('filename').dispatchEvent(new MouseEvent('click',{detail:3,bubbles:true}))");
    ok &= check(eval(L"window.getSelection().toString()===document.getElementById('filename').textContent") == L"true" && readTextClipboard()==L"FILENAME_COPY_SENTINEL",
        "triple-click selects the complete filename without copying anything");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'c',ctrlKey:true,bubbles:true}))");
    ok &= check(copied(filename), "Ctrl+C after triple-click copies the complete filename as plain text");
    waitFor(L"!document.getElementById('copy').disabled");
    selectName(true); writeClipboard(panel->hwnd, L"FILENAME_COPY_SENTINEL", L"<span>FILENAME_COPY_SENTINEL</span>");
    eval(L"document.getElementById('filename').dispatchEvent(new MouseEvent('contextmenu',{clientX:20,clientY:20,bubbles:true,cancelable:true}))");
    const bool menuVisible=eval(L"!document.getElementById('filename-menu')?.hidden && document.querySelector('#filename-menu button')?.textContent==='Copy' && document.querySelector('#filename-menu button')?.title==='Copy filename as plain text'")==L"true";
    // Focus changes must not substitute the document or the whole filename.
    eval(L"window.getSelection().removeAllRanges();document.querySelector('#filename-menu button')?.click()");
    ok &= check(menuVisible && copied(L".md"), "filename context menu has English Copy and preserves the selected partial plain text");
    waitFor(L"!document.getElementById('copy').disabled");
    eval(L"window.getSelection().removeAllRanges();document.getElementById('filename').dispatchEvent(new MouseEvent('contextmenu',{clientX:20,clientY:20,bubbles:true,cancelable:true}));document.querySelector('#filename-menu button')?.click()");
    ok &= check(copied(filename), "filename context menu copies the whole plain filename when nothing is selected");
    waitFor(L"!document.getElementById('copy').disabled");
    eval(L"document.getElementById('filename').dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true}));document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))");
    pump(100);
    ok &= check(eval(L"document.getElementById('filename-menu')?.hidden===true")==L"true" && visible(), "Escape dismisses the filename context menu without hiding Preview");
    selectName(); eval(L"document.getElementById('copy').click()");
    ok &= check(waitFor(L"!document.getElementById('copy').disabled") && readTextClipboard().find(L"Русский текст") != std::wstring::npos && readTextClipboard() != filename,
        "the explicit formatted Copy button still copies the document when only the header is selected");
    selectName(true); writeClipboard(panel->hwnd, L"FILENAME_BUTTON_SENTINEL", L"<b>old formatting</b>");
    eval(L"document.getElementById('filename-copy').click()");
    ok &= check(copied(filename) && waitFor(L"!document.getElementById('filename-copy').disabled"), "filename icon button copies the complete Unicode filename as plain text despite a partial selection");
    ok &= check(eval(L"(()=>{const b=document.getElementById('filename-copy');return b.dataset.state==='copied'&&b.title==='Filename copied'&&b.getAttribute('aria-label')===b.title&&getComputedStyle(b.querySelector('.filename-copied')).display!=='none'&&document.getElementById('copy-status').textContent==='Filename copied'})()") == L"true", "filename button confirms successful copying with an English label and a visible checkmark");
    ok &= check(waitFor(L"!document.getElementById('filename-copy').dataset.state && document.getElementById('filename-copy').title==='Copy filename as plain text'"), "filename copy feedback returns to its ready state");
    // Simulate a rejected clipboard write at the bridge boundary, without
    // holding or locking the user's system clipboard.
    eval(L"window.__filenamePost=window.chrome.webview.postMessage;window.chrome.webview.postMessage=m=>{if(!m.startsWith('clipboard-text:'))window.__filenamePost(m)};document.getElementById('filename-copy').click();window.markdownView.copyResult(false,'The clipboard is busy. Please copy again.');window.chrome.webview.postMessage=window.__filenamePost");
    ok &= check(eval(L"(()=>{const b=document.getElementById('filename-copy');return !b.disabled&&b.dataset.state==='error'&&b.title==='Copy failed. Try again.'&&document.getElementById('copy-status').classList.contains('error')})()") == L"true", "a failed filename copy shows English feedback and allows retry");
    eval(L"window.getSelection().removeAllRanges();document.getElementById('filename-copy').click()");
    ok &= check(copied(filename) && waitFor(L"document.getElementById('filename-copy').dataset.state==='copied'&&!document.getElementById('copy-status').classList.contains('error')"), "filename button retries successfully without any text selection");
    eval(L"window.getSelection().removeAllRanges()");
    return ok;
}
static bool selectionTests(const std::wstring& fixtures, const std::wstring& prefix) {
    bool ok = true;
    loadFixture(fixtures + L"\\selection.md");
    ok &= check(waitFor(L"document.getElementById('selected_section') && window.markdownView.mermaidReady && document.querySelector('.markdown-body img')?.naturalWidth===480"), "selection fixture renders image and Mermaid");
    ok &= check(eval(L"document.getElementById('copy').nextElementSibling.id==='zoom-controls' && !document.getElementById('copy').textContent.trim() && !!document.querySelector('#copy svg rect')") == L"true", "Copy uses the requested outline icon before zoom with no text label");
    eval(L"document.activeElement?.blur();window.getSelection().removeAllRanges();document.dispatchEvent(new KeyboardEvent('keydown',{key:'c',ctrlKey:true,bubbles:true}))");
    ok &= check(waitFor(L"document.getElementById('copy-status').textContent==='Copied with formatting'"), "Ctrl+C without selection copies the whole rendered preview");
    auto html = readHtmlClipboard(); auto plain = readTextClipboard();
    auto offset = [&](const char* key) -> size_t { auto at = html.find(key); return at == std::string::npos ? 0 : strtoul(html.c_str() + at + strlen(key), nullptr, 10); };
    auto start = offset("StartFragment:"), end = offset("EndFragment:"), endHtml = offset("EndHTML:");
    ok &= check(start > 0 && end > start && endHtml == html.size() && html.substr(start - 20, 20) == "<!--StartFragment-->" && html.substr(end, 18) == "<!--EndFragment-->", "CF_HTML uses exact UTF-8 byte offsets with Cyrillic and emoji");
    ok &= check(html.find("<strong") != std::string::npos && html.find("<table") != std::string::npos && html.find("data:image/png;base64,") != std::string::npos && html.find("https://example.com/selection") != std::string::npos && html.find("diagram-tools") == std::string::npos && html.find("link-type-icon") == std::string::npos && plain.find(L"OUTSIDE_BEFORE") != std::wstring::npos && plain.find(L"Кириллица") != std::wstring::npos && plain.find(L"Export to") == std::wstring::npos, "clipboard contains styled semantic HTML embedded images and readable text without plugin controls");
    eval(L"(()=>{const t=document.querySelector('.markdown-body p strong').firstChild,r=document.createRange();r.setStart(t,0);r.setEnd(t,6);const s=window.getSelection();s.removeAllRanges();s.addRange(r);document.getElementById('copy').dispatchEvent(new MouseEvent('mousedown',{bubbles:true,cancelable:true}));document.getElementById('copy').click();})()");
    ok &= check(waitFor(L"!document.getElementById('copy').disabled"), "Copy button finishes with partial inline selection");
    plain = readTextClipboard(); html = readHtmlClipboard();
    ok &= check(plain == L"жирный" && html.find("<strong") != std::string::npos && html.find("OUTSIDE_") == std::string::npos && html.find("SELECTED_BOLD") == std::string::npos, "partial text selection preserves bold and excludes unselected text");
    SetFocus(editor()); auto calls = editorCopyCommands; SendMessageW(npp._nppHandle, WM_COMMAND, 42002, 0);
    ok &= check(editorCopyCommands == calls + 1 && readTextClipboard() == L"жирный", "Notepad++ Copy reaches its normal handler while editor has focus");
    SetFocus(panel->hwnd); pump(100);
    SendMessageW(npp._nppHandle, WM_COMMAND, 42002, 0); pump(200);
    ok &= check(editorCopyCommands == calls + 1 && readTextClipboard() == L"жирный", "Notepad++ Copy is routed to formatted preview copy when WebView2 owns focus");
    ok &= check(eval(L"(()=>{const t=document.querySelector('ol li:nth-child(2)').firstChild,r=document.createRange();r.selectNodeContents(t);window.getSelection().removeAllRanges();window.getSelection().addRange(r);const f=window.markdownView.selectionSnapshot();return f.querySelector('ol').start===2&&f.querySelectorAll('li').length===1&&!f.textContent.includes('SELECTED_LIST_ONE');})()") == L"true", "partial ordered list preserves its original item number");
    ok &= check(eval(L"(()=>{const t=document.querySelector('tbody td').firstChild,r=document.createRange();r.setStart(t,9);r.setEnd(t,13);window.getSelection().removeAllRanges();window.getSelection().addRange(r);const f=window.markdownView.selectionSnapshot();return f.querySelectorAll('table tbody tr').length===1&&f.textContent.trim()==='CELL'&&!f.querySelector('thead');})()") == L"true", "partial table selection preserves table structure and excludes other cells");
    eval(L"(()=>{document.querySelector('.markdown-body').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));const r=document.createRange();r.setStart(document.getElementById('selected_section').firstChild,0);r.setEndAfter(document.querySelector('.mermaid-diagram'));window.getSelection().removeAllRanges();window.getSelection().addRange(r);})()");
    ok &= check(eval(L"(()=>{const f=window.markdownView.selectionSnapshot();return !!f.querySelector('ol li')&&!!f.querySelector('table')&&!!f.querySelector('.mermaid-graphic svg')&&!f.textContent.includes('OUTSIDE_');})()") == L"true", "selection snapshot keeps list table and shadow-root Mermaid while excluding both outer headings");
    notify(SCN_FOCUSIN);
    ok &= check(!panel->editorSelectionActive && eval(L"window.getSelection().toString().includes('SELECTED_PARAGRAPH')") == L"true", "restored Scintilla focus alone does not override a live Preview selection");
    for (bool word : {false, true}) {
        const auto selected = captureSelectionFragment(word);
        ok &= check(selected.find(L"SELECTED_PARAGRAPH") != std::wstring::npos && selected.find(L"OUTSIDE_") == std::wstring::npos,
            word ? "DOCX toolbar keeps Preview selection after host focus restoration" : "PDF toolbar keeps Preview selection after host focus restoration");
    }
    SendMessageW(editor(), WM_LBUTTONDOWN, MK_LBUTTON, 0);
    ok &= check(panel->editorSelectionActive && captureSelectionFragment().empty(), "real editor mouse interaction selects the editor export scope instead of a stale Preview range");
    eval(L"document.querySelector('.markdown-body').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}))"); pump(100);
    SendMessageW(editor(), WM_KEYDOWN, VK_LEFT, 0);
    ok &= check(panel->editorSelectionActive && captureSelectionFragment(true).empty(), "real editor keyboard interaction selects the editor export scope instead of a stale Preview range");
    eval(L"document.querySelector('.markdown-body').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}))"); pump(100);
    capture(prefix + L"-selection.png");
    eval(L"window.__htmlPost=window.chrome.webview.postMessage;window.__htmlSelected='';window.chrome.webview.postMessage=m=>{if(m.startsWith('export:'))window.__htmlSelected=m.slice(7);else window.__htmlPost(m)};document.getElementById('export').click()");
    ok &= check(waitFor(L"!!window.__htmlSelected") && eval(L"window.__htmlSelected.includes('SELECTED_PARAGRAPH')&&!window.__htmlSelected.includes('OUTSIDE_')&&window.__htmlSelected.includes('<svg')&&!window.__htmlSelected.includes('<script')") == L"true", "HTML toolbar exports only the selected Preview fragment with static Mermaid");
    eval(L"window.chrome.webview.postMessage=window.__htmlPost");
    auto fragment = captureSelectionFragment();
    ok &= check(!fragment.empty() && fragment.find(L"data:image/svg+xml;base64,") != std::wstring::npos, "selected export freezes preview image bytes without any repeated GET");
    // The selection and visible buffer may change after capture without changing the export.
    textBuffer = "# CHANGED_AFTER_SELECTION_SNAPSHOT\n"; panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent==='CHANGED_AFTER_SELECTION_SNAPSHOT'"), "visible buffer changes after rendered selection is captured");
    for (bool word : {false, true}) {
        auto snapshot = std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = word; snapshot->fragmentHtml = fragment;
        snapshot->captureContent(); snapshot->images = panel->images;
        ok &= check(selectedExport(prefix + (word ? L"-selection.docx" : L"-selection.pdf"), word, snapshot), word ? "DOCX exports frozen rendered selection" : "PDF exports frozen rendered selection");
    }
    editorSelection = "# EDITOR_SELECTION\n\n**EDITOR_SELECTION_TOKEN**\n\nEND_EDITOR_BOUNDARY";
    const auto originalEditorSelection = editorSelection;
    editorSelection += "\n```mermaid\nflowchart LR\nA[HTML_SELECTED_NODE] --> B[HTML_SELECTED_END]\n```\n";
    auto htmlSnapshot = std::make_shared<Viewer>(); htmlSnapshot->pdfMode = true; htmlSnapshot->htmlMode = true; htmlSnapshot->selectionOnly = true; htmlSnapshot->captureContent();
    bool htmlDone = false; std::wstring editorHtml, htmlError;
    ok &= check(startHtmlExport(panel, htmlSnapshot, [&](const std::wstring& html, const std::wstring& error) { editorHtml = html; htmlError = error; htmlDone = true; }), "HTML starts from frozen selected editor Markdown");
    auto beforeHtml = textBuffer; textBuffer = "# CHANGED_AFTER_HTML_SNAPSHOT\n";
    for (int i = 0; !htmlDone && i < 1200; ++i) pump(100);
    if (!htmlDone) cancelHtml();
    ok &= check(htmlDone && htmlError.empty() && editorHtml.find(L"EDITOR_SELECTION_TOKEN") != std::wstring::npos && editorHtml.find(L"CHANGED_AFTER_HTML_SNAPSHOT") == std::wstring::npos && editorHtml.find(L"OUTSIDE_") == std::wstring::npos && editorHtml.find(L"<strong>") != std::wstring::npos && !htmlExport, "HTML renders only editor selection and preserves its immutable snapshot");
    ok &= check(editorHtml.find(L"<svg") != std::wstring::npos && editorHtml.find(L"HTML_SELECTED_NODE") != std::wstring::npos && editorHtml.find(L"<script") == std::wstring::npos && GetFileAttributesW(htmlSnapshot->page.c_str()) == INVALID_FILE_ATTRIBUTES, "HTML editor selection renders static Mermaid and removes its temporary page");
    editorSelection = originalEditorSelection;
    textBuffer = beforeHtml;
    for (bool word : {false, true}) {
        auto snapshot = std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = word; snapshot->selectionOnly = true; snapshot->captureContent();
        ok &= check(selectedExport(prefix + (word ? L"-editor-selection.docx" : L"-editor-selection.pdf"), word, snapshot), word ? "DOCX renders selected editor Markdown only" : "PDF renders selected editor Markdown only");
    }
    editorSelection.clear();
    eval(L"window.getSelection().removeAllRanges();document.getElementById('zoom-value').focus();document.getElementById('zoom-value').select();window.__copyDefault=new Event('copy',{bubbles:true,cancelable:true});document.getElementById('zoom-value').dispatchEvent(window.__copyDefault)");
    ok &= check(eval(L"!window.__copyDefault.defaultPrevented") == L"true", "toolbar text inputs keep their normal copy behavior");
    eval(L"document.activeElement?.blur();window.getSelection().removeAllRanges()");
    return ok;
}
static bool edgeLabelTests(const std::wstring& fixtures, const std::wstring& prefix) {
    bool ok = true;
    loadFixture(fixtures + L"\\edge-labels.md");
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent==='Arrow label backgrounds' && window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram[data-render-state=ready]').length===4"), "edge label fixtures render");
    for (bool darkTheme : {false, true}) {
        eval(darkTheme ? L"window.markdownView.setTheme(true)" : L"window.markdownView.setTheme(false)");
        waitFor(L"window.markdownView.mermaidReady");
        eval(LR"JS(
          window.__contrastRatio=(ink,paper)=>{
            const l=c=>{const v=(c.match(/[\d.]+/g)||[]).slice(0,3).map(Number).map(v=>v/255).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4);return v[0]*.2126+v[1]*.7152+v[2]*.0722;};
            const a=l(ink),b=l(paper);return (Math.max(a,b)+.05)/(Math.min(a,b)+.05);
          };
        )JS");
        ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelectorAll('.cluster-label text,.cluster-label tspan')].filter(t=>t.textContent.trim()).every(t=>__contrastRatio(getComputedStyle(t).fill,getComputedStyle(t.closest('.cluster').querySelector('rect')).fill)>=4.5)")==L"true", darkTheme ? "dark theme cluster titles have readable contrast on custom light backgrounds" : "light theme cluster titles have readable contrast on custom backgrounds");
        ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelectorAll('.node text,.node tspan')].filter(t=>t.textContent.trim()).every(t=>__contrastRatio(getComputedStyle(t).fill,getComputedStyle(t.closest('.node').querySelector('rect,polygon,circle,ellipse,path')).fill)>=4.5)")==L"true", darkTheme ? "dark theme custom node labels meet contrast including midtone backgrounds" : "light theme custom node labels meet contrast including midtone backgrounds");
        ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelectorAll('path.flowchart-link')].every(p=>__contrastRatio(getComputedStyle(p).stroke,'rgb(255, 242, 204)')>=3)")==L"true", darkTheme ? "dark theme connectors remain visible on the light nested container" : "light theme connectors retain readable contrast");
        ok &= check(eval(L"(()=>{const s=document.querySelectorAll('.mermaid-graphic')[1].shadowRoot;return [...s.querySelectorAll('path.flowchart-link')].every(p=>{const id=getComputedStyle(p).markerEnd.match(/#([^\\s\"')]+)/)?.[1],m=id&&s.getElementById(id);return m&&[...m.querySelectorAll('.arrowMarkerPath')].every(a=>__contrastRatio(getComputedStyle(a).fill,'rgb(255, 242, 204)')>=3)})})()")==L"true", darkTheme ? "dark theme arrowheads remain visible on the light nested container" : "light theme arrowheads retain readable contrast");
        ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelectorAll('.node tspan')].some(t=>t.textContent==='Finish'&&getComputedStyle(t).fill==='rgb(234, 255, 238)')")==L"true", "readable authored text color is preserved");
        ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')].flatMap(g=>[...g.shadowRoot.querySelectorAll('.edgeLabel rect,.classLabel rect.box')]).filter(r=>r.getBoundingClientRect().width>0).every(r=>getComputedStyle(r).opacity==='1'&&getComputedStyle(r).fillOpacity==='1'&&!getComputedStyle(r).fill.startsWith('rgba'))") == L"true", darkTheme ? "dark label backgrounds fully hide crossing lines" : "light label backgrounds fully hide crossing lines");
        ok &= check(eval(darkTheme ? L"getComputedStyle(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('.edgeLabel rect')).fill==='rgb(30, 30, 30)'" : L"getComputedStyle(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('.edgeLabel rect')).fill==='rgb(255, 255, 255)'") == L"true", "root label background matches the diagram surface");
        ok &= check(eval(L"getComputedStyle(document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelector('.edgeLabel rect')).fill==='rgb(255, 242, 204)'") == L"true", "nested label background matches the innermost colored container");
        if (darkTheme) ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')[1].shadowRoot.querySelectorAll('.edgeLabel text,.edgeLabel tspan')].filter(t=>t.textContent.trim()&&t.getBoundingClientRect().width>0).every(t=>__contrastRatio(getComputedStyle(t).fill,'rgb(255, 242, 204)')>=4.5)") == L"true", "labels stay readable on a light colored container in dark theme");
        ok &= check(eval(L"(()=>{const s=document.querySelectorAll('.mermaid-graphic')[1].shadowRoot;return [...s.querySelectorAll('.node > rect:not([aria-hidden=true])')].every(r=>{const c=getComputedStyle(r);return __contrastRatio(c.stroke,c.fill)>=3&&__contrastRatio(c.stroke,'rgb(255, 242, 204)')>=3&&+c.strokeOpacity===1&&parseFloat(c.strokeWidth)>=1.25})})()")==L"true",darkTheme?"dark node borders contrast with both authored fills and surrounding container":"light node borders contrast with both authored fills and surrounding container");
        ok &= check(eval((std::wstring(L"(()=>{const s=document.querySelectorAll('.mermaid-graphic')[1].shadowRoot;return [...s.querySelectorAll('.cluster > rect:not([aria-hidden=true])')].every(r=>{const c=getComputedStyle(r),outer=r.parentElement.id.endsWith('-OUTER'),paper=outer?'")+(darkTheme?L"rgb(30, 30, 30)":L"rgb(255, 255, 255)")+L"':'rgb(229, 241, 255)';return __contrastRatio(c.stroke,c.fill)>=3&&__contrastRatio(c.stroke,paper)>=3&&+c.strokeOpacity===1})})()").c_str())==L"true",darkTheme?"dark nested container borders contrast on both sides":"light nested container borders contrast on both sides");
        capture(prefix + (darkTheme ? L"-edge-labels-dark.png" : L"-edge-labels-light.png"));
    }
    ok &= check(pdfTest(prefix + L"-edge-labels.pdf"), "PDF exports readable vector edge labels");
    ok &= check(docxTest(prefix + L"-edge-labels.docx"), "DOCX exports readable edge labels as SVG and PNG");
    const auto savedEdgeText = textBuffer;
    textBuffer = "# Mixed connector backgrounds\n\n```mermaid\nflowchart LR\n subgraph LIGHT[Light]\n A[Alpha] -.-> B[Beta]\n end\n subgraph DARK[Dark]\n C[Gamma]\n end\n B --> C\n C <--> A\n style LIGHT fill:#fff2cc\n style DARK fill:#223344\n linkStyle 0 stroke:#0066cc,stroke-width:2px\n```\n\n```mermaid\ngantt\n title Decorative grid\n dateFormat YYYY-MM-DD\n section Work\n Task :2026-01-01, 3d\n```\n";
    panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent==='Mixed connector backgrounds' && window.markdownView.mermaidReady"), "mixed light and dark container fixture renders");
    for (bool darkTheme : {false, true}) {
        eval(darkTheme ? L"window.markdownView.setTheme(true)" : L"window.markdownView.setTheme(false)"); waitFor(L"window.markdownView.mermaidReady");
        ok &= check(eval(L"(()=>{const s=document.querySelector('.mermaid-graphic').shadowRoot,p=s.querySelector('path[id*=L_A_B]'),c=getComputedStyle(p);return c.stroke==='rgb(0, 102, 204)'&&parseFloat(c.strokeWidth)===2&&c.strokeDasharray!=='none'&&c.strokeDasharray!=='0px'})()") == L"true", darkTheme ? "dark theme preserves readable authored blue, width and dashed edge semantics" : "light theme preserves readable authored blue, width and dashed edge semantics");
        ok &= check(eval(L"[...document.querySelector('.mermaid-graphic').shadowRoot.querySelectorAll('path[aria-hidden=true]')].some(h=>{const p=h.nextElementSibling;return p?.getAttribute('d')===h.getAttribute('d')&&parseFloat(getComputedStyle(h).strokeWidth)>parseFloat(getComputedStyle(p).strokeWidth)&&getComputedStyle(h).stroke!==getComputedStyle(p).stroke&&['markerStart','markerMid','markerEnd'].every(k=>getComputedStyle(h)[k]==='none')})") == L"true", darkTheme ? "dark theme mixed-background connectors have an opposite-color underlay without duplicate arrowheads" : "light theme mixed-background connectors have an opposite-color underlay without duplicate arrowheads");
        ok &= check(eval(L"(()=>{const s=document.querySelectorAll('.mermaid-graphic')[1].shadowRoot,lines=[...s.querySelectorAll('.grid .tick line')];return lines.length>0&&lines.every(l=>{const c=l.cloneNode(true);c.style.removeProperty('stroke');l.after(c);const unchanged=getComputedStyle(l).stroke===getComputedStyle(c).stroke;c.remove();return unchanged})})()") == L"true", darkTheme ? "dark theme decorative Gantt grid keeps its original subdued color" : "light theme decorative Gantt grid keeps its original subdued color");
        capture(prefix + (darkTheme ? L"-mixed-arrows-dark.png" : L"-mixed-arrows-light.png"));
    }
    textBuffer = savedEdgeText; panel->load(); waitFor(L"document.querySelector('h1')?.textContent==='Arrow label backgrounds' && window.markdownView.mermaidReady");
    eval(L"window.markdownView.setTheme(false)"); waitFor(L"window.markdownView.mermaidReady");
    return ok;
}
static bool navigationTests(const std::wstring& fixtures, const std::wstring& prefix) {
    bool ok = true;
    const auto savedPath = documentPath; const auto savedText = textBuffer; const auto savedHistory = panel->history;
    panel->history = {}; openedBuffers.clear();
    auto rendered = [](const wchar_t* title) {
        return waitFor((L"document.querySelector('h1')?.textContent==='" + std::wstring(title) + L"' && window.markdownView.mermaidReady").c_str());
    };
    loadFixture(fixtures + L"\\fixtures\\navigation-a.md");
    ok &= check(rendered(L"Navigation A") && eval(L"document.getElementById('history-back').disabled && document.getElementById('history-forward').disabled") == L"true", "navigation starts with disabled native-style back and forward buttons");
    ok &= check(eval(L"(()=>{const controls=document.getElementById('view-controls'),ids=[...controls.querySelectorAll('button')].map(e=>e.id);return ids.slice(0,4).join(',')==='outline-button,history-back,history-forward,copy';})()") == L"true",
        "toolbar order is outline, back, forward, copy");
    eval(L"window.markdownView.setTheme(false)"); waitFor(L"window.markdownView.mermaidReady");
    eval(L"document.getElementById('theme-button').click()");
    ok &= check(waitFor(L"window.markdownView.mermaidReady && document.querySelector('.mermaid-diagram').dataset.theme==='dark'") && panel->dark &&
        eval(L"document.getElementById('theme-button').getAttribute('aria-pressed')==='true' && document.getElementById('theme-button').title==='Switch to light theme'") == L"true", "toolbar theme button changes Mermaid colors and native preview theme state");
    textBuffer += "\nUNSAVED_NAVIGATION_A\n"; panel->load();
    waitFor(L"window.markdownView.mermaidReady && document.querySelector('.markdown-body').textContent.includes('UNSAVED_NAVIGATION_A')");
    eval(L"window.scrollTo(0,220);document.getElementById('outline-button').click();document.querySelector('#outline a[href=\"#details\"]').click()");
    ok &= check(waitFor(L"window.scrollY>700 && !document.getElementById('history-back').disabled") && panel->history.visits.size() == 2,
        "outline section jump creates a same-document history visit");
    eval(L"window.__outlineY=window.scrollY;document.getElementById('history-back').click()");
    ok &= check(waitFor(L"Math.abs(window.scrollY-220)<2 && !document.getElementById('history-forward').disabled") && textBuffer.find("UNSAVED_NAVIGATION_A") != std::string::npos,
        "Back from the outline restores the previous position and unsaved content");
    eval(L"document.getElementById('history-forward').click()");
    ok &= check(waitFor(L"Math.abs(window.scrollY-window.__outlineY)<2"), "Forward returns to the section chosen in the outline");
    eval(L"document.getElementById('history-back').click()"); waitFor(L"Math.abs(window.scrollY-220)<2");
    eval(L"document.querySelector('.markdown-body a[href=\"#details\"]').click()");
    ok &= check(waitFor(L"Math.abs(window.scrollY-window.__outlineY)<2 && document.getElementById('history-forward').disabled") && panel->history.visits.size() == 2,
        "inline section links share history and replace its forward branch");
    eval(L"document.querySelector('#outline a[href=\"#navigation-a\"]').click()");
    ok &= check(waitFor(L"window.scrollY<80") && panel->history.visits.size() == 3,
        "multiple outline headings retain separate positions in the same file");
    eval(L"document.getElementById('history-back').click()");
    ok &= check(waitFor(L"Math.abs(window.scrollY-window.__outlineY)<2"), "Back returns between two headings without changing the document");
    eval(L"document.getElementById('outline-button').click()");
    panel->history = {}; panel->load();
    waitFor(L"document.getElementById('history-back').disabled && document.getElementById('history-forward').disabled");
    eval(L"window.scrollTo(0,600);window.__navigationAY=window.scrollY;document.querySelector('a[href=\"navigation-b.md#chapter\"]').click()");
    ok &= check(rendered(L"Navigation B") && documentPath == fixtures + L"\\fixtures\\navigation-b.md" && panel->history.visits.size() == 2,
        "Markdown link opens the native document and creates one history visit");
    ok &= check(eval(L"(()=>{const y=document.getElementById('chapter').getBoundingClientRect().top,t=document.getElementById('toolbar').offsetHeight;return y>=t&&y<t+20&&window.scrollY>100;})()") == L"true", "cross-document fragment scrolls below the fixed toolbar");
    ok &= check(eval(L"document.documentElement.classList.contains('dark') && !document.getElementById('history-back').disabled && document.getElementById('history-forward').disabled") == L"true", "navigation keeps the selected theme and updates available directions");
    eval(L"window.scrollTo(0,350);document.querySelector('a[href=\"navigation-c.md\"]').click()");
    ok &= check(rendered(L"Navigation C") && panel->history.visits.size() == 3, "A to B to C preserves a three-document history");
    eval(L"document.getElementById('history-back').click()");
    ok &= check(rendered(L"Navigation B") && eval(L"Math.abs(window.scrollY-350)<2 && !document.getElementById('history-forward').disabled") == L"true", "Back restores B and its saved scroll position");
    eval(L"document.getElementById('history-back').click()");
    ok &= check(rendered(L"Navigation A") && textBuffer.find("UNSAVED_NAVIGATION_A") != std::string::npos &&
        eval(L"window.__navigationAY>100 && Math.abs(window.scrollY-window.__navigationAY)<2 && document.querySelector('.markdown-body').textContent.includes('UNSAVED_NAVIGATION_A')") == L"true",
        "Back reactivates unsaved A without reloading its disk content");
    eval(L"document.getElementById('history-forward').click()");
    ok &= check(rendered(L"Navigation B") && eval(L"Math.abs(window.scrollY-350)<2") == L"true", "Forward restores B without duplicating history");
    eval(L"document.querySelector('a[href=\"navigation-d.md\"]').click()");
    ok &= check(rendered(L"Navigation D") && panel->history.visits.size() == 3 && eval(L"document.getElementById('history-forward').disabled") == L"true",
        "new link after Back replaces the obsolete forward branch");
    panel->load(); textBuffer += "\nUPDATED_D\n"; panel->load();
    ok &= check(waitFor(L"document.querySelector('.markdown-body').textContent.includes('UPDATED_D')") && panel->history.visits.size() == 3,
        "refresh and live editing do not append duplicate history visits");
    eval(L"document.getElementById('history-back').click()"); rendered(L"Navigation B");
    refusedOpenPath = fixtures + L"\\fixtures\\navigation-d.md";
    eval(L"document.getElementById('history-forward').click()");
    ok &= check(waitFor(L"!document.getElementById('navigation-status').hidden") && documentPath == fixtures + L"\\fixtures\\navigation-b.md" &&
        panel->history.index == 1 && panel->history.canForward(), "failed history opening preserves the document and forward branch and shows a hint");
    refusedOpenPath.clear();
    eval(L"document.getElementById('history-forward').click()");
    ok &= check(rendered(L"Navigation D") && textBuffer.find("UPDATED_D") != std::string::npos,
        "history retry returns to the modified open buffer");
    eval(L"document.getElementById('theme-button').click()");
    ok &= check(waitFor(L"window.markdownView.mermaidReady && !document.documentElement.classList.contains('dark')") && !panel->dark &&
        eval(L"document.getElementById('theme-button').getAttribute('aria-pressed')==='false' && document.getElementById('theme-button').title==='Switch to dark theme'") == L"true", "theme button returns to light with the matching hint");
    MoveWindow(panel->hwnd, 0, 0, 320, 820, TRUE); pump(300);
    ok &= check(eval(L"[...document.querySelectorAll('#toolbar button,#zoom-combo')].every(e=>e.getBoundingClientRect().right<=document.documentElement.clientWidth+1) && Math.abs(parseFloat(getComputedStyle(document.body).paddingTop)-document.getElementById('toolbar').offsetHeight)<1") == L"true", "navigation and theme controls wrap within a narrow preview");
    capture(prefix + L"-navigation-narrow.png");
    MoveWindow(panel->hwnd, 0, 0, 1000, 820, TRUE); pump(300);
    loadFixture(fixtures + L"\\fixtures\\navigation-a.md"); rendered(L"Navigation A"); capture(prefix + L"-navigation-light.png");
    eval(L"document.getElementById('theme-button').click()"); waitFor(L"window.markdownView.mermaidReady"); capture(prefix + L"-navigation-dark.png");
    notify(NPPN_DARKMODECHANGED);
    ok &= check(waitFor(L"window.markdownView.mermaidReady && !document.documentElement.classList.contains('dark')") && !panel->dark,
        "Notepad++ theme notification still updates the preview after a manual toggle");
    panel->history = savedHistory; openedBuffers.clear(); documentPath = savedPath; textBuffer = savedText; panel->load();
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3 && window.markdownView.mermaidReady"), "preview restores the original buffer after navigation tests");
    return ok;
}

static bool zoomTests(const wchar_t* fixtures) {
    bool ok = true;
    ComPtr<ICoreWebView2Settings> settings;
    panel->webview->get_Settings(settings.put());
    BOOL browserZoom = TRUE; double factor = 0;
    if (settings) settings->get_IsZoomControlEnabled(&browserZoom);
    panel->controller->get_ZoomFactor(&factor);
    ok &= check(settings && !browserZoom && factor == 1.0, "browser zoom is disabled and controller scale is 1");
    loadFixture(std::wstring(fixtures) + L"\\mermaid.md");
    ok &= check(waitFor(L"window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram[data-render-state=ready]').length===6"), "zoom fixture renders six Mermaid diagrams");
    eval(LR"JS(
      window.markdownView.setZoom('actual');
      window.__zoomToolbar = document.getElementById('toolbar').getBoundingClientRect().height;
      window.__zoomSvg = document.querySelector('.mermaid-graphic').shadowRoot.querySelector('svg');
      window.__zoomWidth = window.__zoomSvg.getBoundingClientRect().width;
      window.__wheel = (dy, ctrl=true, target=window.__zoomSvg) => {
        const event = new WheelEvent('wheel', {deltaY:dy, ctrlKey:ctrl, bubbles:true, composed:true, cancelable:true});
        target.dispatchEvent(event); return event.defaultPrevented;
      };
    )JS");
    ok &= check(eval(L"__wheel(-120) && document.getElementById('zoom-value').value==='125%' && document.getElementById('zoom-presets').value==='125' && Math.abs(__zoomSvg.getBoundingClientRect().width/__zoomWidth-1.25)<0.02 && document.getElementById('toolbar').getBoundingClientRect().height===__zoomToolbar") == L"true", "Ctrl wheel from Mermaid shadow DOM enlarges only document and synchronizes controls");
    ok &= check(eval(L"!__wheel(120,false) && document.getElementById('zoom-value').value==='125%'") == L"true", "ordinary wheel keeps native scrolling and leaves zoom unchanged");
    eval(L"document.getElementById('zoom-value').value='204%';document.getElementById('zoom-value').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));__wheel(120)");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='200%' && document.getElementById('zoom-presets').value==='200'") == L"true", "wheel continues from custom percentage using the same steps as buttons");
    eval(L"document.getElementById('zoom-out').click();__wheel(-120, true, document.getElementById('toolbar'))");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='200%' && document.getElementById('toolbar').getBoundingClientRect().height===__zoomToolbar") == L"true", "button and wheel over toolbar share document zoom without resizing controls");
    eval(L"document.getElementById('zoom-presets').value='50';document.getElementById('zoom-presets').dispatchEvent(new Event('change'));__wheel(-120)");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='75%'") == L"true", "wheel continues from dropdown percentage");
    for (const auto fit : {L"width", L"content"}) {
        eval((std::wstring(L"window.markdownView.setZoom('") + fit + L"');window.__fit=parseFloat(document.getElementById('zoom-value').value);__wheel(-120)").c_str());
        ok &= check(eval(L"Number(document.querySelector('.markdown-body').dataset.viewZoom)>__fit && document.getElementById('zoom-presets').value!=='width' && document.getElementById('zoom-presets').value!=='content'") == L"true", "wheel switches fit mode to the next explicit percentage");
    }
    ok &= check(eval(L"window.markdownView.setZoom(6400);__wheel(-120) && document.getElementById('zoom-value').value==='6400%' && document.getElementById('zoom-in').disabled") == L"true", "upper limit cancels wheel default without additional browser zoom");
    ok &= check(eval(L"window.markdownView.setZoom(10);__wheel(120) && document.getElementById('zoom-value').value==='10%' && document.getElementById('zoom-out').disabled") == L"true", "lower limit cancels wheel default without additional browser zoom");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'0',ctrlKey:true,bubbles:true}));document.dispatchEvent(new KeyboardEvent('keydown',{key:'+',ctrlKey:true,bubbles:true}));__wheel(120)");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='100%' && document.getElementById('toolbar').getBoundingClientRect().height===__zoomToolbar") == L"true", "Ctrl 0 Ctrl plus and wheel share scale and keep toolbar geometry");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'-',ctrlKey:true,bubbles:true}))");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='75%'") == L"true", "Ctrl minus selects previous document zoom step");
    eval(L"window.markdownView.setZoom('actual')");
    return ok;
}

static bool sourceJumpTests() {
    bool ok = true;
    const auto savedPath = documentPath; const auto savedText = textBuffer;
    const auto savedFirst = firstLine; const bool savedSync = syncScroll;
    documentPath = moduleDirectory() + L"\\rc2-jump-top.md";
    textBuffer = "# Jump alignment\n\n";
    for (int i=0; i<90; ++i) textBuffer += "Paragraph " + std::to_string(i) + ". Source navigation fixture.\n\n";
    const int targetLine = std::count(textBuffer.begin(), textBuffer.end(), '\n');
    textBuffer += "## Near-end **target**\n\nShort tail.\n\n[Local link](#jump-alignment)\n";
    panel->load();
    ok &= check(waitFor(L"!!document.getElementById('near-end-target') && window.markdownView.mermaidReady"), "source jump near-end fixture renders");
    for (const bool follow : {false,true}) for (const int zoom : {100,200}) {
        syncScroll = follow; firstLine = 0; jumpedLine = ensuredLine = -1;
        eval((L"window.markdownView.setZoom("+std::to_wstring(zoom)+L");window.scrollTo(0,0);document.querySelector('#near-end-target strong').dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))").c_str());
        ok &= check(waitFor(L"Math.abs(document.getElementById('near-end-target').getBoundingClientRect().top-document.getElementById('toolbar').offsetHeight-12)<3"), "double-click aligns near-end Preview target below fixed toolbar at either zoom/sync mode");
        pump(150);
        ok &= check(jumpedLine == targetLine && firstLine == targetLine && ensuredLine == targetLine, "double-click reveals source and places its line at the top of the editor");
        SCNotification scroll{}; scroll.nmhdr.code=SCN_UPDATEUI;scroll.nmhdr.hwndFrom=editor();scroll.updated=SC_UPDATE_V_SCROLL;beNotified(&scroll);pump(150);
        ok &= check(eval(L"Math.abs(document.getElementById('near-end-target').getBoundingClientRect().top-document.getElementById('toolbar').offsetHeight-12)<3")==L"true", "editor scroll notification does not undo explicit Preview alignment");
    }
    ok &= check(eval(L"!!document.getElementById('source-jump-space') && !document.querySelector('.markdown-body').contains(document.getElementById('source-jump-space'))")==L"true", "near-end navigation space is outside Markdown and export/copy content");
    wrappedDocumentLine=targetLine;firstWrappedDisplayLine=700;displayedWrapCount=4;firstLine=703;
    jumpToLine(targetLine);pump(150);
    ok &= check(firstLine==700 && editorLine()==targetLine, "source jump uses first wrapped display line instead of the document line number");
    const int previousJump=jumpedLine,previousFirst=firstLine;
    jumpToLine(-1);jumpToLine(10000);
    ok &= check(jumpedLine==previousJump && firstLine==previousFirst, "invalid source navigation does not change the caret or viewport");
    eval(L"document.querySelector('.markdown-body a').dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))");pump(100);
    ok &= check(jumpedLine==previousJump, "double-click on a link is not converted into a source jump");
    syncScroll=savedSync;firstLine=savedFirst;wrappedDocumentLine=-1;displayedWrapCount=1;
    eval(L"window.markdownView.setZoom(100)");
    documentPath=savedPath;textBuffer=savedText;panel->load();
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3 && window.markdownView.mermaidReady && !document.getElementById('source-jump-space')"), "navigation space is cleared when another document opens");
    return ok;
}
static bool interiorSourceJumpTests() {
    bool ok=true;
    const auto savedPath=documentPath;const auto savedText=textBuffer;const bool savedSync=syncScroll;
    documentPath=moduleDirectory()+L"\\interior-jump.md";
    textBuffer="# Interior source jump\n\n```cpp\n";
    for(int i=0;i<110;++i)textBuffer+="const int CLICK_"+std::to_string(i)+" = "+std::to_string(i)+";\n";
    textBuffer+="```\n\n";
    const int paragraphLine=std::count(textBuffer.begin(),textBuffer.end(),'\n');
    for(int i=0;i<5;++i)textBuffer+="PARACLICK_"+std::to_string(i)+" **formatted bold** text and Кириллица.\n";
    textBuffer+="\n";
    for(int i=0;i<80;++i)textBuffer+="Tail paragraph "+std::to_string(i)+".\n\n";
    syncScroll=false;panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent==='Interior source jump' && window.markdownView.mermaidReady"),"interior double-click fixture renders");
    eval(LR"JS(
      window.__clickText=needle=>{
        const walker=document.createTreeWalker(document.querySelector('.markdown-body'),NodeFilter.SHOW_TEXT);let node;
        while(node=walker.nextNode()){
          const index=node.data.indexOf(needle);if(index<0)continue;
          const range=document.createRange();range.setStart(node,index);range.setEnd(node,index+1);
          const r=range.getBoundingClientRect();
          node.parentElement.dispatchEvent(new MouseEvent('dblclick',{bubbles:true,clientX:r.left+r.width/2,clientY:r.top+r.height/2}));
          return true;
        }return false;
      };
      window.__clickedTop=needle=>{
        const walker=document.createTreeWalker(document.querySelector('.markdown-body'),NodeFilter.SHOW_TEXT);let node;
        while(node=walker.nextNode()){const i=node.data.indexOf(needle);if(i<0)continue;const r=document.createRange();r.setStart(node,i);r.setEnd(node,i+1);return Math.abs(r.getBoundingClientRect().top-document.getElementById('toolbar').offsetHeight-12)<3;}return false;
      };
    )JS");
    for(const bool follow:{false,true})for(const int zoom:{100,200}){
        syncScroll=follow;jumpedLine=-1;
        eval((L"window.markdownView.setZoom("+std::to_wstring(zoom)+L");window.markdownView.scrollToLine(80);__clickText('CLICK_77')").c_str());pump(150);
        ok &= check(jumpedLine==80&&firstLine==80,"double-click maps highlighted interior code text to its physical source line at either zoom/sync mode");
        ok &= check(eval(L"__clickedTop('CLICK_77')")==L"true","double-click keeps the clicked code line at the top instead of the opening fence");
        jumpedLine=-1;
        eval((L"window.markdownView.scrollToLine("+std::to_wstring(paragraphLine+3)+L");__clickText('PARACLICK_3')").c_str());pump(150);
        ok &= check(jumpedLine==paragraphLine+3&&firstLine==paragraphLine+3,"double-click maps soft-break paragraph text through inline formatting to its physical source line");
        ok &= check(eval(L"__clickedTop('PARACLICK_3')")==L"true","double-click aligns the selected paragraph source line in Preview");
    }
    syncScroll=savedSync;documentPath=savedPath;textBuffer=savedText;
    eval(L"window.markdownView.setZoom(100)");panel->load();
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3 && window.markdownView.mermaidReady"),"original document restores after interior double-click regression");
    return ok;
}
static bool scrollSyncTests() {
    bool ok = true;
    const auto savedPath = documentPath; const auto savedText = textBuffer;
    const auto savedFirst = firstLine; const bool savedSync = syncScroll;
    documentPath = moduleDirectory() + L"\\scroll-sync.md";
    textBuffer = "# Scroll sync\n\n```cpp\n";
    for (int i=0; i<150; ++i) textBuffer += "const int ROW_" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    textBuffer += "```\n\n## Middle\n\n";
    const int paragraphLine = std::count(textBuffer.begin(), textBuffer.end(), '\n');
    for (int i=0; i<8; ++i) {
        textBuffer += "PARA_" + std::to_string(i) + " ";
        for (int j=0; j<35; ++j) textBuffer += "wrapped words ";
        textBuffer += "**bold text** and Кириллица.\n";
    }
    textBuffer += "\n";
    const int singleLine = std::count(textBuffer.begin(), textBuffer.end(), '\n');
    textBuffer += "LONG_SINGLE "; for (int j=0; j<1000; ++j) textBuffer += "wide paragraph ";
    textBuffer += "\n\n| Name | Value |\n| --- | --- |\n";
    const int tableLine = std::count(textBuffer.begin(), textBuffer.end(), '\n');
    for (int j=0; j<12; ++j) textBuffer += "| TABLE_" + std::to_string(j) + " | value |\n";
    textBuffer += "\n```text\n"; for (int j=0; j<250; ++j) textBuffer += "TAIL_" + std::to_string(j) + "\n";
    textBuffer += "```\n"; syncScroll = false; panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent==='Scroll sync' && window.markdownView.mermaidReady"), "long code and wrapped paragraph scroll fixture renders");
    eval(LR"JS(
      window.__syncTop = needle => {
        const walker=document.createTreeWalker(document.querySelector('.markdown-body'),NodeFilter.SHOW_TEXT);let node;
        while(node=walker.nextNode()) {const offset=node.data.indexOf(needle);if(offset>=0){const r=document.createRange();r.setStart(node,offset);r.setEnd(node,offset+1);return r.getBoundingClientRect().top;}}
        return -100000;
      };
      window.__syncAligned = needle => Math.abs(__syncTop(needle)-document.getElementById('toolbar').offsetHeight-12)<3;
    )JS");
    eval(L"window.markdownView.scrollToLine(53)");
    ok &= check(waitFor(L"__syncAligned('ROW_50')"), "scroll aligns the actual highlighted line inside a long fenced block");
    eval(L"window.__syncBefore=window.scrollY;window.markdownView.scrollToLine(103)");
    ok &= check(waitFor(L"__syncAligned('ROW_100') && window.scrollY>__syncBefore+500"), "scroll advances within a block without waiting for a new heading");
    eval(L"window.markdownView.scrollToLine(23)");
    ok &= check(waitFor(L"__syncAligned('ROW_20')"), "scroll follows reverse movement within a long block");
    eval((L"window.markdownView.scrollToLine("+std::to_wstring(paragraphLine+3)+L")").c_str());
    ok &= check(waitFor(L"__syncAligned('PARA_3')"), "soft-break source lines align inside a wrapped paragraph with inline formatting");
    eval((L"window.markdownView.scrollToLine("+std::to_wstring(tableLine+8)+L")").c_str());
    ok &= check(waitFor(L"Math.abs(document.querySelectorAll('tbody tr')[8].getBoundingClientRect().top-document.getElementById('toolbar').offsetHeight-12)<3"), "table row source ranges select the row instead of the table start");
    firstLine=2005;wrappedDocumentLine=singleLine;firstWrappedDisplayLine=2000;displayedWrapCount=10;
    ok &= check(editorLine()==singleLine+0.5, "native editor preserves the subline position within a wrapped logical line");
    const auto scrollNotification = [] {
        SCNotification notification{}; notification.nmhdr.code=SCN_UPDATEUI; notification.nmhdr.hwndFrom=editor();
        notification.updated=SC_UPDATE_V_SCROLL; beNotified(&notification); pump(150);
    };
    syncScroll=true;scrollNotification();
    ok &= check(eval(L"(()=>{const p=[...document.querySelectorAll('p')].find(p=>p.textContent.startsWith('LONG_SINGLE')),t=p.firstChild,r=document.createRange();r.setStart(t,Math.floor(t.length/2));r.setEnd(t,Math.floor(t.length/2)+1);return Math.abs(r.getBoundingClientRect().top-document.getElementById('toolbar').offsetHeight-12)<3})()")==L"true", "native scroll notification follows the middle of a wrapped single-line paragraph");
    displayedWrapCount=0;
    ok &= check(editorLine()==singleLine, "unavailable wrap information safely falls back to the document line");
    wrappedDocumentLine=-1;displayedWrapCount=1;firstLine=53;syncScroll=false;
    eval(L"window.__syncBefore=window.scrollY");scrollNotification();
    ok &= check(eval(L"window.scrollY===__syncBefore")==L"true", "disabled synchronization leaves Preview scrolling untouched");
    syncScroll=true;scrollNotification();
    ok &= check(eval(L"__syncAligned('ROW_50')")==L"true", "native scroll notification moves to the interior source line when enabled");
    eval(L"window.markdownView.setZoom(200);window.scrollTo(100,window.scrollY);window.markdownView.scrollToLine(73)");
    ok &= check(eval(L"__syncAligned('ROW_70') && window.scrollX===100")==L"true", "source synchronization uses zoomed geometry and preserves horizontal position");
    eval(L"window.__syncBefore=window.scrollY;window.markdownView.scrollToLine(-1);window.markdownView.scrollToLine(NaN)");
    ok &= check(eval(L"window.scrollY===__syncBefore")==L"true", "invalid source positions do not change scrolling");
    eval(L"window.markdownView.setZoom('actual')");
    syncScroll=savedSync;firstLine=savedFirst;wrappedDocumentLine=-1;displayedWrapCount=1;
    documentPath=savedPath;textBuffer=savedText;panel->load();
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3 && window.markdownView.mermaidReady"), "original preview restores after source synchronization tests");
    return ok;
}
static HWND widthTestDock = nullptr, widthTestSplitter = nullptr;
static int widthTestMoves = 0;
static LRESULT CALLBACK widthTestManagerProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == 0x5000 + 11 && reinterpret_cast<HWND>(lp) == widthTestSplitter) {
        RECT frame{}, dock{}; GetClientRect(npp._nppHandle, &frame); GetWindowRect(widthTestDock, &dock);
        const int width = dock.right - dock.left + static_cast<int>(wp); ++widthTestMoves;
        MoveWindow(widthTestDock, frame.right - width, 0, width, frame.bottom, TRUE);
        MoveWindow(widthTestSplitter, frame.right - width - 4, 0, 4, frame.bottom, TRUE);
        MoveWindow(editor(), 0, 0, frame.right - width - 4, frame.bottom, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
static bool panelWidthTests(const wchar_t* output) {
    bool ok = true;
    ok &= check(initialPanelWidth(1400, 1200, 200, 96) == 700, "first preview width shares a normal window equally with the editor");
    ok &= check(initialPanelWidth(2800, 2400, 400, 192) == 1400, "initial preview width follows per-window DPI scaling");
    ok &= check(initialPanelWidth(4000, 3800, 200, 96) == 720, "large displays keep the initial preview at a readable capped width");
    ok &= check(initialPanelWidth(500, 300, 200, 96) == 200, "small windows do not squeeze an already narrow editor further");
    ok &= check(savedPanelLayout("<PluginDlg id='0' pluginName = 'markdown_WITH_Mermaid.DLL' curr='1'/>") &&
        savedPanelLayout("<PluginDlg pluginName=\"Markdown_with_Mermaid.dll\" />"), "saved docking records recognize the plugin with XML whitespace and either quote style");
    ok &= check(!savedPanelLayout("<PluginDlg pluginName='Other.dll'/><PluginDlg pluginName='Markdown_with_Mermaid.dll.backup'/>") && !savedPanelLayout(""), "other plugins do not suppress the first-use preview width");
    const auto previousConfig = hostConfigDirectory; const bool previousAutomatic = automatic, previousSync = syncScroll;
    const auto profile = std::wstring(output) + L"-width-profile-" + std::to_wstring(GetCurrentProcessId());
    hostConfigDirectory = profile + L"\\plugins\\Config";
    const auto preferences = settingsFile();
    WNDCLASSW managerClass{}; managerClass.hInstance = instance; managerClass.lpfnWndProc = widthTestManagerProc; managerClass.lpszClassName = L"dockingManager"; RegisterClassW(&managerClass);
    WNDCLASSW splitterClass = managerClass; splitterClass.lpfnWndProc = DefWindowProcW; splitterClass.lpszClassName = L"wedockspliter"; RegisterClassW(&splitterClass);
    RECT frame{}; GetClientRect(npp._nppHandle, &frame);
    HWND manager = CreateWindowExW(0, L"dockingManager", L"", WS_CHILD, 0, 0, 0, 0, npp._nppHandle, nullptr, instance, nullptr);
    widthTestDock = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, frame.right - 200, 0, 200, frame.bottom, npp._nppHandle, nullptr, instance, nullptr);
    widthTestSplitter = CreateWindowExW(0, L"wedockspliter", L"", WS_CHILD | WS_VISIBLE, frame.right - 204, 0, 4, frame.bottom, npp._nppHandle, nullptr, instance, nullptr);
    HWND client = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 300, widthTestDock, nullptr, instance, nullptr);
    MoveWindow(editor(), 0, 0, frame.right - 204, frame.bottom, FALSE); widthTestMoves = 0;
    RECT enlarged{};
    const bool applied = initializePanelWidth(client); GetWindowRect(widthTestDock, &enlarged);
    ok &= check(applied && widthTestMoves == 1 && enlarged.right - enlarged.left > 400 &&
        GetPrivateProfileIntW(L"Layout", L"InitialWidthApplied", 0, preferences.c_str()) == 1,
        "first-use docking grows the real Win32 container through its splitter and persists completion");
    MoveWindow(widthTestDock, frame.right - 300, 0, 300, frame.bottom, TRUE);
    MoveWindow(widthTestSplitter, frame.right - 304, 0, 4, frame.bottom, TRUE);
    setInfo(npp);
    const bool repeated = initializePanelWidth(client); GetWindowRect(widthTestDock, &enlarged);
    ok &= check(!repeated && widthTestMoves == 1 && enlarged.right - enlarged.left == 300,
        "reloading preferences preserves a manually narrowed panel rather than applying the initial width again");
    WritePrivateProfileStringW(L"Layout", L"InitialWidthApplied", nullptr, preferences.c_str());
    const auto configFile = profile + L"\\config.xml";
    const std::string restoredXml = "<NotepadPlus><GUIConfigs><GUIConfig name='DockingManager'><PluginDlg pluginName='Markdown_with_Mermaid.dll' id='0' curr='1'/></GUIConfig></GUIConfigs></NotepadPlus>";
    HANDLE config = CreateFileW(configFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0; if (config != INVALID_HANDLE_VALUE) { WriteFile(config, restoredXml.data(), static_cast<DWORD>(restoredXml.size()), &written, nullptr); CloseHandle(config); }
    const bool restored = initializePanelWidth(client); std::string unchanged; readBytes(configFile, unchanged);
    ok &= check(!restored && widthTestMoves == 1 && unchanged == restoredXml,
        "an existing Notepad++ docking record preserves the chosen layout without rewriting config.xml");
    DeleteFileW(configFile.c_str()); DestroyWindow(manager);
    ok &= check(!initializePanelWidth(client) && GetPrivateProfileIntW(L"Layout", L"InitialWidthApplied", 0, preferences.c_str()) == 0,
        "an unavailable native docking manager leaves layout and the first-use marker untouched");
    DestroyWindow(widthTestDock); DestroyWindow(widthTestSplitter); widthTestDock = widthTestSplitter = nullptr;
    MoveWindow(editor(), 0, 0, 10, 10, FALSE);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, preferences.c_str()); DeleteFileW(preferences.c_str());
    hostConfigDirectory = previousConfig; automatic = previousAutomatic; syncScroll = previousSync;
    return ok;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    if (argc > 3) printf("PDF regression input: %s\n", utf8(argv[3]).c_str());
    instance = GetModuleHandleW(nullptr); bool ok = true;
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = hostProc; wc.lpszClassName = L"Mermaid.TestHost";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MERMAID)); RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"Markdown_with_Mermaid integration tests", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        50, 50, 1030, 870, nullptr, nullptr, instance, nullptr);
    setTestWindowIcons(parent);
    HWND sci = CreateWindowExW(0, wc.lpszClassName, L"editor", WS_CHILD, 0, 0, 10, 10, parent, nullptr, instance, nullptr);
    documentPath = std::wstring(argv[1]) + L"\\Демонстрация.md"; readBytes(documentPath, textBuffer);
#ifndef MWM_DEVELOPMENT_RUNTIME_DIRECTORY
    hostConfigDirectory = std::wstring(argv[2]) + L"-defaults-" + std::to_wstring(GetCurrentProcessId());
    setInfo({parent, sci, sci});
    const auto defaultsFile = settingsFile();
    ok &= check(syncScroll && automatic && commands[2]._init2Check && commands[3]._init2Check && !commands[0]._init2Check,
        "first install enables scroll sync and Markdown auto-show without forcing panel visibility");
    ok &= check(GetFileAttributesW(defaultsFile.c_str()) == INVALID_FILE_ATTRIBUTES,
        "loading defaults does not write a settings file");
    toggleSync(); toggleAutomatic(); setInfo({parent, sci, sci});
    ok &= check(!syncScroll && !automatic && !commands[2]._init2Check && !commands[3]._init2Check,
        "explicitly disabled options remain disabled after reloading settings");
    WritePrivateProfileStringW(L"Options", L"AutoShow", nullptr, defaultsFile.c_str());
    setInfo({parent, sci, sci});
    ok &= check(!syncScroll && automatic && !commands[2]._init2Check && commands[3]._init2Check,
        "missing option defaults to enabled while another saved disabled option is preserved");
    toggleSync(); setInfo({parent, sci, sci});
    ok &= check(syncScroll && automatic && commands[2]._init2Check && commands[3]._init2Check,
        "re-enabled option persists alongside the default enabled option");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, defaultsFile.c_str());
    DeleteFileW(defaultsFile.c_str()); RemoveDirectoryW(runtimeDirectory().c_str()); RemoveDirectoryW(hostConfigDirectory.c_str());
    if (GetEnvironmentVariableW(L"MWM_DEFAULTS_ONLY", nullptr, 0)) {
        DestroyWindow(parent); puts(ok ? "ALL DEFAULT SETTINGS TESTS PASSED" : "DEFAULT SETTINGS TESTS FAILED"); return ok ? 0 : 1;
    }
#endif
    hostConfigDirectory = std::wstring(argv[2]) + L"-user-config\\Профиль с пробелами";
    setInfo({parent, sci, sci}); syncScroll = false; automatic = false;
    {
        std::string selected;
        editorSelection.clear();
        ok &= check(!editorExportSelection(selected), "empty editor selection does not select the whole document as a fragment");
        const std::pair<std::string, const char*> cases[] = {
            {"X", "single ASCII character remains selected instead of falling back to the full document"},
            {"SELECT_START selected text SELECT_END", "editor selection preserves its final ASCII byte"},
            {u8"Кириллица Я", "editor selection preserves the final two-byte Cyrillic character"},
            {u8"Emoji 🙂", "editor selection preserves the final four-byte emoji"},
            {"**bold**", "editor selection preserves the closing Markdown delimiter"},
            {"trailing line\r\n", "editor selection preserves selected trailing CRLF"}
        };
        for (const auto& item : cases) {
            editorSelection = item.first; selected.clear();
            ok &= check(editorExportSelection(selected) && selected == item.first, item.second);
        }
        textCodepage = 1251; editorSelection = std::string(1, static_cast<char>(0xDF)); selected.clear();
        ok &= check(editorExportSelection(selected) && selected == u8"Я", "single-byte legacy-encoded selection is retained and converted to UTF-8");
        textCodepage = CP_UTF8; editorSelection.clear();
    }
    if (GetEnvironmentVariableW(L"MWM_EDITOR_BOUNDARY_ONLY", nullptr, 0)) {
        DestroyWindow(parent); puts(ok ? "ALL EDITOR BOUNDARY TESTS PASSED" : "EDITOR BOUNDARY TESTS FAILED"); return ok ? 0 : 1;
    }
#ifndef MWM_DEVELOPMENT_RUNTIME_DIRECTORY
    ok &= panelWidthTests(argv[2]);
    if (GetEnvironmentVariableW(L"MWM_PANEL_WIDTH_ONLY", nullptr, 0)) {
        DestroyWindow(parent); puts(ok ? "ALL PANEL WIDTH TESTS PASSED" : "PANEL WIDTH TESTS FAILED"); return ok ? 0 : 1;
    }
#endif
#ifndef MWM_DEVELOPMENT_RUNTIME_DIRECTORY
    ok &= check(runtimeDirectory() == hostConfigDirectory + L"\\Markdown_with_Mermaid" &&
        GetFileAttributesW(runtimeDirectory().c_str()) != INVALID_FILE_ATTRIBUTES,
        "release uses the host user config path with Unicode and spaces, without an installer INI");
    auto firstConfiguration = hostConfigDirectory;
    hostConfigDirectory += L"-portable";
    ok &= check(runtimeDirectory() == hostConfigDirectory + L"\\Markdown_with_Mermaid", "portable host config isolates plugin data from another installation");
    hostConfigDirectory = firstConfiguration;
    const auto overrideIni = moduleDirectory() + L"\\Markdown_with_Mermaid.ini";
    if (GetFileAttributesW(overrideIni.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto customDirectory = std::wstring(argv[2]) + L"-explicit-data";
        WritePrivateProfileStringW(L"Paths", L"DataDirectory", customDirectory.c_str(), overrideIni.c_str());
        ok &= check(runtimeDirectory() == customDirectory, "explicit development DataDirectory overrides public config defaults");
        WritePrivateProfileStringW(L"Paths", L"DataDirectory", L"relative-test-data", overrideIni.c_str());
        ok &= check(runtimeDirectory() == moduleDirectory() + L"\\relative-test-data", "explicit relative DataDirectory remains relative to the plugin module");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, overrideIni.c_str());
        DeleteFileW(overrideIni.c_str());
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, overrideIni.c_str());
        ok &= check(runtimeDirectory() == hostConfigDirectory + L"\\Markdown_with_Mermaid", "removing an explicit override restores the host config default");
    } else {
        ok &= check(false, "test host must start without a stale installer INI");
    }
#endif
    int count = 0; auto funcs = getFuncsArray(&count);
    ok &= check(count == 11 && isUnicode() && !wcscmp(getName(), L"Markdown_with_Mermaid") && funcs[0]._pFunc && funcs[7]._pFunc && funcs[8]._pFunc, "Notepad++ plugin ABI and PDF/DOCX commands");
    ok &= check(jsonString("</script>\n\"").find('<') == std::string::npos, "inline JSON escapes script delimiters");
    commands[0]._cmdID = 43001; notify(NPPN_TBMODIFICATION);
    BITMAP bitmap{};
    ok &= check(toolbarRegistrations == 1 && toolbarCommand == 43001 && registeredImages.hToolbarIcon && registeredImages.hToolbarIconDarkMode &&
        GetObjectW(registeredImages.hToolbarBmp, sizeof(bitmap), &bitmap) && bitmap.bmWidth == 16 && bitmap.bmHeight == 16,
        "Mermaid toolbar registers classic, light and dark images for preview command");
    auto originalIcon = registeredImages.hToolbarIcon; notify(NPPN_TBMODIFICATION);
    ok &= check(toolbarRegistrations == 2 && registeredImages.hToolbarIcon == originalIcon,
        "toolbar registration reuses retained image handles");
    funcs[0]._pFunc();
    if (!check(waitFor(L"!!window.markdownView && document.querySelectorAll('table tbody tr').length === 3"), "preview initializes in real WebView2")) return 1;
    if (GetEnvironmentVariableW(L"MWM_REPORTED_ONLY", nullptr, 0)) {
        ok &= interiorSourceJumpTests();ok &= edgeLabelTests(argv[1],argv[2]);
        notify(NPPN_SHUTDOWN);pump(500);DestroyWindow(parent);
        puts(ok ? "ALL REPORTED BUG TESTS PASSED" : "REPORTED BUG TESTS FAILED");return ok ? 0 : 1;
    }
    if (GetEnvironmentVariableW(L"MWM_JUMP_ONLY", nullptr, 0)) {
        ok &= sourceJumpTests();
        notify(NPPN_SHUTDOWN);pump(500);DestroyWindow(parent);
        puts(ok ? "ALL SOURCE JUMP TESTS PASSED" : "SOURCE JUMP TESTS FAILED");return ok ? 0 : 1;
    }
    ok &= filenameCopyTests();
    if (GetEnvironmentVariableW(L"MWM_FILENAME_COPY_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); pump(500); DestroyWindow(parent);
        puts(ok ? "ALL FILENAME COPY TESTS PASSED" : "FILENAME COPY TESTS FAILED"); return ok ? 0 : 1;
    }
    const auto initialDocument = documentPath;
    ok &= scrollSyncTests();
    ok &= sourceJumpTests();
    ok &= interiorSourceJumpTests();
    if (GetEnvironmentVariableW(L"MWM_SCROLL_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN);pump(500);DestroyWindow(parent);
        puts(ok ? "ALL SCROLL SYNC TESTS PASSED" : "SCROLL SYNC TESTS FAILED");return ok ? 0 : 1;
    }
    ok &= zoomTests(argv[1]);
    if (GetEnvironmentVariableW(L"MWM_ZOOM_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); pump(500); DestroyWindow(parent);
        puts(ok ? "ALL ZOOM TESTS PASSED" : "ZOOM TESTS FAILED"); return ok ? 0 : 1;
    }
    loadFixture(initialDocument);
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3"), "initial fixture restores after zoom checks");
    ok &= navigationTests(argv[1], argv[2]);
    if (GetEnvironmentVariableW(L"MWM_NAVIGATION_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); DestroyWindow(parent); puts(ok ? "ALL NAVIGATION TESTS PASSED" : "NAVIGATION TESTS FAILED"); return ok ? 0 : 1;
    }
    ok &= edgeLabelTests(argv[1], argv[2]);
    if (GetEnvironmentVariableW(L"MWM_EDGE_LABELS_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); DestroyWindow(parent); return ok ? 0 : 1;
    }
    ok &= selectionTests(argv[1], argv[2]);
    if (GetEnvironmentVariableW(L"MWM_SELECTION_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); DestroyWindow(parent); puts(ok ? "ALL SELECTION TESTS PASSED" : "SELECTION TESTS FAILED"); return ok ? 0 : 1;
    }
    ok &= imageSizeTests(argv[1], argv[2]);
    if (GetEnvironmentVariableW(L"MWM_IMAGE_SIZE_ONLY", nullptr, 0)) {
        notify(NPPN_SHUTDOWN); DestroyWindow(parent); puts(ok ? "ALL IMAGE SIZE TESTS PASSED" : "IMAGE SIZE TESTS FAILED"); return ok ? 0 : 1;
    }
    loadFixture(std::wstring(argv[1]) + L"\\Демонстрация.md");
    ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3"), "preview restores source after image size tests");
    if (GetEnvironmentVariableW(L"MWM_TRACE_HTTP", nullptr, 0)) {
        auto ready = cb<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; });
        panel->webview->CallDevToolsProtocolMethod(L"Network.enable", L"{}", ready); ready->Release();
        for (auto name : {L"Network.loadingFailed", L"Network.responseReceived"}) {
            ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> receiver;
            panel->webview->GetDevToolsProtocolEventReceiver(name, receiver.put());
            auto event = cb<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
                [name](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args) -> HRESULT {
                    LPWSTR raw = nullptr; args->get_ParameterObjectAsJson(&raw); std::wstring json = raw ? raw : L""; CoTaskMemFree(raw);
                    if (json.find(L"127.0.0.1") != std::wstring::npos || !wcscmp(name, L"Network.loadingFailed")) printf("TRACE %s %s\n", utf8(name).c_str(), utf8(json).c_str());
                    return S_OK;
                });
            EventRegistrationToken token; receiver->add_DevToolsProtocolEventReceived(event, &token); event->Release();
        }
    }
    wchar_t smokeBase[512]{};
    bool smokeOnly = GetEnvironmentVariableW(L"MWM_IMAGE_SMOKE", smokeBase, 512) != 0;
    if (smokeOnly || GetEnvironmentVariableW(L"MWM_TEST_IMAGE_BASE", smokeBase, 512)) {
        auto url = std::wstring(smokeBase) + L"/once.svg?run=" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        textBuffer = "# Preview cache smoke\n\n![ONCE](" + utf8(url) + ")\n"; panel->load();
        ok &= check(waitFor(L"document.querySelector('.markdown-body img')?.naturalWidth===144"), "single preview image displays");
        for (int i = 0; i < 200; ++i) { auto image = panel->images.find(url); if (image != panel->images.end() && image->second->complete) break; pump(25); }
        auto image = panel->images.find(url);
        ok &= check(image != panel->images.end() && !image->second->data.empty(), "single preview image bytes cached");
        eval(L"document.activeElement?.blur();window.getSelection().removeAllRanges();window.markdownView.copy()");
        ok &= check(waitFor(L"!document.getElementById('copy').disabled") && readHtmlClipboard().find("data:image/svg+xml;base64,") != std::string::npos, "clipboard embeds single-use no-CORS image from preview cache");
        eval(L"(()=>{document.querySelector('.markdown-body').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));const r=document.createRange();r.selectNode(document.querySelector('.markdown-body img'));window.getSelection().removeAllRanges();window.getSelection().addRange(r);})()");
        auto selectedImage = captureSelectionFragment();
        ok &= check(selectedImage.find(L"data:image/svg+xml;base64,") != std::wstring::npos, "selection embeds the displayed single-use image");
        for (bool word : {false, true}) {
            auto snapshot = std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = word; snapshot->fragmentHtml = selectedImage; snapshot->captureContent();
            ok &= check(selectedExport(std::wstring(argv[2]) + (word ? L"-selection-cache.docx" : L"-selection-cache.pdf"), word, snapshot), word ? "selected DOCX reuses single-use image" : "selected PDF reuses single-use image");
        }
        ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-cache-smoke.docx", true), "single-use image exports after buffer change");
        if (smokeOnly) { notify(NPPN_SHUTDOWN); DestroyWindow(parent); return ok ? 0 : 1; }
        readBytes(documentPath, textBuffer); panel->load();
        ok &= check(waitFor(L"document.querySelectorAll('table tbody tr').length===3"), "preview restores source after cached-image export");
    }
    ok &= check(eval(L"!!document.querySelector('code .hljs-keyword')") == L"true", "syntax highlighting");
    ok &= check(waitFor(L"document.querySelector('.markdown-body img').naturalWidth === 480"), "relative image from source directory");
    ok &= check(eval(L"document.querySelectorAll('input[type=checkbox]').length === 3 && !!document.getElementById('возможности')") == L"true", "task lists and Unicode heading IDs");
    ok &= check(eval(L"document.getElementById('export').title==='Export to HTML' && document.getElementById('export-pdf').title==='Export to PDF' && document.getElementById('export-docx').title==='Export to DOCX' && document.getElementById('export-pdf').nextElementSibling.id==='export-docx' && !window.docx") == L"true", "three export buttons have requested hints and DOCX library loads only on demand");
    ok &= check(eval(L"!document.getElementById('reload') && !!document.getElementById('theme-button') && document.getElementById('document-info').getBoundingClientRect().top >= document.getElementById('toolbar-actions').getBoundingClientRect().bottom && document.getElementById('zoom-combo').previousElementSibling.id==='zoom-out' && document.getElementById('zoom-combo').nextElementSibling.id==='zoom-in'") == L"true", "native toolbar separates file name and retains the requested theme button");
    MoveWindow(panel->hwnd, 0, 0, 320, 820, TRUE); pump(300);
    ok &= check(eval(L"[...document.querySelectorAll('#toolbar button,#zoom-combo')].every(e=>e.getBoundingClientRect().right<=document.documentElement.clientWidth+1) && Math.abs(parseFloat(getComputedStyle(document.body).paddingTop)-document.getElementById('toolbar').offsetHeight)<1") == L"true", "native toolbar wraps at 320px and document clears its full height");
    capture(std::wstring(argv[2]) + L"-native-narrow.png");
    MoveWindow(panel->hwnd, 0, 0, 1000, 820, TRUE); pump(300);
    textBuffer = "# Link destinations\n\n[Local](fixtures/linked-document.md#intro)\n\n[Unicode](%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82.md)\n\n[External](https://example.com/readme.md?view=1#part \"Original title\")\n\n[Anchor](#link-destinations)\n\n[Mail](mailto:reader@example.com)\n\n[![Badge](sample.svg)](https://example.com)\n";
    panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Link destinations'"), "link hint fixture loads");
    ok &= check(eval(L"[...document.querySelectorAll('.markdown-body a')].map(a=>a.title).every(t=>t.includes('\\n')) && document.querySelector('.markdown-body a').title.includes('Markdown document') && document.querySelector('.markdown-body a').title.includes('fixtures\\\\linked-document.md#intro') && document.querySelectorAll('.markdown-body a')[1].title.includes('Привет.md') && document.querySelectorAll('.markdown-body a')[2].title==='Original title\\n\\nExternal link\\nhttps://example.com/readme.md?view=1#part' && document.querySelectorAll('.markdown-body a')[3].title.includes('Демонстрация.md#link-destinations') && document.querySelectorAll('.markdown-body a')[4].title.includes('mailto:reader@example.com')") == L"true", "hover titles show resolved targets and link types while preserving authored titles");
    ok &= check(eval(L"document.querySelectorAll('.link-type-md').length===2 && document.querySelectorAll('.link-type-external').length===1 && document.querySelector('.markdown-body a').firstElementChild.classList.contains('link-type-md') && document.querySelectorAll('.markdown-body a')[2].lastElementChild.classList.contains('link-type-external') && !document.querySelectorAll('.markdown-body a')[3].querySelector('svg') && !document.querySelectorAll('.markdown-body a')[5].querySelector('svg')") == L"true", "document icons precede local Markdown links, external icons follow web links, anchors and badges stay plain");
    ok &= check(eval(L"(()=>{const previous=window.chrome.webview.postMessage;let target='';window.chrome.webview.postMessage=m=>target=m;try{document.querySelector('.link-type-external path').dispatchEvent(new MouseEvent('click',{bubbles:true}));return target==='link:https://example.com/readme.md?view=1#part';}finally{window.chrome.webview.postMessage=previous;}})()") == L"true", "clicking external link icon follows the same destination as its text");
    capture(std::wstring(argv[2]) + L"-link-icons.png");
    const auto unicodeTarget = std::wstring(argv[1]) + L"\\fixtures\\Привет %41.md";
    wchar_t encodedTarget[32768]; DWORD encodedLength = 32768;
    const auto targetUrl = fileUrl(unicodeTarget);
    ok &= check(SUCCEEDED(UrlEscapeW(targetUrl.c_str(), encodedTarget, &encodedLength, URL_ESCAPE_AS_UTF8)), "browser-style Unicode file URL can be created");
    std::wstring decodedTarget;
    ok &= check(pathFromFileUrl(encodedTarget, decodedTarget) && decodedTarget == unicodeTarget &&
        !pathFromFileUrl(L"https://example.com/document.md", decodedTarget), "local URL decoding preserves Unicode and literal percent escapes and rejects remote URLs");
    auto savedPath = documentPath; auto savedBuffer = textBuffer;
    openLink(panel, std::wstring(encodedTarget) + L"#unicode-link-target");
    ok &= check(documentPath == unicodeTarget && textBuffer.find("UNICODE_LINK_TARGET") != std::string::npos, "Unicode Markdown link opens the exact native file without double-decoding percent escapes");
    documentPath = savedPath; textBuffer = savedBuffer;
    loadFixture(std::wstring(argv[1]) + L"\\mermaid.md");
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent.includes('Mermaid') && window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram[data-render-state=ready]').length === 6"), "six offline diagram types render");
    ok &= check(eval(L"[...document.querySelectorAll('.mermaid-graphic')].every(e => e.shadowRoot.querySelector('svg')?.getBoundingClientRect().width > 0)") == L"true", "every SVG has visible geometry");
    ok &= check(eval(L"document.querySelector('.mermaid-graphic').shadowRoot.textContent.includes('Markdown-файл')") == L"true", "Cyrillic diagram labels");
    ok &= check(eval(L"document.querySelector('code.language-text').textContent.includes('┌')") == L"true", "ASCII art remains plain text");
    eval(L"window.__svgWidth=document.querySelector('.mermaid-graphic').shadowRoot.querySelector('svg').getBoundingClientRect().width; window.__toolbarHeight=document.getElementById('toolbar').getBoundingClientRect().height; document.getElementById('zoom-in').click()");
    ok &= check(eval(L"Math.abs(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('svg').getBoundingClientRect().width/window.__svgWidth-1.25)<0.02 && document.getElementById('zoom-value').value==='125%' && document.getElementById('toolbar').getBoundingClientRect().height===window.__toolbarHeight") == L"true", "zoom enlarges actual Mermaid SVG while toolbar stays unchanged");
    eval(L"document.getElementById('zoom-value').value='204%'; document.getElementById('zoom-value').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}))");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='204%' && Math.abs(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('svg').getBoundingClientRect().width/window.__svgWidth-2.04)<0.02") == L"true", "custom percentage scales the whole diagram without shrinking it to panel width");
    eval(L"window.scrollTo(200,0)");
    ok &= check(eval(L"window.scrollX>100 && document.getElementById('toolbar').getBoundingClientRect().left===0 && document.getElementById('zoom-in').getBoundingClientRect().right<window.innerWidth") == L"true", "zoom toolbar stays accessible during horizontal scrolling");
    eval(L"window.scrollTo(0,0)");
    eval(L"document.getElementById('zoom-out').click()");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='200%'") == L"true", "minus button selects previous zoom level");
    eval(L"document.getElementById('zoom-presets').value='50';document.getElementById('zoom-presets').dispatchEvent(new Event('change'))");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='50%' && Math.abs(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('svg').getBoundingClientRect().width/window.__svgWidth-0.5)<0.02") == L"true", "dropdown percentage reduces SVG geometry");
    eval(L"document.getElementById('zoom-value').value='invalid';document.getElementById('zoom-value').dispatchEvent(new Event('change'))");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='50%' && !document.getElementById('find-button')") == L"true", "invalid zoom is rejected and Search toolbar button is removed");
    eval(L"window.markdownView.setZoom('content')");
    ok &= check(eval(L"document.querySelector('.markdown-body').getBoundingClientRect().right<=document.documentElement.clientWidth+2") == L"true", "fit content keeps document within preview width");
    eval(L"window.markdownView.setZoom('width')");
    ok &= check(eval(L"Math.abs(document.querySelector('.markdown-body').getBoundingClientRect().width-document.documentElement.clientWidth)<2") == L"true", "fit width fills preview viewport");
    eval(L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'f',ctrlKey:true,bubbles:true}))");
    ok &= check(eval(L"!document.getElementById('findbar').hidden") == L"true", "Ctrl+F search remains available without toolbar button");
    eval(L"window.markdownView.escape();window.markdownView.setZoom(150)");
    ok &= check(capture(std::wstring(argv[2]) + L"-zoom150.png"), "capture zoom controls at 150 percent");
    eval(L"window.markdownView.setZoom('actual')");
    ok &= check(eval(L"!!document.querySelector('.mermaid-diagram[data-line]') && document.querySelectorAll('#outline a').length >= 7") == L"true", "source line mapping and outline");
    eval(L"document.getElementById('outline-button').click()");
    ok &= check(eval(L"!document.getElementById('outline').hidden") == L"true", "outline toggle");
    eval(L"document.getElementById('outline-button').click(); window.__fill=getComputedStyle(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('.node rect')).fill; window.markdownView.setTheme(true)");
    ok &= check(waitFor(L"window.markdownView.mermaidReady && [...document.querySelectorAll('.mermaid-diagram')].every(e => e.dataset.theme === 'dark')"), "Mermaid re-renders in dark theme");
    ok &= check(eval(L"window.__fill !== getComputedStyle(document.querySelector('.mermaid-graphic').shadowRoot.querySelector('.node rect')).fill") == L"true", "theme changes actual diagram colors");
    ok &= check(capture(std::wstring(argv[2]) + L"-dark.png"), "capture dark preview");
    eval(L"window.markdownView.setTheme(false)"); waitFor(L"window.markdownView.mermaidReady");
    ok &= check(capture(std::wstring(argv[2]) + L"-light.png"), "capture light preview");
    eval(L"document.querySelector('.diagram-tools button').click()");
    ok &= check(eval(L"!!document.querySelector('.mermaid-diagram.expanded')") == L"true", "expand diagram");
    eval(L"window.markdownView.escape()");
    // Export without opening the save dialog: temporarily collect the bridge string.
    eval(L"window.__post=window.chrome.webview.postMessage; window.chrome.webview.postMessage = message => { if(message.startsWith('export:')) window.__exported=message.slice(7); }; window.markdownView.exportHtml()");
    ok &= check(waitFor(L"!!window.__exported"), "HTML export finishes");
    ok &= check(eval(L"(window.__exported.match(/<svg/g)||[]).length === 6 && !window.__exported.includes('<script') && window.__exported.includes('MWM_EXPORT_STYLES')") == L"true", "HTML export contains static SVG without scripts");
    eval(L"window.chrome.webview.postMessage=window.__post");
    ok &= check(eval(L"document.getElementById('export').nextElementSibling.id==='export-pdf' && !document.getElementById('export-pdf').disabled") == L"true", "PDF button beside HTML");
    eval(L"window.markdownView.setZoom(204);window.markdownView.setTheme(true)"); waitFor(L"window.markdownView.mermaidReady");
    ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-mermaid.docx", true), "DOCX exports six offline Mermaid diagrams from frozen buffer");
    ok &= check(eval(L"document.querySelector('h1').textContent==='CHANGED_AFTER_DOCX_SNAPSHOT' && document.documentElement.classList.contains('dark') && document.getElementById('zoom-value').value==='204%' && !document.getElementById('export-docx').disabled") == L"true", "DOCX export preserves current preview theme zoom and current buffer");
    loadFixture(std::wstring(argv[1]) + L"\\docx-export.md");
    for (int i=1; i<=120; ++i) textBuffer += "| " + std::to_string(i) + " | DOCX_ROW_" + std::to_string(i) + " | Несохранённая строка |\n";
    panel->load();
    ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-document.docx"), "DOCX exports editable formatting lists tables links images and invalid Mermaid");
    auto lockedDocx = std::wstring(argv[2]) + L"-docx-locked.docx";
    HANDLE lockedDocxFile = CreateFileW(lockedDocx.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD docxSentinelBytes; WriteFile(lockedDocxFile, "ORIGINAL_DOCX", 13, &docxSentinelBytes, nullptr);
    textBuffer = "# Locked DOCX target\n";
    ok &= check(docxTest(lockedDocx, false, false), "locked DOCX destination reports failure");
    CloseHandle(lockedDocxFile); std::string docxOriginal; readBytes(lockedDocx, docxOriginal);
    ok &= check(docxOriginal == "ORIGINAL_DOCX", "failed DOCX export preserves existing file"); DeleteFileW(lockedDocx.c_str());
    ok &= check(docxTest(std::wstring(argv[2]) + L"-missing-directory\\out.docx", false, false), "unwritable DOCX folder reports failure and clears busy state");
    wchar_t imageBase[512]{};
    if (GetEnvironmentVariableW(L"MWM_TEST_IMAGE_BASE", imageBase, 512)) {
        auto base = utf8(imageBase);
        textBuffer = "# Native image export\n\n<img src=\"" + base + "/badge.svg\" alt=\"NO_CORS_BADGE\" height=\"16\">\n\n";
        for (const auto& route : {"redirect", "html", "missing", "oversize", "loop"}) textBuffer += "![REMOTE_" + std::string(route) + "](" + base + "/" + route + ")\n\n";
        textBuffer += "![LOCAL_MISSING](no-such-local-image.png)\n\n![AFTER_ERRORS](" + base + "/after-errors.svg)\n";
        panel->load();
        ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-http-images.docx"), "DOCX embeds images without CORS and reports unavailable resources");
        textBuffer = "# Cancel image download\n\n![SLOW](" + base + "/slow.svg)\n";
        panel->load(); bool networkCancelled = false; int networkCalls = 0;
        auto networkTarget = std::wstring(argv[2]) + L"-docx-network-cancelled.docx";
        ok &= check(startDocxExport(panel, networkTarget, [&](bool success, const std::wstring&) { networkCancelled = !success; ++networkCalls; }), "DOCX starts with pending network image");
        for (int i = 0; i < 100 && docxExport && !docxExport->image; ++i) pump(50);
        auto pendingImage = docxExport ? docxExport->image : nullptr;
        auto before = GetTickCount64();
        ok &= check(pendingImage && eval(L"1+1") == L"2" && GetTickCount64() - before < 1500, "native image download leaves WebView2 and UI responsive");
        cancelDocx();
        ok &= check(pendingImage && pendingImage->cancelled && networkCancelled && networkCalls == 1 && !docxExport && GetFileAttributesW(networkTarget.c_str()) == INVALID_FILE_ATTRIBUTES, "cancelling pending HTTP export removes output without blocking UI");
        for (int i = 0; pendingImage && !pendingImage->complete && i < 160; ++i) pump(50);
        ok &= check(pendingImage && pendingImage->complete && networkCalls == 1, "cancelled HTTP worker finishes without late callbacks");
    }
    if (argc > 4 && argv[4][0]) {
        documentPath = argv[4];
        ok &= check(readBytes(documentPath, textBuffer), "supplied DOCX image regression Markdown is readable"); panel->load();
        ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-readme.docx"), "DOCX exports supplied README with remote images");
    }
    loadFixture(std::wstring(argv[1]) + L"\\mermaid.md"); waitFor(L"window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram').length===6");
    eval(L"window.markdownView.setZoom(204);window.markdownView.setTheme(true)"); waitFor(L"window.markdownView.mermaidReady");
    ok &= check(pdfTest(std::wstring(argv[2]) + L"-pdf-mermaid.pdf", true), "PrintToPdf exports six diagrams from frozen buffer despite tab content change");
    ok &= check(eval(L"document.querySelector('h1').textContent==='CHANGED_AFTER_PDF_SNAPSHOT' && document.documentElement.classList.contains('dark') && document.getElementById('zoom-value').value==='204%' && document.getElementById('pdf-status').textContent.includes('PDF') && !document.getElementById('export-pdf').disabled") == L"true", "PDF leaves main preview theme zoom and current document unchanged");
    loadFixture(std::wstring(argv[1]) + L"\\mermaid.md"); waitFor(L"window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram').length===6");
    eval(L"window.markdownView.setTheme(false);window.markdownView.setZoom(50)");waitFor(L"window.markdownView.mermaidReady");
    ok &= check(pdfTest(std::wstring(argv[2]) + L"-pdf-mermaid-50.pdf"), "PDF from 50 percent light preview");
    loadFixture(std::wstring(argv[1]) + L"\\pdf-export.md");
    for (int i=1; i<=120; ++i) textBuffer += "| " + std::to_string(i) + " | TABLE_ROW_" + std::to_string(i) + " | Example value |\n";
    panel->load();
    ok &= check(pdfTest(std::wstring(argv[2]) + L"-pdf-document.pdf"), "PDF includes local image long table invalid diagram and unsaved rows");
    textBuffer = "# Long code pagination regression\n\n```\n";
    for (int i = 1; i <= 160; ++i) textBuffer += "PLAIN_CODE_ROW_" + std::to_string(i) + "  |  packages/core/src/module.ts\n";
    textBuffer += "```\n\n## Highlighted code\n\n```javascript\n";
    for (int i = 1; i <= 100; ++i) textBuffer += "const HIGHLIGHTED_CODE_ROW_" + std::to_string(i) + " = 'pagination';\n";
    textBuffer += "```\n\nEND_LONG_CODE_DOCUMENT\n";
    panel->load();
    ok &= check(pdfTest(std::wstring(argv[2]) + L"-pdf-long-code.pdf"), "PDF exports multipage plain and highlighted code");
    ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-long-code.docx"), "DOCX exports multipage code without giant indivisible paragraphs");
    if (argc > 3 && argv[3][0]) {
        documentPath = argv[3];
        ok &= check(readBytes(documentPath, textBuffer), "supplied PDF regression file is readable");
        panel->load();
        ok &= check(pdfTest(std::wstring(argv[2]) + L"-pdf-regression.pdf"), "PDF exports supplied regression Markdown");
        ok &= check(docxTest(std::wstring(argv[2]) + L"-docx-regression.docx"), "DOCX exports supplied regression Markdown");
    }
    // A locked existing destination must survive a failed atomic replacement.
    auto lockedPath = std::wstring(argv[2]) + L"-pdf-locked.pdf";
    HANDLE locked = CreateFileW(lockedPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD sentinelBytes; WriteFile(locked, "ORIGINAL_FILE", 13, &sentinelBytes, nullptr);
    textBuffer = "# Locked target test\n";
    ok &= check(pdfTest(lockedPath, false, false), "locked destination reports failure without replacing original");
    CloseHandle(locked); std::string original; readBytes(lockedPath, original);
    ok &= check(original == "ORIGINAL_FILE", "failed PDF export preserves existing file"); DeleteFileW(lockedPath.c_str());
    ok &= check(pdfTest(std::wstring(argv[2]) + L"-missing-directory\\out.pdf", false, false), "unwritable PDF folder reports failure and clears busy state");
    loadFixture(std::wstring(argv[1]) + L"\\mermaid.md");waitFor(L"window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram').length===6");
    // Unsaved editor buffer: must render after a SCN_MODIFIED, without changing disk.
    eval(L"window.markdownView.setZoom(175)");
    textBuffer = "# Unsaved edit\n\n```mermaid\nflowchart LR\nA[Live editor] --> B[Unsaved buffer]\n```\n";
    notify(SCN_MODIFIED, SC_MOD_INSERTTEXT);
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Unsaved edit' && window.markdownView.mermaidReady && document.querySelector('.mermaid-graphic').shadowRoot.textContent.includes('Unsaved buffer')"), "debounced unsaved editor update rebuilds Mermaid");
    ok &= check(eval(L"document.getElementById('zoom-value').value==='175%'") == L"true", "zoom survives live Markdown updates");
    // Race: queued old Mermaid generation may not replace the newest content.
    for (int i = 0; i < 4; ++i) { textBuffer = "# Latest " + std::to_string(i) + "\n\n```mermaid\nflowchart LR\nA-->B\n```"; panel->load(); }
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Latest 3' && window.markdownView.mermaidReady && document.querySelectorAll('.mermaid-diagram[data-render-state=ready]').length === 1"), "rapid edits retain latest rendering");
    loadFixture(std::wstring(argv[1]) + L"\\mermaid-invalid.md");
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Mermaid error isolation' && window.markdownView.mermaidReady"), "invalid diagram settles");
    ok &= check(eval(L"document.querySelectorAll('.mermaid-diagram[data-render-state=ready]').length === 2 && document.querySelectorAll('.mermaid-diagram[data-render-state=error] details[open]').length === 1") == L"true", "syntax error isolated from other diagrams");
    loadFixture(std::wstring(argv[1]) + L"\\security.md");
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Security fixture'"), "security fixture loads");
    ok &= check(eval(L"!window.INJECTED && !document.querySelector('.markdown-body script,.markdown-body iframe,.markdown-body style,.markdown-body form,.markdown-body [onerror]')") == L"true", "untrusted document scripts and handlers removed");
    ok &= check(eval(L"['MV_PAYLOAD','MV_NONCE','MV_ASSET_BASE'].every(t => document.querySelector('.markdown-body').textContent.includes(t))") == L"true", "template tokens cannot replace source content");
    loadFixture(std::wstring(argv[1]) + L"\\mermaid-security.md");
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Mermaid security fixture' && window.markdownView.mermaidReady"), "Mermaid security fixture completes");
    ok &= check(eval(L"window.mermaid.mermaidAPI.getConfig().securityLevel === 'strict' && [...document.querySelectorAll('.mermaid-graphic')].every(e => !e.shadowRoot.querySelector('script,foreignObject,iframe,image,a,[onclick],[onerror]'))") == L"true", "Mermaid directives cannot lower security or inject SVG handlers");
    textBuffer = "---\ntitle: Metadata\ntags: [markdown, mermaid]\n---\n\n# Front matter\n"; panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Front matter' && document.querySelector('code.language-yaml')?.textContent.includes('title: Metadata')"), "VS Code YAML front matter codeBlock presentation");
    textBuffer = "---\n\n# Unclosed preamble\n\nText remains visible\n"; panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Unclosed preamble' && !document.querySelector('code.language-yaml')"), "unterminated YAML delimiter does not swallow Markdown");
    textBuffer = "# Duplicate\n\n# Duplicate\n\n# Привет **мир**\n\n> [!NOTE]\n> Alert text\n"; panel->load();
    ok &= check(waitFor(L"!!document.getElementById('duplicate-1') && !!document.getElementById('привет-мир') && !!document.querySelector('.markdown-alert')"), "VS Code duplicate slugs, inline heading text and alerts");
    eval(L"document.querySelector('h1').dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))"); pump(100);
    ok &= check(jumpedLine == 0, "double-click preview jumps to source line");
    documentPath = L"new 1"; textBuffer = "# Untitled Unicode buffer\n\nРусский текст"; panel->load();
    ok &= check(waitFor(L"document.querySelector('h1')?.textContent === 'Untitled Unicode buffer'"), "untitled document preview");
    funcs[0]._pFunc(); ok &= check(!visible(), "toolbar command hides docked panel"); funcs[0]._pFunc(); ok &= check(visible(), "toolbar command shows docked panel again");
    bool cancelled = false; int cancelledCalls = 0;
    bool htmlCancelled = false; int htmlCancelledCalls = 0;
    ok &= check(startHtmlExport(panel, {}, [&](const std::wstring& html, const std::wstring& error) { htmlCancelled = html.empty() && !error.empty(); ++htmlCancelledCalls; }), "HTML starts before shutdown");
    auto htmlPage = htmlExport ? htmlExport->view->page : L"";
    bool docxCancelled = false; int docxCancelledCalls = 0;
    auto cancelledDocx = std::wstring(argv[2]) + L"-docx-cancelled.docx";
    ok &= check(startDocxExport(panel, cancelledDocx, [&](bool success, const std::wstring&) { docxCancelled = !success; ++docxCancelledCalls; }), "DOCX starts before shutdown");
    auto docxPage = docxExport ? docxExport->view->page : L"";
    auto docxTemporary = docxExport ? docxExport->temporary : L"";
    auto cancelledPath = std::wstring(argv[2]) + L"-pdf-cancelled.pdf";
    ok &= check(startPdfExport(panel, cancelledPath, [&](bool success, const std::wstring&) { cancelled = !success; ++cancelledCalls; }), "PDF starts before shutdown");
    auto pdfPage = pdfExport ? pdfExport->view->page : L"";
    auto pdfTemporary = pdfExport ? pdfExport->temporary : L"";
    auto page = panel->page; notify(NPPN_SHUTDOWN);
    ok &= check(htmlCancelled && htmlCancelledCalls == 1 && !htmlExport && GetFileAttributesW(htmlPage.c_str()) == INVALID_FILE_ATTRIBUTES, "shutdown cancels HTML once and removes its temporary preview");
    ok &= check(docxCancelled && docxCancelledCalls == 1 && !docxExport && GetFileAttributesW(docxPage.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(docxTemporary.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(cancelledDocx.c_str()) == INVALID_FILE_ATTRIBUTES, "shutdown cancels DOCX once and removes temporary files");
    ok &= check(cancelled && cancelledCalls == 1 && !pdfExport && GetFileAttributesW(pdfPage.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(pdfTemporary.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(cancelledPath.c_str()) == INVALID_FILE_ATTRIBUTES, "shutdown cancels PDF once and removes export temporary files");
    ok &= check(GetFileAttributesW(page.c_str()) == INVALID_FILE_ATTRIBUTES, "temporary preview HTML removed on shutdown");
    pump(500); DestroyWindow(parent);
    puts(ok ? "ALL TESTS PASSED" : "TESTS FAILED"); return ok ? 0 : 1;
}
