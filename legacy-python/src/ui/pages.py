"""Page classes replacing the old tab framework.

All fix definitions, fix functions, and page UI live here.
Key change from tabs.py: no per-page Apply/Revert — everything queues to
the shared App.pending dict and the sidebar's Apply All runs it.
"""
import json
import os
import re
import tkinter as tk
from tkinter import messagebox

import customtkinter as ctk

from core import backups as bakmod
from core import db as dbmod
from core import hosts as hostsmod
from core import keymaps as km
from core import log as logmod
from core import paths
from core import perf as perfmod
from core import procs
from core import registry as reg
from core import journal as journ
from ui import theme as T

LOG = logmod.get_logger()
PKG = paths.PKG


# ═══════════════════════════════════════════════════════════════════════════
#  Fix definitions (unchanged logic from tabs.py)
# ═══════════════════════════════════════════════════════════════════════════

def _need_db():
    dbp = paths.appdata_db()
    if not dbp:
        raise RuntimeError('Configs.db not found — is GameLoop installed/launched for this user?')
    return dbp


def _db_con(dbp):
    return dbmod.connect(dbp)


def fix_pin_flat(dry=False):
    dbp = _need_db()
    con = _db_con(dbp)
    cur = con.cursor()
    rows = km.solutions(cur, PKG)
    pick = km.pick_flat(rows)
    con.close()
    if not pick:
        raise RuntimeError('no flat (0-rule) layout found on this PC')
    mode, sol, name, _ = pick
    ops = km.pin_flat_ops(dbmod.connect(dbp).cursor(), PKG, mode, sol)
    res = dbmod.guarded_updates(dbp, ops, dry_run=dry)
    return res['notes'] + ['active: %s (%s)' % (name, sol)]


def fix_flatten(mode, sol, name, dry=False):
    dbp = _need_db()
    con = _db_con(dbp)
    cur = con.cursor()
    off = cur.execute('SELECT value0 FROM %s WHERE key=?' % km.CACHE,
                      ('offcial_keymap_data2.%s.%s.%s' % (PKG, mode, sol),)).fetchone()
    con.close()
    if not off:
        raise RuntimeError('official source for %s/%s not found' % (mode, sol))
    flat = km.flatten_blob(off[0], sol, name)
    ops = km.register_mode_ops(dbmod.connect(dbp).cursor(), PKG, mode, sol, name)
    ops.append(('INSERT OR REPLACE INTO %s (key, value0) VALUES (?, ?)' % km.CACHE,
                ('keymap_data.unlogin.%s.%s.%s' % (PKG, mode, sol), flat),
                'flat user blob %s/%s (0 rules)' % (mode, sol)))
    res = dbmod.guarded_updates(dbp, ops, dry_run=dry)
    return res['notes']


# Tex templates that drive mode jumps (Tex id = 10000 + Switch TextureId).
# 10004 = lobby mailbox -> SetUp1/TextureId=4 -> DMZ (mode 531686)
# 10046 = skill-select box -> SkillCircle/TextureId=46 -> BR (mode 146)
# Each has a 30xxx twin (30000 + TextureId) the engine matches as a second
# binding for the SAME switch — verified: blinding 10046 alone did NOT stop BR,
# the live 30046 (SkillCircle) kept firing. Both series must go.
BLIND_TEX_IDS = (10004, 10046)


def blind_tex_ids() -> tuple:
    """All Tex ids to blind: primary (10xxx) + 30xxx series twin each."""
    ids = set(BLIND_TEX_IDS)
    for tid in BLIND_TEX_IDS:
        ids.add(tid + 20000)  # twin series: 30000 + TextureId
    return tuple(sorted(ids))


def _blind_tex_in_text(xml: str, tex_id: int):
    """Remove one <Tex id=...> template. Returns (new_xml, comm) or (xml, None)."""
    m = re.search(r'<Tex id="%d"[^>]*\n?\s*comm="([^"]*)"\s*/>' % tex_id, xml)
    if not m:
        return xml, None
    return xml[:m.start()] + xml[m.end():], m.group(1)


def _smk_conf_readonly() -> bool:
    """True if smk.conf has no write bits set (Windows read-only attribute)."""
    import stat
    fp = paths.smk_conf()
    if not fp:
        return False
    st = os.stat(fp)
    return (st.st_mode & (stat.S_IWRITE | stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)) == 0


def _set_smk_readonly(ro: bool) -> bool:
    fp = paths.smk_conf()
    if not fp:
        return False
    os.chmod(fp, 0o444 if ro else 0o644)
    return True


def _smk_file_tex_ids() -> set:
    """Tex ids present in the smk.conf FILE (the operative scene source)."""
    fp = paths.smk_conf()
    if not fp:
        return set()
    try:
        with open(fp, encoding='utf-8', errors='replace') as f:
            txt = f.read()
    except OSError:
        return set()
    return {int(t) for t in re.findall(r'<Tex id="(\d+)"', txt)}


def _blind_smk_file(tex_ids=None, dry=False):
    """Remove jump-trigger templates from smk.conf too (engine reads both DB and file)."""
    import shutil
    import time
    if tex_ids is None:
        tex_ids = blind_tex_ids()
    fp = paths.smk_conf()
    if not fp:
        return ['smk.conf: not found, file blinding skipped']
    with open(fp, encoding='utf-8') as f:
        txt = f.read()
    notes, changed = [], False
    for tid in tex_ids:
        txt, comm = _blind_tex_in_text(txt, tid)
        if comm is None:
            notes.append('smk.conf: Tex %d already absent' % tid)
        else:
            changed = True
            notes.append('smk.conf: blind Tex %d (%s)' % (tid, comm))
    if not changed:
        return notes
    if dry:
        return ['DRYRUN: smk.conf templates would be removed'] + notes
    bak = fp + '.bak-' + time.strftime('%Y%m%d-%H%M%S')
    shutil.copy2(fp, bak)
    notes.append('smk.conf backup: ' + bak)
    _set_smk_readonly(False)  # must clear locks before overwriting
    with open(fp, 'w', encoding='utf-8', newline='') as f:
        f.write(txt)
    return notes


