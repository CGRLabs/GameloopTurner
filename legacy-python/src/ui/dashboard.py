"""Dashboard page — status cards with live system data."""
import json
import threading

import customtkinter as ctk

from core import db as dbmod
from core import disk as diskmod
from core import keymaps as km
from core import log as logmod
from core import paths
from core import perf as perfmod
from core import procs
from core import registry as reg
from ui import theme as T

LOG = logmod.get_logger()


class StatusCard(ctk.CTkFrame):
    """A single status card with title, status dot, and content lines."""

    def __init__(self, parent, title: str, icon: str = '', **kw):
        super().__init__(parent, fg_color=T.BG_CARD, corner_radius=T.CARD_RADIUS,
                         border_width=0, border_color=T.BORDER, **kw)
        # ── Header ───────────────────────────────────────────────────────
        hdr = ctk.CTkFrame(self, fg_color='transparent')
        hdr.pack(fill='x', padx=T.CARD_PAD, pady=(T.CARD_PAD, 4))
        if icon:
            ctk.CTkLabel(hdr, text=icon, font=('Segoe UI', 15),
                         width=22).pack(side='left', padx=(0, 6))
        ctk.CTkLabel(hdr, text=title, font=T.FONT_HEADING,
                     text_color=T.TEXT_PRIMARY).pack(side='left')
        self._dot = ctk.CTkLabel(hdr, text='●', font=('Segoe UI', 12),
                                 text_color=T.DOT_NEUTRAL, width=20)
        self._dot.pack(side='right')
        # ── Body ─────────────────────────────────────────────────────────
        self._body = ctk.CTkFrame(self, fg_color='transparent')
        self._body.pack(fill='x', padx=T.CARD_PAD, pady=(0, T.CARD_PAD))
        self._lines: list[ctk.CTkLabel] = []

    def set_status(self, color: str):
        """Set the status dot colour."""
        self._dot.configure(text_color=color)

    def set_content(self, lines: list[tuple[str, str]]):
        """Set card body. Each item is (label, value) or just (text,)."""
        for w in self._lines:
            w.destroy()
        self._lines.clear()
        for item in lines:
            row = ctk.CTkFrame(self._body, fg_color='transparent', height=22)
            row.pack(fill='x', pady=1)
            row.pack_propagate(False)
            if len(item) >= 2:
                ctk.CTkLabel(row, text=item[0], font=T.FONT_SMALL,
                             text_color=T.TEXT_MUTED, anchor='w',
                             width=130).pack(side='left')
                ctk.CTkLabel(row, text=str(item[1]), font=T.FONT_SMALL,
                             text_color=T.TEXT_PRIMARY, anchor='w').pack(side='left', fill='x', expand=True)
            else:
                ctk.CTkLabel(row, text=item[0], font=T.FONT_SMALL,
                             text_color=T.TEXT_PRIMARY, anchor='w').pack(side='left', fill='x', expand=True)
            self._lines.append(row)


class ProgressBar(ctk.CTkFrame):
    """A small labelled progress/gauge bar."""

    def __init__(self, parent, label: str, value: float, maximum: float,
                 unit: str = '', warn_below: float | None = None, **kw):
        super().__init__(parent, fg_color='transparent', **kw)
        hdr = ctk.CTkFrame(self, fg_color='transparent')
        hdr.pack(fill='x')
        ctk.CTkLabel(hdr, text=label, font=T.FONT_SMALL,
                     text_color=T.TEXT_MUTED).pack(side='left')
        val_text = '%.1f / %.1f %s' % (value, maximum, unit)
        ctk.CTkLabel(hdr, text=val_text, font=T.FONT_SMALL,
                     text_color=T.TEXT_PRIMARY).pack(side='right')
        ratio = max(0.0, min(1.0, value / maximum)) if maximum else 0
        color = T.ACCENT
        if warn_below is not None and value < warn_below:
            color = T.ERROR if value < warn_below * 0.5 else T.WARNING
        bar = ctk.CTkProgressBar(self, progress_color=color,
                                  fg_color=T.BORDER, height=8,
                                  corner_radius=4)
        bar.pack(fill='x', pady=(2, 0))
        bar.set(ratio)


