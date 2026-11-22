#include "glt/ui/app.hpp"

#include "glt/core/db.hpp"
#include "glt/core/bundle.hpp"
#include "glt/core/i18n.hpp"
#include "glt/core/journal.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/netinfo.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/report.hpp"
#include "glt/core/strings.hpp"
#include "glt/core/update.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj_core.h>

namespace glt::ui {

using glt::i18n::tr;

const char* page_title(Page p) {
    switch (p) {
        case Page::Dashboard:    return tr("Dashboard");
        case Page::Keymaps:      return tr("Keymaps");
        case Page::Performance:  return tr("Performance");
        case Page::VmPriority:   return tr("VM & Priority");
        case Page::Network:      return tr("Network & Servers");
        case Page::Backups:      return tr("Backups");
        case Page::BackupBundle: return tr("Backup Bundle");
        case Page::Logs:         return tr("Logs");
        case Page::About:        return tr("About");
    }
    return "?";
}

std::string drift_text(DriftState s) {
    // The glyph prefix stays source-drawn (it comes from the symbol ranges in
    // fonts.hpp); only the word is translated. Returned by value because the
    // translated word no longer lives in static storage.
    switch (s) {
        case DriftState::NoBaseline: return std::string(tr("No baseline"));
        case DriftState::Stable:     return "\xE2\x9A\x99 " + std::string(tr("Stable"));
        case DriftState::Drifted:    return "\xE2\x9A\xA0 " + std::string(tr("Drifted"));
    }
    return "?";
}

bool drift_is_warn(DriftState s) { return s != DriftState::Stable; }

// ── Lifecycle ────────────────────────────────────────────────────────────────

App::App() {
    host_ = procs::host_resources();
}

App::~App() { shutdown(); }

void App::startup() {
    log("GameLoop Tuner started.", log::Level::Info);

    // ── Language ────────────────────────────────────────────────────────────
    // Persisted choice wins; otherwise follow the Windows UI language. Must
    // run before any worker exists (i18n.hpp threading contract) and before
    // the first tr() anywhere in the UI.
    language_ = i18n::init();
    {
        const auto saved = i18n::load_choice();
        language_auto_ = !saved || str::ascii_lower(str::trim(*saved)) == "auto";
    }
    log("UI language: " + language_ + (language_auto_ ? " (auto)." : " (saved choice)."),
        log::Level::Info);

    // ── First-launch variant selection ─────────────────────────────────────
    // Never auto-detect: the persisted choice is the user's answer to "which
    // GameLoop do you have?", and default_id() is Beta when nothing is saved.
    chosen_variant_ = variant::default_id();
    if (const auto saved = variant::load_choice()) {
        chosen_variant_ = *saved;
        variant_chosen_ = true;
        log(std::string("Variant: ") + variant::label(chosen_variant_) + " (saved choice).",
            log::Level::Info);
    } else {
        variant_picker_open_ = true;
        log("First launch: choose which GameLoop you have.", log::Level::Info);
    }
    if (variant_chosen_ && chosen_variant_ != variant::compiled()) {
        log(std::string("Warning: this is the ") + variant::label(variant::compiled()) +
                " build, but the saved choice is " + variant::label(chosen_variant_) +
                ". Download the matching build for correct behaviour.",
            log::Level::Warning);
    }

    const std::wstring dbp = paths::appdata_db();
    if (dbp.empty()) {
        log("Configs.db not found - is GameLoop installed and launched for this user?",
            log::Level::Warning);
        drift_state_ = DriftState::NoBaseline;
    } else {
        // Pre-tick the boxes that are already applied, so the UI reflects the
        // machine's real state instead of an empty slate.
        const auto applied = fixes::detect_applied();
        if (!applied.empty()) {
            std::vector<std::string> names;
            names.reserve(applied.size());
            for (fixes::Key k : applied) {
                names.emplace_back(fixes::key_name(k));
                applied_.push_back(k);
            }
            log("Detected already applied: " + str::join(names, ", "), log::Level::Info);
        }

        drift_ = std::make_unique<drift::DriftMonitor>();
        drift_->set_baseline();
        drift_->start(drift::DriftMonitor::kDefaultIntervalS);
        drift_state_ = DriftState::Stable;
    }

    refresh_host();
    refresh_dashboard();
    refresh_backups();

    // ── Update check (silent, worker) ─────────────────────────────────────
    // One check per launch against the GitHub API (well within its unauthenti-
    // cated rate limit). Failures log at Info: offline is a normal state, not
    // an alarm. The result surfaces as an About-card badge, never as a popup.
    check_update(false);

    // ── What's new (once per version) ──────────────────────────────────────
    const std::wstring dbp2 = paths::appdata_db();
    if (!dbp2.empty()) {
        const std::wstring ver_path = str::path_join(str::path_parent(dbp2), L"tuner-last-version.txt");
        std::string last_ver;
        {
            std::ifstream vf(ver_path);
            if (vf) std::getline(vf, last_ver);
        }
        if (last_ver != report::kVersion) {
            log("Welcome to GameLoop Tuner v" + std::string(report::kVersion) + ".", log::Level::Info);
            log("New: Network & Servers tab (guest connections, game server ping, DNS "
                "override, high-ping help). Dashboard now shows CODM live settings. "
                "Help tooltips (?) added across pages.",
                log::Level::Info);
            char welcome[64];
            std::snprintf(welcome, sizeof(welcome), tr("Welcome to v%s"),
                          report::kVersion);
            toast(welcome, log::Level::Success);
            std::ofstream vf(ver_path, std::ios::trunc);
            if (vf) vf << report::kVersion;
        }
    }
}

void App::shutdown() {
    if (drift_) {
        drift_->stop();
        drift_.reset();
    }
    // jthread's destructor joins, so a still-running apply cannot outlive App
    // and touch freed state.
    workers_.clear();
}

// ── Variant ──────────────────────────────────────────────────────────────────

void App::choose_variant(variant::Id id) {
    chosen_variant_ = id;
    variant_chosen_ = true;
    variant_picker_open_ = false;

    if (variant::save_choice(id)) {
        log(std::string("Variant set to ") + variant::label(id) + ".", log::Level::Success);
    } else {
        log(std::string("Variant set to ") + variant::label(id) +
                " for this session, but it could not be saved; you will be asked again.",
            log::Level::Warning);
    }

    if (id != variant::compiled()) {
        log(std::string("This binary is the ") + variant::label(variant::compiled()) +
                " build but you selected " + variant::label(id) +
                ". Behaviour follows the compiled build; download the matching build.",
            log::Level::Warning);
        toast(std::string("Running the ") + variant::label(variant::compiled()) +
                  " build for a " + variant::label(id) + " install",
              log::Level::Warning);
    } else {
        toast(std::string("Using ") + variant::label(id), log::Level::Success);
    }
}

void App::open_variant_picker() { variant_picker_open_ = true; }

void App::clear_variant_choice() {
    if (variant::clear_choice()) {
        variant_chosen_ = false;
        chosen_variant_ = variant::default_id();
        log("Variant choice cleared; the selector will ask again.", log::Level::Info);
    } else {
        log("Could not clear the variant choice file.", log::Level::Warning);
    }
    variant_picker_open_ = true;
}

// ── Language ────────────────────────────────────────────────────────────────

void App::choose_language(std::string_view code) {
    const std::string c = str::ascii_lower(str::trim(code));

    // Empty or "auto": clear the pin and follow the Windows UI language again.
    if (c.empty() || c == "auto") {
        language_auto_ = true;
        i18n::save_choice("auto");
        const std::string detected = i18n::detect_system_language();
        if (!i18n::set_language(detected)) i18n::set_language("en");
        language_ = std::string(i18n::current());
        log("UI language: " + language_ + " (auto).", log::Level::Info);
        return;
    }

    if (i18n::set_language(c)) {
        language_auto_ = false;
        i18n::save_choice(c);
        language_ = c;
        log("UI language: " + c + " (saved choice).", log::Level::Info);
        return;
    }

    log("UI language '" + c + "' is not available; keeping " + language_ + ".",
        log::Level::Warning);
}

// ── Thread-safe UI queue ─────────────────────────────────────────────────────

void App::post(std::function<void()> fn) {
    std::scoped_lock lock(queue_mutex_);
    // Bound the queue: a frozen UI plus a chatty worker is a slow leak, and the
    // oldest messages are the least interesting once the user returns.
    if (queue_.size() >= 4096) queue_.pop_front();
    queue_.push_back(std::move(fn));
}

void App::drain() {
    // Swap the queue out under the lock, then run it unlocked — a callback is
    // free to post() more work (a nested prompt answer, for example) without
    // deadlocking against ourselves.
    std::deque<std::function<void()>> batch;
    {
        std::scoped_lock lock(queue_mutex_);
        batch.swap(queue_);
    }

    for (auto& fn : batch) {
        if (!fn) continue;
        try {
            fn();
        } catch (const std::exception& e) {
            // A failed UI callback must not take down the render loop.
            log(std::string("ui callback error: ") + e.what(), log::Level::Error);
        }
    }
}

// ── Logging ──────────────────────────────────────────────────────────────────

void App::log(std::string msg, log::Level level) {
    // Write to the file BEFORE consuming `msg` into the ring. std::move leaves
    // the source in a valid-but-unspecified state that is empty for every
    // standard string type, so doing it the other way round writes a blank
    // line to tuner.log for every message the UI ever shows.
    glt::log::write(level, msg);

    std::scoped_lock lock(log_mutex_);
    log_entries_.push_back(LogEntry{str::timestamp_time(), std::move(msg), level, false});
    if (log_entries_.size() > kMaxLogEntries) {
        log_entries_.erase(log_entries_.begin(),
                           log_entries_.begin() + (log_entries_.size() - kMaxLogEntries));
    }
}

void App::log_many(const std::vector<std::string>& msgs, log::Level level) {
    for (const auto& m : msgs) log(m, level);
}

void App::log_group(const std::string& title) {
    std::scoped_lock lock(log_mutex_);
    log_entries_.push_back(
        LogEntry{str::timestamp_time(), title, log::Level::Progress, true});
}

void App::clear_log() {
    std::scoped_lock lock(log_mutex_);
    log_entries_.clear();
    glt::log::clear_mem();
}

// ── Navigation ───────────────────────────────────────────────────────────────

void App::show_page(Page p) {
    page_ = p;
    if (p == Page::Dashboard || p == Page::Performance || p == Page::VmPriority ||
        p == Page::Keymaps) {
        refresh_dashboard();
    }
    if (p == Page::Backups) refresh_backups();
    if (p == Page::Network) refresh_network();
}

// ── Pending queue ────────────────────────────────────────────────────────────

void App::toggle_pending(fixes::Key k, bool on) {
    const auto pit = std::find(pending_.begin(), pending_.end(), k);
    const auto ait = std::find(applied_.begin(), applied_.end(), k);
    if (on) {
        if (pit == pending_.end()) pending_.push_back(k);
        // If it was previously applied, un-mark it as applied (user is
        // re-queuing it for a fresh apply).
        if (ait != applied_.end()) applied_.erase(ait);
    } else {
        if (pit != pending_.end()) pending_.erase(pit);
        if (ait != applied_.end()) applied_.erase(ait);
    }
}

void App::set_alloc_pending(int cores, int mem_gb, bool on) {
    alloc_cores_ = cores;
    alloc_mem_gb_ = mem_gb;
    alloc_pending_ = on;
}

void App::set_clarity_pending(int level, bool on) {
    clarity_level_ = level;
    clarity_pending_ = on;
}

void App::clear_pending() {
    pending_.clear();
    alloc_pending_ = false;
    clarity_pending_ = false;
}

bool App::is_pending(fixes::Key k) const {
    if (std::find(pending_.begin(), pending_.end(), k) != pending_.end()) return true;
    if (std::find(applied_.begin(), applied_.end(), k) != applied_.end()) return true;
    return false;
}

int App::pending_count() const {
    int n = static_cast<int>(pending_.size());
    if (alloc_pending_) ++n;
    if (clarity_pending_) ++n;
    return n;
}

// ── Host ─────────────────────────────────────────────────────────────────────

int App::max_cores() const {
    int c = std::max(2, host_.cores - 1);
    if (dash_.alloc.cpu_cores && *dash_.alloc.cpu_cores > c) {
        c = static_cast<int>(*dash_.alloc.cpu_cores);
    }
    return c;
}

int App::max_mem_gb() const {
    int m = std::max(3, host_.mem_gb - 2);
    if (dash_.alloc.memory_gb && *dash_.alloc.memory_gb > m) {
        m = static_cast<int>(*dash_.alloc.memory_gb);
    }
    return m;
}

void App::refresh_host() {
    post([this] { host_ = procs::host_resources(); });
}

// ── Dashboard ────────────────────────────────────────────────────────────────

void App::refresh_dashboard() {
    // The gather runs on a worker, NOT on the render thread. It used to be
    // posted directly, which meant process enumeration, the SQLite queries and
    // the registry reads all executed inside drain() — on the render thread —
    // stalling every frame for the duration (~1 s). That is what made clicking
    // the taskbar icon or switching pages feel like a freeze.
    workers_.emplace_back([this] {
        Dashboard d;

        d.procs = procs::list_gameloop();
        d.service = procs::service_status();
        d.alloc = reg::perf_allocation();
        d.install_root = paths::install_root();
        d.db_path = paths::appdata_db();
        d.build_dir = paths::app_version_dir(d.install_root);
        d.drive = disk::game_drive_status(d.install_root);

        if (!d.db_path.empty()) {
            try {
                db::Connection con(d.db_path, 5000);

                const auto dr = keymaps::drift_report(con, paths::kPkg);
                d.lastmodeid = dr.lastmodeid;
                d.mode145_solution = dr.mode145_solution;
                d.recentlyused = dr.recentlyused;
                d.solutions = keymaps::solutions(con, paths::kPkg);
                d.jump_tags = keymaps::jump_tags(con, paths::kPkg);

                for (const auto& t : db::triggers(con)) d.triggers.push_back(t.name);

                d.local_user_config = perf::local_user_config_raw(con, paths::kPkg);
                d.host_config = perf::host_config_raw(con, paths::kPkg);

                if (auto smk = keymaps::read_smk(con, paths::kPkg)) {
                    d.tex_10004 = smk->find("id=\"10004\"") != std::string::npos;
                    d.tex_10046 = smk->find("id=\"10046\"") != std::string::npos;
                }
            } catch (const db::Error& e) {
                d.db_error = e.what();
            }
        }

        // Install-shape verdict: cheap, read-only, and what turns a GameLoop
        // schema change into a visible status instead of silent no-op fixes.
        d.preflight = preflight::run();

        // Assign on the render thread — dash_ is read every frame by every page.
        post([this, d = std::move(d)]() mutable { dash_ = std::move(d); });
    });
}

// ── Backups ──────────────────────────────────────────────────────────────────

void App::refresh_backups() {
    // Directory enumeration off the render thread, same reason as above.
    workers_.emplace_back([this] {
        auto list = backups::list_backups(paths::appdata_db());
        post([this, list = std::move(list)]() mutable { backups_ = std::move(list); });
    });
}

void App::refresh_network() {
    if (network_.busy) return;
    network_.busy = true;
    network_.error.clear();
    workers_.emplace_back([this] {
        auto conns = glt::core::netinfo::capture();
        // Measure ping for each unique remote IP.
        std::vector<std::string> seen;
        for (auto& c : conns) {
            if (c.remote_ip.empty() || c.remote_ip == "0.0.0.0" ||
                c.remote_ip == "127.0.0.1") continue;
            bool done = false;
            for (const auto& s : seen) if (s == c.remote_ip) { done = true; break; }
            if (done) continue;
            seen.push_back(c.remote_ip);
            c.ping_ms = glt::core::netinfo::ping_ms(c.remote_ip);
            c.reachable = c.ping_ms >= 0;
            // Propagate ping to other conns with same IP.
            for (auto& c2 : conns) {
                if (c2.remote_ip == c.remote_ip) {
                    c2.ping_ms = c.ping_ms;
                    c2.reachable = c.reachable;
                }
            }
        }
        post([this, conns = std::move(conns)] {
            network_.conns = std::move(conns);
            network_.busy = false;
            network_.last_refresh = std::chrono::steady_clock::now();
            log("Network refresh complete: " + std::to_string(network_.conns.size()) +
                    " connections captured.", log::Level::Info);
        });
    });
}

void App::restore_backup(std::size_t index) {
    if (index >= backups_.size()) return;

    const auto item = backups_[index];

    {
        char prompt[320];
        std::snprintf(prompt, sizeof(prompt),
                      tr("Restore %s?\n\nGameLoop must be closed first. The current "
                         "file is overwritten."),
                      str::to_utf8(item.filename).c_str());
        ask(prompt,
        [this, item](bool yes) {
            if (!yes) {
                log("Restore cancelled.", log::Level::Warning);
                return;
            }
            busy_.store(true);
            log_group("Restore " + str::to_utf8(item.filename));

            workers_.emplace_back([this, item] {
                // Close first. Copying Configs.db while the emulator holds a
                // 1.3 MB WAL open produces a database that is neither the
                // backup nor the current state.
                if (!procs::running().empty()) {
                    procs::CloseOptions opts;
                    opts.force = true;
                    const auto res = procs::close_gameloop(opts);
                    if (!res.ok) {
                        post([this] {
                            log("Restore aborted - GameLoop is still running.",
                                log::Level::Error);
                            busy_.store(false);
                        });
                        return;
                    }
                }

                const std::wstring target = item.kind == "smk" ? paths::smk_conf()
                                                               : paths::appdata_db();
                const auto restore = item.kind == "smk" ? &backups::restore_smk
                                                        : &backups::restore_db;
                const std::string msg = item.kind == "smk" ? "smk.conf" : "Configs.db";

                try {
                    const auto res = restore(target, item.path);
                    post([this, res, msg] {
                        log_many(res, log::Level::Success);
                        log("Restored " + msg + ". Relaunch GameLoop.", log::Level::Success);
                        refresh_backups();
                        refresh_dashboard();
                        busy_.store(false);
                    });
                } catch (const std::exception& e) {
                    const std::string what = e.what();
                    post([this, what] {
                        log("Restore failed: " + what, log::Level::Error);
                        busy_.store(false);
                    });
                }
            });
        });
    }
}

// ── Backup Bundle (.gltbak) ──────────────────────────────────────────────────

void App::create_bundle(std::wstring save_path, std::string title) {
    busy_.store(true);
    log_group("Create Bundle");

    workers_.emplace_back([this, save_path = std::move(save_path),
                           title = std::move(title)] {
        try {
            auto b = bundle::capture_bundle(title);
            if (!bundle::write_bundle(save_path, b)) {
                post([this] {
                    log("Bundle write failed.", log::Level::Error);
                    busy_.store(false);
                });
                return;
            }
            post([this, path = std::wstring(save_path)] {
                log("Bundle created: " + str::to_utf8(path), log::Level::Success);
                busy_.store(false);
            });
        } catch (const std::exception& e) {
            post([this, what = std::string(e.what())] {
                log("Bundle creation failed: " + what, log::Level::Error);
                busy_.store(false);
            });
        }
    });
}

bool App::open_bundle(std::wstring path) {
    bundle_path_ = std::move(path);
    try {
        auto b = bundle::read_bundle(bundle_path_);
        if (!b) {
            log("Failed to read bundle (corrupt or tampered).", log::Level::Error);
            bundle_.reset();
            bundle_diffs_.clear();
            return false;
        }
        bundle_ = std::move(*b);

        // Compute diff against live system for the preview UI.
        bundle_diffs_ = bundle::diff_bundle(*bundle_);

        log("Bundle opened: " + bundle_->title, log::Level::Success);
        return true;
    } catch (const std::exception& e) {
        log("Failed to read bundle: " + std::string(e.what()), log::Level::Error);
        bundle_.reset();
        bundle_diffs_.clear();
        return false;
    }
}

void App::apply_bundle(bool dry_run) {
    if (!bundle_) return;

    busy_.store(true);
    log_group("Apply Bundle");

    workers_.emplace_back([this, dry_run] {
        try {
            auto notes = bundle::apply_bundle(*bundle_, bundle_options_, dry_run);
            post([this, notes = std::move(notes), dry_run] {
                log_many(notes, log::Level::Success);
                if (!dry_run) {
                    log("Bundle applied. Relaunch GameLoop.", log::Level::Success);
                    refresh_dashboard();
                }
                busy_.store(false);
            });
        } catch (const std::exception& e) {
            post([this, what = std::string(e.what())] {
                log("Bundle apply failed: " + what, log::Level::Error);
                busy_.store(false);
            });
        }
    });
}

void App::clear_bundle() {
    bundle_.reset();
    bundle_diffs_.clear();
    bundle_path_.clear();
}

// ── CLI (headless) support ─────────────────────────────────────────────

std::string App::create_bundle_sync(std::wstring save_path, std::string title) {
    std::ostringstream out;
    try {
        auto b = bundle::capture_bundle(title);
        if (!bundle::write_bundle(save_path, b)) {
            out << "FAILED: could not write bundle to " << str::to_utf8(save_path);
            return out.str();
        }
        out << "OK: bundle created at " << str::to_utf8(save_path)
            << " | sections: configs_db=" << b.configs_db.size()
            << ", smk_conf=" << (b.smk_conf.empty() ? 0 : 1)
            << ", registry=" << b.registry.size()
            << ", source: " << b.source_system.hostname << "/" << b.source_system.gameloop_version;
        return out.str();
    } catch (const std::exception& e) {
        out << "FAILED: " << e.what();
        return out.str();
    }
}

std::string App::apply_bundle_sync(bool dry_run) {
    std::ostringstream out;
    if (!bundle_) {
        out << "FAILED: no bundle loaded";
        return out.str();
    }
    try {
        auto notes = bundle::apply_bundle(*bundle_, bundle_options_, dry_run);
        for (const auto& n : notes) out << n << "\n";
        out << (dry_run ? "DRYRUN complete" : "APPLY complete");
        return out.str();
    } catch (const std::exception& e) {
        out << "FAILED: " << e.what();
        return out.str();
    }
}

std::string App::list_backups_sync() {
    std::ostringstream out;
    auto dbp = paths::appdata_db();
    if (dbp.empty()) {
        out << "FAILED: Configs.db not found - GameLoop not installed?";
        return out.str();
    }
    auto list = backups::list_backups(dbp);
    if (list.empty()) {
        out << "No backups found.";
        return out.str();
    }
    for (const auto& e : list) {
        out << str::to_utf8(e.filename) << " | " << e.kind << " | " << e.bytes
            << " bytes | " << str::to_utf8(e.path) << "\n";
    }
    return out.str();
}

// ── Self-update ────────────────────────────────────────────────────────────

void App::check_update(bool manual) {
    if (update_.busy) return;
    update_.busy = true;
    if (manual) update_.error.clear();

    workers_.emplace_back([this, manual] {
        try {
            auto rel = update::fetch_latest();
            const bool avail = update::is_newer(rel.tag, report::kVersion);
            post([this, rel = std::move(rel), avail, manual] {
                update_.checked = true;
                update_.busy = false;
                update_.available = avail;
                update_.tag = rel.tag;
                update_.page_url = rel.page_url;
                update_.asset_url = rel.asset_url;
                update_.notes = rel.notes;
                update_.sha256 = rel.sha256;
                update_.size = rel.asset_size;
                update_.error.clear();
                update_.downloaded_ok = false;
                update_.setup_path.clear();
                update_.kind = rel.kind;
                if (avail) {
                    if (rel.kind == glt::update::Kind::None) {
                        // Newer tag, but nothing this build can install — the
                        // release page is the only actionable path.
                        log("Update available: " + rel.tag +
                                " — but the release carries no asset this build can "
                                "install. See the About page.",
                            log::Level::Info);
                    } else {
                        log("Update available: " + rel.tag + " (this build is v" +
                                report::kVersion + "). See the About page.",
                            log::Level::Info);
                    }
                } else if (manual) {
                    log("You are on the latest release (v" +
                            std::string(report::kVersion) + ").",
                        log::Level::Info);
                }
            });
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            post([this, manual, msg] {
                update_.checked = true;
                update_.busy = false;
                update_.error = msg;
                log(manual ? "Update check failed: " + msg
                           : "Update check skipped: " + msg,
                    manual ? log::Level::Warning : log::Level::Info);
            });
        }
    });
}

void App::install_update() {
    if (update_.busy || !update_.available) return;
    update_.busy = true;
    update_.error.clear();

    workers_.emplace_back([this] {
        try {
            // Installer assets land in %TEMP%; a portable self-replace must be
            // staged on the running exe's own volume so the final move is a
            // same-volume rename (a cross-volume move would copy over a live
            // image and fail).
            std::wstring target;
            {
                wchar_t temp_dir[MAX_PATH];
                const DWORD n = ::GetTempPathW(MAX_PATH, temp_dir);
                const std::wstring base =
                    update_.kind == glt::update::Kind::Installer
                        ? (n > 0 && n < MAX_PATH ? std::wstring(temp_dir, n) : std::wstring())
                        : update::running_exe_dir();
                if (!base.empty()) {
                    target = str::path_join(base,
                                            str::to_wide("GameLoopTunerSetup-") +
                                                str::to_wide(update_.tag) + L".exe");
                }
            }
            if (target.empty()) {
                throw update::Error("could not resolve a download path for the update");
            }

            update::Release rel;
            rel.tag = update_.tag;
            rel.asset_url = update_.asset_url;
            rel.sha256 = update_.sha256;
            rel.asset_size = update_.size;
            rel.kind = update_.kind;

            log_group("Download " + rel.tag);
            std::uint64_t last_posted = 0;
            const std::string hex = update::download_verified(
                rel, target, [this, &last_posted](std::uint64_t done, std::uint64_t total) {
                    // Throttle queue posts: the frame drain runs at most ~60/s
                    // and a 1.7 MB download on fast fibre would otherwise post
                    // hundreds of no-op updates.
                    if (done - last_posted < 262144 && (total == 0 || done < total)) return;
                    last_posted = done;
                    post([this, done, total] {
                        update_.downloaded = done;
                        if (total > 0) update_.size = total;
                    });
                });

            post([this, target, hex] {
                update_.downloaded = update_.size;
                update_.setup_path = target;
                update_.downloaded_ok = true;
                update_.busy = false;
                log("Downloaded and verified (sha256 " + hex.substr(0, 16) + "...).",
                    log::Level::Success);

                char prompt[256];
                std::snprintf(prompt, sizeof(prompt),
                              update_.kind == glt::update::Kind::Installer
                                  ? tr("Install %s now? The installer closes the tuner and "
                                       "replaces the current installation.")
                                  : tr("Install %s now? The tuner replaces itself with the "
                                       "verified update and restarts."),
                              update_.tag.c_str());
                ask(prompt, [this](bool yes) {
                    if (!yes) {
                        log("Install postponed; the verified update stays downloaded "
                            "and can be installed from the About page.",
                            log::Level::Info);
                        return;
                    }
                    std::string err;
                    if (update_.kind == glt::update::Kind::Installer) {
                        if (update::launch_installer(update_.setup_path, err)) {
                            log("Installer launched; closing the tuner.", log::Level::Info);
                            request_exit();
                        } else {
                            log("Update not installed: " + err, log::Level::Warning);
                        }
                    } else {
                        if (update::self_replace(update_.setup_path, err)) {
                            log("Update installed; restarting the tuner.", log::Level::Info);
                            request_exit();
                        } else {
                            log("Update not installed: " + err, log::Level::Warning);
                        }
                    }
                });
            });
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            post([this, msg] {
                update_.busy = false;
                update_.error = msg;
                log("Update download failed: " + msg, log::Level::Error);
            });
        }
    });
}

// ── Modal ────────────────────────────────────────────────────────────────────

void App::ask(std::string question, std::function<void(bool)> on_answer) {
    {
        std::scoped_lock lock(prompt_mutex_);
        prompt_ = std::move(question);
        prompt_action_ = std::move(on_answer);
    }
    prompt_pending_.store(true);
}

void App::answer_prompt(bool yes) {
    if (!prompt_pending_.exchange(false)) return;

    std::function<void(bool)> action;
    {
        std::scoped_lock lock(prompt_mutex_);
        action = std::move(prompt_action_);
        prompt_action_ = nullptr;
    }
    if (action) action(yes);
}

// ── Apply ────────────────────────────────────────────────────────────────────

void App::apply_all() {
    if (busy()) return;

    // ── Pre-flight gate ─────────────────────────────────────────────────────
    // A GameLoop update can silently change the schema the fixes were reverse-
    // engineered against. The preflight converts that failure mode — writes
    // that succeed but mean nothing — into a loud refusal before the first
    // backup is even taken. Read-only, a few ms.
    {
        const auto pf = preflight::run();
        if (pf.overall == preflight::Status::Fail) {
            log_group("Pre-flight check FAILED");
            for (const auto& c : pf.checks) {
                if (c.status == preflight::Status::Fail) {
                    log("  " + c.name + ": " + c.detail, log::Level::Error);
                } else if (c.status == preflight::Status::Warn) {
                    log("  " + c.name + ": " + c.detail, log::Level::Warning);
                }
            }
            log("Apply aborted - the GameLoop install does not match the schema the "
                "fixes expect. Re-apply after the next tuner release, and attach the "
                "diagnostic report if it persists.",
                log::Level::Error);
            toast(tr("GameLoop changed too much to apply fixes safely"),
                  log::Level::Error);
            show_page(Page::Logs);
            return;
        }
        if (pf.overall == preflight::Status::Warn) {
            for (const auto& c : pf.checks) {
                if (c.status == preflight::Status::Warn) {
                    log("Pre-flight warning - " + c.name + ": " + c.detail,
                        log::Level::Warning);
                }
            }
        }
    }

    std::vector<fixes::Key> keys = pending_;
    const bool alloc = alloc_pending_;
    const bool clarity = clarity_pending_;
    const int cores = alloc_cores_;
    const int mem = alloc_mem_gb_;
    const int level = clarity_level_;

    if (keys.empty() && !alloc && !clarity) {
        log("Nothing to apply. Tick fixes on the pages first.", log::Level::Warning);
        show_page(Page::Logs);
        return;
    }

    std::vector<fixes::Key> closable;
    for (fixes::Key k : keys) {
        if (k != fixes::Key::Priority && k != fixes::Key::HostsBlock &&
            k != fixes::Key::HostsUnblock) {
            closable.push_back(k);
        }
    }

    const auto live = procs::running();

    if (!live.empty() && !closable.empty()) {
        // DB and smk.conf writes need GameLoop closed. Ask first — a user may
        // be mid-match and want to finish rather than be closed out.
        char prompt[256];
        std::snprintf(prompt, sizeof(prompt),
                      tr("GameLoop is running (%d processes).\n\nDB and smk.conf "
                         "writes need it closed.\n\nClose it now?"),
                      static_cast<int>(live.size()));
        ask(prompt,
            [this, keys = std::move(keys), alloc, cores, mem, clarity, level](bool yes) {
                if (!yes) {
                    log("Apply cancelled - close GameLoop first.", log::Level::Warning);
                    busy_.store(false);
                    return;
                }
                run_apply(keys, alloc, cores, mem, clarity, level);
            });
        return;
    }

    run_apply(std::move(keys), alloc, cores, mem, clarity, level);
}

void App::run_apply(std::vector<fixes::Key> keys, bool alloc, int cores, int mem_gb,
                    bool clarity, int clarity_level) {
    busy_.store(true);
    show_page(Page::Logs);
    log_group(dry_run_ ? "Apply All (Dry-run)" : "Apply All");

    workers_.emplace_back(
        [this, keys = std::move(keys), alloc, cores, mem_gb, clarity, clarity_level] {
            const std::wstring dbp = paths::appdata_db();
            const std::wstring jpath = journal::path_for(dbp);
            const bool dry = dry_run_;

            int success = 0;

            std::vector<fixes::Key> closable;
            for (fixes::Key k : keys) {
                if (k != fixes::Key::Priority && k != fixes::Key::HostsBlock &&
                    k != fixes::Key::HostsUnblock) {
                    closable.push_back(k);
                }
            }

            if (!closable.empty() && !procs::running().empty()) {
                post([this] { log("Closing GameLoop before applying fixes...", log::Level::Info); });
                procs::CloseOptions copts;
                copts.grace_wait_s = 3;
                copts.kill_wait_ms = 2000;
                copts.force = true;
                const auto cres = procs::close_gameloop(copts);
                post([this, notes = cres.notes] {
                    log_many(notes, log::Level::Info);
                });
                if (!cres.ok) {
                    post([this] {
                        log("Apply aborted: GameLoop could not be closed. Close it manually before applying fixes.",
                            log::Level::Error);
                        busy_.store(false);
                    });
                    return;
                }
                post([this] { log("GameLoop closed cleanly.", log::Level::Success); });
            }

            for (fixes::Key k : keys) {
                const std::string name(fixes::key_name(k));
                try {
                    std::vector<std::string> notes;
                    switch (k) {
                        case fixes::Key::FullChain:    notes = fixes::full_chain(dry); break;
                        case fixes::Key::Pin:          notes = fixes::pin_flat(dry); break;
                        case fixes::Key::FlatDmz:      notes = fixes::flatten_dmz(dry); break;
                        case fixes::Key::FlatBr:       notes = fixes::flatten_br(dry); break;
                        case fixes::Key::Blind:        notes = fixes::blind_tex(dry); break;
                        case fixes::Key::Scrub:        notes = fixes::scrub_recent(dry); break;
                        case fixes::Key::Quarantine:   notes = fixes::quarantine(dry); break;
                        case fixes::Key::Unquarantine: notes = fixes::unquarantine(dry); break;
                        case fixes::Key::HostsBlock:   notes = fixes::hosts_block(dry); break;
                        case fixes::Key::HostsUnblock: notes = fixes::hosts_unblock(dry); break;
                        case fixes::Key::Latency:      notes = fixes::set_latency(dry); break;
                        case fixes::Key::NoFrameGen:   notes = fixes::set_no_frame_gen(dry); break;
                        case fixes::Key::Priority:     notes = fixes::set_priorities(dry); break;
                    }

                    std::vector<std::string> indented;
                    indented.reserve(notes.size());
                    for (const auto& n : notes) indented.push_back("  " + n);

                    post([this, name, indented] {
                        log("[" + name + "] applied successfully", log::Level::Success);
                        log_many(indented, log::Level::Info);
                    });

                    if (!dry && !jpath.empty() && !dbp.empty()) {
                        (void)journal::record(jpath, name,
                                        "{\"backup\": \"see Configs.db.bak-* in the db dir\"}");
                    }
                    ++success;
                } catch (const std::exception& e) {
                    post([this, name, msg = std::string(e.what())] {
                        log("[" + name + "] FAILED: " + msg, log::Level::Error);
                    });
                }
            }

            // Allocation is not a Configs.db write — it goes to the registry,
            // so it is journalled with its previous values instead of a backup
            // pointer.
            if (alloc) {
                try {
                    auto notes = fixes::apply_alloc(cores, mem_gb, dry);
                    post([this, notes] {
                        log("[alloc] applied", log::Level::Success);
                        log_many(notes, log::Level::Info);
                    });
                    if (!dry && !jpath.empty()) {
                        const auto before = reg::perf_allocation();
                        (void)journal::record(
                            jpath, "alloc",
                            "{\"cores\": " +
                                std::string(before.cpu_cores ? std::to_string(*before.cpu_cores)
                                                              : "null") +
                                ", \"mem_gb\": " +
                                std::string(before.memory_gb ? std::to_string(*before.memory_gb)
                                                              : "null") +
                                "}");
                    }
                    ++success;
                } catch (const std::exception& e) {
                    post([this, msg = std::string(e.what())] {
                        log("[alloc] FAILED: " + msg, log::Level::Error);
                    });
                }
            }

            if (clarity) {
                try {
                    auto notes = fixes::set_clarity(clarity_level, dry);
                    post([this, notes] {
                        log("[clarity] applied", log::Level::Success);
                        log_many(notes, log::Level::Info);
                    });
                    ++success;
                } catch (const std::exception& e) {
                    post([this, msg = std::string(e.what())] {
                        log("[clarity] FAILED: " + msg, log::Level::Error);
                    });
                }
            }

            post([this, success, dry] {
                log("Finished applying " + std::to_string(success) + " fixes. Restart GameLoop.",
                    log::Level::Progress);
                if (!dry) {
                    // Move successfully applied fixes from pending to applied,
                    // so they show as checked but won't be re-run on the next
                    // Apply All unless the user explicitly toggles them.
                    for (fixes::Key k : pending_) {
                        if (std::find(applied_.begin(), applied_.end(), k) == applied_.end())
                            applied_.push_back(k);
                    }
                    pending_.clear();
                }

                // Re-baseline so the drift monitor compares against what we
                // just wrote rather than what was there before.
                if (!dry && drift_) {
                    drift_->set_baseline();
                    drift_state_ = DriftState::Stable;
                }

                refresh_dashboard();
                toast("Fixes applied successfully!", log::Level::Success);
                busy_.store(false);
            });
        });
}

// ── Close GameLoop ───────────────────────────────────────────────────────────

void App::close_gameloop() {
    if (busy()) return;

    if (procs::running().empty()) {
        log("GameLoop is not running.", log::Level::Info);
        toast("GameLoop is not running.", log::Level::Info);
        return;
    }

    busy_.store(true);
    show_page(Page::Logs);
    log_group("Close GameLoop");

    workers_.emplace_back([this] {
        procs::CloseOptions opts;
        opts.grace_wait_s = 6;
        opts.kill_wait_ms = 3000;
        opts.force = true;
        // The service is deliberately NOT stopped here. GameLoop auto-starts
        // it on the next launch, so stopping it changes machine state beyond
        // the current session. The dedicated force path in the sidebar is the
        // only place that opts in.
        opts.stop_service_now = false;

        const auto res = procs::close_gameloop(opts);
        post([this, res] {
            log_many(res.notes, log::Level::Info);
            if (res.ok) {
                log("GameLoop closed.", log::Level::Success);
                toast("GameLoop closed cleanly.", log::Level::Success);
            } else {
                std::string names;
                for (const auto& p : res.left) {
                    if (!names.empty()) names += ", ";
                    names += str::to_utf8(p.name) + "(" + std::to_string(p.pid) + ")";
                }
                log("Still running: " + names +
                        ". Right-click the GameLoop tray icon -> Quit, or force-stop the "
                        "service.",
                    log::Level::Error);
                toast("Could not terminate all processes", log::Level::Error);
            }
            busy_.store(false);
            refresh_dashboard();
        });
    });
}

void App::launch_gameloop() {
    const std::wstring exe = paths::launcher_exe();
    if (exe.empty()) {
        log("GameLoop launcher executable not found in install paths.", log::Level::Error);
        toast("GameLoop executable not found", log::Level::Error);
        return;
    }

    HINSTANCE res = ::ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(res) > 32) {
        log("Launched GameLoop: " + str::to_utf8(exe), log::Level::Success);
        toast("GameLoop launched successfully!", log::Level::Success);
    } else {
        log("Failed to launch GameLoop (error code " + std::to_string(reinterpret_cast<INT_PTR>(res)) + ")",
            log::Level::Error);
        toast("Failed to launch GameLoop", log::Level::Error);
    }
}

