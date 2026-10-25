"""Centralized styling constants for GameLoop Tuner v2."""

# ── Backgrounds ──────────────────────────────────────────────────────────
BG_PRIMARY   = '#0f0f0f'     # app / window background
BG_CARD      = '#1a1a1a'     # card / panel surface
BG_SIDEBAR   = '#111111'     # sidebar background
BG_INPUT     = '#1e1e1e'     # input / textbox background
BG_HOVER     = '#252525'     # hover state for nav buttons
BG_ACTIVE    = '#1c2d1c'     # active nav item (subtle green tint)
BORDER       = '#2a2a2a'     # subtle borders
BORDER_HERO  = '#22c55e'     # hero / recommended card border

# ── Accent colours ───────────────────────────────────────────────────────
ACCENT       = '#22c55e'     # primary green (success, recommended, apply)
ACCENT_HOVER = '#16a34a'     # green hover
WARNING      = '#f59e0b'     # amber (drift, caution)
WARNING_BG   = '#2d2410'     # warning background tint
ERROR        = '#ef4444'     # red (failure, revert)
ERROR_HOVER  = '#dc2626'     # red hover
ERROR_BG     = '#2d1414'     # error background tint
INFO         = '#3b82f6'     # blue (info, in-progress)
TEXT_PRIMARY  = '#f5f5f5'     # main text
TEXT_MUTED   = '#a3a3a3'     # secondary / label text
TEXT_ACCENT  = '#a5b4fc'     # hint / guidance text (kept from v1)
TEXT_DIM     = '#666666'     # timestamps, subtle metadata

# ── Fonts ────────────────────────────────────────────────────────────────
FONT_APP     = ('Segoe UI', 24, 'bold')    # app title in sidebar
FONT_TITLE   = ('Segoe UI', 22, 'bold')    # page / section titles
FONT_HEADING = ('Segoe UI', 15, 'bold')    # card headings
FONT_BODY    = ('Segoe UI', 14)            # body text
FONT_SMALL   = ('Segoe UI', 12)            # hints, timestamps
FONT_MONO    = ('Consolas', 13)            # log entries (Consolas = safe Windows fallback)
FONT_NAV     = ('Segoe UI', 15)            # sidebar navigation items
FONT_NAV_ACT = ('Segoe UI', 15, 'bold')   # active sidebar item

# ── Layout ───────────────────────────────────────────────────────────────
SIDEBAR_W    = 220           # sidebar width in px
CARD_PAD     = 16            # inner padding for cards
CARD_RADIUS  = 16            # corner radius for cards
PAGE_PAD_X   = 20            # horizontal page padding
PAGE_PAD_Y   = 16            # vertical page padding

# ── Log entry icons (Unicode) ────────────────────────────────────────────
ICON_SUCCESS = '✓'
ICON_ERROR   = '✗'
ICON_WARNING = '⚠'
ICON_INFO    = '●'
ICON_PROGRESS = '◌'
ICON_DRIFT   = '⇄'

# ── Status dot colours ───────────────────────────────────────────────────
DOT_OK       = '#22c55e'
DOT_BAD      = '#ef4444'
DOT_WARN     = '#f59e0b'
DOT_NEUTRAL  = '#666666'
