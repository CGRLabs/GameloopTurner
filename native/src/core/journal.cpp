#include "glt/core/journal.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

namespace glt::journal {
namespace {

using json = nlohmann::json;

json load_json(const std::wstring& jpath) {
    std::string text;
    if (jpath.empty() || !str::read_file(jpath, text)) return json::object();
    try {
        auto j = json::parse(text);
        return j.is_object() ? j : json::object();
    } catch (...) {
        // A truncated journal (power loss mid-write) must not block a new
        // Apply. It is discarded; the Configs.db backups are the real undo
        // path anyway.
        return json::object();
    }
}

bool save_json(const std::wstring& jpath, const json& j) {
    // Indent 1, matching json.dump(indent=1) in journal.py, so a user opening
    // the file in Notepad sees something readable rather than one long line.
    const std::string text = j.dump(1, ' ', false);
    if (!str::write_file(jpath, text)) {
        log::error("journal write failed: " + str::to_utf8(jpath));
        return false;
    }
    return true;
}

} // namespace

std::wstring path_for(const std::wstring& db_path) {
    // The Python build fell back to ~ (expanduser) when db_path was empty,
    // which on Windows resolves to %USERPROFILE%. The native build returns ""
    // instead: every caller already branches on "GameLoop not installed", and
    // silently creating a journal in the user profile for a machine with no
    // GameLoop is worse than creating none.
    if (db_path.empty()) return {};
    return str::path_join(str::path_parent(db_path), L"tuner-journal.json");
}

std::string load(const std::wstring& jpath) {
    return load_json(jpath).dump(1, ' ', false);
}

bool record(const std::wstring& jpath, const std::string& action,
            const std::string& entry_json) {
    if (jpath.empty()) return false;

    json j = load_json(jpath);

    json entry = json::object();
    if (!entry_json.empty()) {
        try {
            auto parsed = json::parse(entry_json);
            if (parsed.is_object()) entry = parsed;
        } catch (...) {
            entry = json{{"raw", entry_json}};
        }
    }
    entry["time"] = str::timestamp_readable();

    j[action] = entry;
    if (!save_json(jpath, j)) return false;

    log::info("journal " + action + " -> " + entry.dump());
    return true;
}

std::string pop(const std::wstring& jpath, const std::string& action) {
    if (jpath.empty()) return {};

    json j = load_json(jpath);
    if (!j.contains(action)) return {};

    json entry = j[action];
    j.erase(action);
    save_json(jpath, j);

    return entry.is_object() ? entry.dump() : entry.dump();
}

std::vector<std::pair<std::string, std::string>> entries(const std::wstring& jpath) {
    std::vector<std::pair<std::string, std::string>> out;
    if (jpath.empty()) return out;

    const json j = load_json(jpath);
    if (!j.is_object()) return out;

    for (const auto& [k, v] : j.items()) {
        out.emplace_back(k, v.dump());
    }
    return out;
}

} // namespace glt::journal