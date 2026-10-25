// ─────────────────────────────────────────────────────────────────────────────
// GameLoop Tuner — WinMain + DirectX 11 host.
//
// Replaces src/main.py's ctkinter.CTk shell. Responsibilities, in order:
//
//   1. DPI awareness BEFORE any window exists
//   2. DX11 device + swap chain on a resizable window
//   3. ImGui context, theme, font atlas
//   4. The frame loop, including the idle throttle that keeps background CPU
//      at 0%
//   5. Teardown in reverse
//
// UAC
// ---
// The Python build popped a messagebox at runtime and exited when it was not
// elevated. This build requests elevation through the embedded manifest
// (cmake/app.manifest.in), so Windows prompts BEFORE the process image is
// mapped and there is no runtime check to fail.
//
// IDLE THROTTLING
// ---------------
// An uncapped immediate-mode loop burns a core redrawing an unchanged window.
// This loop sleeps in tiers and, crucially, SKIPS the whole frame when nothing
// has invalidated it. Skipping is what makes "0% CPU in the tray" true rather
// than approximately true — sleeping after a full render still costs a
// present per wake-up.
// ─────────────────────────────────────────────────────────────────────────────
#include <windows.h>
#include <commctrl.h>
#include <d3d11.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <tchar.h>

#include <algorithm>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/app.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/page_dashboard.hpp"
#include "glt/ui/pages.hpp"
#include "glt/ui/sidebar.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/variant_picker.hpp"
#include "glt/ui/widgets.hpp"

// imgui_impl_win32.h keeps the WndProc declaration inside `#if 0` in 1.91.x:
// the symbol exists in the .cpp but is not part of the published API. Without
// this forward declaration the window procedure cannot hand messages to ImGui,
// and every widget silently stops receiving mouse and keyboard input.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

// ── Frame budget ─────────────────────────────────────────────────────────────

// Sleep between frames, per state. The tiers are deliberately coarse: finer
// granularity buys nothing visible and costs wake-ups.
constexpr int kSleepActiveMs = 0;     // being interacted with
constexpr int kSleepHoverMs = 33;     // ~30 fps while hovered but idle
constexpr int kSleepVisibleMs = 250;  // ~4 fps while visible but unfocused
constexpr int kSleepHiddenMs = 1000;  // ~1 fps while minimised

// How long a frame stays "active" after the last input, so a click does not
// drop to 4 fps the instant the button is released.
constexpr ULONGLONG kActiveHoldMs = 400;

constexpr int kMinWidth = 800;  // mirrors the Python minsize
constexpr int kMinHeight = 640;
constexpr int kDefaultWidth = 1180;
constexpr int kDefaultHeight = 760;

constexpr wchar_t kWindowClass[] = L"GameLoopTunerHost";
constexpr wchar_t kWindowTitle[] = L"GameLoop Tuner";

struct Host {
    HWND hwnd = nullptr;
    bool running = true;
    bool resized = true;
    bool foreground = true;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    UINT dpi = 96;
    ULONGLONG last_input = 0;  // GetTickCount64 at the last interaction
    bool hovered = false;
    bool was_minimised = false;

    void create_render_target();
    void destroy_render_target();
    bool create_device_d3d(HWND wnd);
    void cleanup_device_d3d();
    void resize_from_client();
};

Host g_host;

// Set once, before the frame loop. The idle policy needs to ask the App whether
// a worker is running or a modal is pending, and Host is a plain struct that
// deliberately does not own the App.
glt::ui::App* g_app = nullptr;

void mark_interaction() {
    g_host.last_input = ::GetTickCount64();
    g_host.hovered = true;
}

// ── Idle policy ──────────────────────────────────────────────────────────────

int frame_sleep_ms() {
    if (g_host.hovered && g_host.foreground) return kSleepActiveMs;

    const ULONGLONG since = ::GetTickCount64() - g_host.last_input;
    if (since < kActiveHoldMs) return kSleepHoverMs;

    if (g_host.foreground && !::IsIconic(g_host.hwnd)) return kSleepVisibleMs;
    return kSleepHiddenMs;
}

