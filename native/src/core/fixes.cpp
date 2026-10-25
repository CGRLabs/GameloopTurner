#include "glt/core/fixes.hpp"

#include "glt/core/db.hpp"
#include "glt/core/hosts.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
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

// ── Reverse-engineered constants ─────────────────────────────────────────────

// The three GameLoop modes this tool understands.
//
// 145      Multiplayer      — the mode that works for all three game types
// 146      Battle Royale
// 531686   DMZ (搜打撤)
//
// Their flat solution ids are NOT hardcoded: pick_flat() discovers a 0-rule
// layout at runtime, which is what lets this work on a machine where Tencent
// renumbered anything.
constexpr std::string_view kModeMultiplayer = "145";
constexpr std::string_view kModeBr = "146";
constexpr std::string_view kModeDmz = "531686";

constexpr std::string_view kSolDmz = "1063553";
constexpr std::string_view kSolBr = "146";
constexpr std::string_view kNameDmz = "DMZ";
constexpr std::string_view kNameBr = "Battle Royale";

// Files the engine keeps in ano_tmp as its LOCAL template/integrity source:
//   ob_*.zip        the packed keymap configs it re-extracts smk.conf from
//   config2.dat.*   signature-check state
//   cache_crc.dat   integrity cache
//   cache_md5.dat   integrity cache
//
// The engine regenerates these at will — verified: the pack reappeared
// mid-session and caused no switch once smk.conf was locked. So the quarantine
// is best-effort deprivation of restore material; the DURABLE kill-switch is
// the read-only attribute on smk.conf, because the runtime restore cannot
// overwrite a read-only file.
constexpr std::wstring_view kQuarantinePatterns[] = {
    L"ob_*.zip", L"config2.dat.*", L"cache_crc.dat", L"cache_md5.dat",
};

// ── smk.conf file blinding ───────────────────────────────────────────────────

