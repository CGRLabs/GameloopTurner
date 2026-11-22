// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/app.hpp — application state, the pending queue and Apply All.
//
// Ported from legacy-python/src/main.py, minus the Tkinter.
//
// THREADING CONTRACT
// ------------------
// ImGui is not thread-safe. Every worker (Apply All, the close ladder, the
// dashboard gather, the drift monitor) runs on its own thread and may only
// touch App through post(). That queue is drained once per frame on the render
// thread, which is the native equivalent of Tkinter's `self.after(0, ...)`.
//
// Nothing else in this class is allowed to be called from a worker thread.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "glt/core/backups.hpp"
#include "glt/core/bundle.hpp"
#include "glt/core/netinfo.hpp"
#include "glt/core/disk.hpp"
#include "glt/core/drift.hpp"
#include "glt/core/fixes.hpp"
#include "glt/core/i18n.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/log.hpp"
#include "glt/core/preflight.hpp"
#include "glt/core/procs.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/update.hpp"
#include "glt/core/variant.hpp"

namespace glt::ui {

// ── Navigation ───────────────────────────────────────────────────────────────

enum class Page {
    Dashboard,
    Keymaps,
    Performance,
    VmPriority,
    Network,
    Backups,
    BackupBundle,
    Logs,
    About,
};

[[nodiscard]] const char* page_title(Page p);

// ── Drift badge ──────────────────────────────────────────────────────────────

enum class DriftState {
    NoBaseline, // no Configs.db, or the user has not applied anything yet
    Stable,     // green — everything matches the baseline
    Drifted,    // amber — GameLoop reverted something
};

[[nodiscard]] std::string drift_text(DriftState s);
[[nodiscard]] bool drift_is_warn(DriftState s);

// ── Log console model ────────────────────────────────────────────────────────

struct LogEntry {
    std::string timestamp; // HH:MM:SS
    std::string text;
    log::Level level = log::Level::Info;
    bool is_group_header = false;
};

// ── App ──────────────────────────────────────────────────────────────────────

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void startup();   // detect_applied + baseline + start the drift monitor
    void shutdown();  // stop threads, flush the queue

    // ── Variant (first-launch selector) ────────────────────────────────────
    // The compiled variant governs behaviour; the persisted choice is the
    // user's record of which GameLoop they have. See glt/core/variant.hpp.
    [[nodiscard]] bool variant_picker_open() const { return variant_picker_open_; }
    [[nodiscard]] bool variant_chosen() const { return variant_chosen_; }
    [[nodiscard]] variant::Id chosen_variant() const { return chosen_variant_; }
    [[nodiscard]] variant::Id compiled_variant() const { return variant::compiled(); }

    // Records + persists the choice and closes the picker.
    void choose_variant(variant::Id id);
    // Re-opens the picker (About page).
    void open_variant_picker();
    // Forgets the persisted choice and re-opens the picker.
    void clear_variant_choice();

    // ── Language (i18n) ──────────────────────────────────────────────────
    [[nodiscard]] std::string_view language() const { return language_; }
    [[nodiscard]] bool language_is_automatic() const { return language_auto_; }
    // "" or "auto" switches back to follow the Windows UI language; a concrete
    // code pins that language. Unknown codes are logged and ignored.
    void choose_language(std::string_view code);

    // ── Thread-safe UI queue ───────────────────────────────────────────────
    // Callable from any thread. Runs on the render thread during drain().
    void post(std::function<void()> fn);
    void drain();     // called once per frame, render thread only

    // ── Logging ────────────────────────────────────────────────────────────
    void log(std::string msg, log::Level level = log::Level::Info);
    void log_many(const std::vector<std::string>& msgs, log::Level level = log::Level::Info);
    void log_group(const std::string& title);
    void clear_log();

    [[nodiscard]] const std::vector<LogEntry>& log_entries() const { return log_entries_; }
    [[nodiscard]] bool log_auto_scroll() const { return log_auto_scroll_; }
    void set_log_auto_scroll(bool v) { log_auto_scroll_ = v; }

    // ── Navigation ─────────────────────────────────────────────────────────
    void show_page(Page p);
    [[nodiscard]] Page page() const { return page_; }

    // ── Pending queue ──────────────────────────────────────────────────────
    void toggle_pending(fixes::Key k, bool on);
    void set_alloc_pending(int cores, int mem_gb, bool on);
    void set_clarity_pending(int level, bool on);
    void clear_pending();

    [[nodiscard]] bool is_pending(fixes::Key k) const;
    [[nodiscard]] bool alloc_pending() const { return alloc_pending_; }
    [[nodiscard]] int alloc_cores() const { return alloc_cores_; }
    [[nodiscard]] int alloc_mem_gb() const { return alloc_mem_gb_; }
    [[nodiscard]] bool clarity_pending() const { return clarity_pending_; }
    [[nodiscard]] int clarity_level() const { return clarity_level_; }

    // Count shown on the Apply All button. Fixes plus at most two extras
    // (allocation, clarity), matching the Python button.
    [[nodiscard]] int pending_count() const;

    // ── Dry run ────────────────────────────────────────────────────────────
    [[nodiscard]] bool dry_run() const { return dry_run_; }
    void set_dry_run(bool v) { dry_run_ = v; }

    // ── Actions (all return immediately; progress streams to the log) ───────
    void apply_all();
    void close_gameloop();
    void launch_gameloop();
    void revert_all();
    void export_report();

    // ── Self-update (GitHub Releases) ────────────────────────────────────
    // check_update runs on a worker; results arrive through post(). A manual
    // check reports failures loudly, the automatic startup check logs them at
    // Info because offline is a normal state.
    struct UpdateState {
        bool checked = false;        // a completed check exists
        bool available = false;      // a newer release is published
        bool busy = false;           // check or download running
        bool downloaded_ok = false;  // asset on disk, digest verified
        glt::update::Kind kind = glt::update::Kind::None;
        std::string error;           // last failure, shown on the About card
        std::string tag;             // "v2.1.0"
        std::string page_url;        // release page
        std::string asset_url;
        std::string notes;
        std::string sha256;
        std::uint64_t size = 0;
        std::uint64_t downloaded = 0;
        std::wstring setup_path;
    };
    void check_update(bool manual);
    // Downloads (if needed) and, after a confirm prompt, launches the setup
    // and requests app exit.
    void install_update();
    [[nodiscard]] const UpdateState& update() const { return update_; }

    // Set by install_update after the installer is launched; the frame loop
    // checks it so teardown runs through the normal shutdown path.
    void request_exit() { exit_requested_.store(true); }
    [[nodiscard]] bool exit_requested() const { return exit_requested_.load(); }

    // ── Toast notifications ────────────────────────────────────────────────
    struct Toast {
        std::string message;
        log::Level level = log::Level::Success;
        std::chrono::steady_clock::time_point expires_at{};
        bool active = false;
    };

    void toast(std::string msg, log::Level level = log::Level::Success);
    [[nodiscard]] const Toast& current_toast() const { return toast_; }
    void dismiss_toast() { toast_.active = false; }

    // ── Busy state ─────────────────────────────────────────────────────────
    // Disables Apply All / Close GameLoop while a worker is running, so the
    // user cannot queue a second apply on top of the first.
    [[nodiscard]] bool busy() const { return busy_.load(); }

    // ── Drift ──────────────────────────────────────────────────────────────
    [[nodiscard]] DriftState drift_state() const { return drift_state_; }
    void set_drift_state(DriftState s) { drift_state_ = s; }
    [[nodiscard]] const std::vector<std::vector<drift::DriftItem>>& drift_reports() const {
        return drift_reports_;
    }

    // ── Network / servers (captured from the guest via ADB) ───────────────
    using NetConn = glt::core::netinfo::Connection;
    struct NetworkInfo {
        std::vector<NetConn> conns;
        std::string error;   // non-empty on failure
        bool busy = false;
        std::chrono::steady_clock::time_point last_refresh{};
    };
    void refresh_network();
    [[nodiscard]] const NetworkInfo& network() const { return network_; }
    [[nodiscard]] NetworkInfo& network_mut() { return network_; }

    // ── Host resources (cached; the sliders need them every frame) ─────────
    [[nodiscard]] const procs::HostResources& host() const { return host_; }
    [[nodiscard]] int max_cores() const;
    [[nodiscard]] int max_mem_gb() const;

    // Refreshes host_ off the render thread. Cheap, but not free.
    void refresh_host();

    // ── Dashboard data ─────────────────────────────────────────────────────
    struct Dashboard {
        std::vector<procs::ProcInfo> procs;
        procs::ServiceStatus service;
        reg::PerfAllocation alloc;
        std::wstring install_root;
        std::wstring db_path;
        std::wstring build_dir;
        std::string db_error;
        // Install-shape verdict from glt/core/preflight (nullopt when the DB is
        // missing entirely — the Dashboard already says why).
        std::optional<preflight::Report> preflight;
        // keymap
        std::optional<std::string> lastmodeid;
        std::optional<std::string> mode145_solution;
        std::optional<std::string> recentlyused;
        std::vector<keymaps::Solution> solutions;
        std::vector<std::string> jump_tags;
        std::vector<std::string> triggers;
        bool tex_10004 = false;
        bool tex_10046 = false;
        // codm
        std::string local_user_config; // raw JSON
        std::string host_config;      // raw JSON
        disk::DriveStatus drive;
    };
    void refresh_dashboard();
    [[nodiscard]] const Dashboard& dashboard() const { return dash_; }

    // ── Backups page ───────────────────────────────────────────────────────
    void refresh_backups();
    [[nodiscard]] const std::vector<backups::BackupEntry>& backups() const { return backups_; }
    void restore_backup(std::size_t index);

    // ── Backup Bundle (.gltbak) ────────────────────────────────────────────
    // Captures the whole GameLoop/CODM install and writes a .gltbak file to
    // `save_path` (user-chosen via file picker). Runs off the render thread.
    void create_bundle(std::wstring save_path, std::string title);

    // Reads a .gltbak file into memory for preview. Returns true on success.
    bool open_bundle(std::wstring path);

    // Applies the currently opened bundle to the live system (or dry-run).
    void apply_bundle(bool dry_run = false);

    // Clears the in-memory bundle and its diffs.
    void clear_bundle();

    // ── CLI (headless) support ─────────────────────────────────────────────
    // Synchronous bundle operations for agent/script use. Each returns a
    // human-readable result string suitable for stdout or JSON logging.
    //
    // create_bundle_sync() captures all settings and writes a .gltbak file.
    [[nodiscard]] std::string create_bundle_sync(std::wstring save_path,
                                                  std::string title);

    // apply_bundle_sync() applies the currently loaded bundle (or dry-run).
    // Returns per-section notes including version mismatches and auto-backup path.
    [[nodiscard]] std::string apply_bundle_sync(bool dry_run = false);

    // list_backups_sync() returns a newline-separated list of existing backups.
    [[nodiscard]] std::string list_backups_sync();

    // Per-section apply toggles (mirrors the UI checkboxes).
    [[nodiscard]] const bundle::ApplyOptions& bundle_options() const { return bundle_options_; }
    void set_apply_configs_db(bool v) { bundle_options_.configs_db = v; }
    void set_apply_smk_conf(bool v) { bundle_options_.smk_conf = v; }
    void set_apply_registry(bool v) { bundle_options_.registry = v; }

    [[nodiscard]] const std::optional<bundle::Bundle>& current_bundle() const { return bundle_; }
    [[nodiscard]] const std::vector<bundle::FieldDiff>& bundle_diffs() const { return bundle_diffs_; }
    [[nodiscard]] bool bundle_loaded() const { return bundle_.has_value(); }

    // ── Modal ──────────────────────────────────────────────────────────────
    // A confirmation a worker needs an answer to before it can continue.
    // The question and its callback are set from a worker and consumed by the
    // render thread, which is the only place ImGui may be touched.
    //
    // ask() is safe from any thread. The callback ALWAYS runs on the render
    // thread during drain(), so it is the correct place to touch App state.
    void ask(std::string question, std::function<void(bool)> on_answer);

    [[nodiscard]] bool has_pending_prompt() const { return prompt_pending_.load(); }
    [[nodiscard]] const std::string& pending_prompt() const { return prompt_; }

    // Invokes the stored callback with the user's answer. Render thread only.
    void answer_prompt(bool yes);

private:
    void run_apply(std::vector<fixes::Key> keys, bool alloc, int cores, int mem_gb, bool clarity,
                   int clarity_level);

    // ── UI-thread state ────────────────────────────────────────────────────
    std::mutex queue_mutex_;
    std::deque<std::function<void()>> queue_;

    std::vector<LogEntry> log_entries_;
    std::mutex log_mutex_;
    bool log_auto_scroll_ = true;
    static constexpr std::size_t kMaxLogEntries = 2000;

    Page page_ = Page::Dashboard;

    std::vector<fixes::Key> pending_;   // queued for next Apply All
    std::vector<fixes::Key> applied_;   // detected as already applied (display only)
    bool alloc_pending_ = false;
    int alloc_cores_ = 4;
    int alloc_mem_gb_ = 8;
    bool clarity_pending_ = false;
    int clarity_level_ = 2;

    bool dry_run_ = false;
    std::atomic<bool> busy_{false};

    DriftState drift_state_ = DriftState::NoBaseline;
    std::vector<std::vector<drift::DriftItem>> drift_reports_;

    // Variant selection. chosen_variant_ mirrors the persisted choice; the
    // compiled variant is read from the build macro via variant::compiled().
    variant::Id chosen_variant_ = variant::Id::Beta;
    bool variant_chosen_ = false;
    bool variant_picker_open_ = false;

    // Active UI language (glt/core/i18n.hpp). language_auto_ distinguishes "the
    // user pinned this language" from "it follows Windows" — the About card
    // needs exactly that to highlight the right chip.
    std::string language_ = "en";
    bool language_auto_ = true;

    UpdateState update_;
    std::atomic<bool> exit_requested_{false};

    procs::HostResources host_;
    Dashboard dash_;
    NetworkInfo network_;
    std::vector<backups::BackupEntry> backups_;

    // Backup Bundle state (in-memory .gltbak being previewed).
    std::optional<bundle::Bundle> bundle_;
    std::vector<bundle::FieldDiff> bundle_diffs_;
    std::wstring bundle_path_;
    bundle::ApplyOptions bundle_options_;

    // Prompt plumbing. prompt_ and prompt_action_ are written under
    // prompt_mutex_ and read on the render thread.
    mutable std::mutex prompt_mutex_;
    std::string prompt_;
    std::function<void(bool)> prompt_action_;
    std::atomic<bool> prompt_pending_{false};

    std::unique_ptr<drift::DriftMonitor> drift_;

    // Workers. std::jthread joins in the destructor, so a crashing apply
    // cannot outlive the App.
    std::vector<std::jthread> workers_;
    Toast toast_;

public:
    // Drains drift reports the monitor thread queued. Called from draw().
    void pump_drift();
};

} // namespace glt::ui