// True when it is worth drawing at all. Skipping the frame entirely — not just
// sleeping after it — is what takes the app to 0% CPU and 0% GPU while it sits
// minimised.
bool should_render() {
    if (g_host.resized) return true;

    // A worker is running or a modal is up: the UI is changing regardless of
    // whether the mouse moved.
    if (g_app && (g_app->busy() || g_app->has_pending_prompt())) return true;

    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) return true;
    if (ImGui::IsAnyItemHovered()) return true;
    if (ImGui::IsAnyItemActive()) return true;

    return frame_sleep_ms() == kSleepActiveMs;
}

// ── DX11 ─────────────────────────────────────────────────────────────────────

void Host::create_render_target() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back))) && back != nullptr) {
        device->CreateRenderTargetView(back, nullptr, &rtv);
        back->Release();
    }
}

void Host::destroy_render_target() {
    if (rtv) {
        rtv->Release();
        rtv = nullptr;
    }
}

void Host::resize_from_client() {
    if (!swap_chain) return;

    destroy_render_target();

    RECT cr{};
    ::GetClientRect(hwnd, &cr);
    const auto w = static_cast<UINT>(cr.right);
    const auto h = static_cast<UINT>(cr.bottom);

    if (w == 0 || h == 0) {
        // Minimised: the buffers stay at their current size and are rebuilt on
        // restore. Resizing a swap chain to 0x0 fails.
        return;
    }

    if (FAILED(swap_chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
        // Device removed / reset: drop everything so the next frame rebuilds.
        cleanup_device_d3d();
        return;
    }
    create_render_target();
    resized = true;
}

bool Host::create_device_d3d(HWND wnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // DISCARD rather than FLIP: flip-model needs a flip-model-compatible
    // buffer format, which older Intel/AMD laptop drivers do not always offer.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};

    HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd, &swap_chain, &device,
        &got, &context);

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // WARP keeps the UI usable on a machine whose GPU driver is disabled or
        // mismatched, instead of showing "cannot start".
        hr = ::D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
            static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd, &swap_chain,
            &device, &got, &context);
    }
    if (FAILED(hr)) return false;

    create_render_target();
    return rtv != nullptr;
}

