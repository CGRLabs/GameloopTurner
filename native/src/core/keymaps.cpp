#include "glt/core/keymaps.hpp"

#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace glt::keymaps {
namespace {

using json = nlohmann::json;

std::string sql_select_cache() { return "SELECT value0 FROM key_map_configs_cache_official WHERE key=?"; }

// Reads one value0 from the keymap cache table, or nullopt when absent.
std::optional<std::string> read_cache_value(const db::Connection& con,
                                            const std::string& key) {
    std::vector<db::Value> params;
    params.emplace_back(key);

    const auto v = con.query_one(sql_select_cache(), params);
    if (!v || v->is_null()) return std::nullopt;
    return v->as_text();
}

// Reads one value from the `configs` table, or nullopt when absent.
std::optional<std::string> read_config_value(const db::Connection& con,
                                             const std::string& key) {
    std::vector<db::Value> params;
    params.emplace_back(key);

    const auto v = con.query_one("SELECT value FROM configs WHERE key=?", params);
    if (!v || v->is_null()) return std::nullopt;
    return v->as_text();
}

// nlohmann stores object keys in a std::map, so `.dump()` emits them sorted.
// Python emitted them in insertion order. Neither GameLoop nor our own
// round-trip depends on the order, but the written JSON is a file a human may
// inspect during a bug report, so it is dumped compactly and un-indented.
std::string dump_compact(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

// Count of non-overlapping occurrences of a literal substring.
std::size_t count_sub(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return 0;
    std::size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string_view::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// ── <Tex …/> scanner ─────────────────────────────────────────────────────────

// Finds the '>' that closes the tag starting at `start`, honouring quoted
// attribute values so a '>' inside comm="…" does not terminate the tag early.
std::size_t find_tag_end(std::string_view s, std::size_t start) {
    bool in_quote = false;
    char quote_char = 0;
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (in_quote) {
            if (c == quote_char) in_quote = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quote = true;
            quote_char = c;
            continue;
        }
        if (c == '>') return i;
    }
    return std::string_view::npos;
}

// Scans the whole document for '<Tex id="N"' and returns every match.
// The layout is <Tex …/> with no nested elements, so a flat scan is enough.
std::vector<std::pair<std::size_t, std::size_t>> find_tex_tags(std::string_view s) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    const std::string_view needle = "<Tex id=\"";

    std::size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string_view::npos) {
        // Require at least one digit then a closing quote, so '<Tex id="' in a
        // comment or a differently-named element cannot match.
        std::size_t d = pos + needle.size();
        while (d < s.size() && s[d] == ' ') ++d;
        const std::size_t digit_start = d;
        while (d < s.size() && s[d] >= '0' && s[d] <= '9') ++d;
        if (d == digit_start) { pos += needle.size(); continue; }
        while (d < s.size() && s[d] == ' ') ++d;
        if (d >= s.size() || s[d] != '"') { pos += needle.size(); continue; }

        const std::size_t tag_end = find_tag_end(s, pos);
        if (tag_end == std::string_view::npos) break;
        out.emplace_back(pos, tag_end + 1);
        pos = tag_end + 1;
    }
    return out;
}

// Pulls the comm="…" value out of a Tex element. Returns "" when absent.
std::string extract_comm(std::string_view tag) {
    const auto k = tag.find("comm=\"");
    if (k == std::string_view::npos) return {};
    std::size_t v = k + 6;
    const auto e = tag.find('"', v);
    if (e == std::string_view::npos) return {};
    return std::string(tag.substr(v, e - v));
}

// ── <SwitchOperation> stripper ───────────────────────────────────────────────