# Files the engine keeps in ano_tmp as its LOCAL template/integrity source:
# ob_*.zip holds the packed keymap configs it re-extracts smk.conf from,
# config2/cache *_dat tracks signature checks. The engine regenerates these at
# will (verified: the pack reappears mid-session and causes no switch) — so the
# quarantine is best-effort deprivation of restore material; the durable lock is
# the read-only attribute on smk.conf itself, because the runtime restore cannot
# overwrite a read-only file (verified: file stayed blinded while the pack returned).
QUARANTINE_PATTERNS = ('ob_*.zip', 'config2.dat.*', 'cache_crc.dat', 'cache_md5.dat')


def _ano_tmp_dir() -> str:
    av = paths.app_version_dir()
    return os.path.join(av, 'ano_tmp') if av else ''


def fix_quarantine(dry=False):
    """Move the engine's local template source aside + lock smk.conf read-only.

    The engine re-burns blinded smk.conf templates at runtime from a LOCAL zip
    in ano_tmp (ob_x_ver_86.zip) — the network block never stops that. This
    moves the pack + integrity caches aside (weak help: the engine regenerates
    them) AND marks smk.conf read-only (the real kill-switch — a read-only file
    cannot be restored over). Reversible via fix_unquarantine.
    """
    import glob as _g
    notes = []
    ano = _ano_tmp_dir()
    if not ano or not os.path.isdir(ano):
        notes.append('ano_tmp: not found (no GameLoop version dir?)')
        return notes
    quar = os.path.join(os.path.dirname(ano), 'ano_tmp_quarantine')
    found = 0
    for pat in QUARANTINE_PATTERNS:
        for p in _g.glob(os.path.join(ano, pat)):
            if not os.path.isfile(p):
                continue
            found += 1
            if dry:
                notes.append('DRYRUN: would quarantine %s' % os.path.basename(p))
                continue
            os.makedirs(quar, exist_ok=True)
            dst = os.path.join(quar, os.path.basename(p))
            try:
                os.replace(p, dst)
                notes.append('quarantined: %s' % os.path.basename(p))
            except OSError as e:
                notes.append('quarantine FAILED: %s (%s)' % (os.path.basename(p), e))
    if dry:
        return ['DRYRUN: %d ano_tmp item(s) to quarantine' % found] + notes
    if _set_smk_readonly(True):
        notes.append('smk.conf -> read-only (engine re-burn blocked)')
    else:
        notes.append('smk.conf read-only NOT set (missing?)')
    return notes if found else ['ano_tmp template pack already quarantined'] + notes


def _quarantine_ok() -> bool:
    # The engine regenerates its ano_tmp pack at will (it came back mid-session
    # and caused no switch) — the durable guarantee is smk.conf's read-only
    # lock, which blocks the runtime template restore the pack feeds.
    return _smk_conf_readonly()


def fix_unquarantine(dry=False):
    """Restore quarantined ano_tmp files + un-lock smk.conf (undo fix_quarantine)."""
    import glob as _g
    import shutil
    notes = []
    ano = _ano_tmp_dir()
    quar = os.path.join(os.path.dirname(ano), 'ano_tmp_quarantine') if ano else ''
    if not quar or not os.path.isdir(quar):
        notes.append('quarantine dir not found — nothing to restore')
    else:
        for p in _g.glob(os.path.join(quar, '*')):
            if not os.path.isfile(p):
                continue
            dst = os.path.join(ano, os.path.basename(p)) if ano else ''
            if dry:
                notes.append('DRYRUN: would restore %s' % os.path.basename(p))
                continue
            try:
                os.makedirs(ano, exist_ok=True) if ano else None
                shutil.move(p, dst)
                notes.append('restored: %s' % os.path.basename(p))
            except OSError as e:
                notes.append('restore FAILED: %s (%s)' % (os.path.basename(p), e))
    if not dry:
        _set_smk_readonly(False)
        notes.append('smk.conf locks cleared')
    return notes


def fix_blind_tex(dry=False, tex_ids=None):
    if tex_ids is None:
        tex_ids = blind_tex_ids()
    dbp = _need_db()
    con = _db_con(dbp)
    smk = con.execute("SELECT value FROM configs WHERE key='androws.%s.smk'" % PKG).fetchone()[0]
    con.close()
    ops, notes_head = [], []
    for tid in tex_ids:
        smk, comm = _blind_tex_in_text(smk, tid)
        if comm is None:
            notes_head.append('Tex %d already absent' % tid)
        else:
            ops.append(('UPDATE configs SET value=? WHERE key=?',
                        (smk, 'androws.%s.smk' % PKG),
                        'blind Tex %d (%s)' % (tid, comm)))
    # NOTE: all blind ops carry the final text; keep only the last one per
    # identical key so sequential removals don't clobber each other.
    if ops:
        last_sql, last_params, _ = ops[-1]
        descs = [d for _, _, d in ops]
        ops = [(last_sql, last_params, ' + '.join(descs))]
    res = dbmod.guarded_updates(dbp, ops, dry_run=dry)
    notes = notes_head + res['notes']
    try:
        notes += _blind_smk_file(tex_ids, dry=dry)
    except Exception as e:  # noqa - DB part already applied + backed up
        notes.append('smk.conf blinding FAILED: %s' % e)
    return notes


def fix_scrub_recent(dry=False):
    dbp = _need_db()
    ops = [('UPDATE %s SET value0=? WHERE key=?' % km.CACHE,
            (json.dumps({'145': 0}, separators=(',', ':')), 'recentlyused_mode.%s' % PKG),
            'recentlyused -> 145-only')]
    return dbmod.guarded_updates(dbp, ops, dry_run=dry)['notes']


def fix_hosts_block(dry=False):
    return hostsmod.block(dry=dry)


def fix_hosts_unblock(dry=False):
    return hostsmod.unblock(dry=dry)


def fix_fullchain(dry=False):
    """Pin + flatten both + blind + scrub, single backup."""
    dbp = _need_db()
    con = dbmod.connect(dbp)
    cur = con.cursor()
    ops, notes_head = [], []
    pick = km.pick_flat(km.solutions(cur, PKG))
    if not pick:
        con.close()
        raise RuntimeError('no flat (0-rule) layout found on this PC')
    mode, sol, name, _ = pick
    ops.extend(km.pin_flat_ops(cur, PKG, mode, sol))
    notes_head.append('pin: %s (%s)' % (name, sol))
    for m2, s2, nm in (('531686', '1063553', 'DMZ'), ('146', '146', 'Battle Royale')):
        off = cur.execute('SELECT value0 FROM %s WHERE key=?' % km.CACHE,
                          ('offcial_keymap_data2.%s.%s.%s' % (PKG, m2, s2),)).fetchone()
        if not off:
            notes_head.append('flatten %s: official source missing, skipped' % nm)
            continue
        flat = km.flatten_blob(off[0], s2, nm)
        ops.extend(km.register_mode_ops(cur, PKG, m2, s2, nm))
        ops.append(('INSERT OR REPLACE INTO %s (key, value0) VALUES (?, ?)' % km.CACHE,
                    ('keymap_data.unlogin.%s.%s.%s' % (PKG, m2, s2), flat),
                    'flat user blob %s/%s (0 rules)' % (m2, s2)))
        notes_head.append('flatten %s: registered + 0-rule blob' % nm)
        ops.append(('UPDATE %s SET value0=? WHERE key=?' % km.CACHE,
                    (json.dumps({'145': 0}, separators=(',', ':')), 'recentlyused_mode.%s' % PKG),
                    'recentlyused -> 145-only'))
    # Any SMART layout living inside mode 145 itself (e.g. 'Multi Player', 93 rules):
    # one runtime solution-flip away from auto-switch, no mode jump needed.
    # Discovered generically — no hardcoded solution IDs.
    for (m2, s2, nm, rules) in km.solutions(cur, PKG):
        if m2 != '145' or rules == 0:
            continue
        mp_ops = km.flatten_user_blob_ops(cur, PKG, m2, s2)
        if mp_ops:
            ops.extend(mp_ops)
            notes_head.append("flatten '%s': stripped to 0 rules" % nm)
        else:
            notes_head.append("flatten '%s': already 0 rules" % nm)
    smk = cur.execute("SELECT value FROM configs WHERE key='androws.%s.smk'" % PKG).fetchone()[0]
    con.close()
    for tid in blind_tex_ids():
        smk, comm = _blind_tex_in_text(smk, tid)
        if comm is None:
            notes_head.append('blind: Tex %d already absent' % tid)
        else:
            ops.append(('UPDATE configs SET value=? WHERE key=?',
                        (smk, 'androws.%s.smk' % PKG),
                        'blind Tex %d (%s)' % (tid, comm)))
            notes_head.append('blind: Tex %d trigger removed (%s)' % (tid, comm))
    res = dbmod.guarded_updates(dbp, ops, dry_run=dry)
    notes = notes_head + res['notes']
    try:
        notes += _blind_smk_file(None, dry=dry)
    except Exception as e:  # noqa - DB part already applied + backed up
        notes.append('smk.conf blinding FAILED: %s' % e)
    try:
        notes += fix_quarantine(dry=dry)
    except Exception as e:  # noqa - quarantine is best-effort hardening
        notes.append('quarantine FAILED: %s' % e)
    return notes


def fix_clarity(level=2, dry=False):
    dbp = _need_db()
    con = _db_con(dbp)
    ops = perfmod.set_clarity_ops(con.cursor(), PKG, level)
    con.close()
    return dbmod.guarded_updates(
        dbp, ops, dry_run=dry,
        verify=lambda c: perfmod.clarity_verify(c, PKG, level))['notes']


def fix_flags(flag, val, dry=False):
    dbp = _need_db()
    con = _db_con(dbp)
    ops = perfmod.set_flag_ops(con.cursor(), PKG, flag, val)
    con.close()
    return dbmod.guarded_updates(dbp, ops, dry_run=dry)['notes']


def fix_alloc(cores, mem, dry=False):
    h = procs.host_resources()
    host_c, host_gb = h['cores'], h['mem_gb']
    if dry:
        return ['DRYRUN: alloc %dc/%dGB (host %dc/%dGB)' % (cores, mem, host_c, host_gb)]
    before = reg.perf_allocation()
    notes = reg.set_perf(int(cores), int(mem), host_cores=host_c, host_mem_gb=host_gb)
    return notes, before


# ── Fix registry (key -> metadata) ──────────────────────────────────────

