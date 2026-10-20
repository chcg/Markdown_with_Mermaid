// Uses the real loader/runtime and host API adapter; does not change machine Runtime registration.
#define wmain integrationMain
#include "host.cpp"
#undef wmain

static void marker(const std::wstring& folder, unsigned pending = 0, DWORD browser = 0) {
    std::string text = "MWM_SESSION_V1\n0 0 " + std::to_string(pending) + "\n";
    if (browser) text += std::to_string(browser) + " " + std::to_string(SessionData::creationTime(GetCurrentProcess())) + "\n";
    HANDLE file = CreateFileW((folder+L"\\session.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    DWORD written = 0; WriteFile(file,text.data(),text.size(),&written,nullptr); CloseHandle(file);
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    instance = GetModuleHandleW(nullptr); bool ok = true;
    const auto root = std::wstring(argv[2]); SHCreateDirectoryExW(nullptr,root.c_str(),nullptr);
    const auto settings = root + L"\\settings.ini", document = root + L"\\user-document.md";
    WritePrivateProfileStringW(L"Options",L"SyncScroll",L"1",settings.c_str());
    HANDLE file = CreateFileW(document.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    const char* sentinel="USER_DOCUMENT"; DWORD written; WriteFile(file,sentinel,13,&written,nullptr); CloseHandle(file);
    auto stale = std::make_unique<SessionData::Session>(root);
    const auto staleFolder = stale->folder; marker(staleFolder);
    const auto staleProfile=stale->profile; SHCreateDirectoryExW(nullptr,staleProfile.c_str(),nullptr);
    const auto crashedPage=stale->previews+L"\\"+PathFindFileNameW(staleFolder.c_str())+L"-crash.html";
    HANDLE crashed=CreateFileW(crashedPage.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr); CloseHandle(crashed);
    SessionData::collect(root);
    ok &= check(SessionData::normalDirectory(staleFolder),"cleanup preserves an exclusively leased session");
    stale.reset(); SessionData::collect(root);
    ok &= check(GetFileAttributesW(staleFolder.c_str())==INVALID_FILE_ATTRIBUTES,"next startup removes a marked inactive session");
    ok &= check(GetFileAttributesW(staleProfile.c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW(crashedPage.c_str())==INVALID_FILE_ATTRIBUTES,"inactive session cleanup removes its browser profile and orphaned preview HTML");
    auto activeBrowser = std::make_unique<SessionData::Session>(root);
    const auto activeFolder = activeBrowser->folder; marker(activeFolder,0,GetCurrentProcessId()); activeBrowser.reset();
    SessionData::collect(root);
    ok &= check(SessionData::normalDirectory(activeFolder),"cleanup preserves a still-running browser process by PID and creation time");
    auto unknown = std::make_unique<SessionData::Session>(root);
    const auto unknownFolder=unknown->folder; marker(unknownFolder,1); unknown.reset(); SessionData::collect(root);
    ok &= check(SessionData::normalDirectory(unknownFolder),"cleanup retains uncertain interrupted browser initialization");
    const auto unmarked = root+L"\\sessions\\session-{11111111-1111-1111-1111-111111111111}";
    SHCreateDirectoryExW(nullptr,unmarked.c_str(),nullptr); SessionData::collect(root);
    ok &= check(SessionData::normalDirectory(unmarked),"cleanup never removes an unmarked directory");
    const auto junction = root+L"\\junction-fixture";
    if (GetFileAttributesW(junction.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ok &= check(SessionData::absolute(junction).empty(),"temporary root rejects a junction ancestor");
    }
    std::string preserved;
    ok &= check(readBytes(document,preserved) && preserved==sentinel && GetPrivateProfileIntW(L"Options",L"SyncScroll",0,settings.c_str())==1,"session cleanup preserves documents and persistent settings");
    ok &= check(!IsUserAnAdmin(),"lifecycle process runs without administrator membership");
    const auto missingRuntime = root+L"\\empty-runtime"; SHCreateDirectoryExW(nullptr,missingRuntime.c_str(),nullptr);
    SetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER",missingRuntime.c_str());
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=hostProc; wc.lpszClassName=L"Mermaid.LifecycleHost";
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_MERMAID)); RegisterClassW(&wc);
    HWND parent=CreateWindowExW(0,wc.lpszClassName,L"Lifecycle tests",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,instance,nullptr);
    setTestWindowIcons(parent);
    HWND sci=CreateWindowExW(0,wc.lpszClassName,L"editor",WS_CHILD,0,0,10,10,parent,nullptr,instance,nullptr);
    documentPath=std::wstring(argv[1])+L"\\fixtures\\navigation-a.md"; readBytes(documentPath,textBuffer);
    const auto source=textBuffer;
    hostConfigDirectory=root+L"\\host-config"; setInfo({parent,sci,sci});
    showPanel();
    for(int i=0;i<300 && panel && panel->error.find(L"Runtime")==std::wstring::npos;++i) pump(10);
    ok &= check(panel && !panel->webview && panel->error.find(L"WebView2 Runtime")!=std::wstring::npos && panel->error.find(L"https://developer.microsoft.com/microsoft-edge/webview2/")!=std::wstring::npos,"real loader with unavailable Runtime reports an installation URL without crashing");
    ok &= check(textBuffer==source && IsWindow(parent),"missing Runtime preserves editor content and host window");
    const auto error=panel ? panel->error : L"";
    if(panel) {
        ok &= check(pdfTest(root+L"\\missing-runtime.pdf",false,false),"missing Runtime cancels PDF and clears busy state");
        ok &= check(docxTest(root+L"\\missing-runtime.docx",false,false),"missing Runtime cancels DOCX and clears busy state");
        ok &= check(GetFileAttributesW((root+L"\\missing-runtime.pdf").c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW((root+L"\\missing-runtime.docx").c_str())==INVALID_FILE_ATTRIBUTES,"missing Runtime leaves no partial PDF or DOCX");
    }
    notify(NPPN_SHUTDOWN); DestroyWindow(parent);
    std::wprintf(L"Runtime error: %ls\n",error.c_str());
    puts(ok?"ALL LIFECYCLE TESTS PASSED":"LIFECYCLE TESTS FAILED"); return ok?0:1;
}
