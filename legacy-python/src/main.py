"""GameLoop Tuner v2 — entry point.

Run via: py -m src.main
"""
import ctypes
import os
import sys
import threading
import tkinter as tk
from tkinter import messagebox

# ── Admin Check ────────────────────────────────────────────────────────
def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except Exception:
        return False

if not is_admin():
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror(
        "Administrator Required",
        "GL Tuner must be run as Administrator to modify processes and GameLoop settings.\n\n"
        "Please right-click the application and select 'Run as administrator'."
    )
    sys.exit(1)

import customtkinter as ctk

# Ensure src/ is in path for frozen execution
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core import db as dbmod
from core import log as logmod
from core import paths
from core import procs
from core import journal as journ
from core import backups as bakmod
from core.drift import DriftMonitor

from ui import theme as T
from ui.sidebar import Sidebar
from ui.dashboard import DashboardPage
from ui.log_console import LogConsole
from ui.pages import FixPage, BackupPage, LogPage, AboutPage, needs_gl_closed, ask_close_needed, do_close_ladder, fix_alloc, fix_clarity, detect_applied, FIXES

ctk.set_appearance_mode('dark')
LOG = logmod.get_logger()


class App(ctk.CTk):
    """Main application window."""

    def __init__(self):
        super().__init__()
        self.title('GameLoop Tuner')
        self.geometry('1024x680')
        self.configure(fg_color=T.BG_PRIMARY)
        self.minsize(800, 640)

        # Global state
        self.dry = tk.BooleanVar(value=False)
        self.pending: dict = {}
        
        # UI Layout: Grid 1 row, 2 columns (sidebar, content)
        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=1)

        # ── Sidebar ──────────────────────────────────────────────────────
        self.sidebar = Sidebar(self, app=self, on_select=self.show_page)
        self.sidebar.grid(row=0, column=0, sticky='nsew')

        # ── Content Area ─────────────────────────────────────────────────
        self.content_frame = ctk.CTkFrame(self, fg_color=T.BG_PRIMARY, corner_radius=0)
        self.content_frame.grid(row=0, column=1, sticky='nsew')
        self.content_frame.grid_rowconfigure(0, weight=1)
        self.content_frame.grid_columnconfigure(0, weight=1)
        
        # ── Window Icon ──────────────────────────────────────────────────
        try:
            def resource_path(relative_path):
                try:
                    # PyInstaller creates a temp folder and stores path in _MEIPASS
                    base_path = sys._MEIPASS
                except Exception:
                    base_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
                return os.path.join(base_path, relative_path)
            
            icon_path = resource_path('mythic.png')
            if os.path.exists(icon_path):
                img = tk.PhotoImage(file=icon_path)
                self.iconphoto(False, img)
        except Exception:
            pass

        self.pages: dict[str, ctk.CTkFrame] = {}
        self._build_pages()
        
        # ── Global Log Console ───────────────────────────────────────────
        self.console = LogConsole(self.pages['Logs'].console_slot)
        self.console.pack(fill='both', expand=True)

        # ── Drift Monitor ────────────────────────────────────────────────
        self.drift_monitor = DriftMonitor(on_drift=self._on_drift)
        # We try to get a baseline immediately if possible
        if paths.appdata_db():
            self.drift_monitor.set_baseline()
            self.sidebar.update_drift('✓ In Sync', T.DOT_OK)
        self.drift_monitor.start(interval=30)

        # Initial state
        self.show_page('Dashboard')
        self._restore_state()
        self.log('GameLoop Tuner started.', 'info')

    def _build_pages(self):
        """Instantiate all pages."""
        self.pages['Dashboard'] = DashboardPage(self.content_frame, app=self)
        self.pages['Keymaps'] = FixPage(self.content_frame, 'Keymaps', app=self)
        self.pages['Performance'] = FixPage(self.content_frame, 'Performance', app=self)
        self.pages['VM & Priority'] = FixPage(self.content_frame, 'VM & Priority', app=self)
        self.pages['Backups'] = BackupPage(self.content_frame, app=self)
        self.pages['Logs'] = LogPage(self.content_frame, app=self)
        self.pages['About'] = AboutPage(self.content_frame, app=self)
        
        for page in self.pages.values():
            page.grid(row=0, column=0, sticky='nsew')

    def show_page(self, name: str):
        """Switch active page."""
        for page in self.pages.values():
            page.grid_remove()
        if name in self.pages:
            self.pages[name].grid()
            self.sidebar.set_active(name)
            # Auto-refresh dashboard when opened
            if name == 'Dashboard' and hasattr(self.pages[name], 'refresh'):
                self.pages[name].refresh()

    def update_pending_count(self):
        """Update sidebar Apply All button label."""
        self.sidebar.update_pending_count(len(self.pending))

    def _restore_state(self):
        """Read DB/registry on startup and pre-check boxes that are already applied."""
        try:
            applied = detect_applied()
        except Exception:
            return
        if not applied:
            return
        for page in self.pages.values():
            if not hasattr(page, 'vars'):
                continue
            for k, var in page.vars.items():
                if k in applied:
                    var.set(True)
                    self.pending[k] = FIXES[k]
        self.update_pending_count()
        if applied:
            self.log('Detected already applied: %s' % ', '.join(sorted(applied)), 'info')

    # ── Logging API ──────────────────────────────────────────────────────

    def log(self, msg: str, level: str = 'info'):
        """Write to unified log console."""
        self.console.add(msg, level)
        LOG.info('[%s] %s', level, msg)

    def log_many(self, msgs: list[str], level: str = 'info'):
        self.console.add_many(msgs, level)
        for m in msgs:
            LOG.info('[%s] %s', level, m)

    # ── Apply / Revert ───────────────────────────────────────────────────

    def apply_all(self):
        """Execute all pending fixes."""
        if not self.pending:
            self.log('Nothing to apply. Tick fixes on the pages first.', 'warning')
            self.show_page('Logs')
            return

        need_close = needs_gl_closed(list(self.pending.keys()))

        # Dialog on main thread — fast, non-blocking
        if need_close:
            if not ask_close_needed(self):
                self.log('Apply cancelled — close GameLoop first.', 'warning')
                return

        keys = list(self.pending.keys())
        dry = self.dry.get()

        # Switch to Logs page to show progress
        self.show_page('Logs')
        self.console.begin_group('Apply All (Dry-run)' if dry else 'Apply All')

        def work():
            try:
                # Close GameLoop in background thread (no main-thread freeze)
                if need_close:
                    def _close_log(msg):
                        self.after(0, lambda m=msg: self.log(m, 'info'))
                    if not do_close_ladder(callback=_close_log):
                        self.after(0, lambda: self.log(
                            'Apply aborted — GameLoop still running. Quit the tray icon, then retry.', 'error'))
                        return

                dbp = paths.appdata_db()
                jpath = journ.path_for(dbp) if dbp else ''
                success_count = 0

                # 1. Standard Fixes
                for k in keys:
                    if k in ('alloc', 'clarity'):
                        continue
                    try:
                        fix_def = self.pending[k]
                        notes, _ = fix_def['apply'](dry)
                        self.after(0, lambda n=notes, name=k: (
                            self.log('[%s] applied successfully' % name, 'success'),
                            self.log_many(['  ' + x for x in n], 'info')
                        ))
                        if jpath and not dry and dbp:
                            journ.record(jpath, k, {'backup': 'see Configs.db.bak-* (db dir)', 'db': dbp})
                        success_count += 1
                    except Exception as e:
                        self.after(0, lambda name=k, err=e: self.log('[%s] FAILED: %s' % (name, err), 'error'))

                # 2. Performance Extras (Alloc)
                if 'alloc' in keys:
                    try:
                        c, m = self.pages['Performance'].get_alloc_values()
                        if dry:
                            self.after(0, lambda c=c, m=m: self.log('[alloc] DRYRUN: %d cores / %d GB' % (c, m), 'info'))
                        else:
                            notes, before = fix_alloc(c, m)
                            self.after(0, lambda n=notes: (
                                self.log('[alloc] applied', 'success'),
                                self.log_many(['  ' + x for x in n], 'info')
                            ))
                            if jpath:
                                journ.record(jpath, 'alloc', {'before': before})
                        success_count += 1
                    except Exception as e:
                        self.after(0, lambda err=e: self.log('[alloc] FAILED: %s' % err, 'error'))

                # 3. Performance Extras (Clarity)
                if 'clarity' in keys:
                    try:
                        lvl = self.pages['Performance'].get_clarity_level()
                        if dry:
                            self.after(0, lambda lvl=lvl: self.log('[clarity] DRYRUN: tier %d' % lvl, 'info'))
                        else:
                            notes = fix_clarity(lvl)
                            self.after(0, lambda n=notes: (
                                self.log('[clarity] applied', 'success'),
                                self.log_many(['  ' + x for x in n], 'info')
                            ))
                        success_count += 1
                    except Exception as e:
                        self.after(0, lambda err=e: self.log('[clarity] FAILED: %s' % err, 'error'))

                # Wrap up
                def finish():
                    self.log('Finished applying %d fixes. Restart GameLoop.' % success_count, 'progress')
                    # Uncheck everything
                    for page in self.pages.values():
                        if hasattr(page, 'vars'):
                            for var in page.vars.values():
                                var.set(False)
                        if hasattr(page, 'alloc_var'):
                            page.alloc_var.set(False)
                        if hasattr(page, 'clarity_var'):
                            page.clarity_var.set(False)
                    self.pending.clear()
                    self.update_pending_count()

                    # Update drift baseline
                    if not dry and dbp:
                        self.drift_monitor.set_baseline()
                        self.sidebar.update_drift('✓ In Sync', T.DOT_OK)

                self.after(0, finish)

            except Exception as e:
                import traceback
                tb = traceback.format_exc()
                LOG.error('apply_all crashed: %s\n%s', e, tb)
                self.after(0, lambda: self.log('Apply crashed: %s' % e, 'error'))

        threading.Thread(target=work, daemon=True).start()

    def close_gameloop(self):
        """Dedicated Close GameLoop action (sidebar button).

        Runs the full ladder off the UI thread; escalates to force + service
        stop only with explicit consent.
        """
        if not procs.running():
            self.log('GameLoop is not running.', 'info')
            return
        self.show_page('Logs')
        self.console.begin_group('Close GameLoop')

        def work():
            res = procs.close_gameloop()
            self.after(0, lambda: self.log_many(res['notes'], 'info'))
            if res['ok']:
                self.after(0, lambda: self.log('GameLoop closed.', 'success'))
                return

            def ask():
                if not messagebox.askyesno(
                    'Force close?',
                    'These resisted normal close:\n' +
                    '\n'.join('%s (%d)' % (p['name'], p['pid']) for p in res['left']) +
                    '\n\nForce-kill them and stop GameLoopService?'
                ):
                    self.log('Force close declined.', 'warning')
                    return

                def work2():
                    res2 = procs.close_gameloop(force=True, stop_service_now=True)
                    self.after(0, lambda: self.log_many(res2['notes'], 'info'))
                    if res2['ok']:
                        self.after(0, lambda: self.log('GameLoop closed (forced).', 'success'))
                    else:
                        self.after(0, lambda: self.log(
                            'Still running — right-click the GameLoop tray icon → Quit.', 'error'))
                threading.Thread(target=work2, daemon=True).start()

            self.after(0, ask)

        threading.Thread(target=work, daemon=True).start()

    def revert_all(self):
        """Simple revert action: navigates to Backups page."""
        self.show_page('Backups')
        self.log('To revert changes, please restore the latest backup from the list.', 'info')
        # If there's a specific 'alloc' revert in journal, we could do it here,
        # but directing users to the Backups tab is safer and more visual for DB changes.

    # ── Drift Handling ───────────────────────────────────────────────────

    def _on_drift(self, drifts: list[dict]):
        """Called by background thread when drift is detected."""
        def update_ui():
            self.sidebar.update_drift('⚠ Drifted', T.DOT_WARN)
            self.console.begin_group('Drift Detected')
            for item in drifts:
                msg = DriftMonitor.format_drift(item)
                self.log(msg, 'drift')
            tex_drift = any(i['key'] in ('file_tex_clean', 'smk_readonly')
                            for i in drifts)
            if tex_drift:
                self.log(
                    'Engine restored a jump-trigger template or unlocked smk.conf '
                    '(local template pack / app update). Re-run the recommended fix '
                    'to re-blind + re-lock; landing content (flat blobs) still holds.',
                    'warning')
            else:
                self.log(
                    'Re-apply: quit GameLoop (tray → Quit), tick the ⭐ recommended fix, Apply All.',
                    'warning')
            if self.sidebar._active != 'Logs':
                self.sidebar.update_drift('⚠ Drifted (Click Logs)', T.DOT_WARN)
        self.after(0, update_ui)


if __name__ == '__main__':
    LOG.info('Tuner v2 started')
    app = App()
    app.mainloop()
