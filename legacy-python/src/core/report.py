"""One-file diagnostic TXT for remote review: everything, secrets stripped, size-capped."""
import datetime
import json
import os

from . import db as dbmod
from . import disk as diskmod
from . import keymaps as km
from . import log as logmod
from . import paths
from . import perf as perfmod
from . import procs
from . import registry as reg

VERSION = '0.3'
SKIP_REG_KEYS = ('svid', 'SyzsGuid', 'cert_hash', 'VPoDioa', 'VOPoDioa', 'VWiDioa',
                 'ams_union_id_v2', 'cert_path', 'ip_latitude', 'ip_longitude')


def _clean(mapping: dict) -> dict:
    return {k: v for k, v in mapping.items() if k not in SKIP_REG_KEYS}


def build() -> str:
    L = []
    L.append('GameLoop Tuner diagnostic report v%s' % VERSION)
    L.append('generated: %s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    L.append('host: %s' % procs.host_resources())
    L.append('')
    root, dbp = paths.install_root(), paths.appdata_db()
    L.append('[paths]')
    L.append('install=' + (root or 'NOT FOUND'))
    L.append('db=' + ('present' if dbp else 'NOT FOUND'))
    L.append('build=' + (paths.app_version_dir(root) or '?'))
    L.append('adb=' + ('present' if paths.adb_exe(root) else 'missing'))
    L.append('')
    L.append('[processes]')
    for p in procs.list_gameloop():
        L.append('%s pid=%d mem_kb=%d' % (p['name'], p['pid'], p['mem_kb']))
    L.append('service=%s' % procs.service_status().get('state'))
    L.append('')
    L.append('[perf]')
    try:
        L.append(json.dumps(_clean(reg.perf_allocation())))
        L.append('common_full=' + json.dumps(logmod.scrub(json.dumps(_clean(reg.read_all(reg.COMMON))))))
        L.append('launcher=' + json.dumps(logmod.scrub(json.dumps(_clean(reg.read_all(reg.BASE + '\\Settings\\GameLoop'))))))
    except Exception as e:  # noqa
        L.append('registry unreadable: %s' % e)
    L.append('')
    if dbp:
        try:
            con = dbmod.connect(dbp, timeout=5)
            cur = con.cursor()
            L.append('[keymaps]')
            d = km.drift_report(cur, paths.PKG)
            L.append('lastmodeid=%s mode145=%s' % (d['lastmodeid'], d['mode145_solution']))
            L.append('recentlyused=%s' % logmod.scrub(str(d['recentlyused']))[:300])
            for s in km.solutions(cur, paths.PKG):
                L.append('layout mode=%s sol=%s name=%s switchRules=%d' % s)
            L.append('jump ModeIDs=%s' % km.jump_tags(cur, paths.PKG))
            L.append('triggers=%s' % [t[0] for t in dbmod.triggers(con)])
            L.append('[codm]')
            u = perfmod.local_user_config(cur, paths.PKG)
            for k in ('frame_rate', 'clarity', 'low_latency_mode', 'default_frame_gen_on',
                      'aspect_ratio', 'is_toolbar_open', 'hide_full_screen_topbar'):
                L.append('%s=%s' % (k, u.get(k)))
            L.append('host_fps=%s/%s' % (perfmod.host_config(cur, paths.PKG).get('default_frame_rate'),
                                         perfmod.host_config(cur, paths.PKG).get('maximum_frame_rate')))
            smk = cur.execute("SELECT value FROM configs WHERE key='androws.%s.smk'" % paths.PKG).fetchone()
            if smk:
                import re
                ids = re.findall(r'<Tex id="(\d+)"', smk[0])
                L.append('smk templates=%d has10004=%s' % (len(ids), '10004' in ids))
            con.close()
        except Exception as e:  # noqa
            L.append('db unreadable: %s' % e)
    L.append('')
    g = diskmod.game_drive_status(root)
    L.append('[disk] drive=%s free=%sGB min=%sGB ok=%s' % (g.get('drive'), g.get('free_gb'), g.get('min_gb'), g.get('ok')))
    for k, v in diskmod.footprint(root).items():
        L.append('size %s=%.2fGB' % (k, v / 1e9))
    L.append('')
    L.append('[tuner log tail]')
    L.extend(logmod.mem_lines()[-80:])
    return logmod.scrub('\n'.join(L)) + '\n'


def export_to_desktop() -> str:
    txt = build()[:200000]  # hard cap ~200KB
    desk = os.path.join(os.path.expanduser('~'), 'Desktop')
    os.makedirs(desk, exist_ok=True)
    path = os.path.join(desk, 'GameLoopTuner-report-%s.txt' % datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    with open(path, 'w', encoding='utf-8') as f:
        f.write(txt)
    return path
