"""Performance presets + plain-language explains. Values validated live on RX 5700 XT rig."""
import json
import sqlite3

EXPLAINS = {
    'low_latency': 'Smaller frame queue: input sampled closer to frame present. Best feel-per-cost. Keep ON.',
    'frame_gen': 'Fake interpolated frames inflate the FPS number but add input lag + artifacts. Real 120 beats fake 240. Keep OFF.',
    'clarity': 'Render-resolution tier (no official docs — best guess). NOTE (tested): tier 1 was LAGGIER than 3 on this engine — low tiers can shift work to CPU. Tier 2 is the current sweet spot: clearer texture + more FPS.',
    'frame_rate': 'Real frame cap. 120 is the competitive pick; 240 allowed but rarely real.',
    'softvideo': 'CPU decode of flat VIDEO clips (lobby/killcam) only — not 3D rendering. Leave ON.',
    'cores': 'VM CPU slice. Max sensible = host cores - 1. Needs GameLoop restart.',
    'mem': 'VM RAM slice. Max sensible = host GB - 2. Needs GameLoop restart.',
}


def local_user_config(cur: sqlite3.Cursor, pkg: str) -> dict:
    v = cur.execute("SELECT value FROM configs WHERE key='androws.%s.local_user_config'" % pkg).fetchone()
    return json.loads(v[0]) if v else {}


def set_clarity_ops(cur: sqlite3.Cursor, pkg: str, level: int) -> list:
    u = local_user_config(cur, pkg)
    before = u.get('clarity')
    u['clarity'] = int(level)
    return [('UPDATE configs SET value=? WHERE key=?',
             (json.dumps(u, separators=(',', ':')), 'androws.%s.local_user_config' % pkg),
             'clarity %s -> %d' % (before, level))]


def set_flag_ops(cur: sqlite3.Cursor, pkg: str, flag: str, val) -> list:
    u = local_user_config(cur, pkg)
    before = u.get(flag)
    u[flag] = val
    return [('UPDATE configs SET value=? WHERE key=?',
             (json.dumps(u, separators=(',', ':')), 'androws.%s.local_user_config' % pkg),
             '%s %s -> %s' % (flag, before, val))]


def clarity_verify(cur: sqlite3.Cursor, pkg: str, level: int) -> list:
    """Post-write re-read: proves the clarity write landed in the DB."""
    now = local_user_config(cur, pkg).get('clarity')
    ok = str(now) == str(level)
    return ['verify clarity: wrote %s, re-read %s -> %s' % (level, now, 'OK' if ok else 'MISMATCH')]


def host_config(cur: sqlite3.Cursor, pkg: str) -> dict:
    v = cur.execute("SELECT value FROM configs WHERE key='androws.%s.host_config'" % pkg).fetchone()
    return json.loads(v[0]) if v else {}
