#!/usr/bin/env python3
"""Generate src/EmojiFont.cpp - the built-in 16x16 colour emoji sprites.

    python tools/gen_emoji.py

Writes the C++ sprite table plus a throwaway _emoji_preview.html sprite sheet
(open it in a browser to eyeball the art; delete it afterwards).

A sprite is a 16x16 grid of 4-bit palette indices: 0 = transparent, N =
palette[N-1] (RGB565). Shapes are rasterised with 3x3 supersampling, so the
ASCII-free definitions below stay readable while the output is crisp.
"""

import io
import math
import os
import re

CELL = 16
SS = 3  # supersampling samples per axis, per pixel


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# ── named palette ────────────────────────────────────────────────────────────
C = {
    'yellow':    rgb565(247, 200, 60),
    'yellow_dk': rgb565(196, 138, 20),
    'dark':      rgb565(58, 38, 26),
    'white':     rgb565(250, 248, 240),
    'red':       rgb565(228, 62, 62),
    'pink':      rgb565(248, 160, 176),
    'orange':    rgb565(244, 128, 26),
    'gold':      rgb565(252, 198, 54),
    'blue':      rgb565(92, 166, 236),
    'grey':      rgb565(158, 160, 168),
    'grey_dk':   rgb565(84, 86, 96),
    'wood':      rgb565(214, 166, 104),
    'wood_dk':   rgb565(146, 100, 54),
}


# ── shape predicates (all coordinates in pixel space, 0..16) ─────────────────
def p_and(*preds):
    return lambda x, y: all(p(x, y) for p in preds)


def p_or(*preds):
    return lambda x, y: any(p(x, y) for p in preds)


def p_disc(cx, cy, r):
    rr = r * r
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= rr


def p_ring(cx, cy, r, thick):
    inner, outer = r - thick / 2.0, r + thick / 2.0
    return lambda x, y: inner * inner <= (x - cx) ** 2 + (y - cy) ** 2 <= outer * outer


def p_ellipse(cx, cy, rx, ry, rot=0.0):
    cr, sr = math.cos(-rot), math.sin(-rot)

    def f(x, y):
        dx, dy = x - cx, y - cy
        u = dx * cr - dy * sr
        v = dx * sr + dy * cr
        return (u / rx) ** 2 + (v / ry) ** 2 <= 1.0
    return f


def p_rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def p_poly(pts):
    n = len(pts)

    def f(x, y):
        inside = False
        j = n - 1
        for i in range(n):
            xi, yi = pts[i]
            xj, yj = pts[j]
            if (yi > y) != (yj > y):
                if x < xi + (y - yi) * (xj - xi) / (yj - yi):
                    inside = not inside
            j = i
        return inside
    return f


def p_arc(cx, cy, r, thick, a0, a1):
    """Annulus slice between angles a0..a1 (degrees, 0 = +x, y grows down)."""
    ring = p_ring(cx, cy, r, thick)

    def f(x, y):
        if not ring(x, y):
            return False
        a = math.degrees(math.atan2(y - cy, x - cx)) % 360.0
        return (a0 <= a <= a1) if a0 <= a1 else (a >= a0 or a <= a1)
    return f


def p_star(cx, cy, r_out, r_in, rot=-90.0, points=5):
    pts = []
    for i in range(points * 2):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(rot + i * 180.0 / points)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return p_poly(pts)


def p_pentagon(cx, cy, r, rot=-90.0):
    pts = [(cx + r * math.cos(math.radians(rot + i * 72)),
            cy + r * math.sin(math.radians(rot + i * 72))) for i in range(5)]
    return p_poly(pts)


def p_heart(cx, cy, s):
    """Classic implicit heart  (x^2+y^2-1)^3 - x^2 y^3 <= 0, flipped to y-down."""
    def f(x, y):
        u = (x - cx) / s
        v = -(y - cy) / s
        q = u * u + v * v - 1.0
        return q * q * q - u * u * v * v * v <= 0.0
    return f


def stadium_pts(cx, cy, length, width, rot):
    cr, sr = math.cos(rot), math.sin(rot)
    hl, hw = length / 2.0, width / 2.0
    return [(cx + a * cr - b * sr, cy + a * sr + b * cr)
            for a, b in ((-hl, -hw), (hl, -hw), (hl, hw), (-hl, hw))]


