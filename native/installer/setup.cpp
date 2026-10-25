// ─────────────────────────────────────────────────────────────────────────────
// native/installer/setup.cpp — install / uninstall, shortcuts, registry.
//
// Scope note: this installs exactly two files into Program Files. Everything
// below exists because those two files need somewhere to live, three shortcuts,
// and an entry in Apps & Features. There is no payload to extract and no
// prerequisite walk, which is why this can be ~100 KB instead of ~1.5 MB.
// ─────────────────────────────────────────────────────────────────────────────
#include "setup.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <cstdio>
#include <vector>

namespace setup {
namespace {

// ── Small string/utility helpers ──────────────────────────────────────────────

// RegCreateKeyExW et al need a double-null-terminated multi-string.
std::vector<wchar_t> dnull(const std::wstring& s) {
    std::vector<wchar_t> v(s.begin(), s.end());
    v.push_back(L'\0');
    v.push_back(L'\0');
    return v;
}

bool iequals(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring known_folder(const GUID& id) {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        out = raw;
    }
    if (raw) CoTaskMemFree(raw);
    return out;
}

std::wstring programs_dir() {  // per-machine Start Menu, no trailing backslash
    std::wstring p = known_folder(FOLDERID_Programs);
    if (p.empty()) return p;
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    return p;
}

std::wstring public_desktop_dir() {
    std::wstring p = known_folder(FOLDERID_PublicDesktop);
    if (p.empty()) return p;
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    return p;
}

bool reg_write(HKEY root, const wchar_t* subkey, const wchar_t* name,
               DWORD type, const BYTE* data, DWORD bytes) {
    HKEY k = nullptr;
    DWORD disp = 0;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                        nullptr, &k, &disp) != ERROR_SUCCESS) {
        return false;
    }
    const LONG rc = RegSetValueExW(k, name, 0, type, data, bytes);
    RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

bool reg_write_str(HKEY root, const wchar_t* subkey, const wchar_t* name,
                   const std::wstring& value) {
    return reg_write(root, subkey, name, REG_SZ,
                     reinterpret_cast<const BYTE*>(value.c_str()),
                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

bool reg_write_dword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD value) {
    return reg_write(root, subkey, name, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void reg_delete_tree(HKEY root, const wchar_t* subkey) {
    RegDeleteTreeW(root, subkey);
}

std::wstring uninstall_key_path() {
    return std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\")
           + kUninstallKeyName;
}

// ── Shortcuts ─────────────────────────────────────────────────────────────────

bool make_shortcut(const std::wstring& link, const std::wstring& target,
                   const std::wstring& arguments, const std::wstring& description) {
    IShellLinkW* lnk = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&lnk)))) {
        return false;
    }
    lnk->SetPath(target.c_str());
    if (!arguments.empty()) lnk->SetArguments(arguments.c_str());
    lnk->SetDescription(description.c_str());
    // Index 0 means "use the icon embedded in the target file".
    lnk->SetIconLocation(target.c_str(), 0);

    bool ok = false;
    if (IPersistFile* file = nullptr;
        SUCCEEDED(lnk->QueryInterface(IID_PPV_ARGS(&file)))) {
        ok = SUCCEEDED(file->Save(link.c_str(), TRUE));
        file->Release();
    }
    lnk->Release();
    return ok;
}

// Locates the app payload beside this installer. Variant builds ship as
// GameLoopTuner-Beta.exe / GameLoopTuner-Hub.exe; the original single-build
// name is still accepted. Returns "" when none is present.
std::wstring find_source_app() {
    const std::wstring dir = self_dir();
    for (const wchar_t* name : {kExeNameBeta, kExeNameHub, kExeName}) {
        const std::wstring p = dir + name;
        if (file_exists(p)) return p;
    }
    return {};
}

// Silent recursive delete. Used for the install directory so that stray files
// left by an older version do not block the uninstall.
bool remove_tree(const std::wstring& path) {
    if (!file_exists(path)) return true;
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = dnull(path).data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0;
}

