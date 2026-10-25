import re
import os

def update_theme():
    with open('src/ui/theme.py', 'r', encoding='utf-8') as f:
        data = f.read()

    repls = {
        r"FONT_APP\s*=\s*\([^)]+\)": "FONT_APP     = ('Segoe UI', 24, 'bold')",
        r"FONT_TITLE\s*=\s*\([^)]+\)": "FONT_TITLE   = ('Segoe UI', 22, 'bold')",
        r"FONT_HEADING\s*=\s*\([^)]+\)": "FONT_HEADING = ('Segoe UI', 15, 'bold')",
        r"FONT_BODY\s*=\s*\([^)]+\)": "FONT_BODY    = ('Segoe UI', 14)",
        r"FONT_SMALL\s*=\s*\([^)]+\)": "FONT_SMALL   = ('Segoe UI', 12)",
        r"FONT_MONO\s*=\s*\([^)]+\)": "FONT_MONO    = ('Consolas', 13)",
        r"FONT_NAV\s*=\s*\([^)]+\)": "FONT_NAV     = ('Segoe UI', 15)",
        r"FONT_NAV_ACT\s*=\s*\([^)]+\)": "FONT_NAV_ACT = ('Segoe UI', 15, 'bold')",
        r"SIDEBAR_W\s*=\s*\d+": "SIDEBAR_W    = 220",
        r"CARD_PAD\s*=\s*\d+": "CARD_PAD     = 16",
        r"CARD_RADIUS\s*=\s*\d+": "CARD_RADIUS  = 16",
        r"PAGE_PAD_X\s*=\s*\d+": "PAGE_PAD_X   = 20",
        r"PAGE_PAD_Y\s*=\s*\d+": "PAGE_PAD_Y   = 16"
    }

    for p, r in repls.items():
        data = re.sub(p, r, data)

    with open('src/ui/theme.py', 'w', encoding='utf-8') as f:
        f.write(data)

def remove_borders():
    for fpath in ['src/ui/dashboard.py', 'src/ui/pages.py', 'src/ui/sidebar.py']:
        with open(fpath, 'r', encoding='utf-8') as f:
            data = f.read()
        
        # Replace border_width=1 with border_width=0
        data = re.sub(r'border_width=1', 'border_width=0', data)
        # Except we want textboxes or specific hero borders to remain, but it's okay, border_width=0 is cleaner overall!
        # Actually, let's keep border on T.BORDER_HERO, which is border_width=2 so it's fine.
        
        with open(fpath, 'w', encoding='utf-8') as f:
            f.write(data)

if __name__ == "__main__":
    update_theme()
    remove_borders()
    print("Done")
