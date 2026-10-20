// HTTP image downloads run without COM or UI access. A module reference keeps
// this worker's code alive if Notepad++ unloads the plugin during cancellation.
struct DocxImageRequest {
    std::wstring id, url;
    std::string data, error;
    std::atomic<bool> complete{false}, cancelled{false};
};
struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET handle) : value(handle) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    explicit operator bool() const { return value != nullptr; }
};
static std::string imageMime(const std::string& bytes) {
    if (bytes.compare(0, 8, "\211PNG\r\n\032\n") == 0) return "image/png";
    if (bytes.compare(0, 3, "\377\330\377") == 0) return "image/jpeg";
    if (bytes.compare(0, 6, "GIF87a") == 0 || bytes.compare(0, 6, "GIF89a") == 0) return "image/gif";
    if (bytes.compare(0, 2, "BM") == 0) return "image/bmp";
    if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0) return "image/webp";
    auto prefix = bytes.substr(0, 4096);
    auto svg = prefix.find("<svg");
    if (svg != std::string::npos && svg + 4 < prefix.size() && (isspace((unsigned char)prefix[svg+4]) || prefix[svg+4] == '>')) return "image/svg+xml";
    return "";
}
static std::wstring httpHeader(HINTERNET request, DWORD header) {
    wchar_t value[8192]{}; DWORD size = sizeof(value);
    return WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, value, &size, WINHTTP_NO_HEADER_INDEX) ? value : L"";
}
static std::wstring githubImageUrl(const std::wstring& url) {
    URL_COMPONENTS parts{sizeof(parts)}; parts.dwHostNameLength = parts.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return url;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
    auto owner = path.find(L'/', 1), repository = owner == std::wstring::npos ? owner : path.find(L'/', owner + 1);
    if (!_wcsicmp(host.c_str(), L"github.com") && repository != std::wstring::npos && path.compare(repository, 6, L"/blob/") == 0)
        return L"https://raw.githubusercontent.com" + path.substr(0, repository) + L"/" + path.substr(repository + 6);
    return url;
}
static void downloadDocxImage(const std::shared_ptr<DocxImageRequest>& image) {
    constexpr size_t limit = 16 * 1024 * 1024;
    const auto deadline = GetTickCount64() + 20000;
    HttpHandle session(WinHttpOpen(L"Markdown_with_Mermaid/" MWM_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    auto failed = [&] { image->error = "Windows network error " + std::to_string(GetLastError()); };
    if (!session) { failed(); return; }
    WinHttpSetTimeouts(session.value, 3000, 5000, 5000, 5000);
    std::wstring url = githubImageUrl(image->url);
    for (int redirect = 0; redirect <= 5; ++redirect) {
        if (image->cancelled) return;
        if (GetTickCount64() > deadline) { image->error = "Image download timed out"; return; }
        URL_COMPONENTS parts{sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
        if (url.size() > 16384 || !WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) || parts.dwUserNameLength || parts.dwPasswordLength) {
            image->error = "Invalid HTTP image URL"; return;
        }
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        path = path.substr(0, path.find(L'#')); if (path.empty()) path = L"/";
        HttpHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
        if (!connection) { failed(); return; }
        const wchar_t* types[] = {L"image/*", nullptr};
        HttpHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, types,
            parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
        if (!request) { failed(); return; }
        DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_REDIRECTS;
        DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
        WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon));
        if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) { failed(); return; }
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) { failed(); return; }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            auto location = httpHeader(request.value, WINHTTP_QUERY_LOCATION);
            if (redirect == 5 || location.empty()) { image->error = "Too many redirects or missing Location header"; return; }
            wchar_t combined[32768]{}; DWORD length = 32768;
            if (FAILED(UrlCombineW(url.c_str(), location.c_str(), combined, &length, 0))) { image->error = "Invalid redirect"; return; }
            std::wstring next = combined;
            if (parts.nScheme == INTERNET_SCHEME_HTTPS && next.rfind(L"https://", 0) != 0) { image->error = "Unsafe HTTPS redirect"; return; }
            url = next; continue;
        }
        if (status < 200 || status >= 300) { image->error = "HTTP " + std::to_string(status); return; }
        auto type = httpHeader(request.value, WINHTTP_QUERY_CONTENT_TYPE);
        std::transform(type.begin(), type.end(), type.begin(), towlower);
        if (type.rfind(L"text/html", 0) == 0) { image->error = "The server returned an HTML page instead of an image"; return; }
        auto length = httpHeader(request.value, WINHTTP_QUERY_CONTENT_LENGTH);
        auto declaredBytes = length.empty() ? 0 : wcstoull(length.c_str(), nullptr, 10);
        if (declaredBytes > limit) { image->error = "The image exceeds 16 MiB"; return; }
        std::string bytes; char buffer[16384];
        for (;;) {
            if (image->cancelled) return;
            if (GetTickCount64() > deadline) { image->error = "Image download timed out"; return; }
            DWORD read = 0;
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) { failed(); return; }
            if (!read) break;
            if (bytes.size() + read > limit) { image->error = "The image exceeds 16 MiB"; return; }
            bytes.append(buffer, read);
            if (declaredBytes && bytes.size() >= declaredBytes) break;
        }
        auto mime = imageMime(bytes);
        if (mime.empty()) { image->error = "The server response is not a supported image"; return; }
        image->data = "data:" + mime + ";base64," + base64(bytes); return;
    }
}
struct DocxImageWorker {
    std::shared_ptr<DocxImageRequest> image;
    HMODULE module = nullptr;
};
static DWORD WINAPI docxImageThread(void* context) {
    HMODULE module;
    {
        std::unique_ptr<DocxImageWorker> worker((DocxImageWorker*)context); module = worker->module;
        try {
            downloadDocxImage(worker->image);
            // A stale keep-alive connection can be reset before a GET completes.
            // Retry only this transport failure, never a completed preview image.
            if (!worker->image->cancelled && worker->image->data.empty() && worker->image->error == "Windows network error 12030") {
                worker->image->error.clear(); downloadDocxImage(worker->image);
            }
        }
        catch (...) { worker->image->error = "Could not download the image"; }
        worker->image->complete.store(true, std::memory_order_release);
    }
    if (module) FreeLibraryAndExitThread(module, 0);
    return 0;
}
static void startDocxImage(const std::shared_ptr<DocxImageRequest>& image) {
    auto worker = new DocxImageWorker{image};
    if (instance != GetModuleHandleW(nullptr) && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&docxImageThread, &worker->module)) {
        delete worker; image->error = "Could not start the image downloader"; image->complete = true; return;
    }
    auto thread = CreateThread(nullptr, 0, docxImageThread, worker, 0, nullptr);
    if (thread) CloseHandle(thread);
    else { if (worker->module) FreeLibrary(worker->module); delete worker; image->error = "Could not create the download thread"; image->complete = true; }
}
