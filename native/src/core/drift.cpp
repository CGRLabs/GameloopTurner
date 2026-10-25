#include "glt/core/drift.hpp"

#include "glt/core/db.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <type_traits>

namespace glt::drift {
namespace {

// The four Tex ids the blind fix removes from smk.conf.
// 10004 = lobby mailbox  -> SetUp1/TextureId=4    -> DMZ (mode 531686)
// 10046 = skill-select   -> SkillCircle/TextureId=46 -> BR (mode 146)
// Each has a 30xxx twin (30000 + TextureId) that the engine matches as a
// SECOND binding for the same switch. Verified: blinding 10046 alone did NOT
// stop BR — the live 30046 kept firing. Both series must go.
constexpr int kBlindTexIds[] = {10004, 10046, 30004, 30046};

bool contains(const std::vector<int>& hay, int needle) {
    for (int v : hay) {
        if (v == needle) return true;
    }
    return false;
}

// Renders a SnapshotValue the way the Logs page shows it.
std::string render(const SnapshotValue& v) {
    return std::visit(
        [](auto&& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "unset";
            else if constexpr (std::is_same_v<T, bool>) return x ? "ON" : "OFF";
            else if constexpr (std::is_same_v<T, std::int64_t>) return std::to_string(x);
            else return x;
        },
        v);
}

bool same(const SnapshotValue& a, const SnapshotValue& b) {
    // bool and int64 compare across types: a value read as INT 1 on one poll
    // and BOOL true on the next is the same state, not a revert.
    if (a.index() != b.index()) {
        const auto* ai = std::get_if<std::int64_t>(&a);
        const auto* bi = std::get_if<bool>(&b);
        if (ai && bi) return (*ai != 0) == *bi;
    }
    return a == b;
}

} // namespace

// ── Snapshot ─────────────────────────────────────────────────────────────────

bool Snapshot::has(const std::string& key) const { return values.count(key) > 0; }

const SnapshotValue* Snapshot::get(const std::string& key) const {
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
}

void Snapshot::set(std::string key, SnapshotValue v) { values[std::move(key)] = std::move(v); }

// ── Labels ───────────────────────────────────────────────────────────────────

std::string label(std::string_view key) {
    // drift.py::LABELS, carried over verbatim.
    static const std::unordered_map<std::string, std::string> kLabels = {
        {"cores", "VM CPU cores"},
        {"mem_gb", "VM RAM (GB)"},
        {"lastmodeid", "Active keymap mode"},
        {"flat_145_all", "Mode 145 layouts flat (0 rules)"},
        {"flat_146", "BR landing blob flat (146/146)"},
        {"flat_531686", "DMZ landing blob flat (531686/1063553)"},
        {"clarity", "Clarity tier"},
        {"low_latency_mode", "Low-latency mode"},
        {"default_frame_gen_on", "Frame generation"},
        {"file_tex_clean", "smk.conf jump triggers (10004/10046 + twins)"},
        {"smk_readonly", "smk.conf read-only lock"},
    };

    const auto it = kLabels.find(std::string(key));
    return it == kLabels.end() ? std::string(key) : it->second;
}

std::string format(const DriftItem& item) {
    return label(item.key) + " changed: was " + item.expected + " -> now " + item.actual;
}

// ── Monitor ──────────────────────────────────────────────────────────────────

DriftMonitor::DriftMonitor(Callback on_drift) : on_drift_(std::move(on_drift)) {}

DriftMonitor::~DriftMonitor() { stop(); }

Snapshot DriftMonitor::snapshot() const {
    Snapshot snap;

    // ── Registry allocation ───────────────────────────────────────────────
    // Outside the try: a registry read cannot fail in a way that matters, and
    // the allocation values are the cheapest signal to collect.
    try {
        const auto alloc = reg::perf_allocation();
        if (alloc.cpu_cores) snap.set("cores", *alloc.cpu_cores);
        if (alloc.memory_gb) snap.set("mem_gb", *alloc.memory_gb);
    } catch (...) {
        // Leave the keys absent — absent keys never report drift.
    }

    // ── Database ──────────────────────────────────────────────────────────
    const std::wstring dbp = paths::appdata_db();
    if (!dbp.empty()) {
        try {
            db::Connection con(dbp, 5000);

            // ── Keymap state ───────────────────────────────────────────────
            const auto dr = keymaps::drift_report(con, paths::kPkg);
            if (dr.lastmodeid) snap.set("lastmodeid", *dr.lastmodeid);

            // Flatness invariants. Note this checks the USER blobs
            // (keymap_data.unlogin.*): the official rows are trigger-guarded
            // and cannot drift, and the user blobs are what the engine
            // actually renders from after our writes.
            bool flat_145 = true, flat_146 = true, flat_531686 = true;
            try {
                for (const auto& s : keymaps::solutions(con, paths::kPkg)) {
                    if (s.mode == "145" && s.rule_count != 0) flat_145 = false;
                    if (s.mode == "146" && s.sol == "146" && s.rule_count != 0) {
                        flat_146 = false;
                    }
                    if (s.mode == "531686" && s.sol == "1063553" && s.rule_count != 0) {
                        flat_531686 = false;
                    }
                }
            } catch (...) {
                // A failed blob read means we cannot prove flatness. Leaving
                // the keys absent avoids a false "drifted" alert.
            }
            snap.set("flat_145_all", flat_145);
            snap.set("flat_146", flat_146);
            snap.set("flat_531686", flat_531686);

            // ── Perf flags ─────────────────────────────────────────────────
            try {
                const std::string uc = perf::local_user_config_raw(con, paths::kPkg);
                if (const auto c = perf::current_clarity(con, paths::kPkg); c >= 0) {
                    snap.set("clarity", static_cast<std::int64_t>(c));
                }
                auto flags = nlohmann::json::parse(uc);
                if (flags.is_object()) {
                    if (auto it = flags.find(std::string(perf::kLowLatency));
                        it != flags.end() && it->is_boolean()) {
                        snap.set("low_latency_mode", it->get<bool>());
                    }
                    if (auto it = flags.find(std::string(perf::kFrameGen));
                        it != flags.end() && it->is_boolean()) {
                        snap.set("default_frame_gen_on", it->get<bool>());
                    }
                }
            } catch (...) {
                // Same as above: unprovable state is absent, not "drifted".
            }
        } catch (const db::Error& e) {
            log::warn(std::string("drift snapshot: db unavailable: ") + e.what());
        }
    }

    // ── smk.conf FILE ──────────────────────────────────────────────────────
    // The FILE is the operative scene source. The DB row androws.<pkg>.smk is
    // a dormant mirror the engine re-fills at will and which causes no switch,
    // so it is deliberately not tracked — tracking it would produce an alert on
    // every single poll.
    const std::wstring smk = paths::smk_conf();
    if (!smk.empty()) {
        std::string text;
        if (str::read_file(smk, text)) {
            const auto ids = keymaps::tex_ids_in_text(text);
            bool clean = true;
            for (int id : kBlindTexIds) {
                if (contains(ids, id)) {
                    clean = false;
                    break;
                }
            }
            snap.set("file_tex_clean", clean);
            snap.set("smk_readonly", str::is_readonly(smk));
        }
    }

    return snap;
}

void DriftMonitor::set_baseline(const Snapshot& snap) {
    std::scoped_lock lock(mutex_);
    baseline_ = snap.values.empty() ? this->snapshot() : snap;
    log::info("drift baseline set (" + std::to_string(baseline_.values.size()) + " keys)");
}

void DriftMonitor::clear_baseline() {
    std::scoped_lock lock(mutex_);
    baseline_ = Snapshot{};
}

bool DriftMonitor::has_baseline() const {
    std::scoped_lock lock(mutex_);
    return !baseline_.values.empty();
}

std::vector<DriftItem> DriftMonitor::check() const {
    Snapshot baseline;
    {
        std::scoped_lock lock(mutex_);
        if (baseline_.values.empty()) return {};
        baseline = baseline_;
    }

    const Snapshot current = snapshot();

    std::vector<DriftItem> out;
    for (const auto& key : kKeys) {
        const std::string k(key);
        const auto* expected = baseline.get(k);
        if (!expected) continue; // never baselined this key

        const auto* actual = current.get(k);
        if (!actual) continue; // source unreadable right now — not a revert

        if (!same(*expected, *actual)) {
            out.push_back(DriftItem{k, render(*expected), render(*actual)});
        }
    }
    return out;
}

void DriftMonitor::start(int interval_s) {
    if (running_.exchange(true)) return; // already running

    if (interval_s < 1) interval_s = kDefaultIntervalS;

    // std::jthread's destructor joins, so stop() is just a token request and a
    // flag clear; the wake-up below makes the join fast instead of blocking
    // for a whole interval.
    thread_ = std::jthread([this, interval_s](std::stop_token st) { loop(st); });
    log::info("drift monitor started (interval=" + std::to_string(interval_s) + "s)");
}

void DriftMonitor::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    log::info("drift monitor stopped");
}