class DashboardPage(ctk.CTkScrollableFrame):
    """Dashboard with visual status cards — read-only diagnostics."""

    def __init__(self, parent, app, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY,
                         scrollbar_button_color=T.BORDER,
                         scrollbar_button_hover_color=T.TEXT_DIM, **kw)
        self.app = app

        # Title
        ctk.CTkLabel(self, text='Dashboard', font=T.FONT_TITLE,
                     text_color=T.TEXT_PRIMARY).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                     pady=(T.PAGE_PAD_Y, 8))
        ctk.CTkLabel(self, text='Live system status — read-only snapshot.',
                     font=T.FONT_SMALL, text_color=T.TEXT_MUTED).pack(
            anchor='w', padx=T.PAGE_PAD_X, pady=(0, 12))

        # ── Cards grid (2 columns) ──────────────────────────────────────
        grid = ctk.CTkFrame(self, fg_color='transparent')
        grid.pack(fill='both', expand=True, padx=T.PAGE_PAD_X)
        grid.columnconfigure(0, weight=1)
        grid.columnconfigure(1, weight=1)

        self.gl_card = StatusCard(grid, 'GameLoop Status', '⬡')
        self.gl_card.grid(row=0, column=0, sticky='nsew', padx=(0, 6), pady=6)

        self.alloc_card = StatusCard(grid, 'VM Allocation', '⚙')
        self.alloc_card.grid(row=0, column=1, sticky='nsew', padx=(6, 0), pady=6)

        self.keymap_card = StatusCard(grid, 'Active Keymap', '⌨')
        self.keymap_card.grid(row=1, column=0, sticky='nsew', padx=(0, 6), pady=6)

        self.codm_card = StatusCard(grid, 'CODM Settings', '🎯')
        self.codm_card.grid(row=1, column=1, sticky='nsew', padx=(6, 0), pady=6)

        self.disk_card = StatusCard(grid, 'Drive Health', '💿')
        self.disk_card.grid(row=2, column=0, sticky='nsew', padx=(0, 6), pady=6)

        self.service_card = StatusCard(grid, 'Service', '⏻')
        self.service_card.grid(row=2, column=1, sticky='nsew', padx=(6, 0), pady=6)

        # ── Refresh button ───────────────────────────────────────────────
        ctk.CTkButton(self, text='↻  Re-diagnose', width=140, height=32,
                      fg_color=T.BG_CARD, hover_color=T.BG_HOVER,
                      border_width=0, border_color=T.BORDER,
                      text_color=T.TEXT_PRIMARY, font=T.FONT_BODY,
                      command=self.refresh).pack(anchor='w', padx=T.PAGE_PAD_X,
                                                  pady=(12, T.PAGE_PAD_Y))

    def refresh(self):
        """Reload all dashboard data in a background thread."""
        def work():
            data = self._gather()
            self.app.after(0, lambda: self._apply(data))
        threading.Thread(target=work, daemon=True).start()

    def _gather(self) -> dict:
        """Collect all diagnostic data (called from worker thread)."""
        d: dict = {}
        # Processes
        try:
            d['procs'] = procs.list_gameloop()
        except Exception:
            d['procs'] = []
        # Service
        try:
            d['service'] = procs.service_status()
        except Exception:
            d['service'] = {}
        # Host
        try:
            d['host'] = procs.host_resources()
        except Exception:
            d['host'] = {'cores': '?', 'mem_gb': '?'}
        # Allocation
        try:
            d['alloc'] = reg.perf_allocation()
        except Exception:
            d['alloc'] = {}
        # Paths
        d['root'] = paths.install_root()
        d['dbp'] = paths.appdata_db()
        d['build'] = paths.app_version_dir(d['root'])
        # DB data
        if d['dbp']:
            try:
                con = dbmod.connect(d['dbp'], timeout=5)
                cur = con.cursor()
                d['drift'] = km.drift_report(cur, paths.PKG)
                d['solutions'] = km.solutions(cur, paths.PKG)
                d['jumps'] = km.jump_tags(cur, paths.PKG)
                d['triggers'] = [t[0] for t in dbmod.triggers(con)]
                row = cur.execute(
                    "SELECT value FROM configs WHERE key='androws.%s.smk'" % paths.PKG).fetchone()
                smk = row[0] if row else ''
                d['tex10004'] = 'id="10004"' in smk
                d['tex10046'] = 'id="10046"' in smk
                d['user_cfg'] = perfmod.local_user_config(cur, paths.PKG)
                d['host_cfg'] = perfmod.host_config(cur, paths.PKG)
                con.close()
            except Exception as e:
                d['db_error'] = str(e)
        # Disk
        try:
            d['disk'] = diskmod.game_drive_status(d['root'])
        except Exception:
            d['disk'] = {}
        return d

    def _apply(self, d: dict):
        """Update cards from gathered data (called on main thread)."""
        # ── GameLoop Status ──────────────────────────────────────────────
        ps = d.get('procs', [])
        if ps:
            self.gl_card.set_status(T.DOT_OK)
            lines = [('Processes', '%d running' % len(ps))]
            for p in ps[:5]:
                lines.append(('', '%s (PID %d, %d MB)' % (p['name'], p['pid'], p['mem_kb'] // 1024)))
            self.gl_card.set_content(lines)
        else:
            self.gl_card.set_status(T.DOT_NEUTRAL)
            self.gl_card.set_content([('Status', 'Not running')])

        # ── VM Allocation ────────────────────────────────────────────────
        alloc = d.get('alloc', {})
        host = d.get('host', {})
        cores = alloc.get('PerformanceCpuCoreNum', '?')
        mem = alloc.get('PerformanceMemoryGb', '?')
        hc = host.get('cores', '?')
        hm = host.get('mem_gb', '?')
        self.alloc_card.set_status(T.DOT_OK if alloc else T.DOT_NEUTRAL)
        rm = alloc.get('RenderMode', '?')
        rm = '%s — %s' % (rm, reg.render_mode_label(rm))
        rut = alloc.get('RenderUserType', '?')
        rut = '%s — %s' % (rut, reg.render_user_label(rut))
        self.alloc_card.set_content([
            ('CPU Cores', '%s / %s' % (cores, hc)),
            ('RAM', '%s / %s GB' % (mem, hm)),
            ('Render mode', rm),
            ('Renderer override', rut),
            ('Perf tier', str(alloc.get('PerformanceMode', '?'))),
        ])

        # ── Active Keymap ────────────────────────────────────────────────
        dr = d.get('drift', {})
        sols = d.get('solutions', [])
        lm = dr.get('lastmodeid', '?')
        # Find active solution: match the mode's lastSolutionId, not just the mode
        # (mode 145 holds 4 layouts — first-match would show the wrong one).
        mode_rows = [s for s in sols if str(s[0]) == str(lm)]
        want_sol = {'145': dr.get('mode145_solution')}.get(str(lm))
        match = [s for s in mode_rows if str(s[1]) == str(want_sol)] if want_sol else []
        pick = (match or mode_rows or [None])[0]
        active_name = pick[2] if pick else '?'
        active_rules = str(pick[3]) if pick else '?'
        # Name of the mode-145 layout (what MP should be on)
        sol_name = {str(s[1]): (s[2], s[3]) for s in sols}
        m145 = dr.get('mode145_solution')
        m145_txt = '?'
        if m145 is not None:
            nm, rl = sol_name.get(str(m145), ('?', '?'))
            m145_txt = '%s (%s rules)' % (nm, rl)
        # Jump history: which modes the engine touched
        try:
            recent = sorted(json.loads(dr.get('recentlyused') or '{}').keys())
        except (TypeError, ValueError):
            recent = []
        # Smart layouts still present (non-zero switch rules)
        smart = ['%s (%d)' % (s[2], s[3]) for s in sols if s[3] > 0]
        # Jump-trigger templates: absent = jump path cut
        t1 = 'absent ✓' if not d.get('tex10004') else 'PRESENT ✗'
        t2 = 'absent ✓' if not d.get('tex10046') else 'PRESENT ✗'
        rules_color = T.DOT_OK if active_rules == '0' else T.DOT_WARN
        self.keymap_card.set_status(rules_color)
        km_lines = [
            ('Active mode', str(lm)),
            ('Layout', active_name),
            ('Switch rules', active_rules),
            ('Mode 145 layout', m145_txt),
            ('Recent modes', ', '.join(recent) if recent else '?'),
            ('Smart layouts', '; '.join(smart) if smart else 'none ✓'),
            ('Trigger 10004/10046', '%s / %s' % (t1, t2)),
        ]
        if sols:
            km_lines.append(('Total layouts', str(len(sols))))
        jumps = d.get('jumps', [])
        if jumps:
            km_lines.append(('Jump tags', ', '.join(str(j) for j in jumps[:5])))
        self.keymap_card.set_content(km_lines)

        # ── CODM Settings ───────────────────────────────────────────────
        uc = d.get('user_cfg', {})
        hcfg = d.get('host_cfg', {})
        if uc:
            self.codm_card.set_status(T.DOT_OK)
            manual = uc.get('user_select_clarity')
            self.codm_card.set_content([
                ('Frame rate', str(uc.get('frame_rate', '?'))),
                ('Clarity', str(uc.get('clarity', '?'))),
                ('Clarity lock', '✓ manual' if manual else '✗ auto (engine may change)'),
                ('Low latency', '✓ ON' if uc.get('low_latency_mode') else '✗ OFF'),
                ('Frame gen', '✓ OFF' if not uc.get('default_frame_gen_on') else '⚠ ON (adds lag)'),
                ('Host FPS cap', '%s / %s' % (hcfg.get('default_frame_rate', '?'),
                                               hcfg.get('maximum_frame_rate', '?'))),
            ])
        else:
            self.codm_card.set_status(T.DOT_NEUTRAL)
            self.codm_card.set_content([('Status', d.get('db_error', 'No data'))])

        # ── Drive Health ─────────────────────────────────────────────────
        disk = d.get('disk', {})
        if disk:
            free = float(disk.get('free_gb', 0))
            minr = float(disk.get('min_gb', 10))
            ok = disk.get('ok', False)
            self.disk_card.set_status(T.DOT_OK if ok else T.DOT_BAD)
            self.disk_card.set_content([
                ('Drive', str(disk.get('drive', '?'))),
                ('Free', '%.1f GB' % free),
                ('Minimum', '%.0f GB' % minr),
                ('Status', '✓ OK' if ok else '✗ LOW SPACE'),
            ])
        else:
            self.disk_card.set_status(T.DOT_NEUTRAL)
            self.disk_card.set_content([('Status', 'Unknown')])

        # ── Service ──────────────────────────────────────────────────────
        svc = d.get('service', {})
        state = svc.get('state', '?')
        running = 'running' in str(state).lower()
        self.service_card.set_status(T.DOT_OK if running else T.DOT_NEUTRAL)
        self.service_card.set_content([
            ('State', str(state)),
            ('Start type', str(svc.get('start_type', '?'))),
            ('Install', d.get('root') or 'NOT FOUND'),
            ('Build', d.get('build') or '?'),
        ])
