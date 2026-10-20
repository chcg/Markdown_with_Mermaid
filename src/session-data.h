// Temporary preview/browser data is isolated from settings and exported documents.
// Only marked, inactive sessions are collected; junctions/symlinks are never followed.
#include <tlhelp32.h>
namespace SessionData {
static bool normalDirectory(const std::wstring& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
static std::wstring absolute(const std::wstring& path) {
    wchar_t buffer[32768]; const auto length = GetFullPathNameW(path.c_str(), 32768, buffer, nullptr);
    if (!length || length >= 32768) return {};
    std::wstring result(buffer); while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    // Reject reparse points in any existing ancestor, including the configured root.
    for (size_t i = 3; i <= result.size(); ++i) if (i == result.size() || result[i] == L'\\') {
        const auto attributes = GetFileAttributesW(result.substr(0, i).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    }
    return result;
}
static unsigned long long creationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
static bool possiblyAlive(DWORD pid, unsigned long long created) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    const auto actual = creationTime(process);
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT && (!created || !actual || actual == created);
    CloseHandle(process); return alive;
}
static bool livingDescendants(DWORD pid) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return true;
    std::vector<std::pair<DWORD,DWORD>> processes;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return true;}
    do{processes.emplace_back(entry.th32ProcessID,entry.th32ParentProcessID);}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    std::set<DWORD> descendants{pid}; bool changed;
    do{changed=false;for(const auto& process:processes)if(process.first!=pid&&descendants.count(process.second)&&descendants.insert(process.first).second)changed=true;}while(changed);
    return descendants.size()>1;
}
static bool safeTree(const std::wstring& folder) {
    if (!normalDirectory(folder)) return false;
    WIN32_FIND_DATAW item; HANDLE search = FindFirstFileW((folder + L"\\*").c_str(), &item);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool safe = true;
    do {
        if (!wcscmp(item.cFileName,L".") || !wcscmp(item.cFileName,L"..")) continue;
        if ((item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !safeTree(folder + L"\\" + item.cFileName))) { safe = false; break; }
    } while (FindNextFileW(search, &item));
    FindClose(search); return safe;
}
static bool eraseTree(const std::wstring& folder, const std::wstring& boundary) {
    // Every recursive target is checked before deletion, not only the initial path.
    const auto target = absolute(folder);
    if (target.empty() || (target != boundary && target.rfind(boundary + L"\\",0) != 0) || !normalDirectory(target)) return false;
    WIN32_FIND_DATAW item; HANDLE search = FindFirstFileW((target + L"\\*").c_str(), &item);
    bool ok = true;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(item.cFileName,L".") || !wcscmp(item.cFileName,L"..")) continue;
            const auto child = target + L"\\" + item.cFileName;
            if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ok = false; continue; }
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = eraseTree(child,boundary) && ok;
            else {
                if (item.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(child.c_str(),item.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                ok = DeleteFileW(child.c_str()) && ok;
            }
        } while (FindNextFileW(search, &item));
        FindClose(search);
    }
    return ok && RemoveDirectoryW(target.c_str());
}
static void collect(const std::wstring& dataRoot) {
    const auto data = absolute(dataRoot);
    const auto root = absolute(dataRoot + L"\\session-info");
    if (data.empty() || root.empty() || !normalDirectory(root)) return;
    WIN32_FIND_DATAW item; HANDLE search = FindFirstFileW((root + L"\\webview2-*").c_str(), &item);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name(item.cFileName);
        if (name.size() <= 9 || name.size() > 40 || name.rfind(L"webview2-",0) != 0 ||
            !std::all_of(name.begin()+9,name.end(),[](wchar_t c){return iswxdigit(c)||c==L'-';})) continue;
        const auto folder = root + L"\\" + name;
        if (!normalDirectory(folder)) continue;
        const auto lockPath = folder + L"\\session.lock";
        HANDLE lock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (lock == INVALID_HANDLE_VALUE) continue;
        std::string marker; DWORD owner = 0; unsigned long long created = 0; unsigned pending = 1; int offset = 0;
        bool safe = readBytes(folder + L"\\session.txt", marker, 4096) &&
            sscanf(marker.c_str(), "MWM_SESSION_V1\n%lu %llu %u\n%n", &owner, &created, &pending, &offset) == 3 &&
            pending == 0 && owner != GetCurrentProcessId() && !possiblyAlive(owner, created);
        while (safe && offset < static_cast<int>(marker.size())) {
            DWORD browser = 0; unsigned long long time = 0; int consumed = 0;
            if (sscanf(marker.c_str() + offset, "%lu %llu\n%n", &browser, &time, &consumed) != 2 || !consumed || possiblyAlive(browser,time) || livingDescendants(browser)) { safe = false; break; }
            offset += consumed;
        }
        const auto profile = absolute(data + L"\\" + name);
        const auto profileAttributes = profile.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(profile.c_str());
        const auto profileError = GetLastError();
        const bool profileExists = profileAttributes != INVALID_FILE_ATTRIBUTES;
        if (!profileExists && profileError != ERROR_FILE_NOT_FOUND && profileError != ERROR_PATH_NOT_FOUND) safe=false;
        if (safe) safe = !profile.empty() && safeTree(folder) && (!profileExists || safeTree(profile));
        CloseHandle(lock);
        if (safe && (!profileExists || eraseTree(profile,profile)) && eraseTree(folder,folder)) {
            const auto preview = absolute(data + L"\\preview");
            if (!preview.empty() && normalDirectory(preview)) {
                WIN32_FIND_DATAW page; HANDLE pages = FindFirstFileW((preview+L"\\"+name+L"-*.html").c_str(),&page);
                if (pages != INVALID_HANDLE_VALUE) {
                    do { if (!(page.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) DeleteFileW((preview+L"\\"+page.cFileName).c_str()); } while (FindNextFileW(pages,&page));
                    FindClose(pages);
                }
            }
        }
    } while (FindNextFileW(search, &item));
    FindClose(search);
}
struct Session {
    std::wstring folder, profile, previews;
    HANDLE lock = INVALID_HANDLE_VALUE;
    unsigned pending = 0;
    std::map<DWORD,unsigned long long> browsers;
    explicit Session(const std::wstring& dataRoot) {
        collect(dataRoot);
        const auto root = absolute(dataRoot);
        if (root.empty()) return;
        GUID guid{}; if (FAILED(CoCreateGuid(&guid))) return;
        wchar_t id[40]; StringFromGUID2(guid,id,40);
        std::wstring token; for(wchar_t c:std::wstring(id)) if(iswxdigit(c)) token+=c;
        // Keep the established short UDF layout for Chromium compatibility.
        auto name = L"webview2-" + std::to_wstring(GetCurrentProcessId());
        if (GetFileAttributesW((root+L"\\"+name).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW((root+L"\\session-info\\"+name).c_str()) != INVALID_FILE_ATTRIBUTES) name += L"-" + token.substr(0,8);
        folder = root + L"\\session-info\\" + name;
        if (SHCreateDirectoryExW(nullptr,folder.c_str(),nullptr) != ERROR_SUCCESS || !normalDirectory(folder)) { folder.clear(); return; }
        lock = CreateFileW((folder + L"\\session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) { folder.clear(); return; }
        // Keep HTML outside the browser profile and tag filenames for crash cleanup.
        profile = root + L"\\" + name; previews = root + L"\\preview";
        SHCreateDirectoryExW(nullptr,previews.c_str(),nullptr); save();
    }
    ~Session() { if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock); }
    bool save() {
        auto text = std::string("MWM_SESSION_V1\n") + std::to_string(GetCurrentProcessId()) + " " +
            std::to_string(creationTime(GetCurrentProcess())) + " " + std::to_string(pending) + "\n";
        for (const auto& browser : browsers) text += std::to_string(browser.first) + " " + std::to_string(browser.second) + "\n";
        HANDLE file = CreateFileW((folder + L"\\session.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD actual = 0; bool ok = WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&actual,nullptr) && actual == text.size(); CloseHandle(file); return ok;
    }
    void starting() { ++pending; save(); }
    void failedBeforeEnvironment() { if (pending) --pending; save(); }
    void connected(DWORD pid) {
        if (!pid) return;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        // A restricted parent may not query a broker-created browser. Keep its
        // PID with an unknown creation time: collection then errs on retention.
        const auto created = process ? creationTime(process) : 0;
        if (process) CloseHandle(process);
        browsers[pid] = created; if (pending) --pending; save();
    }
};
static std::map<std::wstring,std::shared_ptr<Session>> sessions;
static std::shared_ptr<Session> get(const std::wstring& root) {
    if (root.empty()) return {};
    auto& session = sessions[root];
    if (!session) session = std::make_shared<Session>(root);
    return session;
}
}
static std::wstring previewDirectory() {
    const auto session = SessionData::get(runtimeDirectory());
    return !session || session->lock == INVALID_HANDLE_VALUE ? L"" : session->previews;
}
static std::wstring previewName(const std::wstring& suffix) {
    const auto session = SessionData::get(runtimeDirectory());
    return session ? std::wstring(PathFindFileNameW(session->folder.c_str())) + L"-" + suffix : suffix;
}
