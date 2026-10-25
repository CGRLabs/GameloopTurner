"""Unified rich log console widget — replaces all per-tab status boxes.

Every operation across all pages streams here with ✓/✗/⚠ visual indicators,
timestamps, and grouping by operation batch.
"""
import datetime
import tkinter as tk

import customtkinter as ctk

from ui import theme as T

# ── Log levels ───────────────────────────────────────────────────────────
SUCCESS  = 'success'
ERROR    = 'error'
WARNING  = 'warning'
INFO     = 'info'
PROGRESS = 'progress'
DRIFT    = 'drift'

_STYLE = {
    SUCCESS:  (T.ICON_SUCCESS,  T.ACCENT,  T.BG_CARD),
    ERROR:    (T.ICON_ERROR,    T.ERROR,   T.ERROR_BG),
    WARNING:  (T.ICON_WARNING,  T.WARNING, T.WARNING_BG),
    INFO:     (T.ICON_INFO,     T.INFO,    T.BG_CARD),
    PROGRESS: (T.ICON_PROGRESS, T.TEXT_MUTED, T.BG_CARD),
    DRIFT:    (T.ICON_DRIFT,    T.WARNING, T.WARNING_BG),
}


class LogConsole(ctk.CTkFrame):
    """Scrollable log viewer with styled entries."""

    def __init__(self, parent, **kw):
        super().__init__(parent, fg_color=T.BG_PRIMARY, **kw)

        # ── Header bar ───────────────────────────────────────────────────
        header = ctk.CTkFrame(self, fg_color=T.BG_CARD, height=36, corner_radius=0)
        header.pack(fill='x')
        header.pack_propagate(False)
        ctk.CTkLabel(header, text='Activity Log', font=T.FONT_HEADING,
                     text_color=T.TEXT_PRIMARY).pack(side='left', padx=12, pady=4)
        ctk.CTkButton(header, text='Clear', width=56, height=26,
                      fg_color=T.BG_HOVER, hover_color=T.BORDER,
                      text_color=T.TEXT_MUTED, font=T.FONT_SMALL,
                      command=self.clear).pack(side='right', padx=8, pady=4)
        self._auto_scroll = tk.BooleanVar(value=True)
        ctk.CTkCheckBox(header, text='Auto-scroll', variable=self._auto_scroll,
                        font=T.FONT_SMALL, text_color=T.TEXT_MUTED,
                        width=24, height=20, checkbox_width=16, checkbox_height=16,
                        fg_color=T.ACCENT, hover_color=T.ACCENT_HOVER,
                        border_color=T.BORDER).pack(side='right', padx=4, pady=4)

        # ── Scrollable body ──────────────────────────────────────────────
        self._body = ctk.CTkScrollableFrame(self, fg_color=T.BG_PRIMARY,
                                             scrollbar_button_color=T.BORDER,
                                             scrollbar_button_hover_color=T.TEXT_DIM)
        self._body.pack(fill='both', expand=True)
        self._entries: list[ctk.CTkFrame] = []
        self._group_count = 0

    # ── public API ───────────────────────────────────────────────────────

    def add(self, text: str, level: str = INFO):
        """Add a single log entry with icon, timestamp, and text."""
        icon, color, bg = _STYLE.get(level, _STYLE[INFO])
        ts = datetime.datetime.now().strftime('%H:%M:%S')

        row = ctk.CTkFrame(self._body, fg_color=bg, corner_radius=6, height=28)
        row.pack(fill='x', padx=4, pady=1)
        row.pack_propagate(False)

        # icon
        ctk.CTkLabel(row, text=icon, font=('Segoe UI', 13), text_color=color,
                     width=22).pack(side='left', padx=(8, 2))
        # timestamp
        ctk.CTkLabel(row, text=ts, font=T.FONT_MONO, text_color=T.TEXT_DIM,
                     width=62).pack(side='left', padx=(0, 6))
        # message
        ctk.CTkLabel(row, text=text, font=T.FONT_MONO, text_color=T.TEXT_PRIMARY,
                     anchor='w').pack(side='left', fill='x', expand=True, padx=(0, 8))

        self._entries.append(row)
        if self._auto_scroll.get():
            self._body.after(10, lambda: self._body._parent_canvas.yview_moveto(1.0))

    def begin_group(self, title: str):
        """Insert a group header (e.g. 'Apply All @ 19:04')."""
        self._group_count += 1
        sep = ctk.CTkFrame(self._body, fg_color=T.BG_CARD, corner_radius=6, height=30)
        sep.pack(fill='x', padx=4, pady=(8, 2))
        sep.pack_propagate(False)
        ctk.CTkLabel(sep, text='── %s ──' % title, font=T.FONT_SMALL,
                     text_color=T.TEXT_ACCENT).pack(side='left', padx=12, pady=2)
        self._entries.append(sep)

    def add_many(self, lines: list[str], level: str = INFO):
        """Add multiple entries at once."""
        for ln in lines:
            self.add(ln, level)

    def clear(self):
        """Remove all entries."""
        for w in self._entries:
            w.destroy()
        self._entries.clear()
        self._group_count = 0
