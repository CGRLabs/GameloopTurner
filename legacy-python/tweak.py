import re

with open('src/ui/theme.py', 'r', encoding='utf-8') as f:
    data = f.read()

replacements = {
    r'FONT_APP\s*=\s*\([^)]+\)': \"FONT_APP     = ('Segoe UI', 24, 'bold')\",
    r'FONT_TITLE\s*=\s*\([^)]+\)': \"FONT_TITLE   = ('Segoe UI', 22, 'bold')\",
    r'FONT_HEADING\s*=\s*\([^)]+\)': \"FONT_HEADING = ('Segoe UI', 15, 'bold')\",
    r'FONT_BODY\s*=\s*\([^)]+\)': \"FONT_BODY    = ('Segoe UI', 14)\",
    r'FONT_SMALL\s*=\s*\([^)]+\)': \"FONT_SMALL   = ('Segoe UI', 12)\",
    r'FONT_MONO\s*=\s*\([^)]+\)': \"FONT_MONO    = ('Consolas', 13)\",
    r'FONT_NAV\s*=\s*\([^)]+\)': \"FONT_NAV     = ('Segoe UI', 15)\",
    r'FONT_NAV_ACT\s*=\s*\([^)]+\)': \"FONT_NAV_ACT = ('Segoe UI', 15, 'bold')\",
    r'SIDEBAR_W\s*=\s*\d+': 'SIDEBAR_W    = 220',
    r'CARD_PAD\s*=\s*\d+': 'CARD_PAD     = 16',
    r'CARD_RADIUS\s*=\s*\d+': 'CARD_RADIUS  = 16',
    r'PAGE_PAD_X\s*=\s*\d+': 'PAGE_PAD_X   = 20',
    r'PAGE_PAD_Y\s*=\s*\d+': 'PAGE_PAD_Y   = 16'
}

for pattern, repl in replacements.items():
    data = re.sub(pattern, repl, data)

with open('src/ui/theme.py', 'w', encoding='utf-8') as f:
    f.write(data)