FIXES = {
    'fullchain': dict(tab='Keymaps', star=True,
                label='DISABLE AUTO-SWITCH (recommended: pin + flatten + blind + quarantine)',
                why=('Pins a fixed-key layout, flattens smart destinations so any jump '
                     'lands on fixed keys, blinds the DMZ + BR jump-trigger textures '
                     '(DB + smk.conf, then locks smk.conf read-only), clears jump history, '
                     'and quarantines the engine\'s local template pack so the runtime '
                     'restore of blinded textures is starved. The engine can still jump '
                     'layouts, but every landing should be fixed keys — change layouts '
                     'manually in the GameLoop UI. If the engine reverts, the drift alert fires.'),
                apply=lambda dry: (fix_fullchain(dry), None)),
    'pin': dict(tab='Keymaps', label='Pin flat multiplayer layout only',
                why='Points lastmodeid + mode solution at a 0-rule layout. Generic: discovers IDs on this PC.',
                apply=lambda dry: (fix_pin_flat(dry), None)),
    'flatdmz': dict(tab='Keymaps', label='Flatten DMZ destination',
                    why='Replaces smart DMZ user layout with 0-rule version + registers it.',
                    apply=lambda dry: (fix_flatten('531686', '1063553', 'DMZ', dry), None)),
    'flatbr': dict(tab='Keymaps', label='Flatten Battle-Royale destination',
                   why='Same for mode 146. Official rows untouched (trigger-guarded).',
                   apply=lambda dry: (fix_flatten('146', '146', 'Battle Royale', dry), None)),
    'blind': dict(tab='Keymaps', label='Blind mode-jump trigger textures (Tex 10004/10046 + 30xxx twins)',
                   why='Removes the templates behind the DMZ (lobby) and BR (skill-box) jumps, '
                       'including the 30xxx series twins the engine matches as second bindings '
                       '(blinding 10046 alone left BR firing). DB + smk.conf. Reversible via backups.',
                   apply=lambda dry: (fix_blind_tex(dry), None)),
    'scrub': dict(tab='Keymaps', label='Scrub recently-used to 145-only',
                  why='Clears engine jump history pointers.',
                  apply=lambda dry: (fix_scrub_recent(dry), None)),
    'hostsblock': dict(tab='Keymaps', star=False,
                       label='EXPERIMENTAL: block mode-map download host',
                       why=('Adds "0.0.0.0 pckeymap.yyb.qq.com" to the hosts file so '
                            'the engine cannot re-download the mode maps that restore '
                            'blinded trigger textures. Landings still land on flat blobs '
                            '(the durable guarantee). If launch breaks, use the unblock fix.'),
                       apply=lambda dry: (fix_hosts_block(dry), None)),
    'hostsunblock': dict(tab='Keymaps',
                         label='Undo: unblock mode-map download host',
                         why='Removes the tuner hosts entry. Use if the game misbehaves.',
                         apply=lambda dry: (fix_hosts_unblock(dry), None)),
    'quarantine': dict(tab='Keymaps', star=False,
                       label='Quarantine LOCAL mode-map pack + lock smk.conf',
                       why=('The engine restores blinded smk.conf templates at runtime from a '
                            'LOCAL zip packed in the install dir (ano_tmp/ob_*.zip) — the network '
                            'block never stops that. Quarantining that pack + integrity caches and '
                            'marking smk.conf read-only starves the restore. Run after any '
                            'GameLoop update. Reversible via the quarantine-undo fix.'),
                       apply=lambda dry: (fix_quarantine(dry), None)),
    'unquarantine': dict(tab='Keymaps',
                         label='Undo: restore quarantined template pack',
                         why='Moves the ano_tmp pack + caches back and unlocks smk.conf. Use if the app misbehaves.',
                         apply=lambda dry: (fix_unquarantine(dry), None)),
    'latency': dict(tab='Performance', label='Low-latency ON',
                    why='Smaller frame queue = best feel-per-cost.',
                    apply=lambda dry: (fix_flags('low_latency_mode', True, dry), None)),
    'noframegen': dict(tab='Performance', label='Frame-gen OFF',
                       why='Fake frames add input lag. Real fps > fake fps.',
                       apply=lambda dry: (fix_flags('default_frame_gen_on', False, dry), None)),
    'priority': dict(tab='VM & Priority', label='Set live process priorities',
                     why='Emulator+VM → High, UI → High, Assistant → Above, DldSvr → Below. '
                         'Applies to running processes now — resets on exit.',
                     apply=lambda dry: (['DRYRUN: priorities preview'] if dry else procs.set_priorities(), None)),
}


# ═══════════════════════════════════════════════════════════════════════════
#  Confirm-closed helper
# ═══════════════════════════════════════════════════════════════════════════

def ask_close_needed(parent) -> bool:
    """Non-blocking: ask the user whether to close GameLoop. Returns True/False."""
    live = procs.running()
    if not live:
        return True
    return messagebox.askyesno(
        'GameLoop running',
        'GameLoop is running (%d procs). DB writes need it closed.\n\nClose it now?' % len(live),
        parent=parent
    )


def do_close_ladder(callback=None) -> bool:
    """Blocking close ladder — MUST run off the main thread.

    Runs graceful -> force -> service-stop. If callback is provided, it is
    called with status strings on each stage (for live log updates).
    Returns True if GameLoop is fully closed.
    """
    def _emit(msg):
        if callback:
            callback(msg)
        LOG.info('close ladder: %s', msg)

    res = procs.close_gameloop()
    for n in res['notes']:
        _emit(n)
    if res['ok']:
        return True
    _emit('resisted: %s' % ', '.join('%s(%d)' % (p['name'], p['pid']) for p in res['left']))
    res = procs.close_gameloop(force=True, stop_service_now=True)
    for n in res['notes']:
        _emit(n)
    if res['ok']:
        _emit('closed (forced).')
        return True
    _emit('still alive after force+service — user must quit tray manually.')
    return False


def needs_gl_closed(keys: list[str]) -> bool:
    """Do any of these fix keys require GameLoop to be closed?"""
    db_kinds = {'fullchain', 'pin', 'flatdmz', 'flatbr', 'blind', 'scrub',
                'clarity', 'latency', 'noframegen', 'quarantine', 'unquarantine'}
    return any(k in db_kinds for k in keys)


# ═══════════════════════════════════════════════════════════════════════════
#  State detection — what's already applied?
# ═══════════════════════════════════════════════════════════════════════════

def detect_applied() -> set[str]:
    """Read the DB/registry/hosts and return the set of fix keys already in effect."""
    applied = set()
    dbp = paths.appdata_db()
    if not dbp:
        return applied
    try:
        con = dbmod.connect(dbp)
        cur = con.cursor()

        # fullchain: mode 145 active && flat DMZ+BR blobs && smk file blinded &&
        # pack quarantined. recentlyused is NOT a gate — the engine re-fills
        # history in memory and every landing is flat, so it's cosmetic.
        # (It still gates the standalone `scrub` badge below.)
        try:
            ru = cur.execute("SELECT value0 FROM %s WHERE key=?" % km.CACHE,
                             ('recentlyused_mode.%s' % PKG,)).fetchone()
            ru_val = json.loads(ru[0]) if ru else {}
            ru_keys = set(ru_val.keys()) if isinstance(ru_val, dict) else set()
            is_ru_clean = (ru_keys <= {'145'})
        except Exception:
            is_ru_clean = False
        # The DB smk text is a dormant mirror the engine re-fills; the smk.conf
        # FILE is what actually drives scene detection, so cleanliness is judged
        # on the file.
        file_tex_ids = _smk_file_tex_ids()
        tex_clean = not (set(blind_tex_ids()) & file_tex_ids)

        # Check flat DMZ + BR blobs
        flat_dmz = flat_br = False
        for m, s, nm, rules in km.solutions(cur, PKG):
            if m == '531686' and s == '1063553' and rules == 0:
                flat_dmz = True
            if m == '146' and s == '146' and rules == 0:
                flat_br = True

        # pin: mode 145's active solution has 0 rules
        pin_ok = False
        for m, s, nm, rules in km.solutions(cur, PKG):
            if m == '145' and rules == 0:
                pin_ok = True
                break

        quar_ok = _quarantine_ok()

        if pin_ok and flat_dmz and flat_br and tex_clean and quar_ok:
            applied.add('fullchain')
        elif pin_ok:
            applied.add('pin')
        if flat_dmz and 'fullchain' not in applied:
            applied.add('flatdmz')
        if flat_br and 'fullchain' not in applied:
            applied.add('flatbr')
        if tex_clean and 'fullchain' not in applied:
            applied.add('blind')
        if is_ru_clean and 'fullchain' not in applied:
            applied.add('scrub')
        if quar_ok and 'fullchain' not in applied:
            applied.add('quarantine')

        con.close()
    except Exception:
        pass

    # hosts block
    try:
        if hostsmod.is_blocked():
            applied.add('hostsblock')
    except Exception:
        pass

    return applied


