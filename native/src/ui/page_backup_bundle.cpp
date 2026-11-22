// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_backup_bundle.cpp — the Backup Bundle (.gltbak) page.
//
// A bundle is a single self-describing file that snapshots everything the
// tuner touches: Configs.db keymap/perf rows, smk.conf, registry branches,
// and a source-system fingerprint. It carries an SHA-256 digest so a corrupt
// or tampered bundle is rejected before it can overwrite a live install.
//
// The page supports three flows:
//   * Create Bundle — capture the live system, choose where to save.
//   * Open Bundle   — pick a .gltbak file, verify its digest, preview it.
//   * Apply Bundle  — write every section back to the live install (with a
//                     dry-run first), showing a per-field diff before commit.
//
// Double-clicking a .gltbak in Explorer launches this app and opens the file
// directly in preview mode (shell association, registered by the installer).
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/bundle.hpp"
#include "glt/core/i18n.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"
#include "glt/core/variant.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

#include <imgui.h>
#include <shobjidl.h>
#include <shlobj_core.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace glt::ui {
namespace {

using glt::i18n::tr;

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

// ── Animated text reveal ─────────────────────────────────────────────────────
// Fades text in over `duration` seconds using a simple ImGui style alpha push.
// Returns the alpha factor (0..1) so callers can also tint icons.
float alpha_reveal(std::string_view anim_id, float duration_seconds) {
    static thread_local std::string last_id;
    static thread_local std::chrono::steady_clock::time_point last_t;

    auto now = std::chrono::steady_clock::now();
    if (anim_id != last_id) {
        last_id = std::string(anim_id);
        last_t = now;
    }
    float elapsed = std::chrono::duration<float>(now - last_t).count();
    float t = elapsed / duration_seconds;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    // Smoothstep: ease in-out
    float smooth = t * t * (3.0f - 2.0f * t);
    return smooth;
}

// ── File picker dialogs (Windows Common Item Dialog) ─────────────────────────
std::optional<std::wstring> pick_save_file(HWND owner, std::wstring_view default_name) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IFileSaveDialog* pfd = nullptr;
    if (CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&pfd)) != S_OK) {
        CoUninitialize();
        return std::nullopt;
    }

    COMDLG_FILTERSPEC filter[] = {
        {L"GameloopTuner Backup (*.gltbak)", L"*.gltbak"},
        {L"All Files (*.*)", L"*.*"},
    };
    pfd->SetFileTypes(2, filter);
    pfd->SetDefaultExtension(L"gltbak");
    if (!default_name.empty())
        pfd->SetFileName(default_name.data());

    if (pfd->Show(owner) != S_OK) {
        pfd->Release();
        CoUninitialize();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    std::optional<std::wstring> result;
    if (pfd->GetResult(&item) == S_OK) {
        PWSTR path = nullptr;
        if (item->GetDisplayName(SIGDN_FILESYSPATH, &path) == S_OK) {
            result = std::wstring(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    pfd->Release();
    CoUninitialize();
    return result;
}

std::optional<std::wstring> pick_open_file(HWND owner) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IFileOpenDialog* pfd = nullptr;
    if (CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&pfd)) != S_OK) {
        CoUninitialize();
        return std::nullopt;
    }

    COMDLG_FILTERSPEC filter[] = {
        {L"GameloopTuner Backup (*.gltbak)", L"*.gltbak"},
        {L"All Files (*.*)", L"*.*"},
    };
    pfd->SetFileTypes(2, filter);
    pfd->SetDefaultExtension(L"gltbak");

    if (pfd->Show(owner) != S_OK) {
        pfd->Release();
        CoUninitialize();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    std::optional<std::wstring> result;
    if (pfd->GetResult(&item) == S_OK) {
        PWSTR path = nullptr;
        if (item->GetDisplayName(SIGDN_FILESYSPATH, &path) == S_OK) {
            result = std::wstring(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    pfd->Release();
    CoUninitialize();
    return result;
}

// Renders one diff row: key, backup value, live value, match badge.
void draw_diff_row(const bundle::FieldDiff& d) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = 30.0f;

    ImGui::PushID(d.key.c_str());
    ImGui::InvisibleButton("##row", ImVec2(avail.x, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h),
                          theme::u32(col(theme::kBgHover)), 4.0f);
    }

    // Section chip.
    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 10.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushFont(fonts::current().mono_small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
    ImGui::TextUnformatted(d.section.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Key (truncated if too long).
    std::string key_label = d.key;
    if (key_label.size() > 60) key_label = key_label.substr(0, 57) + "...";
    ImGui::SameLine(120.0f);
    ImGui::PushFont(fonts::current().mono_small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(key_label.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Match badge.
    const float badge_x = avail.x - 150.0f;
    ImGui::SameLine(badge_x);
    if (d.matches) {
        widgets::status_pill("MATCH", col(theme::kOk), col(theme::kOkBg));
    } else if (d.live_value == "<missing on this system>" ||
               d.live_value == "<not found>" || d.live_value == "<unreadable>") {
        widgets::status_pill("MISSING", col(theme::kWarn), col(theme::kWarnBg));
    } else {
        widgets::status_pill("CHANGES", col(theme::kError), col(theme::kErrorBg));
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
}

} // namespace

void draw_backup_bundle(App& app) {
    // Per-frame animation alpha (fade-in over 0.4s).
    float alpha = alpha_reveal("bundle_page", 0.4f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    theme::push_transparent();
    ImGui::BeginChild("##bundle_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##bundle_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    using glt::bundle::Bundle;

    const bool loaded = app.bundle_loaded();

    // ── Header ──────────────────────────────────────────────────────────────
    widgets::page_header(tr("Backup Bundle (.gltbak)"),
                         tr("Snapshot your whole GameLoop / CODM setup — keymaps, "
                            "configs, registry, smk.conf — into one file you can "
                            "move, restore, or preview before applying."));

    // ── Actions ─────────────────────────────────────────────────────────────
    const int action = widgets::action_row(
        loaded ? tr("Apply Bundle") : tr("Create Bundle"),
        loaded ? tr("Open Another") : tr("Open Bundle"),
        !app.busy());

    if (action == 0) {
        if (loaded) {
            // Confirm before applying.
            app.ask(tr("Apply this bundle to the live system?\n\nThis overwrites "
                       "Configs.db, smk.conf and registry values with the backup "
                       "values for the sections you left checked."),
                    [app_ptr = &app](bool yes) {
                        if (!yes) return;
                        app_ptr->apply_bundle(false);
                    });
        } else {
            // Create bundle: ask then open file picker.
            app.ask(tr("Choose where to save the bundle?\n\nThe file picker will "
                       "open after you confirm."),
                    [](bool yes) {
                        if (!yes) return;
                    });
        }
    } else if (action == 1) {
        if (!loaded) {
            app.ask(tr("Open a .gltbak file?\n\nThe file picker will open."),
                    [](bool yes) {
                        if (!yes) return;
                    });
        } else {
            app.clear_bundle();
            app.log("Bundle cleared.");
        }
    }

    // ── File picker (render thread, platform dialog) ────────────────────────
    static bool show_save_picker = false;
    static bool show_open_picker = false;
    if (action == 0 && !loaded) show_save_picker = true;
    if (action == 1 && !loaded) show_open_picker = true;

    if (show_save_picker) {
        show_save_picker = false;
        auto path = pick_save_file(nullptr, L"GameloopTuner-Bundle.gltbak");
        if (path) {
            app.create_bundle(*path, "Bundle " +
                                 str::timestamp_compact());
        }
    }
    if (show_open_picker) {
        show_open_picker = false;
        auto path = pick_open_file(nullptr);
        if (path) {
            app.open_bundle(*path);
        }
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ── Bundle info card ────────────────────────────────────────────────────
    if (loaded) {
        const auto& b = *app.current_bundle();
        theme::begin_card(tr("Bundle Contents"));
        widgets::kv_row(tr("Title"), b.title.empty() ? tr("(untitled)") : b.title);
        widgets::kv_row(tr("Created"), b.created);
        widgets::kv_row(tr("Tuner version"), b.tuner_version);
        {
            // Show which variant created this bundle vs which variant this
            // binary is — if they differ, flag it so the user doesn't fuse
            // Hub keymapping into Beta (or vice versa).
            std::string bundle_variant = "unknown";
            if (!b.tuner_version.empty()) {
                auto dot = b.tuner_version.rfind('.');
                if (dot != std::string::npos)
                    bundle_variant = b.tuner_version.substr(dot + 1);
            }
            std::string this_variant = variant::label(app.compiled_variant());

            bool mismatch = (bundle_variant != this_variant &&
                             bundle_variant != "unknown");
            if (mismatch) {
                ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kWarn));
                widgets::kv_row(tr("Bundle variant"),
                                bundle_variant + " ⚠ differs from this build (" + this_variant + ")");
                ImGui::PopStyleColor();
            } else {
                widgets::kv_row(tr("Bundle variant"), bundle_variant);
            }
            widgets::kv_row(tr("This build"), this_variant);
        }
        widgets::kv_row(tr("Source hostname"), b.source_system.hostname);
        widgets::kv_row(tr("Windows"), b.source_system.windows_version);
        widgets::kv_row(tr("GameLoop"), b.source_system.gameloop_version);
        widgets::kv_row(tr("Engine"), b.source_system.engine_version);
        widgets::kv_row(tr("CODM"), b.source_system.codm_version);
        widgets::kv_row(tr("Arch"), b.source_system.arch);
        widgets::kv_row(tr("SHA-256"), b.sha256);
        theme::end_card();

        // ── Summary card ────────────────────────────────────────────────────
        theme::begin_card(tr("Configuration Summary"));
        widgets::kv_row(tr("Keymap mode"),
                        b.summary.contains("keymap_mode") && b.summary["keymap_mode"].is_string()
                            ? b.summary["keymap_mode"].get<std::string>()
                            : "—");
        widgets::kv_row(tr("Active mode (lastmodeid)"),
                        b.summary.contains("lastmodeid") && b.summary["lastmodeid"].is_string()
                            ? b.summary["lastmodeid"].get<std::string>()
                            : "—");
        widgets::kv_row(tr("Clarity tier"),
                        b.summary.contains("clarity")
                            ? std::to_string(b.summary["clarity"].get<int>())
                            : "—");
        widgets::kv_row(tr("Low latency"),
                        b.summary.contains("low_latency_mode")
                            ? (b.summary["low_latency_mode"].get<bool>() ? "ON" : "OFF")
                            : "—");
        widgets::kv_row(tr("smk.conf read-only"),
                        b.summary.contains("smk_readonly")
                            ? (b.summary["smk_readonly"].get<bool>() ? "Yes" : "No")
                            : "—");
        theme::end_card();

        // ── Diff card ───────────────────────────────────────────────────────
        theme::begin_card(tr("Changes vs Live System"));
        {
            std::size_t total = app.bundle_diffs().size();
            std::size_t changed = 0;
            std::size_t missing = 0;
            std::size_t matches = 0;
            for (const auto& d : app.bundle_diffs()) {
                if (d.matches) ++matches;
                else if (d.live_value == "<missing on this system>" ||
                         d.live_value == "<not found>" || d.live_value == "<unreadable>")
                    ++missing;
                else
                    ++changed;
            }
            char line[160];
            std::snprintf(line, sizeof(line),
                          "%zu fields compared  •  %zu match  •  %zu will change  •  %zu missing",
                          total, matches, changed, missing);
            widgets::body_text(line, ImGui::GetContentRegionAvail().x,
                               col(theme::kTextDim));
            ImGui::Dummy(ImVec2(0, 6));
        }

        const auto& diffs = app.bundle_diffs();
        if (diffs.empty()) {
            widgets::body_text(tr("No comparable fields — the bundle may be empty "
                                  "or this system has no GameLoop install."),
                               ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
        } else {
            for (const auto& d : diffs) {
                draw_diff_row(d);
            }
        }
        theme::end_card();

        // ── Apply options (per-section toggles) ─────────────────────────────
        theme::begin_card(tr("Apply Options"));
        widgets::body_text(
            tr("Backup captures everything. Uncheck sections you don't want to "
               "restore — they will be left untouched on the live system."),
            ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        bool apply_configs_db = app.bundle_options().configs_db;
        bool apply_smk_conf = app.bundle_options().smk_conf;
        bool apply_registry = app.bundle_options().registry;
        ImGui::Checkbox(tr("Configs.db (keymaps + perf)"), &apply_configs_db);
        app.set_apply_configs_db(apply_configs_db);
        ImGui::SameLine(0, 16.0f);
        ImGui::Checkbox(tr("smk.conf (Tex templates)"), &apply_smk_conf);
        app.set_apply_smk_conf(apply_smk_conf);
        ImGui::SameLine(0, 16.0f);
        ImGui::Checkbox(tr("Registry branches"), &apply_registry);
        app.set_apply_registry(apply_registry);
        ImGui::PopStyleVar();
        theme::end_card();

        // ── Apply button ────────────────────────────────────────────────────
        std::size_t will_change = 0;
        for (const auto& d : diffs)
            if (!d.matches &&
                d.live_value != "<missing on this system>" &&
                d.live_value != "<not found>" && d.live_value != "<unreadable>")
                ++will_change;

        ImGui::Dummy(ImVec2(0, 8));
        if (widgets::action_button(tr("Apply Bundle to Live System"),
                                   !app.busy() && will_change > 0,
                                   col(theme::kAccent))) {
            app.ask(tr("Apply this bundle?\n\nThis will overwrite live settings "
                       "with the backup values (sections you left checked). "
                       "GameLoop should be closed first."),
                    [app_ptr = &app](bool yes) {
                        if (!yes) return;
                        app_ptr->apply_bundle(false);
                    });
        }
        ImGui::SameLine();
        if (widgets::action_button(tr("Dry Run (preview changes)"), !app.busy(),
                                   col(theme::kWarn))) {
            app.apply_bundle(true);
        }
        ImGui::SameLine();
        if (widgets::action_button(tr("Close Bundle"), !app.busy(),
                                   col(theme::kTextDim))) {
            app.clear_bundle();
        }
    } else {
        // Empty state.
        theme::begin_card(tr("No Bundle Open"));
        widgets::body_text(
            tr("Create a bundle to snapshot your current GameLoop / CODM setup, "
               "or open an existing .gltbak file to preview and restore it."),
            ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
        theme::end_card();
    }

    ImGui::Dummy(ImVec2(0, 12));

    // ── Bundle activity log ─────────────────────────────────────────────────
    theme::begin_card(tr("Bundle Activity Log"));
    widgets::log_console(app, 200.0f);
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::PopStyleVar();
}

} // namespace glt::ui