// ── Self-removal ──────────────────────────────────────────────────────────────

// The uninstaller deletes its own directory, so it has to outlive itself. The
// copy in %TEMP% waits for this process to exit, deletes the original, then
// deletes itself. MoveFileEx on reboot is the fallback if the helper never
// runs (machine powered off mid-uninstall).
void schedule_self_delete() {
    const std::wstring me = self_path();
    // GetTempPathW rather than SHGetKnownFolderPath(FOLDERID_Temp): the known
    // folder GUIDs are only declared when the SDK's DEFINE_KNOWN_FOLDER support
    // is pulled in, and this binary does not otherwise need shlobj's folder API.
    wchar_t temp_buf[MAX_PATH + 2]{};
    const DWORD temp_len = GetTempPathW(MAX_PATH + 1, temp_buf);
    if (temp_len == 0 || temp_len > MAX_PATH + 1) return;
    std::wstring temp_dir(temp_buf, temp_len);
    while (!temp_dir.empty() && temp_dir.back() == L'\\') temp_dir.pop_back();

    std::wstring staged = temp_dir + L"\\glt-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!CopyFileW(me.c_str(), staged.c_str(), FALSE)) {
        MoveFileExW(me.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        return;
    }

    // ping is used as the delay because it does not need a console attached.
    const std::wstring cmd =
        L"ping -n 3 127.0.0.1 >nul & del /f /q \"" + me + L"\" & del /f /q \"" + staged + L"\"";

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi)) {
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pi.hProcess) CloseHandle(pi.hProcess);
    }
}

} // namespace

// ── Public helpers ────────────────────────────────────────────────────────────

std::wstring widen(const char* utf8) {
    if (!utf8 || !*utf8) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
    return out;
}

std::wstring self_path() {
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(),
                                       static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    return std::wstring(buf.data(), n);
}

std::wstring self_dir() {
    const std::wstring me = self_path();
    const size_t slash = me.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : me.substr(0, slash + 1);
}

std::wstring install_dir() {
    const std::wstring pf = known_folder(FOLDERID_ProgramFiles);
    const std::wstring root = pf.empty() ? std::wstring(L"C:\\Program Files") : pf;
    return root + L"\\" + kInstallDirName;
}

std::wstring installed_app()     { return install_dir() + L"\\" + kExeName; }
std::wstring installed_setup()   { return install_dir() + L"\\" + kSetupName; }

bool file_exists(const std::wstring& path) {
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ensure_dir(const std::wstring& path) {
    if (path.empty()) return false;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool app_running() {
    const std::wstring target = installed_app();
    if (target.empty()) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry)) {
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!p) continue;
        wchar_t buf[32768]{};
        DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
            found = iequals(std::wstring(buf, n), target);
        }
        CloseHandle(p);
    }
    CloseHandle(snap);
    return found;
}

// ── Install ───────────────────────────────────────────────────────────────────

