"""Smart drift detection — monitors whether GameLoop silently reverts settings.

Usage:
    monitor = DriftMonitor(on_drift=my_callback)
    monitor.set_baseline()        # snapshot after Apply All
    monitor.start(interval=30)    # background polling every 30s
    # ... later ...
    monitor.stop()

The callback receives a list of DriftItem dicts when drift is detected.
"""
import os
import threading
import time

from core import db as dbmod
from core import keymaps as km
from core import log as logmod
from core import paths
from core import perf as perfmod

LOG = logmod.get_logger()


class DriftMonitor:
    """Periodically compares live settings to a saved baseline."""

    # Keys we track (registry + DB + smk.conf file). Pointer noise (recentlyused
    # timestamps, which 145-solution is active) is NOT tracked: the engine flips
    # those in memory constantly, and every landing is flat, so the visible
    # keymap never changes. What matters: active mode stays sensible, the
    # landing blobs stay 0-rule, and smk.conf stays blinded + read-only.
    TRACKED_KEYS = (
        'cores', 'mem_gb',                        # registry alloc
        'lastmodeid',                             # active keymap mode stays 145
        'flat_145_all',                           # every mode-145 custom layout is 0 rules
        'flat_146', 'flat_531686',                # destination layouts stay 0 rules
        'clarity', 'low_latency_mode',            # perf flags
        'default_frame_gen_on',                   # perf flag
        'file_tex_clean', 'smk_readonly',   # smk.conf clean of jump triggers + read-only lock
    )

    def __init__(self, on_drift=None):
        self._baseline: dict = {}
        self._running = False
        self._thread: threading.Thread | None = None
        self._callback = on_drift
        self._interval = 30
        self._lock = threading.Lock()

    # ── snapshot ─────────────────────────────────────────────────────────

    def snapshot(self) -> dict:
        """Read current monitored values (safe — never raises)."""
        snap: dict = {}
        # Registry allocation
        try:
            from core import registry as reg
            alloc = reg.perf_allocation()
            snap['cores'] = alloc.get('PerformanceCpuCoreNum')
            snap['mem_gb'] = alloc.get('PerformanceMemoryGb')
        except Exception:
            pass
        # DB values
        try:
            dbp = paths.appdata_db()
            if dbp:
                con = dbmod.connect(dbp, timeout=5)
                cur = con.cursor()
                # Keymap drift + landing content
                try:
                    dr = km.drift_report(cur, paths.PKG)
                    snap['lastmodeid'] = dr.get('lastmodeid')
                    # Flatness invariants: every custom layout in mode 145 must be
                    # 0-rule, and the DMZ/BR destination blobs must be 0-rule.
                    sols = km.solutions(cur, paths.PKG)
                    snap['flat_145_all'] = True
                    snap['flat_146'] = True
                    snap['flat_531686'] = True
                    for m, s, nm, rules in sols:
                        if m == '145' and rules != 0:
                            snap['flat_145_all'] = False
                    for m, s, nm, rules in sols:
                        if m == '146' and s == '146' and rules != 0:
                            snap['flat_146'] = False
                        if m == '531686' and s == '1063553' and rules != 0:
                            snap['flat_531686'] = False
                except Exception:
                    pass
                # Perf flags
                try:
                    uc = perfmod.local_user_config(cur, paths.PKG)
                    snap['clarity'] = uc.get('clarity')
                    snap['low_latency_mode'] = uc.get('low_latency_mode')
                    snap['default_frame_gen_on'] = uc.get('default_frame_gen_on')
                except Exception:
                    pass
                # SMK Tex presence: the smk.conf FILE is the operative scene source
                # (the DB mirror is dormant — the engine re-fills it at will and it
                # causes no switch, so it's not tracked). Also track the read-only
                # lock that blocks the runtime template restore.
                try:
                    fp = paths.smk_conf()
                    if fp:
                        with open(fp, encoding='utf-8', errors='replace') as f:
                            ftxt = f.read()
                        jump = {'id="10004"', 'id="10046"',
                                'id="30004"', 'id="30046"'}
                        snap['file_tex_clean'] = not any(t in ftxt for t in jump)
                        snap['smk_readonly'] = False
                        import stat
                        st = os.stat(fp)
                        writable = st.st_mode & (stat.S_IWRITE | stat.S_IWUSR
                                                 | stat.S_IWGRP | stat.S_IWOTH)
                        snap['smk_readonly'] = writable == 0
                except Exception:
                    pass
                con.close()
        except Exception:
            pass
        return snap

    # ── baseline ─────────────────────────────────────────────────────────

    def set_baseline(self, snap: dict | None = None):
        """Save a baseline (typically right after Apply All)."""
        with self._lock:
            self._baseline = snap if snap is not None else self.snapshot()
        LOG.info('drift baseline set: %s', {k: v for k, v in self._baseline.items()})

    @property
    def has_baseline(self) -> bool:
        return bool(self._baseline)

    def clear_baseline(self):
        with self._lock:
            self._baseline = {}

    # ── check ────────────────────────────────────────────────────────────

    def check(self) -> list[dict]:
        """Compare current state to baseline. Returns list of drift items."""
        with self._lock:
            baseline = dict(self._baseline)
        if not baseline:
            return []
        current = self.snapshot()
        drifts = []
        for key in self.TRACKED_KEYS:
            if key not in baseline:
                continue
            expected = baseline[key]
            actual = current.get(key)
            if actual is not None and actual != expected:
                drifts.append({
                    'key': key,
                    'expected': expected,
                    'actual': actual,
                })
        return drifts

    # ── background polling ───────────────────────────────────────────────

    def start(self, interval: int = 30):
        """Start background drift check thread."""
        if self._running:
            return
        self._interval = interval
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True, name='drift-monitor')
        self._thread.start()
        LOG.info('drift monitor started (interval=%ds)', interval)

    def stop(self):
        """Stop background polling."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
            self._thread = None
        LOG.info('drift monitor stopped')

    def _loop(self):
        while self._running:
            time.sleep(self._interval)
            if not self._running:
                break
            try:
                drifts = self.check()
                if drifts and self._callback:
                    self._callback(drifts)
            except Exception as e:
                LOG.warning('drift check error: %s', e)

    # ── human-readable labels ────────────────────────────────────────────

    LABELS = {
        'cores': 'VM CPU cores',
        'mem_gb': 'VM RAM (GB)',
        'lastmodeid': 'Active keymap mode',
        'flat_145_all': 'Mode 145 layouts flat (0 rules)',
        'flat_146': 'BR landing blob flat (146/146)',
        'flat_531686': 'DMZ landing blob flat (531686/1063553)',
        'clarity': 'Clarity tier',
        'low_latency_mode': 'Low-latency mode',
        'default_frame_gen_on': 'Frame generation',
        'file_tex_clean': 'smk.conf jump triggers (10004/10046 + twins)',
        'smk_readonly': 'smk.conf read-only lock',
    }

    @classmethod
    def format_drift(cls, item: dict) -> str:
        """Human-readable drift description."""
        label = cls.LABELS.get(item['key'], item['key'])
        return '%s changed: was %s → now %s' % (label, item['expected'], item['actual'])