def p_stadium(cx, cy, length, width, rot=0.0):
    """Rotated rectangle with rounded ends - bat blades, limbs, rays."""
    cr, sr = math.cos(rot), math.sin(rot)
    hl = length / 2.0
    e1 = (cx - hl * cr, cy - hl * sr)
    e2 = (cx + hl * cr, cy + hl * sr)
    return p_or(p_poly(stadium_pts(cx, cy, length, width, rot)),
                p_disc(e1[0], e1[1], width / 2.0),
                p_disc(e2[0], e2[1], width / 2.0))


def scale_pts(pts, cx, cy, k):
    return [(cx + (x - cx) * k, cy + (y - cy) * k) for x, y in pts]


# ── sprite canvas ────────────────────────────────────────────────────────────
class Sprite:
    def __init__(self, codepoint, name, aliases=()):
        self.codepoint = codepoint
        self.name = name
        self.aliases = list(aliases)
        self.px = [[0] * CELL for _ in range(CELL)]
        self.palette = []

    def _index(self, color):
        if color == 0:
            return 0
        if color not in self.palette:
            self.palette.append(color)
        return self.palette.index(color) + 1

    def fill(self, pred, color, threshold=0.5):
        idx = self._index(color)
        step = 1.0 / SS
        need = threshold * SS * SS
        for y in range(CELL):
            for x in range(CELL):
                if idx == 0 and self.px[y][x] == 0:
                    continue
                hit = 0
                for sy in range(SS):
                    py = y + (sy + 0.5) * step
                    for sx in range(SS):
                        if pred(x + (sx + 0.5) * step, py):
                            hit += 1
                if hit >= need:
                    self.px[y][x] = idx

    def rows(self):
        out = []
        for y in range(CELL):
            v = 0
            for x in range(CELL):
                v = (v << 4) | (self.px[y][x] & 0xF)
            out.append(v)
        return out

    def colors(self):
        return max(len(self.palette), 1)


# ── the catalogue ────────────────────────────────────────────────────────────
def build():
    sprites = []

    # 😍 heart eyes
    s = Sprite(0x1F60D, 'heart eyes')
    s.fill(p_disc(7.5, 7.5, 7.6), C['yellow'])
    s.fill(p_and(p_disc(7.5, 7.5, 7.2), p_ring(7.5, 7.5, 7.0, 0.9)), C['yellow_dk'])
    s.fill(p_heart(4.3, 6.1, 2.15), C['red'])
    s.fill(p_heart(10.7, 6.1, 2.15), C['red'])
    s.fill(p_heart(3.7, 5.4, 0.75), C['pink'])
    s.fill(p_heart(10.1, 5.4, 0.75), C['pink'])
    s.fill(p_arc(7.5, 7.0, 4.7, 1.7, 25, 155), C['dark'])
    sprites.append(s)

    # 😀 grinning
    s = Sprite(0x1F600, 'grinning', aliases=(0x263A,))
    s.fill(p_disc(7.5, 7.5, 7.6), C['yellow'])
    s.fill(p_and(p_disc(7.5, 7.5, 7.2), p_ring(7.5, 7.5, 7.0, 0.9)), C['yellow_dk'])
    # Open smile first, tooth band next, eyes last so nothing paints over them.
    s.fill(p_and(p_disc(7.5, 8.6, 4.5), p_rect(0, 8.6, 16, 16)), C['dark'])
    s.fill(p_and(p_rect(4.9, 8.9, 10.1, 10.0), p_disc(7.5, 8.6, 4.3)), C['white'])
    s.fill(p_ellipse(5.0, 5.4, 1.5, 2.0), C['dark'])
    s.fill(p_ellipse(10.0, 5.4, 1.5, 2.0), C['dark'])
    sprites.append(s)

    # 😎 sunglasses
    s = Sprite(0x1F60E, 'cool')
    s.fill(p_disc(7.5, 7.5, 7.6), C['yellow'])
    s.fill(p_and(p_disc(7.5, 7.5, 7.2), p_ring(7.5, 7.5, 7.0, 0.9)), C['yellow_dk'])
    # Dark frame, lighter panes, then a glint on each pane.
    s.fill(p_stadium(4.4, 6.4, 6.2, 3.6), C['dark'])
    s.fill(p_stadium(11.6, 6.4, 6.2, 3.6), C['dark'])
    s.fill(p_rect(7.0, 6.0, 9.0, 7.0), C['dark'])
    s.fill(p_stadium(4.4, 6.5, 4.6, 2.2), C['grey_dk'])
    s.fill(p_stadium(11.6, 6.5, 4.6, 2.2), C['grey_dk'])
    s.fill(p_rect(2.9, 5.2, 4.3, 6.0), C['white'])
    s.fill(p_rect(10.1, 5.2, 11.5, 6.0), C['white'])
    s.fill(p_arc(7.5, 8.4, 4.5, 1.6, 35, 145), C['dark'])
    sprites.append(s)

    # ❤ heart
    s = Sprite(0x2764, 'heart', aliases=(0x2665, 0x1F496))
    s.fill(p_heart(7.5, 7.9, 6.6), C['red'])
    s.fill(p_heart(5.4, 5.9, 2.2), C['pink'])
    sprites.append(s)

    # ⭐ star
    s = Sprite(0x2B50, 'star', aliases=(0x2605, 0x1F31F))
    s.fill(p_star(7.5, 8.2, 7.7, 3.3), C['yellow_dk'])
    s.fill(p_star(7.5, 8.2, 6.4, 2.6), C['gold'])
    s.fill(p_star(7.5, 8.6, 3.0, 1.2), C['yellow'])
    sprites.append(s)

    # 🔥 fire
    flame = [(8.0, 0.7), (11.6, 6.1), (13.1, 10.3), (10.7, 14.6), (8.0, 15.7),
             (5.3, 14.6), (2.9, 10.3), (4.4, 6.1)]
    s = Sprite(0x1F525, 'fire')
    s.fill(p_poly(flame), C['red'])
    s.fill(p_poly(scale_pts(flame, 8.0, 10.4, 0.78)), C['orange'])
    s.fill(p_poly(scale_pts(flame, 8.0, 11.4, 0.46)), C['gold'])
    s.fill(p_poly(scale_pts(flame, 8.0, 12.4, 0.20)), C['white'])
    sprites.append(s)

    # 🎵 musical note
    s = Sprite(0x1F3B5, 'note', aliases=(0x266A, 0x266B, 0x1F3B6))
    s.fill(p_ellipse(3.9, 13.0, 2.55, 1.85, -0.36), C['blue'])
    s.fill(p_ellipse(9.5, 10.2, 2.55, 1.85, -0.36), C['blue'])
    s.fill(p_rect(6.4, 2.8, 7.8, 12.0), C['blue'])
    s.fill(p_rect(11.6, 1.2, 13.0, 10.2), C['blue'])
    s.fill(p_poly([(6.4, 1.5), (13.0, 0.4), (13.0, 2.9), (6.4, 4.0)]), C['blue'])
    s.fill(p_poly([(6.5, 1.5), (13.3, 0.4), (13.3, 2.9), (6.5, 4.0)]), C['blue'])
    s.fill(p_ellipse(3.7, 11.9, 0.9, 0.5, -0.36), C['white'])
    s.fill(p_ellipse(8.9, 9.8, 0.9, 0.5, -0.36), C['white'])
    sprites.append(s)

    # ✨ sparkles: a big four-point star plus two smaller ones. ratio is the
    # waist of the star (0.2 would collapse to a bare cross at this size).
    def sparkle(cx, cy, r, ratio=0.34):
        pts = []
        for i in range(8):
            rr = r if i % 2 == 0 else r * ratio
            a = math.radians(-90 + i * 45)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        return p_poly(pts)

    s = Sprite(0x2728, 'sparkles', aliases=(0x2727,))
    s.fill(sparkle(6.2, 7.4, 5.4), C['gold'])
    s.fill(sparkle(6.2, 7.4, 2.0), C['white'])
    s.fill(sparkle(12.4, 3.6, 2.7, 0.6), C['gold'])
    s.fill(sparkle(12.8, 12.4, 2.4, 0.6), C['yellow'])
    sprites.append(s)

    # ⚡ high voltage
    bolt = [(10.2, 0.5), (3.3, 9.1), (7.4, 9.1), (5.9, 15.5), (12.7, 6.7), (8.6, 6.7)]
    s = Sprite(0x26A1, 'bolt', aliases=(0x1F329,))
    s.fill(p_poly(bolt), C['yellow_dk'])
    s.fill(p_poly(scale_pts(bolt, 8.0, 8.0, 0.72)), C['gold'])
    s.fill(p_poly(scale_pts(bolt, 8.0, 8.0, 0.40)), C['white'])
    sprites.append(s)

    # ☀ sun
    s = Sprite(0x2600, 'sun', aliases=(0x1F31E,))
    for i in range(8):
        a = math.radians(i * 45)
        s.fill(p_stadium(7.5 + 6.3 * math.cos(a), 7.5 + 6.3 * math.sin(a), 3.2, 1.7, a),
               C['orange'])
    s.fill(p_disc(7.5, 7.5, 4.8), C['gold'])
    s.fill(p_disc(7.5, 7.5, 3.3), C['yellow'])
    sprites.append(s)

    # 🌙 crescent moon
    s = Sprite(0x1F319, 'moon', aliases=(0x263E, 0x1F31B))
    s.fill(p_disc(6.9, 7.7, 7.3), C['gold'])
    s.fill(p_disc(11.9, 5.5, 6.4), 0)  # punch the crescent
    s.fill(p_disc(3.0, 11.0, 1.5), C['yellow'])
    sprites.append(s)

    # ⚽ soccer ball
    s = Sprite(0x26BD, 'football', aliases=(0x26BE,))
    s.fill(p_disc(7.5, 7.5, 7.5), C['grey'])
    s.fill(p_disc(7.5, 7.5, 6.7), C['white'])
    s.fill(p_pentagon(7.5, 7.6, 2.9), C['dark'])
    for k in range(5):
        a = math.radians(-90 + 72 * k)
        s.fill(p_pentagon(7.5 + 5.3 * math.cos(a), 7.6 + 5.3 * math.sin(a), 1.75, -90 + 72 * k),
               C['dark'])
    sprites.append(s)

    # 🏏 cricket bat and ball
    angle = math.radians(-42)
    s = Sprite(0x1F3CF, 'cricket')
    s.fill(p_stadium(7.6, 10.3, 9.4, 4.8, angle), C['wood_dk'])
    s.fill(p_stadium(7.6, 10.3, 9.0, 4.0, angle), C['wood'])
    s.fill(p_stadium(11.9, 6.4, 3.8, 1.6, angle), C['dark'])   # handle
    s.fill(p_stadium(11.9, 6.4, 3.8, 0.7, angle), C['grey_dk'])  # grip
    s.fill(p_disc(3.6, 3.9, 2.7), C['red'])
    s.fill(p_arc(3.6, 3.9, 2.3, 0.55, 200, 340), C['white'])
    sprites.append(s)

    return sprites


