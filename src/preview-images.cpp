// Observe responses already used by the visible preview; never issue a request.
// Snapshot copies retain the exact bytes even when the editor switches files.
static std::wstring imageKey(const std::wstring& url) { return url.substr(0, url.find(L'#')); }
static void watchPreviewImages(const std::shared_ptr<Viewer>& view) {
    ComPtr<ICoreWebView2_2> webview;
    if (FAILED(view->webview->QueryInterface(IID_ICoreWebView2_2, (void**)webview.put()))) return;
    std::weak_ptr<Viewer> weak = view;
    auto response = cb<ICoreWebView2WebResourceResponseReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs*>(
        [weak](ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs* args) -> HRESULT {
            auto view = weak.lock(); if (!view || !view->hwnd) return S_OK;
            ComPtr<ICoreWebView2WebResourceRequest> request;
            ComPtr<ICoreWebView2WebResourceResponseView> response;
            if (FAILED(args->get_Request(request.put())) || FAILED(args->get_Response(response.put()))) return S_OK;
            LPWSTR raw = nullptr; request->get_Uri(&raw);
            std::wstring url = raw ? imageKey(raw) : L""; CoTaskMemFree(raw);
            if (url.empty() || url.rfind(L"data:", 0) == 0) return S_OK;
            ComPtr<ICoreWebView2HttpResponseHeaders> headers; response->get_Headers(headers.put());
            if (!headers) return S_OK;
            int status = 0; response->get_StatusCode(&status);
            if (status >= 300 && status < 400 && status != 304) {
                LPWSTR location = nullptr;
                if (SUCCEEDED(headers->GetHeader(L"Location", &location)) && location) {
                    wchar_t target[32768]{}; DWORD length = 32768;
                    if (view->images.size() < 512 && SUCCEEDED(UrlCombineW(url.c_str(), location, target, &length, 0))) {
                        auto found = view->images.find(url);
                        auto image = found != view->images.end() && !found->second->complete ? found->second : std::make_shared<PreviewImage>();
                        view->images[url] = image; view->images[imageKey(target)] = image;
                    }
                }
                CoTaskMemFree(location); return S_OK;
            }
            LPWSTR contentType = nullptr; headers->GetHeader(L"Content-Type", &contentType);
            std::wstring mime = contentType ? contentType : L""; CoTaskMemFree(contentType);
            std::transform(mime.begin(), mime.end(), mime.begin(), towlower);
            if (status < 200 || status >= 300 || mime.rfind(L"image/", 0) != 0) {
                auto pending = view->images.find(url);
                if (pending != view->images.end() && !pending->second->complete) {
                    pending->second->error = mime.rfind(L"text/html", 0) == 0 ? "The server returned an HTML page instead of an image" : "HTTP " + std::to_string(status);
                    pending->second->complete = true;
                }
                return S_OK;
            }
            if (view->images.size() >= 512 && !view->images.count(url)) return S_OK;
            auto found = view->images.find(url);
            auto image = found != view->images.end() && !found->second->complete ? found->second : std::make_shared<PreviewImage>();
            view->images[url] = image;
            image->contentRequested = true;
            LPWSTR declaredLength = nullptr;
            if (SUCCEEDED(headers->GetHeader(L"Content-Length", &declaredLength)) && declaredLength && wcstoull(declaredLength, nullptr, 10) > 16*1024*1024) {
                CoTaskMemFree(declaredLength); image->error = "The image exceeds 16 MiB"; image->complete = true; return S_OK;
            }
            CoTaskMemFree(declaredLength);
            const auto source = view->source;
            response->AddRef();
            auto retained = std::shared_ptr<ICoreWebView2WebResourceResponseView>(response.get(), [](auto* value) { value->Release(); });
            auto content = cb<ICoreWebView2WebResourceResponseViewGetContentCompletedHandler, HRESULT, IStream*>(
                [weak, image, source, retained](HRESULT hr, IStream* stream) -> HRESULT {
                    std::string bytes;
                    if (SUCCEEDED(hr) && stream) {
                        char buffer[65536]; ULONG count; HRESULT read;
                        do {
                            count = 0; read = stream->Read(buffer, sizeof(buffer), &count);
                            if (FAILED(read) || bytes.size() + count > 16*1024*1024) { bytes.clear(); break; }
                            bytes.append(buffer, count);
                        } while (count);
                    }
                    auto mime = imageMime(bytes); auto view = weak.lock(); size_t cachedBytes = 0;
                    if (view && view->source == source) {
                        std::set<const PreviewImage*> counted;
                        for (const auto& entry : view->images) if (counted.insert(entry.second.get()).second) cachedBytes += entry.second->data.size();
                    }
                    if (mime.empty()) image->error = "Could not retrieve the preview image or the file exceeds 16 MiB";
                    else if (cachedBytes + (bytes.size() + 2) / 3 * 4 > 64*1024*1024) image->error = "The preview image cache exceeds 64 MiB";
                    else {
                        image->data = "data:" + mime + ";base64," + base64(bytes);
                    }
                    image->complete = true; return S_OK;
                });
            HRESULT started = response->GetContent(content); content->Release();
            if (FAILED(started)) { image->error = "Could not read the preview image"; image->complete = true; }
            return S_OK;
        });
    EventRegistrationToken token;
    webview->add_WebResourceResponseReceived(response, &token); response->Release();
}
