// Windows HTML Format uses UTF-8 BYTE offsets, including for Cyrillic/emoji.
// https://learn.microsoft.com/windows/win32/dataxchg/html-clipboard-format
static std::string clipboardHtml(const std::string& fragment) {
    const std::string prefix = "<html><head><meta charset=\"utf-8\"></head><body><!--StartFragment-->", suffix = "<!--EndFragment--></body></html>";
    char header[256];
    const char* format = "Version:1.0\r\nStartHTML:%010u\r\nEndHTML:%010u\r\nStartFragment:%010u\r\nEndFragment:%010u\r\n";
    int length = snprintf(header, sizeof(header), format, 0u, 0u, 0u, 0u);
    auto start = (unsigned)length, first = start + (unsigned)prefix.size(), last = first + (unsigned)fragment.size();
    snprintf(header, sizeof(header), format, start, last + (unsigned)suffix.size(), first, last);
    return std::string(header) + prefix + fragment + suffix;
}
static HGLOBAL clipboardMemory(const void* bytes, size_t size) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    auto pointer = GlobalLock(memory);
    if (!pointer) { GlobalFree(memory); return nullptr; }
    memcpy(pointer, bytes, size); GlobalUnlock(memory); return memory;
}
static bool writeClipboard(HWND owner, const std::wstring& text, const std::wstring& fragment) {
    if (text.size() + fragment.size() > 64 * 1024 * 1024) return false;
    auto html = clipboardHtml(utf8(fragment));
    auto unicode = clipboardMemory(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    auto formatted = clipboardMemory(html.c_str(), html.size() + 1);
    auto format = RegisterClipboardFormatW(L"HTML Format");
    if (!unicode || !formatted || !format || !OpenClipboard(owner)) {
        if (unicode) GlobalFree(unicode); if (formatted) GlobalFree(formatted); return false;
    }
    bool ok = false;
    if (EmptyClipboard()) {
        if (SetClipboardData(CF_UNICODETEXT, unicode)) { unicode = nullptr;
            if (SetClipboardData(format, formatted)) { formatted = nullptr; ok = true; }
        }
    }
    CloseClipboard(); if (unicode) GlobalFree(unicode); if (formatted) GlobalFree(formatted); return ok;
}
static bool writePlainClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || text.size() > 32768) return false;
    auto unicode = clipboardMemory(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    if (!unicode) return false;
    if (!OpenClipboard(owner)) { GlobalFree(unicode); return false; }
    const bool success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, unicode);
    CloseClipboard(); if (!success) GlobalFree(unicode);
    return success;
}
static void clipboardMessage(const std::shared_ptr<Viewer>& view, const std::wstring& message) {
    if (message.rfind(L"clipboard-text:", 0) == 0) {
        const bool success = writePlainClipboard(view->hwnd, message.substr(15));
        view->script(success ? L"window.markdownView.copyResult(true,'Copied')" : L"window.markdownView.copyResult(false)");
        return;
    }
    if (message.rfind(L"clipboard-begin:", 0) == 0 || message.rfind(L"clipboard-end:", 0) == 0) {
        bool begin = message[10] == L'b'; auto id = message.substr(begin ? 16 : 14);
        if (id.empty() || id.size() > 8 || id.find_first_not_of(L"0123456789") != std::wstring::npos) return;
        if (!begin) view->clipboardImages.erase(id);
        else if (view->clipboardImages.size() < 4) view->clipboardImages[id] = view->images;
        return;
    }
    if (message.rfind(L"clipboard-resource:", 0) == 0) {
        auto separator = message.find(L':', 19);
        if (separator == std::wstring::npos) return;
        auto scope = message.substr(19, separator - 19); auto next = message.find(L':', separator + 1);
        if (next == std::wstring::npos) return;
        auto id = message.substr(separator + 1, next - separator - 1), url = message.substr(next + 1);
        if (id.empty() || id.size() > 8 || id.find_first_not_of(L"0123456789") != std::wstring::npos) return;
        auto snapshot = view->clipboardImages.find(scope);
        if (snapshot == view->clipboardImages.end()) { view->script(L"window.markdownView.clipboardResourceResult(" + id + L",'')"); return; }
        std::string data; auto cached = snapshot->second.find(url.substr(0, url.find(L'#')));
        bool pending = cached != snapshot->second.end() && !cached->second->complete;
        if (cached != snapshot->second.end() && cached->second->complete) data = cached->second->data;
        else if (!pending && url.rfind(L"file:", 0) == 0) {
            std::wstring path; std::string bytes;
            if (pathFromFileUrl(url, path) && readBytes(path, bytes, 16*1024*1024)) {
                auto mime = imageMime(bytes); if (!mime.empty()) data = "data:" + mime + ";base64," + base64(bytes);
            }
        }
        view->script(L"window.markdownView.clipboardResourceResult(" + id + L"," + fromBytes(jsonString(data), CP_UTF8) + (pending ? L",true)" : L",false)")); return;
    }
    auto separator = message.find(L':', 10);
    if (separator == std::wstring::npos || separator > 20) return;
    auto number = message.substr(10, separator - 10);
    if (number.empty() || number.find_first_not_of(L"0123456789") != std::wstring::npos) return;
    auto length = wcstoul(number.c_str(), nullptr, 10);
    if (length > message.size() - separator - 1) return;
    auto text = message.substr(separator + 1, length), html = message.substr(separator + 1 + length);
    auto success = writeClipboard(view->hwnd, text, html);
    view->script(success ? L"window.markdownView.copyResult(true)" : L"window.markdownView.copyResult(false)");
}
