// ─────────────────────────────────────────────────────────────────────────────
// glt/core/bundle.hpp — GameloopTuner backup bundle (.gltbak).
//
// A bundle is a single JSON file that snapshots everything the tuner touches:
//   * Configs.db keymap + perf rows (androws.<pkg>.*)
//   * smk.conf (the Tex template file)
//   * GameLoop registry branches (Settings\Common, GameLoop, Env, Image)
//   * Source system fingerprint (Windows version, GameLoop version, CODM version)
//
// It carries an SHA-256 integrity digest so a corrupted or tampered bundle
// is rejected before it can overwrite a live install.
//
// File format (UTF-8 JSON, no BOM):
//
//   {
//     "format": "gltbak",
//     "version": 1,
//     "tuner_version": "1.2.0.beta",
//     "created": "2026-11-22T11:00:00Z",
//     "title": "Before CODM 1.0.61 update",   // user label, optional
//     "source_system": { hostname, windows_version, gameloop_version,
//                        engine_version, codm_version, arch },
//     "sections": {
//       "configs_db": { "<key>": <json value>, ... },
//       "smk_conf": "<root>...xml...</root>",
//       "registry": { "<subkey>": { "<value>": <value>, ... }, ... }
//     },
//     "summary": { keymap_mode, lastmodeid, clarity, low_latency_mode,
//                  frame_gen, smk_readonly, tex_blinded, ... },
//     "sha256": "<hex digest of everything above>"
//   }
//
// The digest is computed over the canonical serialization of the bundle with
// the "sha256" field omitted, then inserted. On load the digest is verified
// before any section is trusted.
//
// A bundle can be previewed (read-only diff against the live system) or
// applied (write every section back to GameLoop, with per-section choice).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace glt::bundle {

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Data model
// ─────────────────────────────────────────────────────────────────────────────

// One registry value, polymorphic because GameLoop mixes DWORD/QWORD/SZ.
struct RegValue {
    enum class Type { Dword, Qword, String, Binary, Unknown };
    Type type = Type::Unknown;
    std::int64_t i = 0;          // for Dword/Qword
    std::string s;               // for String
    std::vector<std::uint8_t> b; // for Binary
};

struct RegBranch {
    std::wstring path;                              // e.g. L"Software\\Tencent\\GameLoop\\Settings\\Common"
    std::unordered_map<std::string, RegValue> values;
};

struct SourceSystem {
    std::string hostname;
    std::string windows_version;   // "10.0.19045.4291"
    std::string gameloop_version;  // "7.0.167.0"
    std::string engine_version;    // "5.10.7000.123"
    std::string codm_version;      // "1.0.60"
    std::string arch;              // "AMD64"
};

// The complete in-memory bundle.
struct Bundle {
    int format = 1;
    std::string tuner_version;
    std::string created;           // ISO-8601 UTC
    std::string title;             // user label
    SourceSystem source_system;

    // Keyed JSON documents. configs_db maps key -> parsed JSON value.
    json configs_db;
    std::string smk_conf;          // raw XML text
    std::vector<RegBranch> registry;

    // Human-readable summary for the preview UI.
    json summary;

    std::string sha256;            // computed on write, verified on read
};

// ─────────────────────────────────────────────────────────────────────────────
// Apply options — which sections of a bundle to restore.
// Backup always captures ALL sections; at apply time the user can uncheck
// sections they don't want to restore.
// ─────────────────────────────────────────────────────────────────────────────
struct ApplyOptions {
    bool configs_db = true;   // Configs.db keymap + perf rows
    bool smk_conf = true;     // smk.conf Tex template file
    bool registry = true;     // GameLoop registry branches
};

// Read / write
// ─────────────────────────────────────────────────────────────────────────────

// Reads a .gltbak file from disk, verifies its SHA-256 digest, and returns
// the parsed bundle. Returns nullopt on missing file, bad JSON, or digest
// mismatch (tampered/corrupt).
[[nodiscard]] std::optional<Bundle> read_bundle(const std::wstring& path);

// Writes `bundle` to `path`, computing the SHA-256 digest first. Returns
// false on failure.
[[nodiscard]] bool write_bundle(const std::wstring& path, const Bundle& bundle);

// ─────────────────────────────────────────────────────────────────────────────
// Capture from live system
// ─────────────────────────────────────────────────────────────────────────────

// Captures a full snapshot of the current GameLoop/CODM install into a Bundle.
// The caller supplies a user title. Read-only: no writes to the system.
[[nodiscard]] Bundle capture_bundle(std::string_view title = {});

// ─────────────────────────────────────────────────────────────────────────────
// Diff against live system (for the preview UI)
// ─────────────────────────────────────────────────────────────────────────────

// One field's backup-vs-live comparison.
struct FieldDiff {
    std::string section;   // "configs_db", "smk_conf", "registry"
    std::string key;       // e.g. "androws....local_user_config.clarity"
    std::string backup_value;
    std::string live_value;
    bool matches = false;  // true when backup == live
};

// Compares `bundle` against the live GameLoop install and returns every
// differing field. Read-only: opens Configs.db, smk.conf and registry for
// reading only.
[[nodiscard]] std::vector<FieldDiff> diff_bundle(const Bundle& bundle);

// Applies every section of `bundle` back to the live install. Writes are
// transactional: Configs.db uses the guarded_update path (backup + verify),
// smk.conf is unlocked before writing, registry values are set via RegSetValue.
// `options` controls which sections are applied; all three are on by default.
// Returns human-readable notes. `dry_run` reports without writing.
[[nodiscard]] std::vector<std::string> apply_bundle(const Bundle& bundle,
                                                     ApplyOptions options = {},
                                                     bool dry_run = false);

// ── File association (.gltbak) ────────────────────────────────────────────
// Registers .gltbak so double-clicking opens this tuner in preview mode.
// Call once at setup or from the About page.
[[nodiscard]] bool register_file_association();

} // namespace glt::bundle
