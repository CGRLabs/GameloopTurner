// ─────────────────────────────────────────────────────────────────────────────
// glt/core/fixes.hpp — the FIXES registry and every apply function.
//
// In the Python build these lived in ui/pages.py alongside the Tkinter widgets,
// which made the actual tuning logic impossible to unit-test without building
// a UI. They are separated here: this header has no ImGui dependency and the
// test binary links it directly.
//
// Every fix:
//   1. takes a dry_run flag (reported, never written)
//   2. runs inside db::guarded_updates(), so a timestamped Configs.db backup
//      exists before the first statement and the whole batch is one transaction
//   3. returns human-readable notes for the Logs page
//   4. is idempotent — re-running an applied fix reports "already …" rather
//      than failing, because users re-apply after every GameLoop update
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace glt::fixes {

// ── Identity ─────────────────────────────────────────────────────────────────

enum class Key {
    FullChain,   // ⭐ the recommended one: pin + flatten + blind + scrub + quarantine
    Pin,         // lastmodeid + mode solution → a 0-rule layout
    FlatDmz,     // 0-rule user blob for the DMZ destination
    FlatBr,      // 0-rule user blob for the Battle Royale destination
    Blind,       // strip Tex 10004/10046 (+30xxx twins) from DB and smk.conf
    Scrub,       // recentlyused_mode → {"145":0}
    Quarantine,  // move ano_tmp pack aside + lock smk.conf read-only
    Unquarantine,
    HostsBlock,
    HostsUnblock,
    Latency,     // low_latency_mode = true
    NoFrameGen,  // default_frame_gen_on = false
    Priority,    // live process priorities
};

[[nodiscard]] std::string_view key_name(Key k);

// ── Destination mode ─────────────────────────────────────────────────────────

enum class Page {
    Keymaps,
    Performance,
    VmPriority,
};

struct Def {
    Key key;
    Page page;
    bool star = false;    // rendered as the hero "RECOMMENDED" card
    std::string_view label;
    std::string_view why;
    std::string_view risk = {};  // what could go wrong (shown in help tooltip)
};

// The registry, in display order. The star card renders first on its page.
[[nodiscard]] const std::vector<Def>& registry();

// Defs belonging to `page`, star entries first.
[[nodiscard]] std::vector<Def> for_page(Page p);

[[nodiscard]] const Def* find(Key k);

// ── Helpers ──────────────────────────────────────────────────────────────────

// Requires a Configs.db. Throws db::Error with the Python message when the
// file is absent — the UI shows it verbatim and the user learns that GameLoop
// has never been launched for their account.
[[nodiscard]] std::wstring require_db();

// Every Tex id to blind: the two primaries plus their 30000+ twins.
[[nodiscard]] std::vector<int> blind_tex_ids();

// Does any of these keys write Configs.db or smk.conf (i.e. does GameLoop need
// to be closed first)?
[[nodiscard]] bool needs_gameloop_closed(const std::vector<Key>& keys);

// ── Individual fixes ─────────────────────────────────────────────────────────

[[nodiscard]] std::vector<std::string> pin_flat(bool dry_run = false);
[[nodiscard]] std::vector<std::string> flatten_destination(std::string_view mode,
                                                           std::string_view sol,
                                                           std::string_view name,
                                                           bool dry_run = false);
[[nodiscard]] std::vector<std::string> flatten_dmz(bool dry_run = false);
[[nodiscard]] std::vector<std::string> flatten_br(bool dry_run = false);
[[nodiscard]] std::vector<std::string> blind_tex(bool dry_run = false);
[[nodiscard]] std::vector<std::string> scrub_recent(bool dry_run = false);
[[nodiscard]] std::vector<std::string> quarantine(bool dry_run = false);
[[nodiscard]] std::vector<std::string> unquarantine(bool dry_run = false);
[[nodiscard]] std::vector<std::string> hosts_block(bool dry_run = false);
[[nodiscard]] std::vector<std::string> hosts_unblock(bool dry_run = false);
[[nodiscard]] std::vector<std::string> set_clarity(int level, bool dry_run = false);
[[nodiscard]] std::vector<std::string> set_latency(bool dry_run = false);
[[nodiscard]] std::vector<std::string> set_no_frame_gen(bool dry_run = false);
[[nodiscard]] std::vector<std::string> set_priorities(bool dry_run = false);

// pin + flatten DMZ + flatten BR + blind + scrub + quarantine, in ONE
// guarded transaction (a single backup, a single rollback).
[[nodiscard]] std::vector<std::string> full_chain(bool dry_run = false);

// ── Registry allocation (the Performance page sliders) ───────────────────────

// Returns notes and the pre-change values, which main.py recorded in the
// journal so a Revert could restore them.
[[nodiscard]] std::vector<std::string> apply_alloc(int cores, int mem_gb, bool dry_run = false);

// ── State detection ──────────────────────────────────────────────────────────

// Which fixes are already in effect. Drives the pre-ticked checkboxes on
// startup so the UI reflects reality rather than an empty slate.
//
// fullchain's gate deliberately EXCLUDES recentlyused: the engine refills that
// history in memory during play and every landing is flat anyway, so gating on
// it made fullchain flicker off the moment the game started.
[[nodiscard]] std::vector<Key> detect_applied();

} // namespace glt::fixes