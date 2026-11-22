// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_about.cpp — the About page.
//
// Ported from pages.py::AboutPage. The body text is the only user-facing
// statement of what the tool is and is not, so it is carried over verbatim.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/bundle.hpp"
#include "glt/core/hosts.hpp"
#include "glt/core/i18n.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/report.hpp"
#include "glt/core/strings.hpp"
#include "glt/core/variant.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

// shellapi.h (ShellExecuteW) requires windows.h first; including it in the
// other order produces a cascade of "DECLSPEC_IMPORT undeclared" errors from
// inside the SDK header.
#include <windows.h>
#include <shellapi.h>

#include <cstdio>

namespace glt::ui {
namespace {

using glt::i18n::tr;

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

constexpr const char* kAbout =
    "GameLoop Tuner by Codmsurge\n"
    "\n"
    "WHAT THIS TOOL IS FOR\n"
    "  Diagnose, tune, and fix GameLoop/CODM configuration on any PC without\n"
    "  hand-editing DB files. Aim for better performance, lower latency, and\n"
    "  stable keymaps that resist GameLoop auto-switching (if the engine\n"
    "  reverts, the drift alert fires).\n"
    "\n"
    "FEATURES\n"
    "  - Dashboard: live read-only status cards (processes, allocation,\n"
    "    keymap, drive space, service).\n"
    "  - Keymaps: DISABLE AUTO-SWITCH (one-tick chain: pin a flat layout,\n"
    "    flatten the smart destinations, blind the lobby + skill-box jump\n"
    "    triggers, clear jump history, lock smk.conf read-only).\n"
    "  - Performance: VM cores/RAM, clarity tier, low-latency, frame-gen.\n"
    "  - VM & Priority: live process priorities (emulator/VM high, assistant\n"
    "    above, downloader below) to reduce stutter when the host is loaded.\n"
    "  - Backups: snapshot and restore Configs.db in a single click.\n"
    "  - Smart drift detection: alerts you if GameLoop silently reverts your\n"
    "    settings.\n"
    "\n"
    "PORTABLE & SAFE\n"
    "  No hardcoded paths, users or drive letters. Every write is backed up\n"
    "  through SQLite's online backup API and verified by re-reading.\n"
    "\n"
    "WHY IT NEEDS ADMIN\n"
    "  GameLoop recreates the emulator from LocalSystem, and the close ladder\n"
    "  must be able to terminate SYSTEM-owned processes mid-match. Windows\n"
    "  asks for elevation before launch.";

} // namespace

void draw_about(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##about_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##about_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    widgets::page_header(tr("About"), "GameLoop Tuner by Codmsurge");

    // ── Links card ──────────────────────────────────────────────────────────
    theme::begin_card(tr("Links"));
    widgets::kv_row(tr("Website"), "codmsurge.uk");
    widgets::kv_row(tr("Source"), "github.com/CGRLabs/GameloopTurner");

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.396f, 0.949f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.278f, 0.322f, 0.769f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    // ShellExecute, not a raw link: both URLs are https, so no
    // "this could hurt your computer" legacy prompt.
    if (ImGui::Button(tr("Open Website"), ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open", L"https://codmsurge.uk", nullptr, nullptr,
                        SW_SHOW);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("View on GitHub"), ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open", L"https://github.com/CGRLabs/GameloopTurner",
                        nullptr, nullptr, SW_SHOW);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("View Docs"), ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open",
                        L"https://github.com/CGRLabs/GameloopTurner/tree/main/docs",
                        nullptr, nullptr, SW_SHOW);
    }
    ImGui::PopStyleColor(3);

    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Discord card ───────────────────────────────────────────────────────
    // Display only: no invite URL is published here, so the name is shown
    // without a join button rather than pointing at an unowned server.
    theme::begin_card("Codmsurge Discord");
    widgets::kv_row(tr("Contact"), "getskulled666");
    theme::end_card();

    // ── Build info ─────────────────────────────────────────────────────────
    theme::begin_card(tr("Build"));
    widgets::help_marker(tr("Version, OS, install path, database path, and log file "
                         "location. Use 'Save diagnostic report' to export a "
                         "scrubbed summary for support."));
    widgets::kv_row(tr("Version"), std::string(report::kVersion));
    {
        const auto hi = paths::host_info();
        widgets::kv_row(tr("OS"),
                        str::to_utf8(hi.machine) + "  " + str::to_utf8(hi.version));
    }
    widgets::kv_row(tr("Install"), paths::install_root().empty()
                                   ? tr("NOT FOUND")
                                   : str::to_utf8(paths::install_root()));
    widgets::kv_row(tr("Database"), paths::appdata_db().empty()
                                    ? tr("NOT FOUND")
                                    : str::to_utf8(paths::appdata_db()));
    widgets::kv_row(tr("Log file"), str::to_utf8(glt::log::file_path()));

    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::action_button(tr("Save diagnostic report"), !app.busy(), col(theme::kBgHover))) {
        app.export_report();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Variant card ───────────────────────────────────────────────────────
    // The first-launch selector records the user's GameLoop. This is where it
    // can be reviewed or changed.
    // ── Language card ────────────────────────────────────────────────────────
    // Chips switch the UI instantly: every glyph any shipped language can draw
    // is already in the atlas (see fonts.hpp), so there is no rebuild and no
    // restart. "Automatic (Windows)" clears the pin and follows the Windows
    // display language instead.
    theme::begin_card(tr("Language"));
    widgets::help_marker(tr(
        "Pick the interface language. 'Automatic (Windows)' follows the "
        "Windows display language. The choice is saved for next launch."));
    {
        const bool auto_lang = app.language_is_automatic();
        const std::string active_code(app.language());

        // Chips flow left-to-right and wrap when the next one would not fit.
        float used = 0.0f;
        const float spacing = 6.0f;
        auto place_chip = [&used, spacing](const char* label, bool active) {
            const float w = ImGui::CalcTextSize(label).x + 28.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (used > 0.0f && used + w > avail + spacing) {
                ImGui::NewLine();
                used = 0.0f;
            }
            (void)widgets::chip_button(label, active);
            ImGui::SameLine(0.0f, spacing);
            used += w + spacing;
        };

        place_chip(tr("Automatic (Windows)"), auto_lang);
        for (const auto& l : i18n::languages()) {
            place_chip(l.native_name, !auto_lang && active_code == l.code);
        }
        ImGui::NewLine();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Update card ────────────────────────────────────────────────────────
    // The self-update surfaces here only: a silent startup check plus a manual
    // button. Nothing ever installs without this card's explicit click and a
    // confirm prompt (see App::install_update).
    theme::begin_card(tr("Updates"));
    widgets::help_marker(tr("Checks GitHub for a newer GameLoop Tuner release. "
                         "Downloads are verified against the release's SHA-256 "
                         "digest before the installer is allowed to run."));
    {
        const auto& up = app.update();
        widgets::kv_row(tr("Current version"), std::string(report::kVersion));

        if (up.busy) {
            char status_buf[96];
            if (up.downloaded > 0 && up.size > 0) {
                std::snprintf(status_buf, sizeof(status_buf), tr("Downloading... %d%%"),
                              static_cast<int>(up.downloaded * 100 / up.size));
            } else {
                std::snprintf(status_buf, sizeof(status_buf), "%s",
                              tr("Checking for updates..."));
            }
            widgets::kv_row(tr("Status"), status_buf);
        } else {
            if (up.available) {
                char vbuf[80];
                std::snprintf(vbuf, sizeof(vbuf), tr("Update available: %s"), up.tag.c_str());
                const ImVec4 okc = col(theme::kAccent);
                widgets::kv_row(tr("Status"), vbuf, &okc);
            } else if (!up.error.empty()) {
                const ImVec4 warnc = col(theme::kWarn);
                widgets::kv_row(tr("Status"), std::string(tr("check failed")), &warnc);
                widgets::body_text(up.error, ImGui::GetContentRegionAvail().x,
                                   col(theme::kTextMuted));
            } else if (up.checked) {
                widgets::kv_row(tr("Status"), tr("You are on the latest release."));
            } else {
                widgets::kv_row(tr("Status"), tr("Not checked yet."));
            }

            ImGui::Dummy(ImVec2(0, 6));
            if (up.available) {
                const char* install_label =
                    up.downloaded_ok ? tr("Install now") : tr("Download & Install");
                if (widgets::action_button(install_label, !app.busy(), col(theme::kBgHover))) {
                    app.install_update();
                }
                ImGui::SameLine(0, 8.0f);
            } else if (!up.busy) {
                if (widgets::action_button(tr("Check for updates"), !app.busy(),
                                           col(theme::kBgHover))) {
                    app.check_update(true);
                }
                ImGui::SameLine(0, 8.0f);
            }
            if (!up.page_url.empty() && ImGui::SmallButton(tr("View release page"))) {
                ::ShellExecuteW(nullptr, L"open", str::to_wide(up.page_url).c_str(),
                                nullptr, nullptr, SW_SHOW);
            }
        }
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    theme::begin_card(tr("GameLoop variant"));
    widgets::help_marker("The tuner ships as a Beta build and a Hub (TAC) build. "
                         "The build you are running decides the behaviour; the "
                         "'your choice' below is just which GameLoop you told the "
                         "tuner you have.");
    widgets::kv_row(tr("This build"), variant::label(app.compiled_variant()));
    {
        const std::string chosen =
            app.variant_chosen()
                ? std::string(variant::label(app.chosen_variant()))
                : (std::string(tr("not set - defaults to ")) +
                   std::string(variant::label(variant::default_id())));
        const ImVec4 chosen_col =
            (app.variant_chosen() && app.chosen_variant() != app.compiled_variant())
                ? col(theme::kWarn)
                : col(theme::kOk);
        widgets::kv_row(tr("Your choice"), chosen, &chosen_col);
    }
    if (app.variant_chosen() && app.chosen_variant() != app.compiled_variant()) {
        widgets::body_text(
            tr("This build does not match your saved choice. Behaviour follows the "
               "compiled build - download the matching build if tuning looks wrong."),
            ImGui::GetContentRegionAvail().x, col(theme::kWarn));
    }

    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::action_button(tr("Change variant..."), !app.busy(), col(theme::kBgHover))) {
        app.open_variant_picker();
    }
    ImGui::SameLine(0, 8.0f);
    if (widgets::action_button(tr("Clear saved choice"), !app.busy(), col(theme::kBgHover))) {
        app.clear_variant_choice();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Glossary card ──────────────────────────────────────────────────────
    theme::begin_card(tr("Glossary"));
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    const char* glossary =
        "Auto-switch: GameLoop watches your screen and swaps your entire key layout "
        "when it recognises the lobby or skill-select box.\n"
        "Pin: locks a fixed-key layout as the active one.\n"
        "Flatten: replaces smart layouts (keys change by scene) with fixed keys.\n"
        "Blind: removes the image templates the engine uses to detect scenes.\n"
        "Quarantine: moves the engine's local template pack aside and locks smk.conf "
        "read-only (the real kill-switch).\n"
        "Tex / smk.conf: image fingerprints GameLoop matches against your screen.\n"
        "Official vs user blobs: official layouts are trigger-guarded (can't edit); "
        "user blobs are writable.\n"
        "Drifted: GameLoop reverted a setting (usually after an update). Re-apply.\n"
        "Clarity tier: render resolution. Tier 2 = clearer + more FPS here.\n"
        "Low-latency: smaller frame queue = best feel-per-cost. Keep ON.\n"
        "Frame-gen: fake frames that add input lag. Keep OFF.\n"
        "VM cores/RAM: how much of your PC GameLoop's VM gets. Leave 1 core + 2 GB.\n"
        "WAL: SQLite's journal. Can exceed the main DB file. Copying the file while "
        "running produces a torn database.\n"
        "ADB: Android Debug Bridge - talks to the emulator's Android OS.\n"
        "Tex template: an image fingerprint matched against your screen to detect "
        "scenes (lobby, skill-select, etc.).\n"
        "SwitchOperation: a rule that changes a key's function by screen state. "
        "0 rules = flat = immune to auto-switch.\n"
        "Bufferbloat: ping spikes when someone else uses the internet.\n"
        "UAC: Windows prompt for admin rights (GameLoop needs it to kill SYSTEM procs).";
    ImGui::TextWrapped("%s", glossary);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Show raw (power users) ─────────────────────────────────────────────
    static bool show_raw_about = false;
    ImGui::Checkbox(tr("Show raw (paths, build info)"), &show_raw_about);
    if (show_raw_about) {
        theme::begin_card(tr("Raw build / path info"));
        widgets::kv_row("Install root", str::to_utf8(paths::install_root()));
        widgets::kv_row("App version dir", str::to_utf8(paths::app_version_dir(paths::install_root())));
        widgets::kv_row("ADB", paths::adb_exe().empty() ? "missing" : "present");
        widgets::kv_row("ano_tmp", paths::ano_tmp_dir().empty() ? "not found" : str::to_utf8(paths::ano_tmp_dir()));
        widgets::kv_row("smk.conf", paths::smk_conf().empty() ? "not found" : str::to_utf8(paths::smk_conf()));
        widgets::kv_row("Configs.db", paths::appdata_db().empty() ? "not found" : str::to_utf8(paths::appdata_db()));
        theme::end_card();
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ── Body ───────────────────────────────────────────────────────────────
    theme::begin_card(nullptr);
    ImGui::PushFont(fonts::current().mono);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(kAbout);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── File association (.gltbak) ──────────────────────────────────────────
    theme::begin_card(tr("File Association (.gltbak)"));
    widgets::body_text(
        tr("Register .gltbak so double-clicking a backup bundle opens it in this "
           "tuner for preview and apply."),
        ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::action_button(tr("Register .gltbak file type"), !app.busy(),
                               col(theme::kAccent))) {
        app.ask(tr("Register .gltbak in Windows Explorer?\n\nThis writes file-type "
                   "associations to HKCU\\Software\\Classes so double-clicking a "
                   ".gltbak launches this tuner and opens the bundle in preview "
                   "mode."),
                [&app](bool yes) {
                    if (!yes) return;
                    if (glt::bundle::register_file_association()) {
                        app.log("Registered .gltbak file association.");
                    } else {
                        app.log("Failed to register .gltbak file association.",
                                glt::log::Level::Error);
                    }
                });
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui