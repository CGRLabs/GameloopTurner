// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_about.cpp — the About page.
//
// Ported from pages.py::AboutPage. The body text is the only user-facing
// statement of what the tool is and is not, so it is carried over verbatim.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/hosts.hpp"
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

    widgets::page_header("About", "GameLoop Tuner by Codmsurge");

    // ── Links card ──────────────────────────────────────────────────────────
    theme::begin_card("Links");
    widgets::kv_row("Website", "codmsurge.uk");
    widgets::kv_row("Source", "github.com/realLLUKS/GameloopTurner");

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.396f, 0.949f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.278f, 0.322f, 0.769f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    // ShellExecute, not a raw link: both URLs are https, so no
    // "this could hurt your computer" legacy prompt.
    if (ImGui::Button("Open Website", ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open", L"https://codmsurge.uk", nullptr, nullptr,
                        SW_SHOW);
    }
    ImGui::SameLine();
    if (ImGui::Button("View on GitHub", ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open", L"https://github.com/realLLUKS/GameloopTurner",
                        nullptr, nullptr, SW_SHOW);
    }
    ImGui::SameLine();
    if (ImGui::Button("View Docs", ImVec2(140.0f, 30.0f))) {
        ::ShellExecuteW(nullptr, L"open",
                        L"https://github.com/realLLUKS/GameloopTurner/tree/main/docs",
                        nullptr, nullptr, SW_SHOW);
    }
    ImGui::PopStyleColor(3);

    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Discord card ───────────────────────────────────────────────────────
    // Display only: no invite URL is published here, so the name is shown
    // without a join button rather than pointing at an unowned server.
    theme::begin_card("Codmsurge Discord");
    widgets::kv_row("Contact", "getskulled666");
    theme::end_card();

    // ── Build info ─────────────────────────────────────────────────────────
    theme::begin_card("Build");
    widgets::help_marker("Version, OS, install path, database path, and log file "
                         "location. Use 'Save diagnostic report' to export a "
                         "scrubbed summary for support.");
    widgets::kv_row("Version", std::string(report::kVersion));
    {
        const auto hi = paths::host_info();
        widgets::kv_row("OS",
                        str::to_utf8(hi.machine) + "  " + str::to_utf8(hi.version));
    }
    widgets::kv_row("Install", paths::install_root().empty()
                                   ? "NOT FOUND"
                                   : str::to_utf8(paths::install_root()));
    widgets::kv_row("Database", paths::appdata_db().empty()
                                    ? "NOT FOUND"
                                    : str::to_utf8(paths::appdata_db()));
    widgets::kv_row("Log file", str::to_utf8(glt::log::file_path()));

    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::action_button("Save diagnostic report", !app.busy(), col(theme::kBgHover))) {
        app.export_report();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Variant card ───────────────────────────────────────────────────────
    // The first-launch selector records the user's GameLoop. This is where it
    // can be reviewed or changed.
    theme::begin_card("GameLoop variant");
    widgets::help_marker("The tuner ships as a Beta build and a Hub (TAC) build. "
                         "The build you are running decides the behaviour; the "
                         "'your choice' below is just which GameLoop you told the "
                         "tuner you have.");
    widgets::kv_row("This build", variant::label(app.compiled_variant()));
    {
        const std::string chosen =
            app.variant_chosen()
                ? std::string(variant::label(app.chosen_variant()))
                : ("not set - defaults to " + std::string(variant::label(variant::default_id())));
        const ImVec4 chosen_col =
            (app.variant_chosen() && app.chosen_variant() != app.compiled_variant())
                ? col(theme::kWarn)
                : col(theme::kOk);
        widgets::kv_row("Your choice", chosen, &chosen_col);
    }
    if (app.variant_chosen() && app.chosen_variant() != app.compiled_variant()) {
        widgets::body_text(
            "This build does not match your saved choice. Behaviour follows the "
            "compiled build - download the matching build if tuning looks wrong.",
            ImGui::GetContentRegionAvail().x, col(theme::kWarn));
    }

    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::action_button("Change variant...", !app.busy(), col(theme::kBgHover))) {
        app.open_variant_picker();
    }
    ImGui::SameLine(0, 8.0f);
    if (widgets::action_button("Clear saved choice", !app.busy(), col(theme::kBgHover))) {
        app.clear_variant_choice();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 10));

    // ── Glossary card ──────────────────────────────────────────────────────
    theme::begin_card("Glossary");
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
    ImGui::Checkbox("Show raw (paths, build info)", &show_raw_about);
    if (show_raw_about) {
        theme::begin_card("Raw build / path info");
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

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui