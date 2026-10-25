// ─────────────────────────────────────────────────────────────────────────────
// glt/core/drift.hpp — detect GameLoop silently reverting our settings.
//
// Ported from legacy-python/src/core/drift.py.
//
// WHAT "DRIFT" MEANS HERE
// -----------------------
// GameLoop re-reads its config on every session start and rewrites whatever
// disagrees with its own idea of the truth. The engine can revert a setting
// minutes after we wrote it, with no error and no dialog. The only way a user
// finds out is that the auto-switch is back.
//
// This monitor snapshots the values we care about, and polls every 30 s.
// Any divergence from the baseline is surfaced in the sidebar as
// "Drifted", with a per-key explanation in the Logs page.
//
// WHAT IS DELIBERATELY *NOT* TRACKED
// -----------------------------------
// Pointer noise — recentlyused_mode timestamps, and which solution inside
// mode 145 is currently selected. The engine flips those constantly in
// memory, and since every landing layout is flattened to 0 rules, the VISIBLE
// keymap never changes when they do. Tracking them produced a drift alert on
// virtually every poll, which trains the user to ignore the alert. That is
// strictly worse than not having one.
//
// The eleven tracked keys below are the complete authoritative set.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace glt::drift {

// ── Snapshot values ──────────────────────────────────────────────────────────
// A value can be absent (the source itself is missing), typed, or unparseable.
// Absent values are skipped during comparison so a missing source never
// registers as a revert.
using SnapshotValue = std::variant<std::monostate, std::int64_t, bool, std::string>;

struct Snapshot {
    std::unordered_map<std::string, SnapshotValue> values;

    [[nodiscard]] bool has(const std::string& key) const;
    [[nodiscard]] const SnapshotValue* get(const std::string& key) const;
    void set(std::string key, SnapshotValue v);
};

// ── The tracked keys, in display order ───────────────────────────────────────
inline constexpr std::string_view kKeys[] = {
    "cores",                  // registry VM core allocation
    "mem_gb",                 // registry VM RAM allocation
    "lastmodeid",             // active keymap mode must stay 145
    "flat_145_all",           // every mode-145 custom layout is 0 rules
    "flat_146",               // BR landing blob (146/146) is 0 rules
    "flat_531686",            // DMZ landing blob (531686/1063553) is 0 rules
    "clarity",                // perf tier
    "low_latency_mode",       // perf flag
    "default_frame_gen_on",   // perf flag
    "file_tex_clean",         // smk.conf has no jump-trigger templates
    "smk_readonly",           // smk.conf read-only lock holds
};

// Human-readable label for a tracked key. Unknown keys return the key itself.
[[nodiscard]] std::string label(std::string_view key);

// ── Drift item ───────────────────────────────────────────────────────────────

struct DriftItem {
    std::string key;
    std::string expected; // rendered
    std::string actual;   // rendered
};

// "Mode 145 layouts flat (0 rules) changed: was true -> now false"
[[nodiscard]] std::string format(const DriftItem& item);

// ── Monitor ──────────────────────────────────────────────────────────────────

// Thread-safe. Owns a std::jthread that polls snapshot() on an interval.
// The callback runs ON THE MONITOR THREAD — it must marshal to the UI queue
// itself. ImGui is not thread-safe and touching it from here would corrupt
// the context.
class DriftMonitor {
public:
    using Callback = std::function<void(const std::vector<DriftItem>&)>;

    explicit DriftMonitor(Callback on_drift = {});
    ~DriftMonitor();

    DriftMonitor(const DriftMonitor&) = delete;
    DriftMonitor& operator=(const DriftMonitor&) = delete;

    // Reads the current values. Never throws — a missing Configs.db simply
    // yields an empty snapshot, which compares as "no change".
    [[nodiscard]] Snapshot snapshot() const;

    // Captures the baseline. Call right after an Apply All succeeds.
    void set_baseline(const Snapshot& snap = {});
    void clear_baseline();
    [[nodiscard]] bool has_baseline() const;

    // Diffs live state against the baseline. Empty when no baseline is set.
    [[nodiscard]] std::vector<DriftItem> check() const;

    // Starts/stops the polling thread. start() is a no-op if already running.
    void start(int interval_s = 30);
    void stop();

    [[nodiscard]] bool running() const { return running_.load(); }

    // Latches a pending drift report for the UI to drain on its own thread.
    // This is the thread-safe handoff the callback uses; the UI polls it once
    // per frame. Returns the reports since the last drain.
    [[nodiscard]] std::vector<std::vector<DriftItem>> drain_pending();

    // Peak concurrency guard for tests.
    static constexpr int kDefaultIntervalS = 30;

private:
    void loop(std::stop_token stop);

    Callback on_drift_;
    mutable std::mutex mutex_;
    Snapshot baseline_;

    std::jthread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex pending_mutex_;
    std::vector<std::vector<DriftItem>> pending_;
};

} // namespace glt::drift