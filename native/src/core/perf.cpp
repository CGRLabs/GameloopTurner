#include "glt/core/perf.hpp"

#include "glt/core/keymaps.hpp"

#include <nlohmann/json.hpp>

#include <map>

namespace glt::perf {
namespace {

using json = nlohmann::json;

std::string cfg_key(std::string_view pkg, std::string_view suffix) {
    return "androws." + std::string(pkg) + "." + std::string(suffix);
}

// Parse, or fall back to an empty object. A corrupt config document must not
// abort an Apply — but it is re-serialised from the fallback, so the operator
// sees "keys dropped" in the resulting JSON rather than a silent no-op.
json parse_or_empty(std::string_view text) {
    if (text.empty()) return json::object();
    try {
        auto j = json::parse(text);
        return j.is_object() ? j : json::object();
    } catch (...) {
        return json::object();
    }
}

std::string describe_before(const json& j, std::string_view key) {
    const auto it = j.find(std::string(key));
    if (it == j.end()) return "unset";
    return it->is_string() ? it->get<std::string>() : it->dump();
}

} // namespace

std::string explain(std::string_view key) {
    for (const auto& [k, v] : explain_table()) {
        if (k == key) return v;
    }
    return {};
}

const std::vector<std::pair<std::string, std::string>>& explain_table() {
    // Carried over from perf.py::EXPLAINS. The clarity note in particular
    // records a measured result (tier 1 laggier than tier 3) that looks like a
    // bug until you know the engine shifts work to the CPU at low tiers.
    static const std::vector<std::pair<std::string, std::string>> kTable = {
        {std::string(kLowLatency),
         "Smaller frame queue: input sampled closer to frame present. Best "
         "feel-per-cost. Keep ON."},
        {std::string(kFrameGen),
         "Fake interpolated frames inflate the FPS number but add input lag + "
         "artifacts. Real 120 beats fake 240. Keep OFF."},
        {std::string(kClarity),
         "Render-resolution tier (no official docs - best guess). NOTE "
         "(tested): tier 1 was LAGGIER than 3 on this engine - low tiers can "
         "shift work to CPU. Tier 2 is the current sweet spot: clearer texture "
         "+ more FPS."},
        {std::string(kFrameRate),
         "Real frame cap. 120 is the competitive pick; 240 allowed but rarely "
         "real."},
        {std::string("softvideo"),
         "CPU decode of flat VIDEO clips (lobby/killcam) only - not 3D "
         "rendering. Leave ON."},
        {std::string("cores"),
         "VM CPU slice. Max sensible = host cores - 1. Needs GameLoop restart."},
        {std::string("mem_gb"),
         "VM RAM slice. Max sensible = host GB - 2. Needs GameLoop restart."},
    };
    return kTable;
}

std::string local_user_config_raw(const db::Connection& con, std::string_view pkg) {
    std::vector<db::Value> p;
    // The key name is owned by keymaps so there is exactly one place where
    // "androws.<pkg>.local_user_config" is spelled.
    p.emplace_back(keymaps::local_user_config_key(pkg));

    const auto v = con.query_one("SELECT value FROM configs WHERE key=?", p);
    if (!v || v->is_null()) return "{}";
    return v->as_text();
}

std::string host_config_raw(const db::Connection& con, std::string_view pkg) {
    std::vector<db::Value> p;
    p.emplace_back(cfg_key(pkg, "host_config"));

    const auto v = con.query_one("SELECT value FROM configs WHERE key=?", p);
    if (!v || v->is_null()) return "{}";
    return v->as_text();
}

int current_clarity(const db::Connection& con, std::string_view pkg) {
    const json j = parse_or_empty(local_user_config_raw(con, pkg));
    const auto it = j.find(std::string(kClarity));
    if (it == j.end()) return -1;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number()) return static_cast<int>(it->get<double>());
    return -1;
}

db::Op set_clarity_op(std::string_view pkg, std::string_view current_json, int level) {
    json j = parse_or_empty(current_json);
    const std::string before = describe_before(j, kClarity);
    j[std::string(kClarity)] = level;

    db::Op op;
    op.sql = "UPDATE configs SET value=? WHERE key=?";
    op.params = {j.dump(-1, ' ', false), keymaps::local_user_config_key(pkg)};
    op.describe = "clarity " + before + " -> " + std::to_string(level);
    return op;
}

db::Op set_flag_op(std::string_view pkg, std::string_view current_json, std::string_view flag,
                   bool value) {
    json j = parse_or_empty(current_json);
    const std::string before = describe_before(j, flag);
    j[std::string(flag)] = value;

    db::Op op;
    op.sql = "UPDATE configs SET value=? WHERE key=?";
    op.params = {j.dump(-1, ' ', false), keymaps::local_user_config_key(pkg)};
    op.describe = std::string(flag) + " " + before + " -> " + (value ? "true" : "false");
    return op;
}

std::vector<std::string> clarity_verify(const db::Connection& con, std::string_view pkg,
                                        int level) {
    const int now = current_clarity(con, pkg);
    const bool ok = now == level;

    return {"verify clarity: wrote " + std::to_string(level) + ", re-read " +
            std::to_string(now) + " -> " + (ok ? "OK" : "MISMATCH")};
}

} // namespace glt::perf