# ── emitters ─────────────────────────────────────────────────────────────────
def cxx(sprites):
    out = []
    out.append('// GENERATED FILE - do not edit by hand. Run: python tools/gen_emoji.py')
    out.append('//')
    out.append('// Built-in 16x16 colour emoji sprites: one 4-bit palette index per pixel')
    out.append('// (0 = transparent) plus a per-sprite RGB565 palette. See EmojiFont.h.')
    out.append('')
    out.append('#include "EmojiFont.h"')
    out.append('')
    out.append('namespace {')
    for s in sprites:
        if s.aliases:
            body = ', '.join('0x%04X' % a for a in s.aliases + [0])
            out.append('const uint32_t ALIAS_%04X[] = { %s };' % (s.codepoint, body))
    out.append('')
    out.append('const EmojiFont::Sprite SPRITES[] = {')
    for s in sprites:
        # Short initialiser lists are fine: C++ zero-fills the rest of the array.
        pal_s = ', '.join('0x%04X' % c for c in s.palette)
        rows_s = ', '.join('0x%016X' % r for r in s.rows())
        alias = ('ALIAS_%04X' % s.codepoint) if s.aliases else 'nullptr'
        out.append('    { 0x%05X, "%s", %d,' % (s.codepoint, s.name, s.colors()))
        out.append('      { %s },' % pal_s)
        out.append('      { %s },' % rows_s)
        out.append('      %s },' % alias)
    out.append('};')
    out.append('')
    out.append('}  // namespace')
    out.append('')
    out.append('namespace EmojiFont {')
    out.append('')
    out.append('size_t count() { return sizeof(SPRITES) / sizeof(SPRITES[0]); }')
    out.append('')
    out.append('const Sprite* at(size_t index) { return (index < count()) ? &SPRITES[index] : nullptr; }')
    out.append('')
    out.append('const Sprite* begin() { return count() ? &SPRITES[0] : nullptr; }')
    out.append('')
    out.append('bool next(const Sprite*& sprite) {')
    out.append('    if (!sprite) return false;')
    out.append('    const size_t i = (size_t)(sprite - SPRITES) + 1;')
    out.append('    if (i >= count()) { sprite = nullptr; return false; }')
    out.append('    sprite = &SPRITES[i];')
    out.append('    return true;')
    out.append('}')
    out.append('')
    out.append('const Sprite* find(uint32_t codepoint) {')
    out.append('    // Fast reject: nothing in the catalogue lives below U+2600, so plain')
    out.append('    // Latin/Indic text never pays for the scan.')
    out.append('    if (codepoint < 0x2600 || codepoint > 0x1FAFF) return nullptr;')
    out.append('    for (size_t i = 0; i < count(); i++) {')
    out.append('        const Sprite& s = SPRITES[i];')
    out.append('        if (s.codepoint == codepoint) return &s;')
    out.append('        for (const uint32_t* a = s.aliases; a && *a; a++) {')
    out.append('            if (*a == codepoint) return &s;')
    out.append('        }')
    out.append('    }')
    out.append('    return nullptr;')
    out.append('}')
    out.append('')
    out.append('uint8_t encodeUtf8(uint32_t cp, char* out) {')
    out.append('    if (cp < 0x80) {')
    out.append('        out[0] = (char)cp;')
    out.append('        return 1;')
    out.append('    }')
    out.append('    if (cp < 0x800) {')
    out.append('        out[0] = (char)(0xC0 | (cp >> 6));')
    out.append('        out[1] = (char)(0x80 | (cp & 0x3F));')
    out.append('        return 2;')
    out.append('    }')
    out.append('    if (cp < 0x10000) {')
    out.append('        out[0] = (char)(0xE0 | (cp >> 12));')
    out.append('        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));')
    out.append('        out[2] = (char)(0x80 | (cp & 0x3F));')
    out.append('        return 3;')
    out.append('    }')
    out.append('    out[0] = (char)(0xF0 | (cp >> 18));')
    out.append('    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));')
    out.append('    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));')
    out.append('    out[3] = (char)(0x80 | (cp & 0x3F));')
    out.append('    return 4;')
    out.append('}')
    out.append('')
    out.append('uint8_t utf8Length(const char* text, size_t i) {')
    out.append('    if (!text) return 0;')
    out.append('    const uint8_t b = (uint8_t)text[i];')
    out.append('    if (b == 0) return 0;')
    out.append('    if (b >= 0xF0) return 4;')
    out.append('    if (b >= 0xE0) return 3;')
    out.append('    if (b >= 0xC0) return 2;')
    out.append('    return 1;')
    out.append('}')
    out.append('')
    out.append('uint8_t utf8Decode(const char* text, size_t i, uint32_t& codepoint) {')
    out.append('    const uint8_t len = utf8Length(text, i);')
    out.append('    if (len <= 1) {')
    out.append('        codepoint = (uint8_t)text[i];')
    out.append('        return len ? 1 : 0;')
    out.append('    }')
    out.append('    static const uint8_t MASK[5] = {0x00, 0x7F, 0x1F, 0x0F, 0x07};')
    out.append('    uint32_t cp = (uint8_t)text[i] & MASK[len];')
    out.append('    for (uint8_t k = 1; k < len; k++) {')
    out.append('        const uint8_t c = (uint8_t)text[i + k];')
    out.append('        if ((c & 0xC0) != 0x80) { codepoint = (uint8_t)text[i]; return 1; }')
    out.append('        cp = (cp << 6) | (c & 0x3F);')
    out.append('    }')
    out.append('    codepoint = cp;')
    out.append('    return len;')
    out.append('}')
    out.append('')
    out.append('bool isFormatCodepoint(uint32_t cp) {')
    out.append('    // Variation selectors (U+FE0E/U+FE0F), ZWJ, zero-width space, skin tones.')
    out.append('    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;')
    out.append('    if (cp == 0x200D || cp == 0x200B || cp == 0xFEFF) return true;')
    out.append('    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true;')
    out.append('    return false;')
    out.append('}')
    out.append('')
    out.append('}  // namespace EmojiFont')
    out.append('')
    return '\n'.join(out)


