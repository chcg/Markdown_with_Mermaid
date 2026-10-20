// Frozen editor Markdown rendering for selection-aware HTML export.
struct HtmlExport {
    std::weak_ptr<Viewer> owner;
    std::shared_ptr<Viewer> view;
    std::wstring html, error;
    std::function<void(const std::wstring&, const std::wstring&)> completed;
    bool finishing = false;
};
static std::shared_ptr<HtmlExport> htmlExport;
static void htmlStatus(const std::shared_ptr<Viewer>& owner, const std::wstring& text, bool busy, bool error = false) {
    if (owner && owner->ready) owner->script(L"window.markdownView.setHtmlStatus(" + fromBytes(jsonString(utf8(text)), CP_UTF8) +
        L"," + (busy ? L"true" : L"false") + L"," + (error ? L"true" : L"false") + L")");
}
static void finishHtml(const std::shared_ptr<Viewer>& view) {
    auto job = htmlExport; if (!job || job->view != view) return;
    if (view->hwnd) { KillTimer(view->hwnd, htmlTimeoutTimer); DestroyWindow(view->hwnd); }
    DeleteFileW(view->page.c_str()); htmlExport.reset();
    htmlStatus(job->owner.lock(), job->error, false, !job->error.empty());
    if (job->completed) job->completed(job->html, job->error);
    else if (!job->html.empty() && job->error.empty()) saveHtml(job->html, view->source);
}
static void htmlFailed(const std::shared_ptr<Viewer>& view, const std::wstring& text) {
    auto job = htmlExport; if (!job || job->view != view || job->finishing) return;
    job->finishing = true; job->error = text; PostMessageW(view->hwnd, htmlFinishMessage, 0, 0);
}
static void htmlReady(const std::shared_ptr<Viewer>& view, const std::wstring& html) {
    auto job = htmlExport; if (!job || job->view != view || job->finishing) return;
    job->finishing = true; job->html = html; PostMessageW(view->hwnd, htmlFinishMessage, 0, 0);
}
static void cancelHtml() {
    if (!htmlExport) return;
    auto job = htmlExport; job->finishing = true; job->error = L"HTML export cancelled.";
    finishHtml(job->view);
}
static bool startHtmlExport(const std::shared_ptr<Viewer>& owner, std::shared_ptr<Viewer> snapshot = {},
    std::function<void(const std::wstring&, const std::wstring&)> completed = {}) {
    if (htmlExport) { htmlStatus(owner, L"HTML export is already in progress…", true); return false; }
    auto job = std::make_shared<HtmlExport>(); job->owner = owner; job->completed = std::move(completed);
    job->view = snapshot ? snapshot : std::make_shared<Viewer>();
    job->view->pdfMode = true; job->view->htmlMode = true; job->view->dark = owner && owner->dark;
    // A supplied snapshot has already frozen its source and mode flags.
    job->view->selectionOnly = true;
    if (!job->view->captureContent()) { htmlStatus(owner, job->view->error, false, true); return false; }
    htmlExport = job;
    GUID guid; CoCreateGuid(&guid); wchar_t id[40]; StringFromGUID2(guid, id, 40);
    auto directory = previewDirectory();
    if (directory.empty()) { job->error = L"Cannot create a temporary HTML preview directory."; finishHtml(job->view); return false; }
    job->view->page = directory + L"\\" + previewName(L"html-" + std::wstring(id) + L".html");
    auto holder = new std::shared_ptr<Viewer>(job->view);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"Markdown_with_Mermaid HTML",
        WS_POPUP, -32000, -32000, 1000, 820, owner ? owner->hwnd : nullptr, nullptr, instance, holder);
    if (!hwnd) { delete holder; job->error = L"Could not create the HTML export window."; finishHtml(job->view); return false; }
    SetTimer(hwnd, htmlTimeoutTimer, 120000, nullptr);
    htmlStatus(owner, L"Preparing HTML…", true);
    if (job->view->render()) initializeWebView(job->view); else htmlFailed(job->view, job->view->error);
    return true;
}
static void chooseHtml(const std::shared_ptr<Viewer>& owner) { startHtmlExport(owner); }