// Removes both spellings:
//   <SwitchOperation …/>          self-closing
//   <SwitchOperation …>…</SwitchOperation>   paired, body possibly multi-line
//
// The paired form is matched non-greedily so the first closing tag wins, which
// is what re.sub(…, flags=re.S) did in Python.
std::string strip_switch_operations(std::string_view xml) {
    constexpr std::string_view kOpen = "<SwitchOperation";
    std::string out;
    out.reserve(xml.size());

    std::size_t i = 0;
    while (i < xml.size()) {
        const auto hit = xml.find(kOpen, i);
        if (hit == std::string_view::npos) {
            out.append(xml.substr(i));
            break;
        }
        out.append(xml.substr(i, hit - i));

        // Require a word boundary after the tag name, so <SwitchOperations>
        // (a different element) is never touched.
        std::size_t after = hit + kOpen.size();
        if (after < xml.size()) {
            const char n = xml[after];
            if ((std::isalnum(static_cast<unsigned char>(n)) != 0) || n == '_') {
                out.append(xml.substr(hit, kOpen.size()));
                i = after;
                continue;
            }
        }

        const std::size_t tag_end = find_tag_end(xml, hit);
        if (tag_end == std::string_view::npos) {
            out.append(xml.substr(hit));
            break;
        }

        const std::string_view open_tag = xml.substr(hit, tag_end - hit + 1);
        if (!open_tag.empty() && open_tag.back() == '>') {
            // Self-closing? (tolerating whitespace before '>')
            const auto body = open_tag.substr(0, open_tag.size() - 1);
            bool self_closing = false;
            for (auto it = body.rbegin(); it != body.rend(); ++it) {
                if (*it == ' ' || *it == '\t' || *it == '\r' || *it == '\n') continue;
                self_closing = (*it == '/');
                break;
            }
            if (self_closing) {
                i = tag_end + 1;
                continue; // drop it entirely
            }
        }

        // Paired form: consume through the next </SwitchOperation>.
        const auto close = xml.find("</SwitchOperation>", tag_end);
        if (close == std::string_view::npos) {
            // Unterminated — leave the remainder untouched rather than eat the
            // rest of the document.
            out.append(xml.substr(hit));
            break;
        }
        i = close + std::string_view("</SwitchOperation>").size();
    }
    return out;
}

// XML attribute escaping for the SolutionID envelope below. The Python build
// emitted &quot; because the whole value sat inside a quoted XML attribute.
std::string xml_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back(c);
        }
    }
    return out;
}

// nlohmann's json escapes are good enough for these payloads, but the solution
// NAME comes from GameLoop and can contain characters that need XML escaping
// specifically (they are written inside an attribute, not as JSON).
std::string all_digits(std::string_view s) {
    if (s.empty()) return {};
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) return {};
    }
    return std::string(s);
}

} // namespace

// ── Key naming ───────────────────────────────────────────────────────────────
//
// 'offcial' is GameLoop's own misspelling of "official" and must NOT be
// corrected: the rows, and the trigger WHEN clauses that guard them, both use
// the misspelling.

std::string mode_key(std::string_view pkg, std::string_view mode) {
    return "keymap_mode.unlogin." + std::string(pkg) + "." + std::string(mode);
}

std::string blob_key(std::string_view pkg, std::string_view mode, std::string_view sol) {
    return "keymap_data.unlogin." + std::string(pkg) + "." + std::string(mode) + "." +
           std::string(sol);
}

std::string official_blob_key(std::string_view pkg, std::string_view mode,
                              std::string_view sol) {
    return "offcial_keymap_data2." + std::string(pkg) + "." + std::string(mode) + "." +
           std::string(sol);
}

std::string official_config_key(std::string_view pkg) {
    return "offcial_config2." + std::string(pkg);
}

std::string lastmodeid_key(std::string_view pkg) {
    return "lastmodeid.unlogin." + std::string(pkg);
}

std::string recentlyused_key(std::string_view pkg) {
    return "recentlyused_mode." + std::string(pkg);
}

std::string smk_key(std::string_view pkg) {
    return "androws." + std::string(pkg) + ".smk";
}

std::string local_user_config_key(std::string_view pkg) {
    return "androws." + std::string(pkg) + ".local_user_config";
}

// ── Typed reads ──────────────────────────────────────────────────────────────

std::optional<std::string> read_official_blob(const db::Connection& con, std::string_view pkg,
                                              std::string_view mode, std::string_view sol) {
    return read_cache_value(con, official_blob_key(pkg, mode, sol));
}

std::optional<std::string> read_user_blob(const db::Connection& con, std::string_view pkg,
                                           std::string_view mode, std::string_view sol) {
    return read_cache_value(con, blob_key(pkg, mode, sol));
}

std::optional<std::string> read_lastmodeid(const db::Connection& con, std::string_view pkg) {
    return read_cache_value(con, lastmodeid_key(pkg));
}

std::optional<std::string> read_recentlyused(const db::Connection& con, std::string_view pkg) {
    return read_cache_value(con, recentlyused_key(pkg));
}

std::optional<std::string> read_smk(const db::Connection& con, std::string_view pkg) {
    return read_config_value(con, smk_key(pkg));
}

// ── Discovery ────────────────────────────────────────────────────────────────