void App::toast(std::string msg, log::Level level) {
    post([this, m = std::move(msg), level] {
        toast_.message = std::move(m);
        toast_.level = level;
        toast_.expires_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(3500);
        toast_.active = true;
    });
}

void App::revert_all() {
    show_page(Page::Backups);
    log("To revert changes, restore the latest backup from the list.", log::Level::Info);
}

void App::export_report() {
    if (busy()) return;
    busy_.store(true);
    log_group("Diagnostic report");
    workers_.emplace_back([this] {
        const std::string path = report::export_to_desktop();
        post([this, path] {
            if (path.empty()) {
                log("Report export failed.", log::Level::Error);
            } else {
                log("Report written: " + path, log::Level::Success);
            }
            busy_.store(false);
        });
    });
}

// ── Drift ────────────────────────────────────────────────────────────────────

void App::pump_drift() {
    if (!drift_) return;

    auto reports = drift_->drain_pending();
    for (auto& drifts : reports) {
        drift_reports_.push_back(std::move(drifts));
    }
    if (drift_reports_.size() > 16) {
        drift_reports_.erase(drift_reports_.begin(), drift_reports_.end() - 16);
    }

    if (reports.empty()) return;

    drift_state_ = DriftState::Drifted;
    log_group("Drift Detected");

    bool tex_drift = false;
    for (const auto& report : reports) {
        for (const auto& item : report) {
            log(drift::format(item), log::Level::Drift);
            if (item.key == "file_tex_clean" || item.key == "smk_readonly") tex_drift = true;
        }
    }

    if (tex_drift) {
        log("The engine restored a jump-trigger template or unlocked smk.conf (local "
            "template pack / app update). Re-run the recommended fix to re-blind and "
            "re-lock; the landing content (flat blobs) still holds.",
            log::Level::Warning);
    } else {
        log("Re-apply: quit GameLoop (tray -> Quit), tick the recommended fix, Apply All.",
            log::Level::Warning);
    }

    // Offer to re-apply automatically. The user can decline and do it manually.
    ask("GameLoop reverted a setting (drift detected).\n\nRe-apply the recommended "
        "fix now? GameLoop will be closed if needed.",
        [this](bool yes) {
            if (!yes) {
                log("Drift re-apply declined.", log::Level::Warning);
                return;
            }
            if (pending_.empty()) {
                // Nothing ticked - tick the recommended fullchain.
                pending_.push_back(fixes::Key::FullChain);
            }
            apply_all();
        });
}

} // namespace glt::ui