void Host::cleanup_device_d3d() {
    destroy_render_target();
    if (swap_chain) {
        swap_chain->Release();
        swap_chain = nullptr;
    }
    if (context) {
        context->Release();
        context = nullptr;
    }
    if (device) {
        device->Release();
        device = nullptr;
    }
}

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // ImGui gets first refusal on every message so its own input handling
    // (cursor shape, key capture, nav) works without special-casing here.
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;

    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                g_host.resize_from_client();
            }
            mark_interaction();
            return 0;

        case WM_DPICHANGED: {
            // Per-monitor v2. Applying the suggested rect avoids a visible jump;
            // the font atlas is rebuilt at the new scale further down the loop.
            g_host.dpi = HIWORD(wp);
            if (auto* rect = reinterpret_cast<RECT*>(lp)) {
                ::SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                               rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            g_host.resized = true;
            mark_interaction();
            return 0;
        }

        case WM_GETMINMAXINFO:
            if (auto* mmi = reinterpret_cast<MINMAXINFO*>(lp)) {
                mmi->ptMinTrackSize.x = kMinWidth;
                mmi->ptMinTrackSize.y = kMinHeight;
            }
            return 0;

        case WM_SETCURSOR:
            // Hand the cursor back to ImGui while it wants it (I-beam over text,
            // hand over buttons) instead of letting the class default win.
            if (LOWORD(lp) == HTCLIENT && ImGui::GetIO().WantCaptureMouse) return 0;
            break;

        case WM_MOUSEMOVE:
            mark_interaction();
            break;

        case WM_MOUSELEAVE:
            g_host.hovered = false;
            break;

        case WM_ACTIVATEAPP:
            g_host.foreground = (wp != FALSE);
            if (g_host.foreground) mark_interaction();
            return 0;

        case WM_DESTROY:
            g_host.running = false;
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Modal ────────────────────────────────────────────────────────────────────

// The confirmation dialog. answer_prompt() invokes the stored callback, which
// App guarantees runs on the render thread — so it is safe to call here.
void draw_modal(glt::ui::App& app) {
    if (!app.has_pending_prompt()) return;

    ImGui::OpenPopup("##confirm");
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::BeginPopupModal("##confirm", &open,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(glt::ui::fonts::current().heading);
        ImGui::TextUnformatted("Confirm");
        ImGui::PopFont();

        ImGui::Separator();
        ImGui::TextWrapped("%s", app.pending_prompt().c_str());
        ImGui::Dummy(ImVec2(0, 12));

        const float avail = ImGui::GetContentRegionAvail().x;
        const float half = (avail - 8.0f) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        if (ImGui::Button("Yes", ImVec2(half, 32.0f))) {
            ImGui::CloseCurrentPopup();
            app.answer_prompt(true);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 8.0f);
        if (ImGui::Button("No", ImVec2(half, 32.0f))) {
            ImGui::CloseCurrentPopup();
            app.answer_prompt(false);
        }

        ImGui::EndPopup();
    }

    // Esc, or clicking outside, closes the popup without answering.
    if (!open) app.answer_prompt(false);
}

// ── Frame ────────────────────────────────────────────────────────────────────

void render_frame(glt::ui::App& app) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Background work first, so THIS frame displays its results.
    app.drain();
    app.pump_drift();

    // ── The one and only ImGui window ─────────────────────────────────────
    //
    // The app MUST open a window. If it does not, ImGui files every widget into
    // its implicit "Debug" window, which draws a title bar, a border, rounded
    // corners and a collapsing header of its own AROUND the whole application —
    // the UI then reads as a window inside a window instead of as the app.
    //
    // This one is pinned to the entire client area and stripped of every piece
    // of chrome: the OS frame already supplies the title bar and the resize
    // border, so a second set is pure duplication. Rounding and the border are
    // zeroed for the same reason — a rounded rectangle inset inside the OS
    // frame is the other half of the "app in an app" look.
    //
    // The theme's WindowPadding (kPagePadX / kPagePadY) is deliberately left
    // in place: it is what puts the pages inside their margins. Every page
    // pushes padding to zero for its own children via theme::push_transparent,
    // so they draw against the app chrome rather than nesting a second inset.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("##glt_host", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    draw_sidebar(app);

    ImGui::SameLine(0, 0);
    switch (app.page()) {
        case glt::ui::Page::Dashboard:   glt::ui::draw_dashboard(app);   break;
        case glt::ui::Page::Keymaps:     glt::ui::draw_keymaps(app);     break;
        case glt::ui::Page::Performance: glt::ui::draw_performance(app); break;
        case glt::ui::Page::VmPriority:  glt::ui::draw_vm_priority(app); break;
        case glt::ui::Page::Network:     glt::ui::draw_network(app);     break;
        case glt::ui::Page::Backups:     glt::ui::draw_backups(app);     break;
        case glt::ui::Page::Logs:        glt::ui::draw_logs(app);        break;
        case glt::ui::Page::About:       glt::ui::draw_about(app);       break;
    }

    // Opened while the host window is the current one, so the modal dims only
    // this window rather than something with no parent at all.
    //
    // The first-launch variant picker is drawn first: it is modal, so nothing
    // else can be interacted with until the user answers it exactly once.
    glt::ui::draw_variant_picker(app);
    draw_modal(app);

    // Floating toast notification at bottom right corner
    {
        const auto& toast = app.current_toast();
        if (toast.active) {
            if (std::chrono::steady_clock::now() > toast.expires_at) {
                app.dismiss_toast();
            } else {
                const ImGuiViewport* vp = ImGui::GetMainViewport();
                const float pad = 24.0f;
                const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - pad,
                                 vp->WorkPos.y + vp->WorkSize.y - pad);

                ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
                ImGui::SetNextWindowBgAlpha(0.95f);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));

                const ImVec4 bg_col = (toast.level == glt::log::Level::Error)
                                          ? ImVec4(0.25f, 0.08f, 0.08f, 1.0f)
                                          : (toast.level == glt::log::Level::Warning)
                                                ? ImVec4(0.25f, 0.18f, 0.05f, 1.0f)
                                                : ImVec4(0.08f, 0.22f, 0.14f, 1.0f);
                const ImVec4 border_col = (toast.level == glt::log::Level::Error)
                                              ? ImVec4(0.85f, 0.25f, 0.25f, 0.8f)
                                              : (toast.level == glt::log::Level::Warning)
                                                    ? ImVec4(0.85f, 0.60f, 0.15f, 0.8f)
                                                    : ImVec4(0.20f, 0.75f, 0.45f, 0.8f);

                ImGui::PushStyleColor(ImGuiCol_WindowBg, bg_col);
                ImGui::PushStyleColor(ImGuiCol_Border, border_col);

                if (ImGui::Begin("##toast_overlay", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
                    const char* icon = (toast.level == glt::log::Level::Error) ? "\xE2\x9C\x97" :
                                       (toast.level == glt::log::Level::Warning) ? "\xE2\x9A\xA0" :
                                       "\xE2\x9C\x93";
                    ImGui::TextColored(border_col, "%s", icon);
                    ImGui::SameLine(0, 8.0f);
                    ImGui::TextUnformatted(toast.message.c_str());
                    ImGui::SameLine(0, 14.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    if (ImGui::SmallButton("\xC3\x97")) {
                        app.dismiss_toast();
                    }
                    ImGui::PopStyleColor();
                    ImGui::End();
                }

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
}

} // namespace

// ── Single-instance guard ────────────────────────────────────────────────────

// Named mutex guarding "the tuner is already running".
//
// WHY THIS MATTERS
// ----------------
// Every instance of this app:
//   * starts a drift-monitor thread polling Configs.db every 30 s
//   * opens its own SQLite connection to the same Configs.db
//   * holds the rebuilt output binary open on disk, so an incremental build
//     fails with a bare LNK1104 that gives no hint what is actually wrong
//
// A double-click from a user who thinks the first click did nothing is all it
// takes. With two instances the drift threads interleave, and a rebuild cannot
// replace the binary it is running from.
//
// `Global\` (rather than `Local\`) so the guard also covers a second elevation
// from a different session, and because this process is elevated anyway.
constexpr wchar_t kSingleInstanceMutex[] = L"Global\\GameLoopTunerSingleInstance";

// Returns true when the caller should give up — i.e. when another instance
// already held the mutex AND was located. The mutex is released before return
// only in that case; on success the caller holds it for the process lifetime.
[[nodiscard]] bool another_instance_is_running(HINSTANCE inst) {
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    const bool existed = ::GetLastError() == ERROR_ALREADY_EXISTS;

    if (!existed) {
        // This process now owns the mutex. Leaving the handle open for the
        // lifetime of the process is what keeps the name reserved, so it is
        // intentionally never closed.
        return false;
    }

    ::CloseHandle(mutex);

    const HWND peer = ::FindWindowW(kWindowClass, nullptr);
    if (peer) {
        // A minimise to the tray is the common case for "I don't see it".
        if (::IsIconic(peer)) ::ShowWindow(peer, SW_RESTORE);
        ::ShowWindow(peer, SW_SHOW);

        // SetForegroundWindow is normally blocked by the foreground lock, but
        // both processes are elevated with the same token here, so it works.
        // Where it is blocked, FlashWindow gives the taskbar a visible cue.
        if (!::SetForegroundWindow(peer)) {
            FLASHWINFO fw{};
            fw.cbSize = sizeof(fw);
            fw.hwnd = peer;
            fw.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
            fw.uCount = 1;
            fw.dwTimeout = 0;
            ::FlashWindowEx(&fw);
        }
    } else {
        // The mutex exists but no window: an instance is mid-startup or a
        // crashed one leaked the name. Report it rather than silently
        // starting a second process that will fight over Configs.db.
        ::MessageBoxW(nullptr,
                      L"GameLoop Tuner is already running.\n\n"
                      L"If you cannot see it, check the task manager for a "
                      L"stale GameLoopTuner.exe.",
                      kWindowTitle, MB_ICONINFORMATION | MB_OK);
    }

    return true;
}

// ── Entry point ──────────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // 1. DPI awareness before any window exists. The manifest also declares
    //    PerMonitorV2; this explicit call is what makes it work when the
    //    manifest is stripped from the build.
    ImGui_ImplWin32_EnableDpiAwareness();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&icc);

    // 2. Single instance, BEFORE the App is constructed — startup() launches
    //    the drift thread, so a second instance would double the polling load.
    if (another_instance_is_running(inst)) return 0;

    glt::ui::App app;
    g_app = &app;
    app.startup();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    // IDI_ICON1 is embedded from mythic.ico by cmake/glt_resources.rc.
    wc.hIcon = ::LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    if (!::RegisterClassExW(&wc)) {
        ::MessageBoxW(nullptr, L"Could not register the window class.", kWindowTitle,
                      MB_ICONERROR);
        return 1;
    }

    RECT wr{0, 0, kDefaultWidth, kDefaultHeight};
    ::AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    g_host.hwnd = ::CreateWindowW(kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
                                  wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    if (!g_host.hwnd) {
        ::MessageBoxW(nullptr, L"Could not create the main window.", kWindowTitle,
                      MB_ICONERROR);
        app.shutdown();
        return 1;
    }

    // Windows 11 / 10 Immersive Dark Mode for window frame and titlebar
    BOOL use_dark_mode = TRUE;
    ::DwmSetWindowAttribute(g_host.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &use_dark_mode, sizeof(use_dark_mode));

    // Windows 11 Rounded corners preference (DWMWCP_ROUND = 2)
    DWORD corner_pref = 2;
    ::DwmSetWindowAttribute(g_host.hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner_pref, sizeof(corner_pref));

    g_host.last_input = ::GetTickCount64();

    if (!g_host.create_device_d3d(g_host.hwnd)) {
        ::MessageBoxW(nullptr,
                      L"Could not create a DirectX 11 device.\n\nThe tuner needs a "
                      L"D3D11-capable display driver. Updating the GPU driver usually "
                      L"fixes this.",
                      kWindowTitle, MB_ICONERROR);
        g_host.cleanup_device_d3d();
        ::DestroyWindow(g_host.hwnd);
        app.shutdown();
        return 1;
    }

    // Per-monitor DPI can differ from the system one on a multi-monitor desk.
    g_host.dpi = ::GetDpiForWindow(g_host.hwnd);
    if (g_host.dpi == 0) g_host.dpi = 96;

    ::ShowWindow(g_host.hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_host.hwnd);
    ::SetForegroundWindow(g_host.hwnd);

    // 2. ImGui.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no gameloop_tuner.ini next to the exe
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();
    glt::ui::theme::apply();
    ImGui::GetStyle().ScaleAllSizes(static_cast<float>(g_host.dpi) / 96.0f);

    ImGui_ImplWin32_Init(g_host.hwnd);
    ImGui_ImplDX11_Init(g_host.device, g_host.context);

    // 3. Frame loop.
    bool quit = false;
    while (!quit && g_host.running) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;

        if (::IsIconic(g_host.hwnd)) {
            g_host.was_minimised = true;
            ::Sleep(kSleepHiddenMs);
            continue;
        }
        if (g_host.was_minimised) {
            // The swap chain was never resized to 0x0, so it is still valid;
            // just re-read the client area and rebuild the render target.
            g_host.was_minimised = false;
            g_host.resize_from_client();
            mark_interaction();
        }

        if (!should_render()) {
            ::Sleep(static_cast<DWORD>(frame_sleep_ms()));
            continue;
        }

        // Font atlas follows the window's DPI, so dragging between a 1080p and
        // a 4K display rebuilds it at the right size.
        const int scale = static_cast<int>(static_cast<float>(g_host.dpi) * 100.0f / 96.0f);
        if (glt::ui::fonts::rebuild(scale) != glt::ui::fonts::Rebuild::Unchanged) {
            // The DX11 backend builds its font texture lazily inside NewFrame().
            // These are the public way to force a rebuild after the atlas has
            // been replaced (a DPI bucket change). The per-texture functions are
            // internal to the .cpp and are not exported.
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
            for (const auto& note : glt::ui::fonts::diagnostics()) {
                app.log(note, glt::log::Level::Warning);
            }
        }

        render_frame(app);

        // Hover is window-level: WM_MOUSELEAVE is what clears it. Re-derived
        // here because WM_MOUSEMOVE may have arrived between frames.
        g_host.hovered = ImGui::GetIO().WantCaptureMouse;

        if (!g_host.device || !g_host.swap_chain) break; // device lost mid-frame

        constexpr float clear[4] = {0.071f, 0.071f, 0.082f, 1.0f}; // #121215
        g_host.context->OMSetRenderTargets(1, &g_host.rtv, nullptr);
        g_host.context->ClearRenderTargetView(g_host.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_host.swap_chain->Present(1, 0); // vsync
        g_host.resized = false;

        const int sleep = frame_sleep_ms();
        if (sleep > 0) ::Sleep(static_cast<DWORD>(sleep));
    }

    // 4. Teardown, reverse of setup.
    app.shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    g_host.cleanup_device_d3d();
    ::DestroyWindow(g_host.hwnd);
    ::UnregisterClassW(kWindowClass, inst);
    return 0;
}