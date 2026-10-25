// ─────────────────────────────────────────────────────────────────────────────
// native/installer/main.cpp — entry point, argument parsing, elevation.
//
// The manifest asks for asInvoker rather than requireAdministrator (unlike the
// app itself). That is deliberate: it lets a non-elevated start detect the
// situation, re-launch itself once with the "runas" verb, and then refuse to
// loop if the user declines. Embedding requireAdministrator would make the
// silent/unattended paths fail with a bare error code instead of a reason.
// ─────────────────────────────────────────────────────────────────────────────
#include "setup.hpp"

#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace setup {
namespace ui {
// Defined in ui.cpp.
int run(const Options& opt);
} // namespace ui
namespace {

bool running_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const bool ok =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) != 0;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

// Re-launch this binary with UAC, forwarding every argument. Returns false only
// when the user declined the prompt (ShellExecute reports that as <= 32).
bool relaunch_elevated(const std::wstring& arguments) {
    const std::wstring me = self_path();
    const HINSTANCE rc = ShellExecuteW(nullptr, L"runas", me.c_str(), arguments.c_str(),
                                        nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

// Rebuilds the original command line, minus --elevated, plus whatever we add.
std::wstring forwardable_args(int argc, LPWSTR* argv, bool add_elevated) {
    std::wstring out;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--elevated") continue;
        out += L'"';
        out += a;
        out += L"\" ";
    }
    if (add_elevated) out += L"--elevated";
    return out;
}

int usage() {
    // Reached only from --help, which never runs elevated.
    static const wchar_t kHelp[] =
        L"GameLoop Tuner setup\n\n"
        L"  --uninstall    remove the installed app\n"
        L"  --silent       no window; the exit code is the result\n"
        L"  --no-desktop   skip the desktop shortcut\n\n";
    MessageBoxW(nullptr, kHelp, L"GameLoop Tuner Setup", MB_OK | MB_ICONINFORMATION);
    return 0;
}

} // namespace
} // namespace setup

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace setup;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    Options opt;
    bool want_desktop = true;
    bool saw_uninstall = false;
    bool already_elevated = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--uninstall") {
            saw_uninstall = true;
        } else if (a == L"--silent" || a == L"/S") {
            opt.silent = true;
        } else if (a == L"--no-desktop") {
            want_desktop = false;
        } else if (a == L"--elevated") {
            already_elevated = true;
        } else if (a == L"--help" || a == L"/?") {
            LocalFree(argv);
            return usage();
        }
    }

    opt.mode = saw_uninstall ? Mode::Uninstall : Mode::Install;
    opt.desktop = want_desktop;

    // Program Files is machine-wide, so both paths need admin. If we are not
    // elevated, ask once. --elevated is the loop guard: if we get here with the
    // flag already set, UAC must have granted us nothing and retrying would
    // just re-prompt forever.
    if (!running_elevated()) {
        if (already_elevated) {
            MessageBoxW(nullptr, L"Administrator rights are required to continue.",
                        L"GameLoop Tuner Setup", MB_OK | MB_ICONERROR);
            LocalFree(argv);
            return 1;
        }
        const std::wstring pass_on = forwardable_args(argc, argv, true);
        const bool started = relaunch_elevated(pass_on);
        LocalFree(argv);
        if (!started) {
            MessageBoxW(nullptr, L"Administrator rights are required to install or "
                                 L"remove GameLoop Tuner.",
                        L"GameLoop Tuner Setup", MB_OK | MB_ICONERROR);
            return 1;
        }
        return 0;  // the elevated copy owns the work from here
    }

    LocalFree(argv);

    // CoInitialize before anything touches IShellLink. S_OK here means COM was
    // already up; RPC_E_CHANGED_MODE would mean it was up in a threading model
    // we cannot change, and the shortcut writes will simply fail.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                  COINIT_DISABLE_OLE1DDE);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 1;

    const int code = ui::run(opt);

    if (SUCCEEDED(com)) CoUninitialize();
    return code;
}