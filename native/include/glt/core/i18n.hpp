// ─────────────────────────────────────────────────────────────────────────────
// glt/core/i18n.hpp — UI localisation.
//
// DESIGN: THE ENGLISH STRING IS THE KEY.
// --------------------------------------
// Every user-facing string is written in English in the source, exactly as it
// always was, and passed through tr(). A translation table maps that English
// text to the active language. When no table is loaded, or the table is missing
// an entry, tr() returns its input untouched — which means:
//
//   * a missing translation degrades to English, never to a blank widget;
//   * there is no separate key catalogue to keep in sync with the source;
//   * new strings are localised by adding data, not by editing call sites.
//
// Translations are embedded in the binary (translations.cpp) as positional
// value arrays over one shared key list — the English string is the key, and
// an empty value falls back to it. No JSON parsing involved: the data is
// embedded and compiler-checked, so there is nothing to parse and nothing to
// fail at runtime.
//
// WHAT IS DELIBERATELY NOT TRANSLATED
// -----------------------------------
// The Logs page, the fix apply notes and the diagnostic report stay English:
// they are the material a user pastes into a bug report, and the maintainer
// reads English. The preflight verdict headline IS translated; its per-check
// detail lines are technical and stay English for the same reason.
//
// THREADING: init() runs once at startup before any worker exists, and
// set_language() is only called from the render thread (the About card). tr()
// is therefore called from the render thread only, like every other ImGui
// touch. No lock is needed and none is taken.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace glt::i18n {

// ── Embedded translation data (defined in translations.cpp) ─────────────────

namespace data {

// One embedded table: `values` has exactly key_count() entries, positionally
// matched to keys(). An empty value ("") means "not translated — fall back to
// English". translations.cpp static_asserts every array against key_count, so
// a forgotten entry is a compile error instead of a shifted row at runtime.
struct Entry {
    const char* code;
    const char* const* values;
};

// The canonical key list: the exact English UI strings, in table order. Pure
// data, no logic — regenerable from the translation sheets.
[[nodiscard]] const char* const* keys();
[[nodiscard]] std::size_t key_count();

[[nodiscard]] const std::vector<Entry>& entries();

} // namespace data

// ── Languages ────────────────────────────────────────────────────────────────

// One selectable language. The picker shows `native_name` so a user can find
// their own language without reading English. `code` is the stable identifier
// persisted to disk and used as the JSON table key — changing one orphans a
// user's saved choice, so they are load-bearing (same rule as variant keys).
struct Lang {
    const char* code;
    const char* native_name;
};

// "en" first: it is the source language and the fallback for everything.
[[nodiscard]] const std::vector<Lang>& languages();

// The active language code. "en" until init() or set_language() runs.
[[nodiscard]] std::string_view current();

// Switches the active table. Unknown or unloaded codes return false and leave
// everything unchanged. Does NOT touch the persisted choice — the caller
// decides what to persist (see App::choose_language).
[[nodiscard]] bool set_language(std::string_view code);

// Maps the Windows UI language onto a supported code; "en" when nothing
// matches. Used when the user has not made an explicit choice.
[[nodiscard]] std::string detect_system_language();

// Startup resolution: the persisted choice, else the Windows UI language.
// Returns the code that became active (for the startup log line).
[[nodiscard]] std::string init();

// ── Translation ──────────────────────────────────────────────────────────────

// The translator. THE ENGLISH STRING IS THE KEY: pass the exact literal the
// source ships, get the active-language rendering back.
//
// CONTRACT: `en` must be null-terminated (a string literal or
// std::string::c_str()); the result is null-terminated and valid until the
// next set_language() call. tr() may return `en` itself.
[[nodiscard]] const char* tr(const char* en);

// ── Persistence ──────────────────────────────────────────────────────────────

// %APPDATA%\GameLoopTuner\language.txt — deliberately outside GameLoop's own
// appdata tree, same rule as variant.txt. Contents: one language code, or
// "auto" meaning "follow the Windows UI language".
[[nodiscard]] std::wstring choice_file();

// nullopt when no file exists (first launch / cleared). A file whose contents
// are not a known code is treated as "auto" rather than as an error.
[[nodiscard]] std::optional<std::string> load_choice();

[[nodiscard]] bool save_choice(std::string_view code);

} // namespace glt::i18n
