// Immutable offscreen export; DOCX is built locally in WebView2 and streamed to disk.
static std::string base64(const std::string&);
#include "docx-images.cpp"
struct DocxExport {
    std::shared_ptr<Viewer> view;
    std::weak_ptr<Viewer> owner;
    std::wstring target, temporary, message;
    std::function<void(bool, const std::wstring&)> completed;
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t bytes = 0;
    unsigned chunk = 0;
    int missingImages = 0;
    bool finishing = false, success = false;
    std::shared_ptr<DocxImageRequest> image;
    std::shared_ptr<PreviewImage> cachedImage;
};
static std::shared_ptr<DocxExport> docxExport;
static void docxStatus(const std::shared_ptr<Viewer>& owner, const std::wstring& text, bool busy, bool error = false) {
    if (owner && owner->hwnd && owner->webview)
        owner->script(L"window.markdownView.setDocxStatus(" + fromBytes(jsonString(utf8(text)), CP_UTF8) +
            (busy ? L",true," : L",false,") + (error ? L"true)" : L"false)"));
}
static void queueDocxFinish(const std::shared_ptr<DocxExport>& job, bool success, const std::wstring& message) {
    if (!job || job != docxExport || job->finishing) return;
    job->finishing = true; job->success = success; job->message = message;
    if (job->view->hwnd) PostMessageW(job->view->hwnd, docxFinishMessage, 0, 0); else finishDocx(job->view);
}
static void docxFailed(const std::shared_ptr<Viewer>& view, const std::wstring& message) {
    if (docxExport && docxExport->view == view) queueDocxFinish(docxExport, false, message);
}
static void finishDocx(const std::shared_ptr<Viewer>& view) {
    auto job = docxExport; if (!job || job->view != view) return;
    if (job->file != INVALID_HANDLE_VALUE) { CloseHandle(job->file); job->file = INVALID_HANDLE_VALUE; }
    if (job->image) job->image->cancelled = true;
    if (view->hwnd) { KillTimer(view->hwnd, docxTimeoutTimer); KillTimer(view->hwnd, docxImageTimer); DestroyWindow(view->hwnd); }
    DeleteFileW(view->page.c_str()); DeleteFileW(job->temporary.c_str()); docxExport.reset();
    auto message = job->success ? L"DOCX saved: " + job->target : job->message;
    if (job->success && job->missingImages) message += L". Unavailable images: " + std::to_wstring(job->missingImages);
    docxStatus(job->owner.lock(), message, false, !job->success);
    if (job->completed) job->completed(job->success, message);
}
static void cancelDocx() {
    if (!docxExport) return;
    auto job = docxExport; job->finishing = true; job->success = false; job->message = L"DOCX export cancelled."; finishDocx(job->view);
}
static std::string base64(const std::string& bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        unsigned a = (unsigned char)bytes[i], b = i+1 < bytes.size() ? (unsigned char)bytes[i+1] : 0, c = i+2 < bytes.size() ? (unsigned char)bytes[i+2] : 0;
        out += alphabet[a >> 2]; out += alphabet[((a & 3) << 4) | (b >> 4)];
        out += i+1 < bytes.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '='; out += i+2 < bytes.size() ? alphabet[c & 63] : '=';
    }
    return out;
}
static bool decodeBase64(const std::wstring& text, std::string& out) {
    if (text.empty() || text.size() > 65536 || text.size() % 4) return false;
    unsigned value = 0; int bits = -8; bool padding = false;
    for (wchar_t c : text) {
        if (c == '=') { padding = true; continue; }
        if (padding) return false;
        int n = c >= 'A' && c <= 'Z' ? c-'A' : c >= 'a' && c <= 'z' ? c-'a'+26 : c >= '0' && c <= '9' ? c-'0'+52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (n < 0) return false;
        value = ((value << 6) | n) & 0xffffff; bits += 6;
        if (bits >= 0) { out += (char)((value >> bits) & 255); bits -= 8; }
    }
    return !out.empty();
}
static void sendDocxImage(const std::shared_ptr<Viewer>& view, const std::shared_ptr<DocxImageRequest>& image) {
    view->script(L"window.markdownView.docxResourceResult(" + image->id + L"," + fromBytes(jsonString(image->data), CP_UTF8) + L"," +
        fromBytes(jsonString(image->error), CP_UTF8) + L")");
}
static void pollDocxImage(const std::shared_ptr<Viewer>& view) {
    auto job = docxExport;
    if (!job || job->view != view || job->finishing || !job->image) { if (view->hwnd) KillTimer(view->hwnd, docxImageTimer); return; }
    if (job->cachedImage) {
        if (!job->cachedImage->complete) return;
        job->image->data = job->cachedImage->data; job->image->error = job->cachedImage->error; job->image->complete = true; job->cachedImage.reset();
    }
    if (!job->image->complete.load(std::memory_order_acquire)) return;
    auto image = job->image; job->image.reset(); KillTimer(view->hwnd, docxImageTimer); sendDocxImage(view, image);
}
static void docxMessage(const std::shared_ptr<Viewer>& view, const std::wstring& message) {
    auto job = docxExport; if (!job || job->view != view || job->finishing) return;
    if (message.rfind(L"docx-error:", 0) == 0) { queueDocxFinish(job, false, message.substr(11, 1000)); return; }
    if (message.rfind(L"docx-resource:", 0) == 0) {
        auto separator = message.find(L':', 14); if (separator == std::wstring::npos) return;
        auto id = message.substr(14, separator - 14), url = message.substr(separator + 1);
        if (id.empty() || id.size() > 8 || id.find_first_not_of(L"0123456789") != std::wstring::npos) return;
        auto image = std::make_shared<DocxImageRequest>(); image->id = id; image->url = url;
        auto cached = view->images.find(url.substr(0, url.find(L'#')));
        if (cached != view->images.end() && (cached->second->complete || cached->second->contentRequested)) {
            job->image = image; job->cachedImage = cached->second; SetTimer(view->hwnd, docxImageTimer, 50, nullptr); pollDocxImage(view); return;
        }
        if (url.rfind(L"https://", 0) == 0 || url.rfind(L"http://", 0) == 0) {
            if (job->image) { image->error = "The previous image download is still in progress"; sendDocxImage(view, image); return; }
            job->image = image; SetTimer(view->hwnd, docxImageTimer, 50, nullptr); startDocxImage(image); return;
        }
        std::wstring path; std::string bytes;
        if (pathFromFileUrl(url, path)) {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) image->error = "Local file not found: " + utf8(path);
            else if (!readBytes(path, bytes, 16*1024*1024)) image->error = "Could not read the file or the image exceeds 16 MiB";
            else {
                auto mime = imageMime(bytes);
                if (!mime.empty()) image->data = "data:" + mime + ";base64," + base64(bytes);
                else image->error = "The file is not a supported image";
            }
        } else image->error = "Invalid image URL";
        sendDocxImage(view, image); return;
    }
    if (message.rfind(L"docx-chunk:", 0) == 0) {
        auto separator = message.find(L':', 11); std::string bytes;
        if (separator == std::wstring::npos || message.substr(11, separator-11) != std::to_wstring(job->chunk) || !decodeBase64(message.substr(separator+1), bytes) ||
            job->bytes + bytes.size() > 128*1024*1024 || (job->chunk == 0 && bytes.compare(0, 4, "PK\3\4") != 0)) {
            queueDocxFinish(job, false, L"Invalid DOCX data or the document exceeds 128 MiB."); return;
        }
        DWORD written = 0;
        if (job->file == INVALID_HANDLE_VALUE || !WriteFile(job->file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) || written != bytes.size()) {
            queueDocxFinish(job, false, L"Could not write DOCX. Check folder access and available disk space."); return;
        }
        job->bytes += bytes.size();
        view->script(L"window.markdownView.docxChunkAccepted(" + std::to_wstring(job->chunk++) + L")"); return;
    }
    if (message.rfind(L"docx-done:", 0) == 0) {
        job->missingImages = std::max(0, _wtoi(message.c_str()+10));
        if (job->bytes < 100 || job->file == INVALID_HANDLE_VALUE) { queueDocxFinish(job, false, L"The generated DOCX is empty."); return; }
        BOOL flushed = FlushFileBuffers(job->file); CloseHandle(job->file); job->file = INVALID_HANDLE_VALUE;
        if (!flushed || !MoveFileExW(job->temporary.c_str(), job->target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            queueDocxFinish(job, false, L"Could not save DOCX. The file may be open in another application.");
        else queueDocxFinish(job, true, L"");
    }
}
static bool startDocxExport(const std::shared_ptr<Viewer>& owner, const std::wstring& target,
    std::function<void(bool, const std::wstring&)> completed = {}, std::shared_ptr<Viewer> snapshot = {}) {
    if (docxExport) return false;
    auto job = std::make_shared<DocxExport>(); job->owner = owner; job->completed = std::move(completed);
    job->view = snapshot ? snapshot : std::make_shared<Viewer>(); job->view->pdfMode = true; job->view->docxMode = true; job->view->dark = false;
    wchar_t absolute[32768]{}; if (!GetFullPathNameW(target.c_str(), 32768, absolute, nullptr)) return false; job->target = absolute;
    docxExport = job;
    if (!job->view->captureContent()) { queueDocxFinish(job, false, job->view->error); return true; }
    if (!snapshot && owner && owner->source == job->view->source) job->view->images = owner->images;
    GUID guid; CoCreateGuid(&guid); wchar_t id[40]; StringFromGUID2(guid, id, 40);
    auto directory = previewDirectory();
    if (directory.empty()) { queueDocxFinish(job,false,L"Cannot create a temporary preview directory. Check data directory permissions."); return true; }
    job->view->page = directory + L"\\" + previewName(L"docx-" + std::wstring(id) + L".html"); job->temporary = job->target + L"." + id + L".tmp.docx";
    job->file = CreateFileW(job->temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (job->file == INVALID_HANDLE_VALUE) { queueDocxFinish(job, false, L"Cannot write a DOCX to the selected folder."); return true; }
    auto holder = new std::shared_ptr<Viewer>(job->view);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"Markdown_with_Mermaid DOCX", WS_POPUP,
        -32000, -32000, 794, 1123, owner ? owner->hwnd : nullptr, nullptr, instance, holder);
    if (!hwnd) { delete holder; queueDocxFinish(job, false, L"Could not create the DOCX export window."); return true; }
    SetTimer(hwnd, docxTimeoutTimer, 180000, nullptr); docxStatus(owner, L"Preparing DOCX…", true);
    if (job->view->render()) initializeWebView(job->view); else queueDocxFinish(job, false, job->view->error);
    return true;
}
static void chooseDocx(const std::shared_ptr<Viewer>& owner) {
    auto requested = owner->pendingDocx; owner->pendingDocx.reset();
    if (docxExport) { docxStatus(owner, L"DOCX export is already in progress…", true); return; }
    auto snapshot = requested ? requested : std::make_shared<Viewer>(); snapshot->pdfMode = true; snapshot->docxMode = true;
    if (snapshot->payload.empty()) snapshot->selectionOnly = owner->editorSelectionActive;
    if (!snapshot->captureContent()) { docxStatus(owner, snapshot->error, false, true); return; }
    if (owner && owner->source == snapshot->source) snapshot->images = owner->images;
    wchar_t path[32768]{}; wcsncpy(path, PathFindFileNameW(snapshot->source.c_str()), 32000); PathRemoveExtensionW(path); wcscat(path, L".docx");
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = owner->hwnd; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Word document (*.docx)\0*.docx\0\0"; dialog.lpstrDefExt = L"docx"; dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) startDocxExport(owner, path, {}, snapshot);
}