def preview(sprites):
    """Sprite sheet drawn from the exact generated bits (canvas, 12x zoom)."""
    data = []
    for s in sprites:
        data.append({
            'name': s.name,
            'cp': 'U+%04X' % s.codepoint,
            'pal': ['#%06x' % rgb888(c) for c in s.palette],
            'rows': ['%016x' % r for r in s.rows()],
        })
    import json
    h = ['<!doctype html><meta charset="utf-8"><title>emoji sprites</title>',
         '<style>body{background:#16130f;color:#efe4cf;font:13px system-ui;margin:24px}',
         '.grid{display:flex;flex-wrap:wrap;gap:18px}',
         '.it{text-align:center}canvas{width:128px;height:128px;image-rendering:pixelated;',
         'background:#000;border:1px solid #444;border-radius:8px;display:block}',
         '.n{margin-top:6px;color:#b07d3f}em{color:#8a765c;font-style:normal}</style>',
         '<div class="grid" id="g"></div>',
         '<script>const D=' + json.dumps(data) + ';',
         'const g=document.getElementById("g");',
         'D.forEach(function(d){var w=document.createElement("div");w.className="it";',
         'var c=document.createElement("canvas");c.width=16;c.height=16;',
         'var x=c.getContext("2d");',
         'for(var y=0;y<16;y++){var row=BigInt("0x"+d.rows[y]);',
         'for(var px=0;px<16;px++){var v=Number((row>>BigInt(4*(15-px)))&15n);if(!v)continue;',
         'x.fillStyle=d.pal[v-1];x.fillRect(px,y,1,1);}}',
         'w.appendChild(c);w.insertAdjacentHTML("beforeend",',
         '"<div class=\\"n\\">"+d.name+"<br><em>"+d.cp+"</em></div>");g.appendChild(w);});',
         '</script>']
    return '\n'.join(h)


