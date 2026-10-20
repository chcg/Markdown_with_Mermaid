// One isolated, immutable WebView2 document per PDF export. Included by webview.cpp.
struct PdfExport {
    std::shared_ptr<Viewer> view;
    std::weak_ptr<Viewer> owner;
    std::wstring target, temporary, message;
    std::function<void(bool, const std::wstring&)> completed;
    bool printing = false, finishing = false, success = false;
    int missingImages = 0;
};
static std::shared_ptr<PdfExport> pdfExport;

static void pdfStatus(const std::shared_ptr<Viewer>& owner, const std::wstring& text, bool busy, bool error = false) {
    if (owner && owner->hwnd && owner->webview)
        owner->script(L"window.markdownView.setPdfStatus(" + fromBytes(jsonString(utf8(text)), CP_UTF8) +
            (busy ? L",true," : L",false,") + (error ? L"true)" : L"false)"));
}
static void queuePdfFinish(const std::shared_ptr<PdfExport>& job, bool success, const std::wstring& text) {
    if (!job || job != pdfExport || job->finishing) return;
    job->finishing = true; job->success = success; job->message = text;
    if (job->view->hwnd) PostMessageW(job->view->hwnd, pdfFinishMessage, 0, 0);
    else finishPdf(job->view);
}
static void pdfFailed(const std::shared_ptr<Viewer>& view, const std::wstring& text) {
    if (pdfExport && pdfExport->view == view) queuePdfFinish(pdfExport, false, text);
}
static void finishPdf(const std::shared_ptr<Viewer>& view) {
    auto job = pdfExport;
    if (!job || job->view != view) return;
    if (view->hwnd) { KillTimer(view->hwnd, pdfTimeoutTimer); DestroyWindow(view->hwnd); }
    DeleteFileW(view->page.c_str());
    if (!job->temporary.empty()) DeleteFileW(job->temporary.c_str());
    pdfExport.reset();
    auto owner = job->owner.lock();
    std::wstring message = job->success ? L"PDF saved: " + job->target : job->message;
    if (job->success && job->missingImages) message += L". Unavailable images: " + std::to_wstring(job->missingImages);
    pdfStatus(owner, message, false, !job->success);
    if (job->completed) job->completed(job->success, message);
}
static void cancelPdf() {
    if (!pdfExport) return;
    auto job = pdfExport;
    job->finishing = true; job->success = false; job->message = L"PDF export cancelled.";
    finishPdf(job->view);
}
static void pdfDocumentReady(const std::shared_ptr<Viewer>& view, int missingImages) {
    auto job = pdfExport;
    if (!job || job->view != view || job->printing || job->finishing) return;
    job->missingImages = std::max(0, missingImages);
    ComPtr<ICoreWebView2_7> printView;
    ComPtr<ICoreWebView2Environment6> printEnvironment;
    ComPtr<ICoreWebView2PrintSettings> settings;
    if (FAILED(view->webview->QueryInterface(IID_ICoreWebView2_7, reinterpret_cast<void**>(printView.put()))) ||
        !view->environment || FAILED(view->environment->QueryInterface(IID_ICoreWebView2Environment6, reinterpret_cast<void**>(printEnvironment.put()))) ||
        FAILED(printEnvironment->CreatePrintSettings(settings.put()))) {
        queuePdfFinish(job, false, L"Update Microsoft Edge WebView2 Runtime to export PDF."); return;
    }
    settings->put_Orientation(COREWEBVIEW2_PRINT_ORIENTATION_PORTRAIT);
    settings->put_PageWidth(210.0 / 25.4); settings->put_PageHeight(297.0 / 25.4);
    settings->put_MarginTop(15.0 / 25.4); settings->put_MarginBottom(15.0 / 25.4);
    settings->put_MarginLeft(15.0 / 25.4); settings->put_MarginRight(15.0 / 25.4);
    settings->put_ScaleFactor(1.0); settings->put_ShouldPrintBackgrounds(TRUE);
    settings->put_ShouldPrintHeaderAndFooter(FALSE);
    job->printing = true;
    pdfStatus(job->owner.lock(), L"Saving PDF…", true);
    auto done = cb<ICoreWebView2PrintToPdfCompletedHandler, HRESULT, BOOL>(
        [job](HRESULT hr, BOOL success) -> HRESULT {
            if (job != pdfExport || job->finishing) { DeleteFileW(job->temporary.c_str()); return S_OK; }
            if (FAILED(hr) || !success) queuePdfFinish(job, false, L"WebView2 could not create the PDF. Check access to the selected folder.");
            else if (!MoveFileExW(job->temporary.c_str(), job->target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                queuePdfFinish(job, false, L"Could not save PDF. The file may be open in another application.");
            else queuePdfFinish(job, true, L"");
            return S_OK;
        });
    auto hr = printView->PrintToPdf(job->temporary.c_str(), settings.get(), done); done->Release();
    if (FAILED(hr)) queuePdfFinish(job, false, L"Could not start PDF export.");
}

static bool startPdfExport(const std::shared_ptr<Viewer>& owner, const std::wstring& target,
    std::function<void(bool, const std::wstring&)> completed = {}, std::shared_ptr<Viewer> snapshot = {}) {
    if (pdfExport) return false;
    auto job = std::make_shared<PdfExport>(); job->owner = owner; job->completed = std::move(completed);
    job->view = snapshot ? snapshot : std::make_shared<Viewer>(); job->view->pdfMode = true; job->view->dark = false;
    wchar_t absolute[32768]{};
    if (!GetFullPathNameW(target.c_str(), 32768, absolute, nullptr)) return false;
    job->target = absolute;
    pdfExport = job;
    if (!job->view->captureContent()) { queuePdfFinish(job, false, job->view->error); return true; }
    GUID guid; CoCreateGuid(&guid); wchar_t id[40]; StringFromGUID2(guid, id, 40);
    auto directory = previewDirectory();
    if (directory.empty()) { queuePdfFinish(job,false,L"Cannot create a temporary preview directory. Check data directory permissions."); return true; }
    job->view->page = directory + L"\\" + previewName(L"pdf-" + std::wstring(id) + L".html");
    job->temporary = job->target + L"." + id + L".tmp.pdf";
    HANDLE probe = CreateFileW(job->temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE) { queuePdfFinish(job, false, L"Cannot write a PDF to the selected folder."); return true; }
    CloseHandle(probe);
    auto holder = new std::shared_ptr<Viewer>(job->view);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"Markdown_with_Mermaid PDF",
        WS_POPUP, -32000, -32000, 794, 1123, owner ? owner->hwnd : nullptr, nullptr, instance, holder);
    if (!hwnd) { delete holder; queuePdfFinish(job, false, L"Could not create the PDF export window."); return true; }
    SetTimer(hwnd, pdfTimeoutTimer, 120000, nullptr);
    pdfStatus(owner, L"Preparing PDF…", true);
    if (job->view->render()) initializeWebView(job->view);
    else queuePdfFinish(job, false, job->view->error);
    return true;
}

static void choosePdf(const std::shared_ptr<Viewer>& owner) {
    auto requested = owner->pendingPdf; owner->pendingPdf.reset();
    if (pdfExport) { pdfStatus(owner, L"PDF export is already in progress…", true); return; }
    // Capture before the save dialog: background changes cannot alter this export.
    auto snapshot = requested ? requested : std::make_shared<Viewer>(); snapshot->pdfMode = true;
    if (snapshot->payload.empty()) snapshot->selectionOnly = owner->editorSelectionActive;
    if (!snapshot->captureContent()) { pdfStatus(owner, snapshot->error, false, true); return; }
    wchar_t path[32768]{};
    wcsncpy(path, PathFindFileNameW(snapshot->source.c_str()), 32000); PathRemoveExtensionW(path); wcscat(path, L".pdf");
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = owner->hwnd;
    dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"PDF document (*.pdf)\0*.pdf\0\0"; dialog.lpstrDefExt = L"pdf";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) startPdfExport(owner, path, {}, snapshot);
}
