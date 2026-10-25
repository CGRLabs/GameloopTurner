"""Keymap system: generic discovery (no hardcoded IDs), pin, flatten, blind, drift."""
import json
import re
import sqlite3

CACHE = 'key_map_configs_cache_official'


def modes(cur: sqlite3.Cursor, pkg: str) -> dict:
    out = {}
    for (k, v0) in cur.execute(
            "SELECT key, value0 FROM %s WHERE key LIKE 'keymap_mode.unlogin.%s.%%'" % (CACHE, pkg)):
        try:
            out[k.rsplit('.', 1)[-1]] = json.loads(v0)
        except Exception:
            pass
    return out


def solutions(cur: sqlite3.Cursor, pkg: str) -> list:
    """[(mode, sol, name, switch_rules)] across all modes."""
    rows = []
    for mode, m in modes(cur, pkg).items():
        for sol, s in ((m.get('solutions') or {}).items()):
            r = cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                            ('keymap_data.unlogin.%s.%s.%s' % (pkg, mode, sol),)).fetchone()
            if r and r[0]:
                rows.append((mode, sol, s.get('solutionName', ''), r[0].count('SwitchOperation')))
    return rows


def pick_flat(rows: list, prefer: str = 'mp') -> tuple | None:
    flats = [c for c in rows if c[3] == 0]
    if not flats:
        return None
    for p in (prefer, 'multi', 'br'):
        for c in flats:
            if p in c[2].lower():
                return c
    return flats[0]


def jump_tags(cur: sqlite3.Cursor, pkg: str) -> list:
    """Switch entries carrying ModeID (scene->mode jump table)."""
    v1 = cur.execute('SELECT value1 FROM %s WHERE key=?' % CACHE,
                     ('offcial_config2.%s' % pkg,)).fetchone()
    if not v1:
        return []
    return re.findall(r'<Switch [^>]*ModeID="(\d+)"[^>]*>', v1[0])


def jump_tags_detail(cur: sqlite3.Cursor, pkg: str) -> list:
    v1 = cur.execute('SELECT value1 FROM %s WHERE key=?' % CACHE,
                     ('offcial_config2.%s' % pkg,)).fetchone()
    if not v1:
        return []
    return re.findall(r'<Switch [^>]*>', v1[0])


def tex_to_switch(tex_id: int) -> int:
    """Proven mapping: Tex id = 10000 + TextureId."""
    return tex_id - 10000


def smk_entries(cur: sqlite3.Cursor, pkg: str) -> list:
    smk = cur.execute("SELECT value FROM configs WHERE key='androws.%s.smk'" % pkg).fetchone()
    if not smk:
        return []
    return re.findall(r'<Tex id="(\d+)"[^>]*\n?\s*comm="([^"]*)"', smk[0])


def flatten_blob(official_xml: str, sol_id: str, sol_name: str) -> str:
    flat = re.sub(r'<SwitchOperation\b[^>]*/>', '', official_xml)
    flat = re.sub(r'<SwitchOperation\b[^>]*>.*?</SwitchOperation>', '', flat, flags=re.S)
    return ('<KeyMapSolution SolutionID="{&quot;id&quot;:&quot;%s&quot;,&quot;type&quot;:1}" '
            'SolutionName="%s" InputType="1" SilentUpdate="0">' % (sol_id, sol_name)
            + flat + '</KeyMapSolution>')


def blob_present(cur: sqlite3.Cursor, pkg: str, mode: str, sol: str) -> bool:
    """Does the user blob for this mode/solution exist?"""
    r = cur.execute('SELECT 1 FROM %s WHERE key=?' % CACHE,
                    ('keymap_data.unlogin.%s.%s.%s' % (pkg, mode, sol),)).fetchone()
    return bool(r)


def flatten_user_blob_ops(cur: sqlite3.Cursor, pkg: str, mode: str, sol: str) -> list:
    """Strip SwitchOperations from an existing USER blob in place.

    Unlike official rows (trigger-protected), user blobs are writable.
    Keeps the blob's own <KeyMapSolution> envelope — no re-wrapping.
    Returns [] when missing or already flat.
    """
    r = cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                    ('keymap_data.unlogin.%s.%s.%s' % (pkg, mode, sol),)).fetchone()
    if not r or not r[0]:
        return []
    before = r[0].count('SwitchOperation')
    if before == 0:
        return []
    flat = re.sub(r'<SwitchOperation\b[^>]*/>', '', r[0])
    flat = re.sub(r'<SwitchOperation\b[^>]*>.*?</SwitchOperation>', '', flat, flags=re.S)
    return [('UPDATE %s SET value0=? WHERE key=?' % CACHE,
             (flat, 'keymap_data.unlogin.%s.%s.%s' % (pkg, mode, sol)),
             'flatten user blob %s/%s (%d -> 0 rules)' % (mode, sol, before))]


def pin_flat_ops(cur: sqlite3.Cursor, pkg: str, mode: str, sol: str) -> list:
    m = json.loads(cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                               ('keymap_mode.unlogin.%s.%s' % (pkg, mode),)).fetchone()[0])
    m['lastSolutionId'] = sol
    m['lastSolutionIdType'] = {'id': sol, 'type': 0}
    return [('UPDATE %s SET value0=? WHERE key=?' % CACHE,
             (json.dumps(m, separators=(',', ':')), 'keymap_mode.unlogin.%s.%s' % (pkg, mode)),
             'mode %s -> solution %s' % (mode, sol)),
            ('UPDATE %s SET value0=? WHERE key=?' % CACHE, (mode, 'lastmodeid.unlogin.%s' % pkg),
             'lastmodeid -> %s' % mode)]


def register_mode_ops(cur: sqlite3.Cursor, pkg: str, mode: str, sol: str, name: str) -> list:
    m = {"lastSolutionId": sol, "lastSolutionIdType": {"id": sol, "type": 1},
         "modeId": int(mode) if str(mode).isdigit() else mode,
         "solutions": {sol: {"inputType": 1, "level": 1, "solutionId": sol, "solutionName": name,
                             "sourceDesc": {"solutionId": sol, "sourceType": 1}}}}
    return [('INSERT OR REPLACE INTO %s (key, value0) VALUES (?, ?)' % CACHE,
             ('keymap_mode.unlogin.%s.%s' % (pkg, mode), json.dumps(m, separators=(',', ':'))),
             'mode %s registered (%s)' % (mode, name))]


def drift_report(cur: sqlite3.Cursor, pkg: str) -> dict:
    last = cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                       ('lastmodeid.unlogin.%s' % pkg,)).fetchone()
    rec = cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                      ('recentlyused_mode.%s' % pkg,)).fetchone()
    m145 = cur.execute('SELECT value0 FROM %s WHERE key=?' % CACHE,
                       ('keymap_mode.unlogin.%s.145' % pkg,)).fetchone()
    sol = None
    if m145:
        try:
            sol = json.loads(m145[0]).get('lastSolutionId')
        except Exception:
            pass
    return {'lastmodeid': last[0] if last else None,
            'recentlyused': rec[0] if rec else None, 'mode145_solution': sol}