def rgb888(c565):
    r = ((c565 >> 11) & 0x1F) << 3
    g = ((c565 >> 5) & 0x3F) << 2
    b = (c565 & 0x1F) << 3
    return (r << 16) | (g << 8) | b


if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sprites = build()
    max_colors = max(s.colors() for s in sprites)
    src = cxx(sprites)
    # Keep the emitter honest about the header's palette size.
    header = io.open(os.path.join(root, 'src', 'EmojiFont.h'), encoding='utf-8').read()
    limit = int(re.search(r'MAX_PALETTE = (\d+);', header).group(1))
    if max_colors > limit:
        raise SystemExit('EmojiFont.h allows %d palette entries; %d used' % (limit, max_colors))

    # Match the line endings of the neighbouring hand-written sources.
    nl = '\r\n' if b'\r\n' in io.open(os.path.join(root, 'src', 'Display.cpp'), 'rb').read() else '\n'
    with io.open(os.path.join(root, 'src', 'EmojiFont.cpp'), 'w', encoding='utf-8', newline='') as f:
        f.write(src.replace('\n', nl))
    with io.open(os.path.join(root, '_emoji_preview.html'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(preview(sprites))
    print('sprites: %d, colours <= %d' % (len(sprites), max_colors))
    for s in sprites:
        print('  U+%05X %-10s %2d colours  %d B rows + %d B palette'
              % (s.codepoint, s.name, s.colors(), 8 * CELL, 2 * s.colors()))
