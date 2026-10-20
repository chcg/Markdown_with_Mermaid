// Preview navigation is independent of WebView2's internal HTML page history.
struct PreviewHistory {
    struct Visit { std::wstring file, fragment; int x = 0, y = 0; };
    std::vector<Visit> visits;
    size_t index = 0;
    unsigned revision = 0;
    bool restore = false;
    bool matches(const std::wstring& file) const {
        return !visits.empty() && !_wcsicmp(visits[index].file.c_str(), file.c_str());
    }
    void append(Visit visit) {
        if (!visits.empty()) visits.resize(index + 1);
        visits.push_back(std::move(visit));
        if (visits.size() > 100) visits.erase(visits.begin());
        index = visits.size() - 1; ++revision; restore = false;
    }
    void observe(const std::wstring& file, int y) {
        if (!matches(file)) append({file, L"", 0, y});
    }
    bool canBack() const { return !visits.empty() && index > 0; }
    bool canForward() const { return !visits.empty() && index + 1 < visits.size(); }
    void remember(unsigned token, int x, int y) {
        if (visits.empty() || token != revision) return;
        auto& visit = visits[index]; visit.x = x; visit.y = y; visit.fragment.clear();
    }
    void anchor(const std::wstring& fragment, bool newVisit = false) {
        if (visits.empty()) return;
        if (newVisit) { auto file = visits[index].file; append({file, fragment, 0, 0}); }
        else { visits[index].fragment = fragment; ++revision; }
        restore = true;
    }
};