Result run_install(const Options& opt) {
    Result r;

    const std::wstring source_app = find_source_app();
    if (source_app.empty()) {
        r.message = L"GameLoopTuner-Beta.exe / GameLoopTuner-Hub.exe / GameLoopTuner.exe "
                    L"was not found next to the installer.\n\n"
                    L"Keep the app and the installer together and run the installer again.";
        return r;
    }

    if (app_running()) {
        r.message = L"GameLoop Tuner is currently running.\n\n"
                    L"Close it (it runs elevated, so check the taskbar for a "
                    L"shielded window) and run the installer again.";
        return r;
    }

    const std::wstring dir = install_dir();
    if (!ensure_dir(dir)) {
        r.message = L"Could not create the install folder:\n" + dir;
        return r;
    }

    if (!CopyFileW(source_app.c_str(), installed_app().c_str(), FALSE)) {
        r.message = L"Could not write:\n" + installed_app() + L"\n\n" +
                    L"Error " + std::to_wstring(GetLastError()) + L".";
        return r;
    }

    // Keep a copy of the installer next to the app so the registry's
    // UninstallString still resolves after the original setup is moved or
    // deleted. It is also what makes "repair" work from Apps & Features.
    CopyFileW(self_path().c_str(), installed_setup().c_str(), FALSE);

    const std::wstring menu = programs_dir() + L"\\" + kInstallDirName;
    if (ensure_dir(menu)) {
        make_shortcut(menu + L"\\" + kProductName + L".lnk", installed_app(), {},
                      kProductName);
        make_shortcut(menu + L"\\Uninstall " + std::wstring(kProductName) + L".lnk",
                      installed_setup(), L"--uninstall",
                      L"Uninstall GameLoop Tuner");
    }

    if (opt.desktop) {
        const std::wstring desktop = public_desktop_dir();
        if (!desktop.empty()) {
            make_shortcut(desktop + L"\\" + kProductName + L".lnk", installed_app(), {},
                          kProductName);
        }
    }

    // ── Apps & Features ──
    const std::wstring key = uninstall_key_path();
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"DisplayName", kProductName);
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"DisplayVersion", widen(version()));
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"Publisher", kPublisher);
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"InstallLocation", dir);
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"DisplayIcon", installed_app());
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(),
                  L"UninstallString", L"\"" + installed_setup() + L"\" --uninstall");
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(),
                  L"QuietUninstallString", L"\"" + installed_setup() + L"\" --uninstall --silent");
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"URLInfoAbout", L"https://codmsurge.uk");
    reg_write_str(HKEY_LOCAL_MACHINE, key.c_str(), L"HelpLink",
                  L"https://github.com/realLLUKS/GameloopTurner");

    WIN32_FILE_ATTRIBUTE_DATA attr{};
    DWORD kb = 0;
    if (GetFileAttributesExW(installed_app().c_str(), GetFileExInfoStandard, &attr)) {
        kb = (attr.nFileSizeLow + 1023) / 1024;
    }
    reg_write_dword(HKEY_LOCAL_MACHINE, key.c_str(), L"EstimatedSize", kb);
    reg_write_dword(HKEY_LOCAL_MACHINE, key.c_str(), L"NoModify", 1);
    reg_write_dword(HKEY_LOCAL_MACHINE, key.c_str(), L"NoRepair", 1);

    r.ok = true;
    r.message = L"GameLoop Tuner is installed.\n\n"
                L"Start it from the Start Menu, or run GameLoopTuner.exe "
                L"straight from the install folder.";
    return r;
}

// ── Uninstall ─────────────────────────────────────────────────────────────────

Result run_uninstall(const Options& opt) {
    (void)opt;
    Result r;

    if (app_running()) {
        r.message = L"GameLoop Tuner is currently running.\n\n"
                    L"Close it and run the uninstaller again.";
        return r;
    }

    // Shortcuts first: they are user-visible, and leaving them behind is the
    // most common complaint about uninstallers.
    const std::wstring menu = programs_dir() + L"\\" + kInstallDirName;
    DeleteFileW((menu + L"\\" + kProductName + L".lnk").c_str());
    DeleteFileW((menu + L"\\Uninstall " + std::wstring(kProductName) + L".lnk").c_str());
    RemoveDirectoryW(menu.c_str());

    const std::wstring desktop = public_desktop_dir();
    if (!desktop.empty()) {
        DeleteFileW((desktop + L"\\" + kProductName + L".lnk").c_str());
    }

    reg_delete_tree(HKEY_LOCAL_MACHINE, uninstall_key_path().c_str());

    const std::wstring dir = install_dir();
    if (!remove_tree(dir)) {
        r.message = L"Most files were removed, but the install folder could not be "
                    L"deleted:\n" + dir + L"\n\nIt is probably in use. Restart Windows "
                    L"and delete it by hand.";
        schedule_self_delete();
        return r;
    }

    schedule_self_delete();
    r.ok = true;
    r.message = L"GameLoop Tuner, its shortcuts and its Apps & Features entry "
                L"have been removed.";
    return r;
}

} // namespace setup