void DriftMonitor::loop(std::stop_token st) {
    const auto interval = std::chrono::seconds(kDefaultIntervalS);

    while (!st.stop_requested() && running_.load()) {
        // Interruptible sleep: std::jthread has no condition variable, so poll
        // in 250 ms slices. That bounds shutdown latency without keeping a
        // thread permanently alive.
        auto remaining = interval;
        while (remaining.count() > 0 && !st.stop_requested()) {
            const auto slice = (std::min)(std::chrono::duration_cast<
                                              std::chrono::seconds>(remaining),
                                          std::chrono::seconds(1));
            std::this_thread::sleep_for(slice);
            remaining -= slice;
        }
        if (st.stop_requested()) break;

        try {
            auto drifts = check();
            if (!drifts.empty()) {
                {
                    std::scoped_lock lock(pending_mutex_);
                    // Bound the queue: if the UI is frozen or the user is on a
                    // different page, stale drift reports are worthless and a
                    // growing queue is a slow memory leak.
                    if (pending_.size() >= 32) pending_.erase(pending_.begin());
                    pending_.push_back(drifts);
                }
                if (on_drift_) {
                    try {
                        on_drift_(drifts);
                    } catch (...) {
                        // A throwing callback must not kill the monitor thread.
                    }
                }
            }
        } catch (const std::exception& e) {
            log::warn(std::string("drift check error: ") + e.what());
        }
    }
}

std::vector<std::vector<DriftItem>> DriftMonitor::drain_pending() {
    std::scoped_lock lock(pending_mutex_);
    if (pending_.empty()) return {};
    auto out = std::move(pending_);
    pending_.clear();
    return out;
}

} // namespace glt::drift