// Removes the jump-trigger templates from the smk.conf FILE.
//
// The engine reads both the DB row and this file, so blinding only the DB
// leaves the trigger live. Backs up, unlocks, rewrites, and reports whether
// anything actually changed.
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
            notes.push_back("smk.conf: Tex " + std::to_string(tid) + " already absent");
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

    // MUST clear the read-only lock before overwriting — quarantine sets it on
    // purpose, and the runtime restore is blocked by exactly this attribute.
    // _blind_smk_file in the Python build cleared it first too; skipping this
    // would make the whole fix a silent no-op on a quarantined machine.
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
    // pages.py::FIXES, carried over with its wording intact. The "why" strings
    // are the only record of WHY each fix exists and what was empirically
    // observed, so they are not paraphrased.
    static const std::vector<Def> kRegistry = {
        {Key::FullChain, Page::Keymaps, true,
         "DISABLE AUTO-SWITCH (recommended: pin + flatten + blind + quarantine)",
         "Pins a fixed-key layout, flattens smart destinations so any jump lands "
         "on fixed keys, blinds the DMZ + BR jump-trigger textures (DB + "
         "smk.conf, then locks smk.conf read-only), clears jump history, and "
         "quarantines the engine's local template pack so the runtime restore of "
         "blinded textures is starved. The engine can still jump layouts, but "
         "every landing should be fixed keys - change layouts manually in the "
         "GameLoop UI. If the engine reverts, the drift alert fires.",
         "Reversible via backups. If GameLoop updates, re-run fullchain "
         "(new smk.conf ships writable with templates back)."},

        {Key::Pin, Page::Keymaps, false, "Pin flat multiplayer layout only",
         "Points lastmodeid + mode solution at a 0-rule layout. Generic: "
         "discovers IDs on this PC.",
         "If no flat layout exists on this PC, pin fails. Run GameLoop once to "
         "generate default layouts."},

        {Key::FlatDmz, Page::Keymaps, false, "Flatten DMZ destination",
         "Replaces smart DMZ user layout with 0-rule version + registers it.",
         "Official blob is the source; if it's missing, flatten is skipped."},

        {Key::FlatBr, Page::Keymaps, false, "Flatten Battle-Royale destination",
         "Same for mode 146. Official rows untouched (trigger-guarded).",
         "Battle Royale has 234 auto-rules in the smart layout; flattening is "
         "essential to stop BR jumps."},

        {Key::Blind, Page::Keymaps, false,
         "Blind mode-jump trigger textures (Tex 10004/10046 + 30xxx twins)",
         "Removes the templates behind the DMZ (lobby) and BR (skill-box) "
         "jumps, including the 30xxx series twins the engine matches as second "
         "bindings (blinding 10046 alone left BR firing). DB + smk.conf. "
         "Reversible via backups.",
         "Blinding the wrong Tex id leaves the jump path live. The fix targets "
         "10004/10046 + 30xxx twins specifically."},

        {Key::Scrub, Page::Keymaps, false, "Scrub recently-used to 145-only",
         "Clears engine jump history pointers.",
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
         "textures. Landings still land on flat blobs (the durable guarantee). "
         "If launch breaks, use the unblock fix.",
         "Mostly redundant now - the durable fix is the read-only lock. "
         "Modifying the hosts file can break other apps."},

        {Key::HostsUnblock, Page::Keymaps, false,
         "Undo: unblock mode-map download host",
         "Removes the tuner hosts entry. Use if the game misbehaves.",
         "Only needed if hostsblock was applied and caused issues."},

        {Key::Latency, Page::Performance, false, "Low-latency ON",
         "Smaller frame queue = best feel-per-cost.",
         "Already ON in your config. Re-applying is harmless."},

        {Key::NoFrameGen, Page::Performance, false, "Frame-gen OFF",
         "Fake frames add input lag. Real fps > fake fps.",
         "Already OFF in your config. Re-applying is harmless."},

        {Key::Priority, Page::VmPriority, false, "Set live process priorities",
         "Emulator+VM -> High, UI -> High, Assistant -> Above, DldSvr -> Below. "
         "Applies to running processes now - resets on exit.",
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
    // Stable partition so starred entries lead, without disturbing registry
    // order within each group.
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

std::vector<int> blind_tex_ids() {
    // Primary series + the 20000-offset twin series.
    constexpr int kPrimary[] = {10004, 10046};
    std::vector<int> ids;
    for (int id : kPrimary) {
        ids.push_back(id);
        ids.push_back(id + 20000);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool needs_gameloop_closed(const std::vector<Key>& keys) {
    // Everything that writes Configs.db or smk.conf. Priority changes apply to
    // running processes and must NOT require a close.
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

    // Two scopes, two connections: one to discover, one to build the ops. The
    // Python build leaked a connection here (dbmod.connect(dbp).cursor() was
    // never closed), and SQLite holds a file handle for the life of the
    // connection.
    std::optional<keymaps::Solution> pick;
    {
        db::Connection con(dbp);
        pick = keymaps::pick_flat(keymaps::solutions(con, paths::kPkg));
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

    // The OFFICIAL blob is the source we flatten FROM. It is trigger-guarded
    // and read-only — GameLoop raises "Resmi yapilandirma degerleri
    // degistirilemez." on any write to it — which is why the flattened copy has
    // to land on the USER blob key instead.
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

    // Read the DB mirror, strip every trigger, write the final text ONCE.
    //
    // The Python build emitted one UPDATE per id, each carrying the cumulative
    // text, and then collapsed them to the last — same result, but doing it
    // directly means the transaction has one statement instead of four.
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
            head.push_back("Tex " + std::to_string(tid) + " already absent");
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
        // The DB half already applied and is already backed up; failing the
        // whole fix here would leave the user with a half-applied state and no
        // indication of which half succeeded.
        notes.push_back(std::string("smk.conf blinding FAILED: ") + e.what());
    }
    return notes;
}

std::vector<std::string> scrub_recent(bool dry_run) {
    const std::wstring dbp = require_db();
    return db::guarded_updates(dbp, {keymaps::scrub_recent_op(paths::kPkg, 145)}, dry_run).notes;
}

std::vector<std::string> quarantine(bool dry_run) {
    std::vector<std::string> notes;

    const std::wstring ano = paths::ano_tmp_dir();
    if (ano.empty() || !str::dir_exists(ano)) {
        notes.push_back("ano_tmp: not found (no GameLoop version dir?)");
        return notes;
    }

    const std::wstring quar = paths::ano_tmp_quarantine_dir();

    int found = 0;
    // `pattern` is a std::wstring_view, not a pointer: each element of the
    // array decays to `const wchar_t*`, and writing `const auto*` for that
    // would declare a pointer to the POINTER.
    for (const std::wstring_view pattern : kQuarantinePatterns) {
        for (const auto& name : str::glob(ano, pattern)) {
            const std::wstring src = str::path_join(ano, name);
            if (!str::file_exists(src)) continue;

            ++found;
            if (dry_run) {
                notes.push_back("DRYRUN: would quarantine " + str::to_utf8(name));
                continue;
            }

            if (!str::create_dirs(quar)) {
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

    // The read-only lock is the part that actually holds. The pack regenerates;
    // the lock does not, because the runtime restore cannot overwrite a
    // read-only file.
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

    // The verify callback re-reads through a fresh query, which is what proves
    // the bytes are in the file rather than merely in our buffer.
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

    {
        db::Connection con(dbp);

        // 1. Pin a flat layout.
        sols = keymaps::solutions(con, paths::kPkg);
        pick = keymaps::pick_flat(sols);
        if (!pick) {
            throw db::Error("no flat (0-rule) layout found on this PC");
        }

        auto pin_ops = keymaps::pin_flat_ops(con, paths::kPkg, pick->mode, pick->sol);
        ops.insert(ops.end(), pin_ops.begin(), pin_ops.end());
        head.push_back("pin: " + pick->name + " (" + pick->sol + ")");

        // 2. Flatten both destinations and scrub the history after each.
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
            ops.push_back(keymaps::scrub_recent_op(paths::kPkg, 145));
        }

        // 3. Flatten any SMART layout living inside mode 145 itself.
        //    e.g. 'Multi Player' with 93 rules is ONE runtime solution-flip
        //    away from auto-switching, with no mode jump needed. Discovered
        //    generically — no solution IDs hardcoded.
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
            head.push_back("blind: Tex " + std::to_string(tid) + " already absent");
        }
    }
    if (!blinded.empty()) {
        auto op = keymaps::set_smk_op(paths::kPkg, smk);
        op.describe = str::join(blinded, " + ");
        ops.push_back(std::move(op));
    }

    // ONE transaction → ONE backup → ONE rollback.
    auto res = db::guarded_updates(dbp, ops, dry_run);

    std::vector<std::string> notes = head;
    notes.insert(notes.end(), res.notes.begin(), res.notes.end());

    // The file half and the quarantine are best-effort hardening that runs
    // AFTER the database commit, exactly as in the Python build. Failing them
    // must not invalidate the committed database work.
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

    // Throws when the request would starve Windows; the UI shows the message.
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

        // recentlyused is NOT a gate for fullchain: the engine refills it in
        // memory during play and every landing is flat regardless, so gating on
        // it made the badge flicker off the instant the game launched. It still
        // gates the standalone `scrub` badge below.
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

        // The DB smk mirror is dormant (the engine re-fills it harmlessly), so
        // cleanliness is judged on the smk.conf FILE, which is what actually
        // drives scene detection.
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
            // pin: mode 145's active solution has 0 rules. ANY 0-rule layout in
            // mode 145 satisfies this — the engine lands on whichever is
            // selected, and a flattened one cannot switch away.
            if (s.mode == kModeMultiplayer && s.rule_count == 0) pin_ok = true;
        }

        // The durable guarantee is the read-only lock. The engine regenerates
        // the ano_tmp pack at will, so quarantining it proves nothing on its
        // own — quarantine_ok() in the Python build deliberately returned just
        // the lock state, and so does this.
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

    // hosts is independent of the database entirely.
    try {
        if (hosts::is_blocked()) applied.push_back(Key::HostsBlock);
    } catch (...) {
        // A hosts read failure just means the badge stays unticked.
    }

    return applied;
}

} // namespace glt::fixes