// ─────────────────────────────────────────────────────────────────────────────
// glt/core/hosts.hpp — block/unblock the mode-map download host.
//
// Ported from legacy-python/src/core/hosts.py.
//
// WHAT THIS IS FOR (and what it is NOT)
// ---------------------------------------
// The original theory was that GameLoop re-downloads its keymap mode maps from
// pckeymap.yyb.qq.com, so blocking the host would stop the templates being
// restored. Blocking the host did NOT stop smk.conf being re-burned mid-game:
// the engine restores from a LOCAL zip in the install dir (ano_tmp/ob_*.zip).
// See docs/keymap-auto-switch.md for the full investigation trail.
//
// So this module is EXPERIMENTAL and largely redundant. It is kept because it
// costs one file edit, it is reversible, and it starves one of the two restore
// paths. The durable kill-switch is the read-only smk.conf lock, not this.
//
// The hosts file is written with CRLF-preserving binary semantics: the tuner
// never rewrites line endings of a file Windows and every other tool on the
// machine also writes to.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace glt::hosts {

inline constexpr std::string_view kDomain = "pckeymap.yyb.qq.com";
inline constexpr std::string_view kMark = "# gameloop-tuner: block mode-map download";

// %SystemRoot%\System32\drivers\etc\hosts.
[[nodiscard]] std::wstring hosts_path();

// True when a 0.0.0.0 line for the domain already exists.
[[nodiscard]] bool is_blocked();

// Appends the block line. Backs up first. dry_run reports without touching.
[[nodiscard]] std::vector<std::string> block(bool dry_run = false);

// Removes every line mentioning the domain or our marker. Backs up first.
[[nodiscard]] std::vector<std::string> unblock(bool dry_run = false);

struct Probe {
    std::string domain;
    bool blocked = false;
    std::vector<std::string> resolved;  // A records from getaddrinfo
    std::string tcp443;                 // "open" / "fail: …"
    std::string error;                  // resolver error, if any
};

// DNS resolve + TCP connect, for the pre/post comparison the user runs around
// the block.
[[nodiscard]] Probe probe();

} // namespace glt::hosts