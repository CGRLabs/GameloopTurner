// ─────────────────────────────────────────────────────────────────────────────
// native/installer/ui.cpp — the installer window.
//
// Everything is painted by hand: there are no child controls, so there is no
// comctl32 theming, no DPI-stretching artefacts on the buttons, and no visual
// mismatch with the app itself. Hit-testing is a handful of rectangles.
//
// The layout is computed once per frame by layout_for() and consumed by both
// paint() and the click handler, so the two can never disagree about where a
// button is.
// ─────────────────────────────────────────────────────────────────────────────
#include "setup.hpp"

#include <objbase.h>
#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps windows.h from dragging in wingdi.h, which is the
// wrong trade for a file that is entirely GDI drawing: HDC, CreateFontW,
// RoundRect and DT_WORDS all live there. windowsx.h is needed for the
// GET_X_LPARAM / GET_Y_LPARAM sign-extension helpers — without them a click
// left of the client origin is read as a huge positive coordinate.
#include <wingdi.h>
#include <windowsx.h>

// NOMINMAX is on (see CMakeLists), so std::min/std::max are the real ones.
#include <algorithm>
#include <string>

namespace setup {
namespace ui {
namespace {

constexpr UINT WM_APP_DONE = WM_APP + 1;

constexpr int kBaseW      = 540;   // client size at 96 DPI
constexpr int kBaseH      = 400;
constexpr int kPad        = 32;
constexpr int kBtnW       = 116;
constexpr int kBtnH       = 38;
constexpr int kRowH       = 26;

enum class Page { Welcome, Confirm, Progress, Done, Error };
enum class Hit { None, Desktop, Primary, Secondary };

struct Fonts {
    HFONT title = nullptr;
    HFONT body = nullptr;
    // NOT "small": objbase.h drags in rpcndr.h, which does `#define small char`.
    // A member named `small` silently becomes `HFONT char` and cascades into
    // dozens of nonsense errors. Same trap for near/far/hyper/IN/OUT.
    HFONT caption = nullptr;
    HFONT button = nullptr;
};

struct Layout {
    RECT title{};
    RECT subtitle{};
    RECT divider{};
    RECT where_label{};
    RECT where_value{};
    RECT checkbox{};
    RECT check_box{};
    RECT message{};
    RECT badge{};
    RECT bar{};
    RECT primary{};
    RECT secondary{};
    Hit primary_hit = Hit::None;
    Hit secondary_hit = Hit::None;
};

struct App {
    Options opt;
    Page page = Page::Welcome;
    bool desktop = true;
    bool silent = false;
    Hit hover = Hit::None;
    bool pressed = false;

    HWND hwnd = nullptr;
    HICON icon = nullptr;
    HFONT icon_font = nullptr;
    Fonts fonts{};
    UINT dpi = 96;