# ═══════════════════════════════════════════════════════════════════════════
#  Page classes
# ═══════════════════════════════════════════════════════════════════════════

class FixPage(ctk.CTkScrollableFrame):
    """A page with fix checkboxes that register into app.pending."""

    INTROS = {
        'Keymaps': 'Auto-switch = the emulator jumping layouts by itself. Tick fixes and use Apply All.',
        'Performance': 'Feed the VM and tune render tiers. Allocation needs a GameLoop restart.',
        'VM & Priority': 'Priorities apply to RUNNING processes — launch the game first, then Apply All.',
    }

    def __init__(self, parent, tabname: str, app, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY,
                         scrollbar_button_color=T.BORDER,
                         scrollbar_button_hover_color=T.TEXT_DIM, **kw)
        self.app = app
        self.tabname = tabname
        self.vars: dict[str, tk.BooleanVar] = {}

        # ── Page title ───────────────────────────────────────────────────
        ctk.CTkLabel(self, text=tabname, font=T.FONT_TITLE,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                     pady=(T.PAGE_PAD_Y, 4))
        if tabname in self.INTROS:
            ctk.CTkLabel(self, text=self.INTROS[tabname], font=T.FONT_SMALL,
                         text_color=T.TEXT_MUTED, wraplength=680).pack(
                anchor='w', padx=T.PAGE_PAD_X, pady=(0, 12))

        # ── Fix checkboxes ───────────────────────────────────────────────
        keys = [k for k, v in FIXES.items() if v['tab'] == tabname]
        starred = [k for k in keys if FIXES[k].get('star')]
        advanced = [k for k in keys if not FIXES[k].get('star')]

        # Hero card for starred fix
        for k in starred:
            self._add_hero_fix(k)

        # Advanced expander for the rest
        if advanced and starred:
            self._expander = ctk.CTkFrame(self, fg_color='transparent')
            self._expander.pack(fill='x', padx=T.PAGE_PAD_X, pady=(8, 0))
            self._adv_visible = tk.BooleanVar(value=False)
            self._adv_toggle = ctk.CTkButton(
                self._expander, text='▸ Advanced options',
                anchor='w', font=T.FONT_SMALL, height=28,
                fg_color='transparent', hover_color=T.BG_HOVER,
                text_color=T.TEXT_DIM,
                command=self._toggle_advanced)
            self._adv_toggle.pack(anchor='w')
            self._adv_frame = ctk.CTkFrame(self._expander, fg_color='transparent')
            # Hidden initially
            for k in advanced:
                self._add_fix(k, self._adv_frame)
        else:
            # No hero/advanced split — just list them all
            for k in (starred + advanced):
                self._add_fix(k, self)

        # ── Performance extras (sliders / clarity) ───────────────────────
        if tabname == 'Performance':
            self._build_perf_extras()

    def _add_hero_fix(self, k: str):
        """Render a prominent hero card for the recommended fix."""
        v = FIXES[k]
        var = tk.BooleanVar(value=False)
        self.vars[k] = var

        card = ctk.CTkFrame(self, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                            border_width=2, border_color=T.BORDER_HERO)
        card.pack(fill='x', padx=T.PAGE_PAD_X, pady=(0, 8))

        ctk.CTkLabel(card, text='⭐  RECOMMENDED', font=T.FONT_SMALL,
                     text_color=T.ACCENT).pack(anchor='w', padx=14, pady=(10, 2))
        ctk.CTkCheckBox(card, text=v['label'], variable=var,
                        font=T.FONT_HEADING, text_color=T.TEXT_PRIMARY,
                        fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                        border_color=T.BORDER,
                        command=lambda: self._on_toggle(k)).pack(
            anchor='w', padx=14, pady=(0, 4))
        ctk.CTkLabel(card, text=v['why'], font=T.FONT_SMALL,
                     text_color=T.TEXT_MUTED, wraplength=620, justify='left').pack(
            anchor='w', padx=14, pady=(0, 12))

    def _add_fix(self, k: str, parent):
        """Render a regular fix checkbox row."""
        v = FIXES[k]
        var = tk.BooleanVar(value=False)
        self.vars[k] = var

        row = ctk.CTkFrame(parent, fg_color=T.BG_CARD, corner_radius=8,
                           border_width=0, border_color=T.BORDER)
        row.pack(fill='x', padx=T.PAGE_PAD_X if parent is self else 0, pady=3)

        inner = ctk.CTkFrame(row, fg_color='transparent')
        inner.pack(fill='x', padx=10, pady=8)
        ctk.CTkCheckBox(inner, text=v['label'], variable=var,
                        font=T.FONT_BODY, text_color=T.TEXT_PRIMARY,
                        fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                        border_color=T.BORDER,
                        command=lambda: self._on_toggle(k)).pack(anchor='w')
        ctk.CTkLabel(inner, text=v['why'], font=T.FONT_SMALL,
                     text_color=T.TEXT_DIM, wraplength=600, justify='left').pack(
            anchor='w', padx=26, pady=(2, 0))

    def _toggle_advanced(self):
        if self._adv_visible.get():
            self._adv_frame.pack_forget()
            self._adv_toggle.configure(text='▸ Advanced options')
            self._adv_visible.set(False)
        else:
            self._adv_frame.pack(fill='x', pady=(4, 0))
            self._adv_toggle.configure(text='▾ Advanced options')
            self._adv_visible.set(True)

    def _on_toggle(self, k: str):
        """Register/deregister a fix in the shared pending queue."""
        if self.vars[k].get():
            self.app.pending[k] = FIXES[k]
        else:
            self.app.pending.pop(k, None)
        self.app.update_pending_count()

    # ── Performance extras ───────────────────────────────────────────────

    def _build_perf_extras(self):
        h = procs.host_resources()
        maxc = max(2, (h['cores'] or 8) - 1)
        maxm = max(3, (h['mem_gb'] or 16) - 2)

        # ── Allocation card ──────────────────────────────────────────────
        alloc_card = ctk.CTkFrame(self, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                                   border_width=0, border_color=T.BORDER)
        alloc_card.pack(fill='x', padx=T.PAGE_PAD_X, pady=(12, 4))

        self.alloc_var = tk.BooleanVar(value=False)
        ctk.CTkCheckBox(alloc_card, text='Feed the VM (drag sliders, then Apply All)',
                        variable=self.alloc_var, font=T.FONT_BODY,
                        text_color=T.TEXT_PRIMARY,
                        fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                        border_color=T.BORDER,
                        command=self._on_alloc_toggle).pack(
            anchor='w', padx=14, pady=(12, 8))

        # Cores slider
        cores_row = ctk.CTkFrame(alloc_card, fg_color='transparent')
        cores_row.pack(fill='x', padx=14)
        ctk.CTkLabel(cores_row, text='CPU Cores', font=T.FONT_SMALL,
                     text_color=T.TEXT_MUTED, width=80).pack(side='left')
        self.cores_lbl = ctk.CTkLabel(cores_row, text='6', font=T.FONT_HEADING,
                                       text_color=T.ACCENT, width=30)
        self.cores_lbl.pack(side='left', padx=(0, 8))
        ctk.CTkLabel(cores_row, text='/ %d' % h['cores'], font=T.FONT_SMALL,
                     text_color=T.TEXT_DIM).pack(side='left')
        self.cores = ctk.CTkSlider(alloc_card, from_=2, to=maxc,
                                    number_of_steps=maxc - 2,
                                    progress_color=T.ACCENT,
                                    button_color=T.ACCENT,
                                    button_hover_color=T.ACCENT_HOVER,
                                    fg_color=T.BORDER,
                                    command=lambda v: self.cores_lbl.configure(
                                        text='%d' % int(float(v))))
        self.cores.pack(fill='x', padx=14, pady=(2, 8))
        self.cores.set(min(6, maxc))

        # RAM slider
        mem_row = ctk.CTkFrame(alloc_card, fg_color='transparent')
        mem_row.pack(fill='x', padx=14)
        ctk.CTkLabel(mem_row, text='RAM (GB)', font=T.FONT_SMALL,
                     text_color=T.TEXT_MUTED, width=80).pack(side='left')
        self.mem_lbl = ctk.CTkLabel(mem_row, text='10', font=T.FONT_HEADING,
                                     text_color=T.ACCENT, width=30)
        self.mem_lbl.pack(side='left', padx=(0, 8))
        ctk.CTkLabel(mem_row, text='/ %d' % h['mem_gb'], font=T.FONT_SMALL,
                     text_color=T.TEXT_DIM).pack(side='left')
        self.mem = ctk.CTkSlider(alloc_card, from_=3, to=maxm,
                                  number_of_steps=maxm - 3,
                                  progress_color=T.ACCENT,
                                  button_color=T.ACCENT,
                                  button_hover_color=T.ACCENT_HOVER,
                                  fg_color=T.BORDER,
                                  command=lambda v: self.mem_lbl.configure(
                                      text='%d' % int(float(v))))
        self.mem.pack(fill='x', padx=14, pady=(2, 4))
        self.mem.set(min(10, maxm))

        ctk.CTkLabel(alloc_card, text='Sliders capped to leave Windows room. Needs GameLoop restart.',
                     font=T.FONT_SMALL, text_color=T.TEXT_DIM).pack(
            anchor='w', padx=14, pady=(0, 12))

        # ── Clarity card ─────────────────────────────────────────────────
        clr_card = ctk.CTkFrame(self, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                                 border_width=0, border_color=T.BORDER)
        clr_card.pack(fill='x', padx=T.PAGE_PAD_X, pady=4)

        self.clarity_var = tk.BooleanVar(value=False)
        ctk.CTkCheckBox(clr_card, text='Set clarity tier', variable=self.clarity_var,
                        font=T.FONT_BODY, text_color=T.TEXT_PRIMARY,
                        fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                        border_color=T.BORDER,
                        command=self._on_clarity_toggle).pack(
            anchor='w', padx=14, pady=(12, 6))

        seg_row = ctk.CTkFrame(clr_card, fg_color='transparent')
        seg_row.pack(fill='x', padx=14)
        self.clarity_seg = ctk.CTkSegmentedButton(
            seg_row, values=['1  Low', '2  Clearer texture + more FPS ⭐', '3  High'],
            font=T.FONT_SMALL,
            selected_color=T.ACCENT, selected_hover_color=T.ACCENT_HOVER,
            unselected_color=T.BG_HOVER, unselected_hover_color=T.BORDER,
            text_color='#000000', text_color_disabled=T.TEXT_DIM)
        self.clarity_seg.pack(side='left', pady=4)
        self.clarity_seg.set('2  Clearer texture + more FPS ⭐')

        ctk.CTkLabel(clr_card,
                     text='Clarity = render-resolution tier (no official docs — label is our best guess). '
                          'TESTED: tier 1 was laggier than 3 '
                          '(pushes work to CPU) — tier 2 is the current sweet spot: clearer texture + more FPS.',
                     font=T.FONT_SMALL, text_color=T.TEXT_DIM, wraplength=620,
                     justify='left').pack(anchor='w', padx=14, pady=(2, 12))

        # ── Renderer card (read-only) ────────────────────────────────────
        try:
            alloc = reg.perf_allocation()
            rm = alloc.get('RenderMode', '?')
            rut = alloc.get('RenderUserType', '?')
            rm_txt = '%s — %s' % (rm, reg.render_mode_label(rm))
            rut_txt = '%s — %s' % (rut, reg.render_user_label(rut))
        except Exception:
            rm_txt = rut_txt = '?'
        rnd_card = ctk.CTkFrame(self, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                                border_width=0, border_color=T.BORDER)
        rnd_card.pack(fill='x', padx=T.PAGE_PAD_X, pady=4)
        ctk.CTkLabel(rnd_card, text='Renderer (read-only)', font=T.FONT_BODY,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=14, pady=(12, 2))
        ctk.CTkLabel(rnd_card, text='RenderMode = %s\nRenderUserType = %s' % (rm_txt, rut_txt),
                     font=T.FONT_SMALL, text_color=T.TEXT_MUTED, justify='left').pack(
            anchor='w', padx=14)
        ctk.CTkLabel(rnd_card,
                     text='0 = Auto: the engine picks OpenGL+/DirectX+ per launched game '
                          '(the vendor path — leave it). 1/2 = force OpenGL+/DirectX+, 3 = Vulkan. '
                          'The Tuner never writes these: forcing a backend fights per-map auto-select, '
                          'and RenderUserType is the "I overrode it" marker.',
                     font=T.FONT_SMALL, text_color=T.TEXT_DIM, wraplength=620,
                     justify='left').pack(anchor='w', padx=14, pady=(2, 12))

    def _on_alloc_toggle(self):
        if self.alloc_var.get():
            self.app.pending['alloc'] = {'type': 'alloc'}
        else:
            self.app.pending.pop('alloc', None)
        self.app.update_pending_count()

    def _on_clarity_toggle(self):
        if self.clarity_var.get():
            self.app.pending['clarity'] = {'type': 'clarity'}
        else:
            self.app.pending.pop('clarity', None)
        self.app.update_pending_count()

    def get_alloc_values(self) -> tuple[int, int]:
        """Return (cores, mem_gb) from sliders."""
        return int(float(self.cores.get())), int(float(self.mem.get()))

    def get_clarity_level(self) -> int:
        """Return clarity level from segmented button."""
        text = self.clarity_seg.get()
        return int(text[0])  # first char is the digit


