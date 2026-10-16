#!/usr/bin/env python3
"""Static checks for the emoji feature (run: python tools/check_emoji.py).

No compiler is needed: it re-reads the generated sprite table, rebuilds the
dashboard HTML the way buildIndexHtml() does, parses it, and cross-checks the
codepoint handling between EmojiFont.cpp, Display.cpp and the web UI.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []


def read(p):
    return io.open(os.path.join(ROOT, p), encoding='utf-8').read()


def check(label, ok, detail=''):
    print('%-4s %s%s' % ('ok' if ok else 'FAIL', label, (' - ' + detail) if detail and not ok else ''))
    if not ok:
        fails.append(label)


def strip_comments(src):
    """Blank out comments and literals, keeping the byte count so offsets into
    the result still line up with the original source."""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            while i < n and not (src[i] == '*' and i + 1 < n and src[i + 1] == '/'):
                if src[i] != '\n':
                    out[i] = ' '
                i += 1
            for _ in range(2):
                if i < n:
                    out[i] = ' '
                    i += 1
        elif c == 'R' and i + 1 < n and src[i + 1] == '"':
            # Raw string (the dashboard HTML): blank the whole literal.
            j = i + 2
            delim = ''
            while j < n and src[j] != '(':
                delim += src[j]
                j += 1
            end = src.find(')' + delim + '"', j)
            end = n if end < 0 else end + len(delim) + 2
            for k in range(i, end):
                if src[k] != '\n':
                    out[k] = ' '
            i = end
        elif c in '\'"':
            quote = c
            out[i] = ' '
            i += 1
            while i < n:
                if src[i] == '\\':
                    out[i] = ' '
                    i += 1
                    if i < n:
                        out[i] = ' '
                        i += 1
                    continue
                if src[i] == quote or src[i] == '\n':
                    out[i] = ' ' if src[i] == quote else '\n'
                    i += 1
                    break
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def balance(src):
    """(brace, paren) imbalance of real code, ignoring comments and literals."""
    s = strip_comments(src)
    return s.count('{') - s.count('}'), s.count('(') - s.count(')')


# ── balanced delimiters on every touched source ──────────────────────────────
for f in ('src/EmojiFont.cpp', 'src/EmojiFont.h', 'src/Display.cpp', 'src/Display.h',
          'src/App.cpp', 'src/WebServer.cpp', 'src/DashboardAssets.h',
          'src/plugins/ScorePlugin.cpp', 'src/plugins/LyricsPlugin.cpp'):
    b, p = balance(read(f))
    check('%s delimiters balanced' % f, b == 0 and p == 0, 'braces %+d parens %+d' % (b, p))

# ── sprite catalogue ────────────────────────────────────────────────────────
font_cpp = read('src/EmojiFont.cpp')
entries = re.findall(r'\{\s*0x([0-9A-F]+),\s*"([^"]+)",\s*(\d+),', font_cpp)
check('sprite table parsed', len(entries) >= 10, 'found %d' % len(entries))
palettes = re.findall(r'\{\s*(0x[0-9A-F]{4}(?:,\s*0x[0-9A-F]{4})*)\s*\},', font_cpp)
check('each sprite has a palette', len(palettes) == len(entries),
      '%d palettes for %d sprites' % (len(palettes), len(entries)))
for cp, name, colors in entries:
    check('sprite %-10s palette covers %s colours' % (name, colors),
          int(colors) <= 8 and int(colors) >= 1)

# row encoding: 16 hex digits (16 px x 4 bits) per row, 16 rows per sprite
rows = re.findall(r'\{\s*(0x[0-9A-F]{16}(?:,\s*0x[0-9A-F]{16}){15})\s*\},', font_cpp)
check('every sprite has 16 x 64-bit rows', len(rows) == len(entries),
      '%d row blocks for %d sprites' % (len(rows), len(entries)))

catalogue = [(int(cp, 16), name) for cp, name, _ in entries]

# ── Display wiring ──────────────────────────────────────────────────────────
disp_cpp = strip_comments(read('src/Display.cpp'))
disp_h = strip_comments(read('src/Display.h'))
for fn in ('containsEmoji', 'drawEmojiSprite', 'drawEmojiCentered', 'codepointAdvance'):
    check('Display declares + defines %s' % fn,
          re.search(r'\b%s\s*\(' % fn, disp_h) and re.search(r'Display::%s\s*\(' % fn, disp_cpp))
check('glyphAdvance maps emoji to a sprite cell',
      'if (EmojiFont::find(codepoint)) return EmojiFont::CELL;' in disp_cpp)
check('drawGlyph blits sprites (shaped-text path)',
      'drawEmojiSprite(x, y, sprite, size);' in disp_cpp)
check('drawText has a bulk fast path for plain text', 'if (!containsEmoji(text))' in disp_cpp)
check('measureText accounts for sprite cells', 'if (containsEmoji(text))' in disp_cpp)
check('centring paths account for sprite cells', disp_cpp.count('if (containsEmoji(text))') >= 3)
check('halo passes skip sprites', disp_cpp.count('if (!dimLevel)') >= 2)
check('format codepoints are skipped in all walkers',
      disp_cpp.count('EmojiFont::isFormatCodepoint') >= 3)
check('sprite blit merges horizontal runs', 'run * size' in disp_cpp)

# Geometry: a sprite cell must span exactly the unifont ink box, i.e. sit
# EMOJI_ASCENT*size above the baseline and CELL-EMOJI_ASCENT below it, which is
# what keeps emoji level with the digits beside them.
cell = int(re.search(r'CELL = (\d+);', read('src/EmojiFont.h')).group(1))
ascent = int(re.search(r'EMOJI_ASCENT = (\d+);', disp_cpp).group(1))
check('sprite cell is 16px (one unifont ink box)', cell == 16, 'cell %d' % cell)
check('sprite baseline offset matches unifont ascent', ascent == 14,
      'ascent %d' % ascent)
check('sprite descent matches unifont (~2px)', cell - ascent == 2,
      'descent %d' % (cell - ascent))

# A row is 16 px x 4 bits: a 64-bit value with the leftmost pixel in the top
# nibble. Catch a regression back to 16-bit rows (which silently truncated
# every sprite to its last four pixels).
row_hex = re.search(r'\{\s*0x([0-9A-F]{16}),', font_cpp)
check('rows are 64-bit (16 px x 4 bits)', bool(row_hex))

# ── macro collisions ────────────────────────────────────────────────────────
# Macros ignore namespaces, so any macro in a dependency that happens to share a
# name with what EmojiFont.h declares silently rewrites that declaration. That
# is how AnimatedGIF.h's `#define MAX_COLORS 256` broke the build once.
deps = os.path.join(ROOT, '.pio', 'libdeps')
header = read('src/EmojiFont.h')
our_names = set(re.findall(r'static constexpr \w+ (\w+) =', header))
our_names |= set(re.findall(r'^(?:const\s+)?[\w\*]+[\s\*]+(\w+)\s*\(', header, re.M))
check('function/constant names harvested for the scan', len(our_names) >= 11,
      'harvested %d: %s' % (len(our_names), sorted(our_names)))
if os.path.isdir(deps):
    defines = set()
    for dirpath, _, files in os.walk(deps):
        for fn in files:
            if not fn.endswith(('.h', '.hpp')):
                continue
            try:
                text = io.open(os.path.join(dirpath, fn), encoding='utf-8', errors='ignore').read()
            except OSError:
                continue
            defines |= set(re.findall(r'^\s*#\s*define\s+(\w+)', text, re.M))
    clash = sorted(our_names & defines)
    check('no dependency macro collides with the EmojiFont API', not clash, 'clash: %s' % clash)
    print('     (%d names checked against %d dependency macros)' % (len(our_names), len(defines)))
else:
    print('skip  dependency macro scan (.pio/libdeps not present)')
ga = disp_cpp.index('int16_t Display::glyphAdvance')
ga_body = disp_cpp[ga:ga + 2000]
check('glyphAdvance resolves emoji before the 16-bit u8g2 lookup',
      ga_body.index('EmojiFont::find') < ga_body.index('u8g2_GetGlyphWidth'))

# ── on-screen usage ─────────────────────────────────────────────────────────
app = strip_comments(read('src/App.cpp'))
check('anime clock face uses heart eyes',
      'drawEmojiCentered(cx - eyeDx, eyeY, 0x2764' in app)
check('anime face no longer hand-draws eye discs', 'disc(cx - eyeDx' not in app)

scores = strip_comments(read('src/plugins/ScorePlugin.cpp'))
check('scores header shows a sport icon',
      'sportIcon' in scores and '0x26BD' in scores and '0x1F3CF' in scores)
check('goal celebration shows a football', scores.count('drawEmojiCentered') >= 2)

lyr = strip_comments(read('src/plugins/LyricsPlugin.cpp'))
check('lyrics header shows a music note', '0x1F3B5' in lyr)

# ── web UI: rebuild the dashboard the way buildIndexHtml() does ──────────────
def _read_c_arg(text, i):
    """Read one C string / raw-string literal argument starting at index i."""
    while i < len(text) and text[i] in ' \t\r\n':
        i += 1
    if text.startswith('R"', i):
        j = text.index('(', i)
        delim = text[i + 2:j]
        end = text.index(')' + delim + '"', j)
        return text[j + 1:end], end + len(delim) + 2
    if text[i] == '"':
        out, i = [], i + 1
        while True:
            c = text[i]
            if c == '\\':
                out.append({'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\'}.get(text[i + 1], text[i + 1]))
                i += 2
                continue
            if c == '"':
                return ''.join(out), i + 1
            out.append(c)
            i += 1
    raise ValueError('unexpected %r' % text[i:i + 20])


def build_scores_section(src, tpl_src):
    """Mirror ScorePlugin::uiSection(): the shared card template (from
    DashboardAssets.h), the two espnCardHtml() calls, the manual card literal
    and the inline script. Returns the section HTML so the whole page (plugin
    cards included) can be parsed."""
    try:
        tpl = re.search(r'R"scorecard\((.*?)\)scorecard"', tpl_src, re.S).group(1)
        manual = re.search(r'html \+= R"rawliteral\((.*?)\)rawliteral";', src, re.S).group(1)
        script = re.search(r'R"rawliteral\(  <script>(.*?)</script>', src, re.S).group(1)
        toks = ['%T%', '%P%', '%S%', '%L%', '%C%', '%Y%', '%H%', '%O%', '%X%', '%D%']
        section = manual
        for m in re.finditer(r'html \+= espnCardHtml\(', src):
            i, args = m.end(), []
            while True:
                val, i = _read_c_arg(src, i)
                args.append(val)
                while i < len(src) and src[i] in ' \t\r\n':
                    i += 1
                if src[i] == ',':
                    i += 1
                    continue
                if src[i] == ')':
                    break
                raise ValueError('malformed argument list')
            card = tpl
            for tok, val in zip(toks, args):
                card = card.replace(tok, val)
            section += card
        return section + '  <script>' + script + '</script>\n    '
    except (AttributeError, ValueError, IndexError):
        return ''


web = read('src/WebServer.cpp')
check('WebServer includes EmojiFont.h', '#include "EmojiFont.h"' in web)
check('WebServer substitutes the emoji strip', '"%EMOJI_PICKER%"' in web)
# Search the page shell only: the placeholder also appears in the substitution
# table in buildIndexHtml(), which is a different (and legitimate) occurrence.
# The design-system CSS now lives in one shared module, spliced into the shell
# via %DASHBOARD_CSS%, so read it back from there when rebuilding the page.
assets = read('src/DashboardAssets.h')
css_m = re.search(r'R"css\((.*?)\)css";', assets, re.S)
css = css_m.group(1) if css_m else ''
shell = web.split('R"rawliteral(', 1)[1].split(')rawliteral', 1)[0]
check('shared stylesheet is in one module', len(css) > 8000 and '--bg:' in css and '.card {' in css)
check('page shell only references the shared stylesheet',
      '%DASHBOARD_CSS%' in shell and ':root {' not in shell and 'box-sizing' not in shell)
label_at = shell.index('Custom text (shown below the time)')
save_at = shell.index("Save Text</button>")
check('picker strip sits between the text field and Save Text',
      label_at < shell.index('%EMOJI_PICKER%') < save_at)
check('insertEmoji appends to the clock text field',
      'function insertEmoji' in web and "el.value += ch;" in web)

src = shell
buttons = []
for cp, name in catalogue:
    ch = chr(cp)
    buttons.append('<button type="button" class="emoji-btn" title="%s U+%X" '
                   "onclick=\"insertEmoji('%s')\">%s</button>" % (name, cp, ch, ch))
scores_section = build_scores_section(read('src/plugins/ScorePlugin.cpp'), assets)
check('scores section rebuilt from the shared template', len(scores_section) > 10000,
      'got %d bytes' % len(scores_section))
html = src.replace('%DASHBOARD_CSS%', css)
html = html.replace('%MODE_OPTIONS%', '<option value="0">Clock</option>')
# The lyrics card is one literal in the plugin, so fold the real markup in as
# well: the parse below then covers every section the browser actually gets.
lyr_raw = read('src/plugins/LyricsPlugin.cpp')
_ui = lyr_raw.index('LyricsPlugin::uiSection')
lyr_card, _ = _read_c_arg(lyr_raw, lyr_raw.index('R"', lyr_raw.index('return', _ui)))
check('lyrics section rebuilt from the plugin', len(lyr_card) > 2000, 'got %d bytes' % len(lyr_card))
html = html.replace('%LYRICS_CARDS%', lyr_card)
html = html.replace('%SCORES_CARDS%', scores_section)
html = html.replace('%EMOJI_PICKER%', '\n          '.join(buttons))
check('no unsubstituted placeholder left',
      not [p for p in ('%DASHBOARD_CSS%', '%MODE_OPTIONS%', '%LYRICS_CARDS%', '%SCORES_CARDS%',
                       '%EMOJI_PICKER%')
           if p in html])
check('every catalogue emoji is in the strip and the text field',
      all(chr(cp) in html for cp, _ in catalogue))

from html.parser import HTMLParser

VOID = {'br', 'img', 'input', 'meta', 'link', 'hr', 'source', 'area', 'base', 'col',
        'embed', 'param', 'track', 'wbr'}


class Parse(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.errors = []

    def handle_starttag(self, tag, attrs):
        if tag not in VOID:
            self.stack.append((tag, self.getpos()))

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1][0] != tag:
            self.errors.append((tag, self.getpos()))
            return
        self.stack.pop()


p = Parse()
p.feed(html)
check('dashboard HTML parses', not p.errors and not p.stack,
      'errors %s unclosed %s' % (p.errors[:3], p.stack[:3]))

script = re.search(r'function showSection.*?</script>', html, re.S)
check('dashboard script extracted for syntax check', bool(script))
if script:
    body = script.group(0).rsplit('</script>', 1)[0]
    io.open(os.path.join(ROOT, '_chk_webui.js'), 'w', encoding='utf-8', newline='\n').write(body)

# ...and the lyrics section has its own inline script.
lyr_script = re.search(r'<script>(.*?)</script>', lyr_card, re.S)
check('lyrics script extracted for syntax check', bool(lyr_script))
if lyr_script:
    io.open(os.path.join(ROOT, '_chk_lyrics.js'), 'w', encoding='utf-8', newline='\n').write(lyr_script.group(1))

# The scores section carries its own inline script; check that one too.
scores_src = read('src/plugins/ScorePlugin.cpp')
scores_m = re.search(r'R"rawliteral\(\s*<script>(.*?)</script>', scores_src, re.S)
check('scores script extracted for syntax check', bool(scores_m))
if scores_m:
    io.open(os.path.join(ROOT, '_chk_scores.js'), 'w', encoding='utf-8', newline='\n').write(scores_m.group(1))
check('scores cards share one markup skeleton', scores_src.count('html += espnCardHtml(') == 2)
check('shared card template lives in DashboardAssets.h', 'ESPN_CARD_HTML[]' in assets)
check('ScorePlugin no longer inlines its own card template', 'ESPN_CARD_HTML[]' not in scores_src)
check('scores section uses the shared classes, not inline styles',
      'class="btn-inline"' in scores_src and 'style="' not in scores_section)

io.open(os.path.join(ROOT, '_preview_webui.html'), 'w', encoding='utf-8', newline='\n').write(html)
print('\n%s (%d checks failed)' % ('ALL CHECKS PASSED' if not fails else 'FAILURES: ' + ', '.join(fails),
                                   len(fails)))
sys.exit(1 if fails else 0)