std::vector<ModeInfo> modes(const db::Connection& con, std::string_view pkg) {
    std::vector<ModeInfo> out;

    const std::string sql =
        "SELECT key, value0 FROM key_map_configs_cache_official WHERE key LIKE ?";

    std::vector<db::Value> params;
    params.emplace_back("keymap_mode.unlogin." + std::string(pkg) + ".%");

    for (const auto& row : con.query(sql, params)) {
        const std::string key = row.at(0).as_text();
        const auto dot = key.rfind('.');
        if (dot == std::string::npos) continue;

        json j;
        try {
            j = json::parse(row.at(1).as_text());
        } catch (...) {
            continue; // one malformed mode must not hide the rest
        }

        ModeInfo m;
        m.mode = key.substr(dot + 1);

        if (j.contains("lastSolutionId") && j["lastSolutionId"].is_string()) {
            m.last_solution_id = j["lastSolutionId"].get<std::string>();
        }
        if (j.contains("lastSolutionIdType") && j["lastSolutionIdType"].is_object()) {
            const auto& t = j["lastSolutionIdType"];
            if (t.contains("type") && t["type"].is_number()) {
                m.last_solution_id_type = t["type"].get<int>();
            }
        }
        if (j.contains("modeId") && j["modeId"].is_number_integer()) {
            m.mode_id = j["modeId"].get<int>();
        }

        if (j.contains("solutions") && j["solutions"].is_object()) {
            for (const auto& [sid, s] : j["solutions"].items()) {
                std::string name;
                int source_type = 1;
                if (s.is_object()) {
                    if (s.contains("solutionName") && s["solutionName"].is_string()) {
                        name = s["solutionName"].get<std::string>();
                    }
                    if (s.contains("sourceDesc") && s["sourceDesc"].is_object() &&
                        s["sourceDesc"].contains("sourceType") &&
                        s["sourceDesc"]["sourceType"].is_number_integer()) {
                        source_type = s["sourceDesc"]["sourceType"].get<int>();
                    }
                }
                m.solutions.emplace_back(sid, std::make_pair(name, source_type));
            }
        }

        out.push_back(std::move(m));
    }
    return out;
}