# ═══════════════════════════════════════════════════════════════════════════
#  Backup page
# ═══════════════════════════════════════════════════════════════════════════

class BackupPage(ctk.CTkScrollableFrame):
    """Backup management page with styled backup list."""

    def __init__(self, parent, app, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY,
                         scrollbar_button_color=T.BORDER,
                         scrollbar_button_hover_color=T.TEXT_DIM, **kw)
        self.app = app

        # Title
        ctk.CTkLabel(self, text='Backups', font=T.FONT_TITLE,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                     pady=(T.PAGE_PAD_Y, 4))
        ctk.CTkLabel(self, text='Timestamped Configs.db backups. Restore to undo changes.',
                     font=T.FONT_SMALL, text_color=T.TEXT_MUTED).pack(
            anchor='w', padx=T.PAGE_PAD_X, pady=(0, 12))

        # Buttons
        btn_row = ctk.CTkFrame(self, fg_color='transparent')
        btn_row.pack(fill='x', padx=T.PAGE_PAD_X, pady=(0, 8))
        ctk.CTkButton(btn_row, text='Backup Now', width=120, height=34,
                      fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                      text_color='#000000', font=T.FONT_BODY,
                      command=self._backup).pack(side='left', padx=(0, 8))
        ctk.CTkButton(btn_row, text='↻  Refresh', width=100, height=34,
                      fg_color=T.BG_CARD, hover_color=T.BG_HOVER,
                      text_color=T.TEXT_PRIMARY, font=T.FONT_BODY,
                      border_width=0, border_color=T.BORDER,
                      command=self._refresh_list).pack(side='left')

        # Backup list
        self._list_frame = ctk.CTkFrame(self, fg_color=T.BG_CARD,
                                          corner_radius=T.CARD_RADIUS,
                                          border_width=0, border_color=T.BORDER)
        self._list_frame.pack(fill='both', expand=True, padx=T.PAGE_PAD_X, pady=4)
        self._items: list[dict] = []
        self._rows: list[ctk.CTkFrame] = []
        self._selected_idx: int = -1

        self._refresh_list()

    def _refresh_list(self):
        """Reload backup list."""
        for w in self._rows:
            w.destroy()
        self._rows.clear()
        self._selected_idx = -1
        dbp = paths.appdata_db()
        self._items = bakmod.list_backups(dbp) if dbp else []
        if not self._items:
            ctk.CTkLabel(self._list_frame, text='No backups found.',
                         font=T.FONT_SMALL, text_color=T.TEXT_DIM).pack(
                padx=12, pady=12)
            return
        for i, b in enumerate(reversed(self._items)):
            fname = b['path'].replace('\\', '/').split('/')[-1]
            size_kb = b['bytes'] // 1024
            row = ctk.CTkFrame(self._list_frame, fg_color='transparent',
                               height=36, corner_radius=6)
            row.pack(fill='x', padx=6, pady=2)
            row.pack_propagate(False)
            idx = len(self._items) - 1 - i  # actual index in self._items
            row.bind('<Button-1>', lambda e, x=idx, r=row: self._select(x, r))

            ctk.CTkLabel(row, text='💾', font=('Segoe UI', 11), width=24).pack(
                side='left', padx=(8, 4))
            name_lbl = ctk.CTkLabel(row, text=fname, font=T.FONT_MONO,
                                     text_color=T.TEXT_PRIMARY, anchor='w')
            name_lbl.pack(side='left', fill='x', expand=True)
            name_lbl.bind('<Button-1>', lambda e, x=idx, r=row: self._select(x, r))
            ctk.CTkLabel(row, text='%d KB' % size_kb, font=T.FONT_SMALL,
                         text_color=T.TEXT_DIM, width=70).pack(side='right', padx=8)
            # Restore button
            ctk.CTkButton(row, text='Restore', width=70, height=26,
                          fg_color=T.BG_HOVER, hover_color=T.ERROR_BG,
                          text_color=T.ERROR, font=T.FONT_SMALL,
                          command=lambda x=idx: self._restore(x)).pack(
                side='right', padx=4)
            self._rows.append(row)

    def _select(self, idx: int, row: ctk.CTkFrame):
        for r in self._rows:
            r.configure(fg_color='transparent')
        row.configure(fg_color=T.BG_HOVER)
        self._selected_idx = idx

    def _backup(self):
        import threading

        def work():
            try:
                dbp = _need_db()
                dst = dbmod.backup(dbp)
                fname = dst.replace('\\', '/').split('/')[-1]
                self.app.after(0, lambda: (
                    self.app.log('Backup created: %s' % fname, 'success'),
                    self._refresh_list()))
            except Exception as e:
                self.app.after(0, lambda: self.app.log('Backup failed: %s' % e, 'error'))
        threading.Thread(target=work, daemon=True).start()

    def _restore(self, idx: int):
        if not ask_close_needed(self.app):
            self.app.log('Restore cancelled — close GameLoop first.', 'warning')
            return
        import threading
        item = self._items[idx]

        def work():
            # Close GameLoop off main thread
            def _close_log(msg):
                self.app.after(0, lambda m=msg: self.app.log(m, 'info'))
            if procs.running() and not do_close_ladder(callback=_close_log):
                self.app.after(0, lambda: self.app.log(
                    'Restore aborted — GameLoop still running.', 'error'))
                return
            try:
                notes = bakmod.restore_db(paths.appdata_db(), item['path'])
                self.app.after(0, lambda: (
                    self.app.log_many(notes, 'success'),
                    self.app.log('Restore complete. Relaunch GameLoop.', 'success'),
                    self._refresh_list()))
            except Exception as e:
                self.app.after(0, lambda: self.app.log('Restore failed: %s' % e, 'error'))
        threading.Thread(target=work, daemon=True).start()


