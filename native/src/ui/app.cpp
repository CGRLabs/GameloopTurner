#include "glt/ui/app.hpp"

#include "glt/core/db.hpp"
#include "glt/core/journal.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/netinfo.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/report.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>
#include <fstream>
#include <windows.h>
#include <shellapi.h>

namespace glt::ui {

const char* page_title(Page p) {
    switch (p) {
        case Page::Dashboard:    return "Dashboard";
        case Page::Keymaps:      return "Keymaps";
        case Page::Performance:  return "Performance";
        case Page::VmPriority:   return "VM & Priority";
        case Page::Network:      return "Network & Servers";
        case Page::Backups:      return "Backups";
        case Page::Logs:         return "Logs";
        case Page::About:        return "About";
    }
    return "?";
}

const char* drift_text(DriftState s) {
    switch (s) {
        case DriftState::NoBaseline: return "No baseline";
        case DriftState::Stable:     return "\xE2\x9A\x99 Stable";       // ● Stable
        case DriftState::Drifted:    return "\xE2\x9A\xA0 Drifted";     // ⚠ Drifted
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
            toast("Welcome to v" + std::string(report::kVersion), log::Level::Success);
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

    ask("Restore " + str::to_utf8(item.filename) +
            "?\n\nGameLoop must be closed first. The current file is overwritten.",
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
        ask("GameLoop is running (" + std::to_string(live.size()) +
                " processes).\n\nDB and smk.conf writes need it closed.\n\nClose it now?",
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