std::vector<Solution> solutions(const db::Connection& con, std::string_view pkg) {
    std::vector<Solution> out;

    for (const auto& m : modes(con, pkg)) {
        for (const auto& [sid, meta] : m.solutions) {
            std::vector<db::Value> params;
            params.emplace_back(blob_key(pkg, m.mode, sid));

            const auto v = con.query_one(sql_select_cache(), params);
            if (!v || v->is_null() || v->as_text().empty()) continue;

            Solution s;
            s.mode = m.mode;
            s.sol = sid;
            s.name = meta.first;
            s.rule_count = static_cast<int>(count_sub(v->as_text(), "SwitchOperation"));
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::optional<Solution> pick_flat(const std::vector<Solution>& rows, std::string_view prefer) {
    std::vector<const Solution*> flats;
    for (const auto& r : rows) {
        if (r.rule_count == 0) flats.push_back(&r);
    }
    if (flats.empty()) return std::nullopt;

    // Preference ladder over the solution NAME, case-insensitively. Written as an
// explicit array rather than a braced-init-list range-for: mixing a function
// parameter with temporaries in `{...}` leaves the element type undeducible
// ("cannot deduce type for 'const auto *' from 'const _Elem'").
constexpr std::string_view kPreferenceLadder[] = {"multi", "br"};

const std::string_view ladder[] = {prefer, kPreferenceLadder[0], kPreferenceLadder[1]};
for (const std::string_view want : ladder) {
    for (const auto* f : flats) {
        if (str::icontains(f->name, want)) return *f;
    }
}
return *flats.front();
}

std::vector<std::string> jump_tags(const db::Connection& con, std::string_view pkg) {
    std::vector<std::string> out;
    for (const auto& tag : jump_tags_detail(con, pkg)) {
        const auto k = tag.find("ModeID=\"");
        if (k == std::string::npos) continue;
        std::size_t v = k + 8;
        const auto e = tag.find('"', v);
        if (e == std::string::npos) continue;
        out.emplace_back(tag.substr(v, e - v));
    }
    return out;
}

std::vector<std::string> jump_tags_detail(const db::Connection& con, std::string_view pkg) {
    std::vector<std::string> out;

    std::vector<db::Value> params;
    params.emplace_back(official_config_key(pkg));

    const auto v = con.query_one(
        "SELECT value1 FROM key_map_configs_cache_official WHERE key=?", params);
    if (!v || v->is_null()) return out;

    const std::string xml = v->as_text();
    std::size_t pos = 0;
    while (true) {
        const auto hit = xml.find("<Switch ", pos);
        if (hit == std::string::npos) break;
        const auto end = xml.find('>', hit);
        if (end == std::string::npos) break;
        out.push_back(xml.substr(hit, end - hit + 1));
        pos = end + 1;
    }
    return out;
}

bool blind_tex_in_text(std::string& text, int tex_id, TexMatch* out) {
    const std::string needle = "<Tex id=\"" + std::to_string(tex_id) + "\"";

    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        // The needle already ends with the closing quote, so a DIGIT here means
        // we matched a prefix of a longer id — '<Tex id="100046"' must NOT be
        // removed when asked to blind 10004. Anything else (space, '/', '>',
        // end-of-text) is a genuine id boundary.
        const std::size_t after = pos + needle.size();
        const bool boundary =
            (after >= text.size()) ||
            (std::isdigit(static_cast<unsigned char>(text[after])) == 0);

        const auto tag_end = find_tag_end(text, pos);
        if (tag_end == std::string::npos) return false;

        if (boundary) {
            TexMatch m;
            m.begin = pos;
            m.end = tag_end + 1;
            m.comm = extract_comm(std::string_view(text).substr(pos, m.end - pos));

            text.erase(pos, m.end - pos);
            if (out) *out = m;
            return true;
        }
        pos = tag_end + 1;
    }
    return false;
}

std::vector<int> tex_ids_in_text(std::string_view text) {
    std::vector<int> ids;
    for (const auto& [b, e] : find_tex_tags(text)) {
        std::size_t d = b + 9; // strlen("<Tex id=\"")
        while (d < e && text[d] == ' ') ++d;
        int v = 0;
        bool any = false;
        while (d < e && text[d] >= '0' && text[d] <= '9') {
            v = v * 10 + (text[d] - '0');
            ++d;
            any = true;
        }
        if (any) ids.push_back(v);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::string flatten_blob(std::string_view official_xml, std::string_view sol_id,
                         std::string_view sol_name) {
    const std::string body = strip_switch_operations(official_xml);

    // The envelope mirrors what GameLoop writes itself, including the JSON
    // blob smuggled into SolutionID. type:1 == "created by the app".
    std::string head = "<KeyMapSolution SolutionID=\"&quot;id&quot;:&quot;";
    head += xml_escape(sol_id);
    head += "&quot;,&quot;type&quot;:1}\" SolutionName=\"";
    head += xml_escape(sol_name);
    head += "\" InputType=\"1\" SilentUpdate=\"0\">";

    return head + body + "</KeyMapSolution>";
}

std::string flatten_in_place(std::string_view user_blob) {
    // Keeps the existing <KeyMapSolution> wrapper — re-wrapping would discard
    // fields GameLoop wrote (sourceDesc badges, level, and so on).
    return strip_switch_operations(user_blob);
}

// ── Operations ───────────────────────────────────────────────────────────────

std::vector<db::Op> pin_flat_ops(db::Connection& con, std::string_view pkg, std::string_view mode,
                                 std::string_view sol) {
    std::vector<db::Op> ops;

    // Read-modify-write the mode JSON so every other field GameLoop wrote into
    // it (solutions map, sourceDesc, badge versions) survives.
    std::vector<db::Value> params;
    params.emplace_back(mode_key(pkg, mode));

    const auto v = con.query_one(sql_select_cache(), params);
    json j;
    if (v && !v->is_null()) {
        try {
            j = json::parse(v->as_text());
        } catch (...) {
            j = json::object();
        }
    }
    if (!j.is_object()) j = json::object();

    j["lastSolutionId"] = std::string(sol);
    j["lastSolutionIdType"] = json{{"id", std::string(sol)}, {"type", 0}};

    {
        db::Op op;
        op.sql = "UPDATE key_map_configs_cache_official SET value0=? WHERE key=?";
        op.params = {dump_compact(j), mode_key(pkg, mode)};
        op.describe = "mode " + std::string(mode) + " -> solution " + std::string(sol);
        ops.push_back(std::move(op));
    }
    {
        db::Op op;
        op.sql = "UPDATE key_map_configs_cache_official SET value0=? WHERE key=?";
        op.params = {std::string(mode), lastmodeid_key(pkg)};
        op.describe = "lastmodeid -> " + std::string(mode);
        ops.push_back(std::move(op));
    }
    return ops;
}

std::vector<db::Op> register_mode_ops(std::string_view pkg, std::string_view mode,
                                      std::string_view sol, std::string_view name) {
    const std::string sol_s(sol);

    // modeId is an INTEGER when the mode is numeric and a STRING otherwise.
    // GameLoop rejects a mode entry whose modeId type does not match the key it
    // lives under, so this branch is load-bearing rather than cosmetic.
    const std::string mode_digits = all_digits(mode);
    json mode_id = mode_digits.empty()
                       ? json(std::string(mode))
                       : json(static_cast<int>(std::strtol(mode_digits.c_str(), nullptr, 10)));

    json m = json{
        {"lastSolutionId", sol_s},
        {"lastSolutionIdType", json{{"id", sol_s}, {"type", 1}}},
        {"modeId", mode_id},
        {"solutions",
         json{{sol_s,
               json{{"inputType", 1},
                    {"level", 1},
                    {"solutionId", sol_s},
                    {"solutionName", std::string(name)},
                    {"sourceDesc", json{{"solutionId", sol_s}, {"sourceType", 1}}}}}}},
    };

    db::Op op;
    op.sql = "INSERT OR REPLACE INTO key_map_configs_cache_official (key, value0) VALUES (?, ?)";
    op.params = {mode_key(pkg, mode), dump_compact(m)};
    op.describe = "mode " + std::string(mode) + " registered (" + std::string(name) + ")";
    return {std::move(op)};
}

std::vector<db::Op> flatten_user_blob_ops(db::Connection& con, std::string_view pkg,
                                           std::string_view mode, std::string_view sol) {
    std::vector<db::Value> params;
    params.emplace_back(blob_key(pkg, mode, sol));

    const auto v = con.query_one(sql_select_cache(), params);
    if (!v || v->is_null() || v->as_text().empty()) return {};

    const std::string before_text = v->as_text();
    const auto before = count_sub(before_text, "SwitchOperation");
    if (before == 0) return {}; // already flat — nothing to do

    db::Op op;
    op.sql = "UPDATE key_map_configs_cache_official SET value0=? WHERE key=?";
    op.params = {flatten_in_place(before_text), blob_key(pkg, mode, sol)};
    op.describe = "flatten user blob " + std::string(mode) + "/" + std::string(sol) + " (" +
                  std::to_string(before) + " -> 0 rules)";
    return {std::move(op)};
}

db::Op scrub_recent_op(std::string_view pkg, int keep_mode) {
    json j = json{{std::to_string(keep_mode), 0}};

    db::Op op;
    op.sql = "UPDATE key_map_configs_cache_official SET value0=? WHERE key=?";
    op.params = {dump_compact(j), recentlyused_key(pkg)};
    op.describe = "recentlyused -> " + std::to_string(keep_mode) + "-only";
    return op;
}

db::Op set_smk_op(std::string_view pkg, std::string_view smk_xml) {
    db::Op op;
    op.sql = "UPDATE configs SET value=? WHERE key=?";
    op.params = {std::string(smk_xml), smk_key(pkg)};
    op.describe = "androws.smk mirror written";
    return op;
}

db::Op set_user_blob_op(std::string_view pkg, std::string_view mode, std::string_view sol,
                        std::string_view blob) {
    // INSERT OR REPLACE rather than UPDATE: the mode may not exist yet on a
    // fresh install, and a no-op UPDATE there would look like a silent failure.
    db::Op op;
    op.sql = "INSERT OR REPLACE INTO key_map_configs_cache_official (key, value0) VALUES (?, ?)";
    op.params = {blob_key(pkg, mode, sol), std::string(blob)};
    op.describe = "write user blob " + std::string(mode) + "/" + std::string(sol);
    return op;
}

DriftReport drift_report(const db::Connection& con, std::string_view pkg) {
    DriftReport out;

    {
        std::vector<db::Value> p;
        p.emplace_back(lastmodeid_key(pkg));
        if (auto v = con.query_one(sql_select_cache(), p)) {
            if (!v->is_null()) out.lastmodeid = v->as_text();
        }
    }
    {
        std::vector<db::Value> p;
        p.emplace_back(recentlyused_key(pkg));
        if (auto v = con.query_one(sql_select_cache(), p)) {
            if (!v->is_null()) out.recentlyused = v->as_text();
        }
    }
    {
        // The mode-145 solution pointer is what actually decides the on-screen
        // keymap: mode 145 holds four layouts, so reporting the mode alone
        // would be ambiguous.
        std::vector<db::Value> p;
        p.emplace_back(mode_key(pkg, "145"));
        if (auto v = con.query_one(sql_select_cache(), p)) {
            if (!v->is_null()) {
                try {
                    const json j = json::parse(v->as_text());
                    if (j.contains("lastSolutionId") && j["lastSolutionId"].is_string()) {
                        out.mode145_solution = j["lastSolutionId"].get<std::string>();
                    }
                } catch (...) {
                    // A malformed mode row is reported as "unknown", not an error.
                }
            }
        }
    }
    return out;
}

} // namespace glt::keymaps