# ═══════════════════════════════════════════════════════════════════════════
#  Log page (wraps LogConsole)
# ═══════════════════════════════════════════════════════════════════════════

class LogPage(ctk.CTkFrame):
    """Full-page log console view."""

    def __init__(self, parent, app, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY, **kw)
        self.app = app
        # Title
        ctk.CTkLabel(self, text='Activity Log', font=T.FONT_TITLE,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                     pady=(T.PAGE_PAD_Y, 4))
        ctk.CTkLabel(self, text='Every operation from all pages, with secrets stripped.',
                     font=T.FONT_SMALL, text_color=T.TEXT_MUTED).pack(
            anchor='w', padx=T.PAGE_PAD_X, pady=(0, 8))
        # The actual LogConsole widget is set from main.py after creation
        self._console_slot = ctk.CTkFrame(self, fg_color='transparent')
        self._console_slot.pack(fill='both', expand=True, padx=T.PAGE_PAD_X,
                                 pady=(0, T.PAGE_PAD_Y))

    @property
    def console_slot(self):
        return self._console_slot


# ═══════════════════════════════════════════════════════════════════════════
#  About page
# ═══════════════════════════════════════════════════════════════════════════

ABOUT_TEXT = """GL Tuner by Codmsurge

WHAT THIS TOOL IS FOR
  Diagnose, tune, and fix GameLoop/CODM configuration on any PC without hand-
  editing DB files. Aim for better performance, lower latency, and stable
  keymaps that resist GameLoop auto-switching (if the engine reverts, the
  drift alert fires).

FEATURES
  • Dashboard: Live read-only status cards (processes, allocation, keymap, drive space).
  • Keymaps: DISABLE AUTO-SWITCH (one-tick chain: pin flat layout, flatten smart 
    destinations, blind lobby trigger, clear jump history).
  • Performance: Tune VM cores/RAM, clarity tier, low-latency, and frame-gen.
  • VM & Priority: Set live process priorities (emulator/VM high, assistant above, downloader below) to reduce stutter when the host is loaded.
  • Backups: Snapshot and restore Configs.db in a single click.
  • Smart Drift Detection: Alerts you if GameLoop silently reverts your settings!

PORTABLE & SAFE — no hardcoded paths. All writes are backed up and verified.
"""

