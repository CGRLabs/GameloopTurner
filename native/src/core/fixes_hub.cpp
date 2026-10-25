// Hub (TAC) fixes. Duplicated from fixes.cpp so the Hub build's SQL/perf
// behaviour can differ without an #ifdef in a file that also serves Beta.
//
// Differences from Beta, all required by docs/targets/hub/PLAN.md:
//   1. pin_flat uses pick_flat_for_mode(145) so the new flat BR solution can
//      never be selected while pinning multiplayer (§3).
//   2. set_latency / set_no_frame_gen SKIP an absent flag and report "not
//      supported on this GameLoop build" instead of creating it (§4).
//   3. blind_tex skips absent Tex ids (30004) and treats that as success (§5).
//   4. scrub_recent is a no-op with a note when recentlyused_mode is absent,
//      so nothing is auto-created (§2 / A3.9).
//   5. fullchain never includes the hosts block (§8).
//   6. no reference to migrate_status_tb / scenes_config anywhere (§10).
#include "glt/core/fixes.hpp"

#include "glt/core/fixes_hub.hpp"

#include "glt/core/db.hpp"
#include "glt/core/hosts.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/keymaps_hub.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/perf_hub.hpp"
#include "glt/core/procs.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>

namespace glt::fixes {
namespace {

using json = nlohmann::json;

// ── Reverse-engineered constants (SAME in Hub — report A3.13/A3.14) ──────────

constexpr std::string_view kModeMultiplayer = "145";
constexpr std::string_view kModeBr = "146";
constexpr std::string_view kModeDmz = "531686";

constexpr std::string_view kSolDmz = "1063553";
// 146 stays the canonical BR flatten source: it exists on Hub and is the
// documented constant. The new GameLoopHub.com solution (1792331375184) is
// accounted for in keymaps_hub but deliberately NOT preferred here (PLAN open
// question, needs live test).
constexpr std::string_view kSolBr = "146";
constexpr std::string_view kNameDmz = "DMZ";
constexpr std::string_view kNameBr = "Battle Royale";

constexpr std::wstring_view kQuarantinePatterns[] = {
    L"ob_*.zip", L"config2.dat.*", L"cache_crc.dat", L"cache_md5.dat",
};

// ── smk.conf file blinding ───────────────────────────────────────────────────

std::vector<std::string> blind_smk_file(const std::vector<int>& tex_ids, bool dry_run) {
    std::vector<std::string> notes;

    const std::wstring fp = paths::smk_conf();
    if (fp.empty()) {
        notes.push_back("smk.conf: not found, file blinding skipped");
        return notes;
    }

    std::string text;
    if (!str::read_file(fp, text)) {
        notes.push_back("smk.conf: read failed");
        return notes;
    }

    bool changed = false;
    for (const int tid : tex_ids) {
        keymaps::TexMatch m;
        if (keymaps::blind_tex_in_text(text, tid, &m)) {
            changed = true;
            notes.push_back("smk.conf: blind Tex " + std::to_string(tid) + " (" + m.comm + ")");
        } else {
            // Absent is SUCCESS for Hub (30004 ships missing): an absent id is
            // one fewer jump path to remove, not a failure.
            notes.push_back("smk.conf: Tex " + std::to_string(tid) + " already absent (ok)");
        }
    }

    if (!changed) return notes;
    if (dry_run) {
        notes.insert(notes.begin(), "DRYRUN: smk.conf templates would be removed");
        return notes;
    }

    const std::wstring bak = fp + L".bak-" + str::timestamp_compact_w();
    if (str::copy_file(fp, bak)) {
        notes.push_back("smk.conf backup: " + str::to_utf8(bak));
    } else {
        notes.push_back("smk.conf: backup FAILED, aborting file blind");
        return notes;
    }

    str::set_readonly(fp, false);

    if (!str::write_file(fp, text)) {
        notes.push_back("smk.conf: write FAILED");
        return notes;
    }

    notes.push_back("smk.conf: written (binary mode, LF endings preserved)");
    return notes;
}

bool smk_file_is_readonly() {
    const std::wstring fp = paths::smk_conf();
    return !fp.empty() && str::is_readonly(fp);
}

bool has(const std::vector<Key>& v, Key k) {
    return std::find(v.begin(), v.end(), k) != v.end();
}

} // namespace

// ── Hub-only helpers (declared in fixes_hub.hpp) ─────────────────────────────

bool hub_hosts_block_default_enabled() {
    // Report A5.1: pckeymap.yyb.qq.com is not referenced anywhere in the Hub
    // install, so the block is left to an explicit user opt-in only.
    return false;
}

std::vector<int> hub_blind_tex_ids() {
    // Same targets as Beta. 30004 is absent in Hub and is handled by the
    // callers skipping missing ids, not by removing it from the list.
    constexpr int kPrimary[] = {10004, 10046};
    std::vector<int> ids;
    for (int id : kPrimary) {
        ids.push_back(id);
        ids.push_back(id + 20000);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string_view> hub_absent_tables() {
    // Hub's Configs.db has neither table. Listed so the Hub suite can assert no
    // Hub code path references them.
    return {"migrate_status_tb", "scenes_config"};
}

std::vector<std::string_view> hub_fullchain_stages() {
    return {"pin", "flatten-dmz", "flatten-br", "flatten-145", "blind", "scrub",
            "quarantine"};
}

// ── Identity / registry ──────────────────────────────────────────────────────

std::string_view key_name(Key k) {
    switch (k) {
        case Key::FullChain:    return "fullchain";
        case Key::Pin:          return "pin";
        case Key::FlatDmz:      return "flatdmz";
        case Key::FlatBr:       return "flatbr";
        case Key::Blind:        return "blind";
        case Key::Scrub:        return "scrub";
        case Key::Quarantine:   return "quarantine";
        case Key::Unquarantine: return "unquarantine";
        case Key::HostsBlock:   return "hostsblock";
        case Key::HostsUnblock: return "hostsunblock";
        case Key::Latency:      return "latency";
        case Key::NoFrameGen:   return "noframegen";
        case Key::Priority:     return "priority";
    }
    return "unknown";
}

const std::vector<Def>& registry() {
    static const std::vector<Def> kRegistry = {
        {Key::FullChain, Page::Keymaps, true,
         "DISABLE AUTO-SWITCH (recommended: pin + flatten + blind + quarantine)",
         "Pins a fixed-key layout, flattens smart destinations so any jump lands "
         "on fixed keys, blinds the DMZ + BR jump-trigger textures (DB + "
         "smk.conf, then locks smk.conf read-only), clears jump history when the "
         "build tracks it, and quarantines the engine's local template pack so "
         "the runtime restore of blinded textures is starved. The engine can "
         "still jump layouts, but every landing should be fixed keys - change "
         "layouts manually in the GameLoop UI. If the engine reverts, the drift "
         "alert fires.",
         "Reversible via backups. If GameLoop updates, re-run fullchain "
         "(new smk.conf ships writable with templates back)."},

        {Key::Pin, Page::Keymaps, false, "Pin flat multiplayer layout only",
         "Points lastmodeid + mode 145's solution at a 0-rule layout. Generic: "
         "discovers IDs on this PC, restricted to mode 145 so the Hub flat "
         "Battle-Royale solution can never be pinned by mistake.",
         "If no flat layout exists on this PC, pin fails. Run GameLoop once to "
         "generate default layouts."},

        {Key::FlatDmz, Page::Keymaps, false, "Flatten DMZ destination",
         "Replaces smart DMZ user layout with 0-rule version + registers it.",
         "Official blob is the source; if it's missing, flatten is skipped."},

        {Key::FlatBr, Page::Keymaps, false, "Flatten Battle-Royale destination",
         "Same for mode 146, using the documented flat BR solution (146). "
         "Official rows untouched (trigger-guarded).",
         "Battle Royale has many auto-rules in the smart layout; flattening is "
         "essential to stop BR jumps."},

        {Key::Blind, Page::Keymaps, false,
         "Blind mode-jump trigger textures (Tex 10004/10046 + 30xxx twins)",
         "Removes the templates behind the DMZ (lobby) and BR (skill-box) "
         "jumps, including the 30xxx series twins the engine matches as second "
         "bindings. Absent ids (30004 on Hub) are skipped and reported as "
         "'already absent'. DB + smk.conf. Reversible via backups.",
         "Blinding the wrong Tex id leaves the jump path live. The fix targets "
         "10004/10046 + 30xxx twins specifically."},

        {Key::Scrub, Page::Keymaps, false, "Scrub recently-used to 145-only",
         "Clears engine jump history pointers. No-op on builds (Hub) that do "
         "not keep recentlyused_mode.",
         "The engine refills recentlyused in memory during play; this is a "
         "best-effort scrub, not a permanent lock."},

        {Key::Quarantine, Page::Keymaps, false,
         "Quarantine LOCAL mode-map pack + lock smk.conf",
         "The engine restores blinded smk.conf templates at runtime from a "
         "LOCAL zip packed in the install dir (ano_tmp/ob_*.zip) - the network "
         "block never stops that. Quarantining that pack + integrity caches and "
         "marking smk.conf read-only starves the restore. Run after any GameLoop "
         "update. Reversible via the quarantine-undo fix.",
         "The engine regenerates the ano_tmp pack at will; the read-only lock on "
         "smk.conf is the durable part."},

        {Key::Unquarantine, Page::Keymaps, false,
         "Undo: restore quarantined template pack",
         "Moves the ano_tmp pack + caches back and unlocks smk.conf. Use if the "
         "app misbehaves.",
         "Restores the engine's ability to re-burn templates. Only use if "
         "something broke after quarantining."},

        {Key::HostsBlock, Page::Keymaps, false,
         "EXPERIMENTAL: block mode-map download host",
         "Adds \"0.0.0.0 pckeymap.yyb.qq.com\" to the hosts file so the engine "
         "cannot re-download the mode maps that restore blinded trigger "
         "textures. NOT part of the Hub fullchain: this host is not referenced "
         "by the Hub build at all, so it is opt-in only. If launch breaks, use "
         "the unblock fix.",
         "The host is not referenced by the Hub install; applying it is unlikely "
         "to matter and modifying the hosts file can break other apps."},

        {Key::HostsUnblock, Page::Keymaps, false,
         "Undo: unblock mode-map download host",
         "Removes the tuner hosts entry. Use if the game misbehaves.",
         "Only needed if hostsblock was applied and caused issues."},

        {Key::Latency, Page::Performance, false, "Low-latency ON",
         "Smaller frame queue = best feel-per-cost.",
         "Skipped (not supported) on builds that do not carry "
         "low_latency_mode, such as GameLoop Hub (TAC)."},

        {Key::NoFrameGen, Page::Performance, false, "Frame-gen OFF",
         "Fake frames add input lag. Real fps > fake fps.",
         "Skipped (not supported) on builds that do not carry "
         "default_frame_gen_on, such as GameLoop Hub (TAC)."},

        {Key::Priority, Page::VmPriority, false, "Set live process priorities",
         "Emulator+VM+Box -> High, UI -> High, Assistant/AceKeeper -> Above, "
         "DldSvr/Vfs -> Below. Applies to running processes now - resets on exit.",
         "Requires admin (SeDebugPrivilege) to reach SYSTEM-owned processes. "
         "Priorities reset when GameLoop exits."},
    };
    return kRegistry;
}

std::vector<Def> for_page(Page p) {
    std::vector<Def> out;
    for (const auto& d : registry()) {
        if (d.page == p) out.push_back(d);
    }
    std::stable_partition(out.begin(), out.end(), [](const Def& d) { return d.star; });
    return out;
}

const Def* find(Key k) {
    for (const auto& d : registry()) {
        if (d.key == k) return &d;
    }
    return nullptr;
}

std::wstring require_db() {
    const std::wstring dbp = paths::appdata_db();
    if (dbp.empty()) {
        throw db::Error(
            "Configs.db not found - is GameLoop installed/launched for this user?");
    }
    return dbp;
}

std::vector<int> blind_tex_ids() { return hub_blind_tex_ids(); }

bool needs_gameloop_closed(const std::vector<Key>& keys) {
    for (Key k : keys) {
        switch (k) {
            case Key::Priority:
            case Key::HostsBlock:
            case Key::HostsUnblock:
                continue;
            default:
                return true;
        }
    }
    return false;
}

// ── Individual fixes ─────────────────────────────────────────────────────────

std::vector<std::string> pin_flat(bool dry_run) {
    const std::wstring dbp = require_db();

    std::optional<keymaps::Solution> pick;
    {
        db::Connection con(dbp);
        // Mode-restricted: never pin a 0-rule BR layout as the multiplayer pin.
        pick = keymaps::pick_flat_for_mode(keymaps::solutions(con, paths::kPkg),
                                           kModeMultiplayer, "mp");
    }
    if (!pick) {
        throw db::Error("no flat (0-rule) layout found on this PC");
    }

    std::vector<db::Op> ops;
    {
        db::Connection con(dbp);
        ops = keymaps::pin_flat_ops(con, paths::kPkg, pick->mode, pick->sol);
    }

    auto res = db::guarded_updates(dbp, ops, dry_run);
    res.notes.push_back("active: " + pick->name + " (" + pick->sol + ")");
    return res.notes;
}

std::vector<std::string> flatten_destination(std::string_view mode, std::string_view sol,
                                             std::string_view name, bool dry_run) {
    const std::wstring dbp = require_db();

    std::optional<std::string> official;
    {
        db::Connection con(dbp);
        official = keymaps::read_official_blob(con, paths::kPkg, mode, sol);
    }
    if (!official || official->empty()) {
        throw db::Error("official source for " + std::string(mode) + "/" + std::string(sol) +
                        " not found");
    }

    const std::string flat = keymaps::flatten_blob(*official, sol, name);

    std::vector<db::Op> ops = keymaps::register_mode_ops(paths::kPkg, mode, sol, name);

    db::Op blob = keymaps::set_user_blob_op(paths::kPkg, mode, sol, flat);
    blob.describe = "flat user blob " + std::string(mode) + "/" + std::string(sol) +
                    " (0 rules)";
    ops.push_back(std::move(blob));

    return db::guarded_updates(dbp, ops, dry_run).notes;
}

std::vector<std::string> flatten_dmz(bool dry_run) {
    return flatten_destination(kModeDmz, kSolDmz, kNameDmz, dry_run);
}

std::vector<std::string> flatten_br(bool dry_run) {
    return flatten_destination(kModeBr, kSolBr, kNameBr, dry_run);
}

std::vector<std::string> blind_tex(bool dry_run) {
    const std::wstring dbp = require_db();
    const std::vector<int> ids = blind_tex_ids();

    std::string smk;
    {
        db::Connection con(dbp);
        if (auto v = keymaps::read_smk(con, paths::kPkg)) smk = *v;
    }

    std::vector<std::string> head;
    std::vector<std::string> blinded;
    for (const int tid : ids) {
        keymaps::TexMatch m;
        if (keymaps::blind_tex_in_text(smk, tid, &m)) {
            blinded.push_back("blind Tex " + std::to_string(tid) + " (" + m.comm + ")");
        } else {
            // HUB: absent is success. 30004 is not shipped by this build.
            head.push_back("Tex " + std::to_string(tid) + " already absent (ok)");
        }
    }

    std::vector<db::Op> ops;
    if (!blinded.empty()) {
        auto op = keymaps::set_smk_op(paths::kPkg, smk);
        op.describe = str::join(blinded, " + ");
        ops.push_back(std::move(op));
    }

    auto res = db::guarded_updates(dbp, ops, dry_run);

    std::vector<std::string> notes = head;
    notes.insert(notes.end(), res.notes.begin(), res.notes.end());

    try {
        auto file_notes = blind_smk_file(ids, dry_run);
        notes.insert(notes.end(), file_notes.begin(), file_notes.end());
    } catch (const std::exception& e) {
        notes.push_back(std::string("smk.conf blinding FAILED: ") + e.what());
    }
    return notes;
}

std::vector<std::string> scrub_recent(bool dry_run) {
    const std::wstring dbp = require_db();

    // Hub does not keep recentlyused_mode. A no-op UPDATE would be harmless, but
    // making the absence explicit guarantees nothing is ever auto-created and
    // the user gets an honest note instead of a "0 rows" success.
    bool present = false;
    {
        db::Connection con(dbp);
        present = keymaps::read_recentlyused(con, paths::kPkg).has_value();
    }
    if (!present) {
        return {"recentlyused_mode." + std::string(paths::kPkg) +
                " absent on this GameLoop build; nothing to scrub"};
    }
    return db::guarded_updates(dbp, {keymaps::scrub_recent_op(paths::kPkg, 145)}, dry_run).notes;
}

std::vector<std::string> quarantine(bool dry_run) {
    std::vector<std::string> notes;

    const std::wstring ano = paths::ano_tmp_dir();
    if (ano.empty() || !str::dir_exists(ano)) {
        notes.push_back("ano_tmp: not found (no GameLoop version dir?)");
        return notes;
    }

    // ano_tmp_quarantine is ABSENT in Hub: it is created on first use. Create it
    // up front (before the move loop) so the first move cannot fail on a
    // missing destination directory.
    const std::wstring quar = paths::ano_tmp_quarantine_dir();

    int found = 0;
    for (const std::wstring_view pattern : kQuarantinePatterns) {
        for (const auto& name : str::glob(ano, pattern)) {
            const std::wstring src = str::path_join(ano, name);
            if (!str::file_exists(src)) continue;

            ++found;
            if (dry_run) {
                notes.push_back("DRYRUN: would quarantine " + str::to_utf8(name));
                continue;
            }

            if (!str::create_dirs(quar) && !str::dir_exists(quar)) {
                notes.push_back("quarantine FAILED: cannot create " + str::to_utf8(quar));
                continue;
            }

            const std::wstring dst = str::path_join(quar, name);
            if (str::move_file(src, dst)) {
                notes.push_back("quarantined: " + str::to_utf8(name));
            } else {
                notes.push_back("quarantine FAILED: " + str::to_utf8(name) +
                                " (file may be in use)");
            }
        }
    }

    if (dry_run) {
        notes.insert(notes.begin(), "DRYRUN: " + std::to_string(found) +
                                       " ano_tmp item(s) to quarantine");
        return notes;
    }

    const std::wstring smk = paths::smk_conf();
    if (!smk.empty() && str::set_readonly(smk, true)) {
        notes.push_back("smk.conf -> read-only (engine re-burn blocked)");
    } else {
        notes.push_back("smk.conf read-only NOT set (missing?)");
    }

    if (found == 0) notes.push_back("ano_tmp template pack already quarantined");
    return notes;
}

std::vector<std::string> unquarantine(bool dry_run) {
    std::vector<std::string> notes;

    const std::wstring ano = paths::ano_tmp_dir();
    const std::wstring quar = paths::ano_tmp_quarantine_dir();

    if (quar.empty() || !str::dir_exists(quar)) {
        notes.push_back("quarantine dir not found - nothing to restore");
    } else {
        for (const auto& name : str::list_dir(quar)) {
            const std::wstring src = str::path_join(quar, name);
            if (!str::file_exists(src)) continue;

            if (dry_run) {
                notes.push_back("DRYRUN: would restore " + str::to_utf8(name));
                continue;
            }
            if (!ano.empty()) str::create_dirs(ano);

            const std::wstring dst = ano.empty() ? std::wstring{} : str::path_join(ano, name);
            if (dst.empty() || !str::move_file(src, dst)) {
                notes.push_back("restore FAILED: " + str::to_utf8(name));
            } else {
                notes.push_back("restored: " + str::to_utf8(name));
            }
        }
    }

    if (!dry_run) {
        const std::wstring smk = paths::smk_conf();
        if (!smk.empty() && str::set_readonly(smk, false)) {
            notes.push_back("smk.conf locks cleared");
        }
    }
    return notes;
}

std::vector<std::string> hosts_block(bool dry_run) { return hosts::block(dry_run); }
std::vector<std::string> hosts_unblock(bool dry_run) { return hosts::unblock(dry_run); }

std::vector<std::string> set_clarity(int level, bool dry_run) {
    const std::wstring dbp = require_db();

    std::string current;
    {
        db::Connection con(dbp);
        current = perf::local_user_config_raw(con, paths::kPkg);
    }

    const std::string pkg(paths::kPkg);
    return db::guarded_updates(dbp, {perf::set_clarity_op(pkg, current, level)}, dry_run,
                               [pkg, level](const db::Connection& c) {
                                   return perf::clarity_verify(c, pkg, level);
                               })
        .notes;
}

std::vector<std::string> set_latency(bool dry_run) {
    const std::wstring dbp = require_db();

    std::string current;
    {
        db::Connection con(dbp);
        current = perf::local_user_config_raw(con, paths::kPkg);
    }

    // HUB: low_latency_mode is absent. Do NOT write (which would create) it.
    if (!perf::hub_flag_supported(current, perf::kLowLatency)) {
        return {perf::hub_unsupported_note(perf::kLowLatency)};
    }

    return db::guarded_updates(
               dbp,
               {perf::set_flag_op(paths::kPkg, current, perf::kLowLatency,
                                  perf::kLowLatencyValue)},
               dry_run)
        .notes;
}

std::vector<std::string> set_no_frame_gen(bool dry_run) {
    const std::wstring dbp = require_db();

    std::string current;
    {
        db::Connection con(dbp);
        current = perf::local_user_config_raw(con, paths::kPkg);
    }

    // HUB: default_frame_gen_on is absent. Do NOT create it.
    if (!perf::hub_flag_supported(current, perf::kFrameGen)) {
        return {perf::hub_unsupported_note(perf::kFrameGen)};
    }

    return db::guarded_updates(
               dbp,
               {perf::set_flag_op(paths::kPkg, current, perf::kFrameGen, perf::kFrameGenValue)},
               dry_run)
        .notes;
}

std::vector<std::string> set_priorities(bool dry_run) {
    if (dry_run) return {"DRYRUN: priorities preview"};
    return procs::set_priorities();
}

// ── fullchain ────────────────────────────────────────────────────────────────

std::vector<std::string> full_chain(bool dry_run) {
    const std::wstring dbp = require_db();

    std::vector<db::Op> ops;
    std::vector<std::string> head;

    std::optional<keymaps::Solution> pick;
    std::vector<keymaps::Solution> sols;
    std::string smk;
    bool recentlyused_present = false;

    {
        db::Connection con(dbp);

        // 1. Pin a flat layout, restricted to mode 145.
        sols = keymaps::solutions(con, paths::kPkg);
        pick = keymaps::pick_flat_for_mode(sols, kModeMultiplayer, "mp");
        if (!pick) {
            throw db::Error("no flat (0-rule) layout found on this PC");
        }

        auto pin_ops = keymaps::pin_flat_ops(con, paths::kPkg, pick->mode, pick->sol);
        ops.insert(ops.end(), pin_ops.begin(), pin_ops.end());
        head.push_back("pin: " + pick->name + " (" + pick->sol + ")");

        recentlyused_present = keymaps::read_recentlyused(con, paths::kPkg).has_value();

        // 2. Flatten both destinations and scrub the history after each, when
        //    the build tracks recentlyused at all.
        for (const auto& [mode, sol, name] :
             {std::tuple{kModeDmz, kSolDmz, kNameDmz}, std::tuple{kModeBr, kSolBr, kNameBr}}) {
            const auto official = keymaps::read_official_blob(con, paths::kPkg, mode, sol);
            if (!official || official->empty()) {
                head.push_back("flatten " + std::string(name) +
                               ": official source missing, skipped");
                continue;
            }

            auto reg = keymaps::register_mode_ops(paths::kPkg, mode, sol, name);
            ops.insert(ops.end(), reg.begin(), reg.end());

            db::Op blob = keymaps::set_user_blob_op(paths::kPkg, mode, sol,
                                                    keymaps::flatten_blob(*official, sol, name));
            blob.describe = "flat user blob " + std::string(mode) + "/" + std::string(sol) +
                            " (0 rules)";
            ops.push_back(std::move(blob));

            head.push_back("flatten " + std::string(name) + ": registered + 0-rule blob");
            if (recentlyused_present) {
                ops.push_back(keymaps::scrub_recent_op(paths::kPkg, 145));
            } else {
                head.push_back("scrub recentlyused: absent on this build, skipped");
            }
        }

        // 3. Flatten any SMART layout living inside mode 145 itself.
        for (const auto& s : sols) {
            if (s.mode != kModeMultiplayer || s.rule_count == 0) continue;

            auto mp_ops = keymaps::flatten_user_blob_ops(con, paths::kPkg, s.mode, s.sol);
            if (!mp_ops.empty()) {
                ops.insert(ops.end(), mp_ops.begin(), mp_ops.end());
                head.push_back("flatten '" + s.name + "': stripped to 0 rules");
            } else {
                head.push_back("flatten '" + s.name + "': already 0 rules");
            }
        }

        // 4. Blind the DB mirror in the same transaction.
        if (auto v = keymaps::read_smk(con, paths::kPkg)) smk = *v;
    }

    std::vector<std::string> blinded;
    for (const int tid : blind_tex_ids()) {
        keymaps::TexMatch m;
        if (keymaps::blind_tex_in_text(smk, tid, &m)) {
            blinded.push_back("blind Tex " + std::to_string(tid) + " (" + m.comm + ")");
            head.push_back("blind: Tex " + std::to_string(tid) + " trigger removed (" +
                           m.comm + ")");
        } else {
            head.push_back("blind: Tex " + std::to_string(tid) + " already absent (ok)");
        }
    }
    if (!blinded.empty()) {
        auto op = keymaps::set_smk_op(paths::kPkg, smk);
        op.describe = str::join(blinded, " + ");
        ops.push_back(std::move(op));
    }

    // NOTE: the experimental hosts block is deliberately NOT part of the Hub
    // fullchain (hub_hosts_block_default_enabled() == false). It stays a
    // standalone opt-in fix.

    auto res = db::guarded_updates(dbp, ops, dry_run);

    std::vector<std::string> notes = head;
    notes.insert(notes.end(), res.notes.begin(), res.notes.end());

    try {
        auto file_notes = blind_smk_file(blind_tex_ids(), dry_run);
        notes.insert(notes.end(), file_notes.begin(), file_notes.end());
    } catch (const std::exception& e) {
        notes.push_back(std::string("smk.conf blinding FAILED: ") + e.what());
    }

    try {
        auto q_notes = quarantine(dry_run);
        notes.insert(notes.end(), q_notes.begin(), q_notes.end());
    } catch (const std::exception& e) {
        notes.push_back(std::string("quarantine FAILED: ") + e.what());
    }

    return notes;
}

// ── Allocation ───────────────────────────────────────────────────────────────

std::vector<std::string> apply_alloc(int cores, int mem_gb, bool dry_run) {
    const auto host = procs::host_resources();

    if (dry_run) {
        return {"DRYRUN: alloc " + std::to_string(cores) + "c/" + std::to_string(mem_gb) +
                "GB (host " + std::to_string(host.cores) + "c/" +
                std::to_string(host.mem_gb) + "GB)"};
    }

    reg::HostResources h;
    h.cores = host.cores;
    h.mem_gb = host.mem_gb;

    auto notes = reg::set_perf(cores, mem_gb, h);
    log::info("alloc -> " + std::to_string(cores) + "c/" + std::to_string(mem_gb) + "GB");
    return notes;
}

// ── State detection ──────────────────────────────────────────────────────────

std::vector<Key> detect_applied() {
    std::vector<Key> applied;

    const std::wstring dbp = paths::appdata_db();
    if (dbp.empty()) return applied;

    try {
        db::Connection con(dbp);

        // On Hub recentlyused_mode is absent, so this stays false and the scrub
        // badge is simply never shown. That is the correct outcome, not a bug.
        bool is_recently_used_clean = false;
        try {
            if (auto v = keymaps::read_recentlyused(con, paths::kPkg)) {
                auto j = json::parse(*v);
                if (j.is_object()) {
                    is_recently_used_clean = true;
                    for (const auto& [k, _] : j.items()) {
                        if (k != "145") is_recently_used_clean = false;
                    }
                }
            }
        } catch (...) {
            is_recently_used_clean = false;
        }

        const std::wstring smk_path = paths::smk_conf();
        bool tex_clean = false;
        if (!smk_path.empty()) {
            std::string text;
            if (str::read_file(smk_path, text)) {
                const auto ids = keymaps::tex_ids_in_text(text);
                tex_clean = true;
                for (int id : blind_tex_ids()) {
                    if (std::find(ids.begin(), ids.end(), id) != ids.end()) tex_clean = false;
                }
            }
        }

        bool flat_dmz = false, flat_br = false, pin_ok = false;
        for (const auto& s : keymaps::solutions(con, paths::kPkg)) {
            if (s.mode == kModeDmz && s.sol == kSolDmz && s.rule_count == 0) flat_dmz = true;
            if (s.mode == kModeBr && s.sol == kSolBr && s.rule_count == 0) flat_br = true;
            if (s.mode == kModeMultiplayer && s.rule_count == 0) pin_ok = true;
        }

        const bool quar_ok = smk_file_is_readonly();

        if (pin_ok && flat_dmz && flat_br && tex_clean && quar_ok) {
            applied.push_back(Key::FullChain);
        } else if (pin_ok) {
            applied.push_back(Key::Pin);
        }
        if (flat_dmz && !has(applied, Key::FullChain)) applied.push_back(Key::FlatDmz);
        if (flat_br && !has(applied, Key::FullChain)) applied.push_back(Key::FlatBr);
        if (tex_clean && !has(applied, Key::FullChain)) applied.push_back(Key::Blind);
        if (is_recently_used_clean && !has(applied, Key::FullChain)) {
            applied.push_back(Key::Scrub);
        }
        if (quar_ok && !has(applied, Key::FullChain)) applied.push_back(Key::Quarantine);
    } catch (const std::exception& e) {
        log::warn(std::string("detect_applied: ") + e.what());
    }

    try {
        if (hosts::is_blocked()) applied.push_back(Key::HostsBlock);
    } catch (...) {
    }

    return applied;
}

} // namespace glt::fixes