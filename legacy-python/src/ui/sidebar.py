"""Sidebar navigation — page switcher, Apply All, dry-run toggle, drift badge."""
import tkinter as tk

import customtkinter as ctk

from ui import theme as T


# ── Navigation items ─────────────────────────────────────────────────────
NAV_ITEMS = [
    ('Dashboard',     '⬡'),
    ('Keymaps',       '⌨'),
    ('Performance',   '⚡'),
    ('VM & Priority', '⚙'),
    ('Backups',       '💾'),
    ('Logs',          '≡'),
    ('About',         'ℹ'),
]


class Sidebar(ctk.CTkFrame):
    """Left sidebar: navigation + Apply All + dry-run + drift status."""

    def __init__(self, parent, app, on_select=None, **kw):
        super().__init__(parent, width=T.SIDEBAR_W, fg_color=T.BG_SIDEBAR,
                         corner_radius=0, **kw)
        self.pack_propagate(False)
        self.app = app
        self._on_select = on_select
        self._buttons: dict[str, ctk.CTkButton] = {}
        self._active: str = ''

        # ── App title ────────────────────────────────────────────────────
        title_frame = ctk.CTkFrame(self, fg_color='transparent')
        title_frame.pack(fill='x', padx=12, pady=(16, 4))
        ctk.CTkLabel(title_frame, text='GameLoop', font=T.FONT_APP,
                     text_color=T.ACCENT).pack(anchor='w')
        ctk.CTkLabel(title_frame, text='Tuner', font=('Segoe UI', 16),
                     text_color=T.TEXT_MUTED).pack(anchor='w')

        # ── Separator ───────────────────────────────────────────────────
        ctk.CTkFrame(self, fg_color=T.BORDER, height=1, corner_radius=0).pack(
            fill='x', padx=16, pady=(12, 12))

        # ── Nav buttons ──────────────────────────────────────────────────
        nav_frame = ctk.CTkFrame(self, fg_color='transparent')
        nav_frame.pack(fill='x')
        for name, icon in NAV_ITEMS:
            btn = ctk.CTkButton(
                nav_frame,
                text='  %s  %s' % (icon, name),
                anchor='w',
                font=T.FONT_NAV,
                fg_color='transparent',
                hover_color=T.BG_HOVER,
                text_color=T.TEXT_MUTED,
                height=36,
                corner_radius=8,
                command=lambda n=name: self._select(n),
            )
            btn.pack(fill='x', padx=8, pady=2)
            self._buttons[name] = btn

        # ── Spacer ───────────────────────────────────────────────────────
        ctk.CTkFrame(self, fg_color='transparent').pack(fill='both', expand=True)

        # ── Drift badge ──────────────────────────────────────────────────
        self._drift_frame = ctk.CTkFrame(self, fg_color=T.BG_CARD,
                                          corner_radius=8, height=30,
                                          border_width=0, border_color=T.BORDER)
        self._drift_frame.pack(fill='x', padx=12, pady=(0, 6))
        self._drift_frame.pack_propagate(False)
        self._drift_dot = ctk.CTkLabel(self._drift_frame, text='●',
                                        font=('Segoe UI', 10),
                                        text_color=T.DOT_NEUTRAL, width=16)
        self._drift_dot.pack(side='left', padx=(10, 4))
        self._drift_label = ctk.CTkLabel(self._drift_frame, text='No baseline',
                                          font=T.FONT_SMALL,
                                          text_color=T.TEXT_MUTED)
        self._drift_label.pack(side='left')

        # ── Dry-run toggle ───────────────────────────────────────────────
        ctk.CTkCheckBox(self, text='Dry-run', variable=app.dry,
                        font=T.FONT_SMALL, text_color=T.TEXT_MUTED,
                        width=24, height=22, checkbox_width=18, checkbox_height=18,
                        fg_color=T.WARNING, hover_color=T.WARNING,
                        border_color=T.BORDER).pack(padx=16, pady=(4, 6), anchor='w')

        # ── Close GameLoop button ────────────────────────────────────────
        self._close_btn = ctk.CTkButton(
            self, text='✕  Close GameLoop', height=28,
            font=T.FONT_SMALL,
            fg_color=T.BG_CARD, hover_color=T.ERROR_BG,
            text_color=T.TEXT_PRIMARY,
            border_width=0, border_color=T.BORDER,
            command=app.close_gameloop,
        )
        self._close_btn.pack(fill='x', padx=12, pady=(2, 2))

        # ── Apply All button ─────────────────────────────────────────────
        self._apply_btn = ctk.CTkButton(
            self, text='Apply All', height=34,
            font=T.FONT_HEADING,
            fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
            text_color='#000000',
            command=app.apply_all,
        )
        self._apply_btn.pack(fill='x', padx=12, pady=(2, 6))

        # ── Revert button ────────────────────────────────────────────────
        self._revert_btn = ctk.CTkButton(
            self, text='Revert', height=30,
            font=T.FONT_SMALL,
            fg_color=T.BG_CARD, hover_color=T.ERROR_BG,
            text_color=T.ERROR,
            border_width=0, border_color=T.BORDER,
            command=app.revert_all,
        )
        self._revert_btn.pack(fill='x', padx=12, pady=(0, 14))

    # ── selection ────────────────────────────────────────────────────────

    def _select(self, name: str):
        if self._on_select:
            self._on_select(name)
        self.set_active(name)

    def set_active(self, name: str):
        """Highlight the active nav button."""
        if self._active and self._active in self._buttons:
            self._buttons[self._active].configure(
                fg_color='transparent', text_color=T.TEXT_MUTED, font=T.FONT_NAV)
        self._active = name
        if name in self._buttons:
            self._buttons[name].configure(
                fg_color=T.BG_ACTIVE, text_color=T.TEXT_PRIMARY, font=T.FONT_NAV_ACT)

    # ── pending count ────────────────────────────────────────────────────

    def update_pending_count(self, n: int):
        """Update the Apply All button label with queued fix count."""
        if n > 0:
            self._apply_btn.configure(text='Apply All (%d)' % n,
                                       fg_color=T.ACCENT)
        else:
            self._apply_btn.configure(text='Apply All', fg_color=T.BG_CARD)

    # ── drift status ─────────────────────────────────────────────────────

    def update_drift(self, status: str, color: str = T.DOT_NEUTRAL):
        """Update drift badge text and dot colour."""
        self._drift_dot.configure(text_color=color)
        self._drift_label.configure(text=status, text_color=color)
