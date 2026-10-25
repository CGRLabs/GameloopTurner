// ─────────────────────────────────────────────────────────────────────────────
// glt/core/journal.hpp — the Apply/Undo record.
//
// Ported from legacy-python/src/core/journal.py.
//
// FORMAT NOTE: the file is tuner-journal.json — a JSON OBJECT keyed by action
// name, NOT a JSONL stream. The port spec said ".jsonl"; that was wrong, and
// changing it would have orphaned the journal the Python build already wrote
// to the user's disk. Each Apply overwrites that action's entry with a fresh
// timestamp, so the file always holds the most recent state per action.
//
// Location: <Configs.db dir>\tuner-journal.json — deliberately NOT in %TEMP%,
// because a journal the user cannot find after a reboot is not a journal.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <vector>
#include <utility>

namespace glt::journal {

// <dirname(db_path)>\tuner-journal.json, or "" when db_path is empty.
[[nodiscard]] std::wstring path_for(const std::wstring& db_path);

// Raw file text, or "" when unreadable/absent.
[[nodiscard]] std::string load(const std::wstring& jpath);

// Records `entry` under `action`, stamped with the current local time,
// overwriting any previous entry for that action.
[[nodiscard]] bool record(const std::wstring& jpath, const std::string& action,
                          const std::string& entry_json);

// Removes `action` and returns what it held. Returns "" when absent.
[[nodiscard]] std::string pop(const std::wstring& jpath, const std::string& action);

// Every (action, entry-json) currently in the file.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> entries(const std::wstring& jpath);

} // namespace glt::journal