class AboutPage(ctk.CTkScrollableFrame):
    """About / help page."""

    def __init__(self, parent, app, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY, **kw)
        
        # ── Title ────────────────────────────────────────────────────────
        ctk.CTkLabel(self, text='About', font=T.FONT_TITLE,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                     pady=(T.PAGE_PAD_Y, 2))
        ctk.CTkLabel(self, text='GL Tuner by Codmsurge', font=T.FONT_HEADING,
                     text_color=T.ACCENT).pack(anchor='w', padx=T.PAGE_PAD_X, pady=(0, 12))

        # ── Discord Section ──────────────────────────────────────────────
        discord_card = ctk.CTkFrame(self, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                                     border_width=0, border_color=T.BORDER)
        discord_card.pack(fill='x', padx=T.PAGE_PAD_X, pady=(0, 16))
        
        discord_inner = ctk.CTkFrame(discord_card, fg_color='transparent')
        discord_inner.pack(fill='x', padx=14, pady=14)
        
        # Try to load discord.png
        try:
            import os
            import sys
            from PIL import Image
            def _resource_path(relative_path):
                try:
                    base_path = sys._MEIPASS
                except Exception:
                    base_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
                return os.path.join(base_path, relative_path)

            d_path = _resource_path('discord.png')
            if os.path.exists(d_path):
                d_img = ctk.CTkImage(light_image=Image.open(d_path), dark_image=Image.open(d_path), size=(40, 40))
                ctk.CTkLabel(discord_inner, text='', image=d_img).pack(side='left', padx=(0, 12))
        except Exception:
            pass
            
        text_frame = ctk.CTkFrame(discord_inner, fg_color='transparent')
        text_frame.pack(side='left')
        
        ctk.CTkLabel(text_frame, text='Codmsurge Discord', font=T.FONT_HEADING,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w')
        ctk.CTkLabel(text_frame, text='Contact me: getskulled666', font=T.FONT_SMALL,
                     text_color=T.TEXT_MUTED).pack(anchor='w')

        import webbrowser
        ctk.CTkButton(discord_inner, text='Join Server', width=110, height=32,
                      fg_color='#5865F2', hover_color='#4752C4', text_color='#FFFFFF',
                      font=T.FONT_HEADING,
                      command=lambda: webbrowser.open('https://discord.gg/5qcbzSPcD')).pack(side='right', padx=4)

        # ── About Text ───────────────────────────────────────────────────
        box = ctk.CTkTextbox(self, fg_color=T.BG_CARD, text_color=T.TEXT_PRIMARY,
                              font=T.FONT_MONO, corner_radius=T.CARD_RADIUS,
                              border_width=0, border_color=T.BORDER,
                              scrollbar_button_color=T.BORDER)
        box.pack(fill='both', expand=True, padx=T.PAGE_PAD_X, pady=(0, T.PAGE_PAD_Y))
        # Arbitrary height to show the text without internal scrolling if possible
        box.configure(height=380)
        box.insert('end', ABOUT_TEXT)
        box.configure(state='disabled')