    setup::Result result;
    std::wstring status;
};

// Single global: the window is modal and there is only ever one of it.
App* g_app = nullptr;

int scale(const App& a, int v) {
    return MulDiv(v, static_cast<int>(a.dpi), 96);
}

// ── GDI helpers ───────────────────────────────────────────────────────────────

void fill_round(HDC dc, const RECT& r, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, brush);
    HGDIOBJ op = SelectObject(dc, pen);
    // RoundRect strokes the right/bottom edges inclusive, so grow by one.
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(brush);
}

void stroke_round(HDC dc, const RECT& r, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

void text(HDC dc, const RECT& r, const std::wstring& s, HFONT font, COLORREF color,
          UINT format) {
    const HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = r;
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &rr,
              format | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

RECT make_rect(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }

// ── Fonts ─────────────────────────────────────────────────────────────────────

HFONT make_font(int px_height, int weight) {
    return CreateFontW(-px_height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void destroy_fonts(App& a) {
    for (HFONT* f : {&a.fonts.title, &a.fonts.body, &a.fonts.caption, &a.fonts.button,
                     &a.icon_font}) {
        if (*f) {
            DeleteObject(*f);
            *f = nullptr;
        }
    }
}

void build_fonts(App& a) {
    const int px = static_cast<int>(a.dpi / 72.0);
    a.fonts.title  = make_font(scale(a, px * 26), FW_SEMIBOLD);
    a.fonts.body   = make_font(scale(a, px * 14), FW_NORMAL);
    a.fonts.caption  = make_font(scale(a, px * 12), FW_NORMAL);
    a.fonts.button = make_font(scale(a, px * 14), FW_SEMIBOLD);
    a.icon_font    = make_font(scale(a, px * 44), FW_NORMAL);
}

// ── Layout ────────────────────────────────────────────────────────────────────

// The copy that explains what the window is about to do, per page.
const wchar_t* headline(App& a) {
    switch (a.page) {
        case Page::Welcome:  return kProductName;
        case Page::Confirm:  return L"Uninstall GameLoop Tuner?";
        case Page::Progress: return a.status.c_str();
        case Page::Done:
            return a.opt.mode == Mode::Install ? L"Installation complete"
                                               : L"Uninstalled";
        case Page::Error:    return L"Could not continue";
    }
    return kProductName;
}

Layout layout_for(App& a, const RECT& client) {
    Layout L{};
    const int pad = scale(a, kPad);
    const int x = client.left + pad;
    const int w = client.right - client.left - pad * 2;

    L.title = make_rect(x, client.top + pad, w, scale(a, 40));
    L.subtitle = make_rect(x, client.top + pad + scale(a, 44), w, scale(a, 20));

    int y = client.top + pad + scale(a, 78);
    if (a.page == Page::Welcome || a.page == Page::Confirm) {
        L.divider = make_rect(x, y, w, 1);
        y += scale(a, 20);
        L.where_label = make_rect(x, y, w, scale(a, 18));
        L.where_value = make_rect(x, y + scale(a, 20), w, scale(a, 22));
        y += scale(a, 58);
    }
    if (a.page == Page::Welcome) {
        // `checkbox` is the whole clickable row; `check_box` is only the square.
        // Hit-testing the row means the label is clickable too.
        L.checkbox = make_rect(x, y - scale(a, 3), w, scale(a, kRowH + 6));
        L.check_box = make_rect(x, y, scale(a, 20), scale(a, 20));
    }
    if (a.page == Page::Progress) {
        L.message = make_rect(x, client.bottom - pad - scale(a, 70), w, scale(a, 34));
        L.bar = make_rect(x, client.bottom - pad - scale(a, 34), w, scale(a, 6));
    }
    if (a.page == Page::Done || a.page == Page::Error) {
        // Reserve a band for the tick so it cannot land on top of the text.
        L.badge = make_rect(client.left, client.top + pad + scale(a, 78), w,
                            scale(a, 46));
        L.message = make_rect(x, L.badge.bottom + scale(a, 18), w, scale(a, 90));
    }

    const int by = client.bottom - pad - scale(a, kBtnH);
    const int bw = scale(a, kBtnW);
    L.secondary = make_rect(client.right - pad - bw * 2 - scale(a, 10), by, bw,
                            scale(a, kBtnH));
    L.primary = make_rect(client.right - pad - bw, by, bw, scale(a, kBtnH));

    switch (a.page) {
        case Page::Welcome:
            L.primary_hit = Hit::Primary;    // Install
            L.secondary_hit = Hit::Secondary; // Cancel
            break;
        case Page::Confirm:
            L.primary_hit = Hit::Primary;    // Uninstall
            L.secondary_hit = Hit::Secondary; // Cancel
            break;
        case Page::Done:
        case Page::Error:
            L.primary_hit = Hit::Primary;    // Close
            break;
        default:
            break; // Progress has no buttons
    }
    return L;
}

// ── Painting ──────────────────────────────────────────────────────────────────

void paint_button(HDC dc, App& a, const RECT& r, const wchar_t* label, bool primary,
                  bool hover, bool enabled = true) {
    const int radius = scale(a, 6);
    COLORREF fill = kBgCard;
    COLORREF edge = kBorder;
    COLORREF label_color = kTextPrimary;

    if (!enabled) {
        label_color = kTextDim;
    } else if (primary) {
        fill = hover ? kAccentHover : kAccent;
        edge = fill;
        label_color = kOnAccent;
    } else if (hover) {
        fill = kBgHover;
    }

    fill_round(dc, r, radius, fill);
    if (!primary) stroke_round(dc, r, radius, edge);

    RECT t = r;
    t.bottom += scale(a, 2); // nudge to optically centre short caps
    text(dc, t, label, a.fonts.button, label_color,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paint(HDC dc, App& a, const RECT& client) {
    const Layout L = layout_for(a, client);

    RECT all = client;
    fill_round(dc, all, 0, kBgPrimary);

    text(dc, L.title, headline(a), a.fonts.title,
         a.page == Page::Error ? kError : kTextPrimary, DT_LEFT | DT_SINGLELINE);

    if (a.page == Page::Welcome || a.page == Page::Confirm) {
        const std::wstring sub =
            std::wstring(L"Version ") + widen(version()) + L"  \x00B7  " + kPublisher;
        text(dc, L.subtitle, sub, a.fonts.caption, kTextMuted, DT_LEFT | DT_SINGLELINE);
    }

    if (L.divider.bottom > L.divider.top) {
        const HBRUSH b = CreateSolidBrush(kBorder);
        RECT d = L.divider;
        FillRect(dc, &d, b);
        DeleteObject(b);
    }

    if (a.page == Page::Welcome || a.page == Page::Confirm) {
        text(dc, L.where_label,
             a.page == Page::Confirm ? L"This removes the app, its shortcuts and its "
                                       L"entry in Apps & Features:"
                                   : L"GameLoop Tuner will be installed to:",
             a.fonts.caption, kTextDim, DT_LEFT | DT_SINGLELINE);
        text(dc, L.where_value, install_dir(), a.fonts.body, kTextPrimary,
             DT_LEFT | DT_SINGLELINE);
    }

    if (a.page == Page::Welcome) {
        const bool hover = a.hover == Hit::Desktop;
        if (hover) fill_round(dc, L.check_box, scale(a, 5), kBgHover);

        if (a.desktop) {
            fill_round(dc, L.check_box, scale(a, 5), kAccent);
            // Tick: two strokes, hand-drawn rather than a glyph so it stays
            // crisp at every DPI.
            HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(a, 2)), kOnAccent);
            HGDIOBJ op = SelectObject(dc, pen);
            const int cx = (L.check_box.left + L.check_box.right) / 2;
            const int cy = (L.check_box.top + L.check_box.bottom) / 2;
            MoveToEx(dc, cx - scale(a, 5), cy, nullptr);
            LineTo(dc, cx - scale(a, 1), cy + scale(a, 4));
            LineTo(dc, cx + scale(a, 6), cy - scale(a, 5));
            SelectObject(dc, op);
            DeleteObject(pen);
        } else {
            stroke_round(dc, L.check_box, scale(a, 5), kBorder);
        }

        RECT label = make_rect(L.check_box.right + scale(a, 10), L.check_box.top,
                               (L.checkbox.right - L.check_box.right) - scale(a, 10),
                               L.check_box.bottom - L.check_box.top);
        text(dc, label, L"Also create a desktop shortcut", a.fonts.body,
             kTextPrimary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (a.page == Page::Progress) {
        text(dc, L.message, L"Doing the file work, this only takes a moment.",
             a.fonts.body, kTextMuted, DT_LEFT | DT_SINGLELINE);

        fill_round(dc, L.bar, scale(a, 3), kBgActive);
        // A short accent block that sweeps across the track: honest about not
        // knowing the total work up front.
        const int track_w = L.bar.right - L.bar.left;
        const int block = std::max(scale(a, 40), track_w / 3);
        const UINT phase = GetTickCount() / 8 % (static_cast<UINT>(track_w + block));
        RECT fill{L.bar.left + static_cast<int>(phase) - block, L.bar.top,
                  L.bar.left + static_cast<int>(phase), L.bar.bottom};
        const RECT clip = L.bar;
        IntersectRect(&fill, &clip, &fill);
        if (fill.right > fill.left) fill_round(dc, fill, scale(a, 3), kAccent);
    }

    if (a.page == Page::Done || a.page == Page::Error) {
        if (a.page == Page::Done) {
            const int d = std::min(L.badge.bottom - L.badge.top,
                                   L.badge.right - L.badge.left);
            const int cx = (L.badge.left + L.badge.right) / 2;
            const RECT dot = make_rect(cx - d / 2, L.badge.top, d, d);
            fill_round(dc, dot, d / 2, kOk);
            text(dc, dot, L"\x2713", a.icon_font, RGB(0x06, 0x1A, 0x12),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        text(dc, L.message, a.result.message, a.fonts.body,
             a.page == Page::Error ? kError : kTextMuted,
             DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    if (a.page == Page::Done || a.page == Page::Error) {
        paint_button(dc, a, L.primary, L"Close", true, a.hover == Hit::Primary);
    } else if (a.page != Page::Progress) {
        const bool installing = a.page == Page::Welcome;
        paint_button(dc, a, L.secondary, L"Cancel", false,
                     a.hover == Hit::Secondary);
        paint_button(dc, a, L.primary, installing ? L"Install" : L"Uninstall", true,
                     a.hover == Hit::Primary);
    }
}

// ── Work ──────────────────────────────────────────────────────────────────────

Options g_pending{};
setup::Result g_result{};

DWORD WINAPI worker(LPVOID) {
    // COM apartments are per-thread, and wWinMain only initialised the main
    // thread. Without this the IShellLink writes in setup.cpp silently fail on
    // this thread and the Start Menu entry is simply missing.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                  COINIT_DISABLE_OLE1DDE);

    g_result = g_pending.mode == Mode::Install ? run_install(g_pending)
                                               : run_uninstall(g_pending);

    if (SUCCEEDED(com)) CoUninitialize();
    // The silent path has no window; skip the notification there.
    if (g_app && g_app->hwnd) PostMessageW(g_app->hwnd, WM_APP_DONE, 0, 0);
    return 0;
}

void start_work(App& a) {
    g_pending = a.opt;
    g_pending.desktop = a.desktop;
    SetWindowTextW(a.hwnd, a.opt.mode == Mode::Install ? L"Installing GameLoop Tuner"
                                                      : L"Uninstalling GameLoop Tuner");
    a.page = Page::Progress;
    a.status = L"Working";
    InvalidateRect(a.hwnd, nullptr, FALSE);
    if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) {
        CloseHandle(t);
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

Hit hit_test(App& a, const Layout& L, POINT p) {
    const auto inside = [&](const RECT& r) {
        return PtInRect(&r, p) != FALSE;
    };
    if (a.page == Page::Done || a.page == Page::Error) {
        return inside(L.primary) ? Hit::Primary : Hit::None;
    }
    if (a.page == Page::Progress) return Hit::None;
    if (a.page == Page::Welcome && inside(L.checkbox)) return Hit::Desktop;
    if (inside(L.primary)) return L.primary_hit;
    if (inside(L.secondary)) return L.secondary_hit;
    return Hit::None;
}

void close_app(App& a, int code) {
    DestroyWindow(a.hwnd);
    PostQuitMessage(code);
}

} // namespace

int run(const Options& opt) {
    App a;
    a.opt = opt;
    a.desktop = opt.desktop;
    g_app = &a;

    if (opt.silent) {
        // No window at all: the exit code is the whole interface.
        g_pending = opt;
        worker(nullptr);
        return g_result.ok ? 0 : 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        App& app = *g_app;
        if (m == WM_APP_DONE) {
            app.result = g_result;
            app.page = app.result.ok ? Page::Done : Page::Error;
            if (!app.result.ok) {
                SetWindowTextW(app.hwnd, L"GameLoop Tuner Setup");
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        switch (m) {
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(h, &ps);
                RECT rc{};
                GetClientRect(h, &rc);
                paint(dc, app, rc);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;  // paint() covers the whole client area
            case WM_MOUSEMOVE: {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                RECT rc{};
                GetClientRect(h, &rc);
                const Layout L = layout_for(app, rc);
                const Hit hit = hit_test(app, L, p);
                if (hit != app.hover) {
                    app.hover = hit;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                app.hover = Hit::None;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            case WM_LBUTTONUP: {
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                RECT rc{};
                GetClientRect(h, &rc);
                const Layout L = layout_for(app, rc);
                const Hit hit = hit_test(app, L, p);
                if (hit == Hit::None) return 0;

                if (hit == Hit::Desktop) {
                    app.desktop = !app.desktop;
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (hit == Hit::Secondary) {
                    close_app(app, 0);
                    return 0;
                }
                // Primary.
                if (app.page == Page::Done || app.page == Page::Error) {
                    close_app(app, app.result.ok ? 0 : 1);
                    return 0;
                }
                if (app.page == Page::Confirm || app.page == Page::Welcome) {
                    start_work(app);
                    return 0;
                }
                return 0;
            }
            case WM_CLOSE:
                // Never close mid-install: it would leave a half-copied app
                // with no registry entry and no way to uninstall it.
                if (app.page == Page::Progress) return 0;
                close_app(app, 0);
                return 0;
            case WM_DPICHANGED: {
                app.dpi = HIWORD(w);
                const RECT* want = reinterpret_cast<const RECT*>(l);
                SetWindowPos(h, nullptr, want->left, want->top, want->right - want->left,
                             want->bottom - want->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                destroy_fonts(app);
                build_fonts(app);
                InvalidateRect(h, nullptr, TRUE);
                return 0;
            }
            case WM_DESTROY:
                destroy_fonts(app);
                if (app.icon) DestroyIcon(app.icon);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"GLTSetupWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 1;

    // Size the window against the system DPI *before* creating it. Under
    // PerMonitorV2 the first WM_DPICHANGED does not arrive until the window is
    // shown, so building fonts from GetDpiForWindow() straight after
    // CreateWindow would size them for the wrong monitor on a mixed-DPI setup.
    a.dpi = GetDpiForSystem();

    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT want = make_rect(0, 0, scale(a, kBaseW), scale(a, kBaseH));
    AdjustWindowRectEx(&want, kStyle, FALSE, 0);
    const int w = want.right - want.left;
    const int h = want.bottom - want.top;

    a.hwnd = CreateWindowExW(0, L"GLTSetupWindow", L"GameLoop Tuner Setup", kStyle,
                             CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
                             wc.hInstance, nullptr);
    if (!a.hwnd) return 1;

    // Load IDI_ICON1 out of our own resource rather than an .ico on disk: the
    // release zip ships only the two executables, so a file next to the binary
    // would simply not be there.
    if (HICON icon = static_cast<HICON>(
            LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                       LR_DEFAULTSIZE))) {
        a.icon = icon;
        SendMessageW(a.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(a.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }

    build_fonts(a);  // uses the a.dpi set above, before the window existed

    a.page = a.opt.mode == Mode::Uninstall ? Page::Confirm : Page::Welcome;

    ShowWindow(a.hwnd, SW_SHOW);
    UpdateWindow(a.hwnd);

    MSG msg{};
    int code = 0;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    code = static_cast<int>(msg.wParam);
    g_app = nullptr;
    return code;
}

} // namespace ui
} // namespace setup