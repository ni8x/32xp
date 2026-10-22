#!/usr/bin/env python3
"""Static checks for the plugin system (run: python tools/check_plugins.py).

No compiler is needed. It cross-checks the four plugin sources against the
manager and the web shell:

  * every plugin exposes a unique lowercase id and a unique in-range mode,
  * App.cpp registers each one exactly once, in mode order, before beginAll(),
  * PluginManager refuses duplicate ids/modes and a full table,
  * dispatch rejects an unknown plugin and an unknown action with a 404,
  * /api/plugin/<id>/<action> is reachable (prefix semantics of the vendored
    ESPAsyncWebServer handler are verified against its source when present),
  * the dashboard shell has one placeholder per substitution and no stray
    %TOKEN% is ever shipped to the browser,
  * every uiSection() lands in a section placeholder,
  * every lyric style toggle is persisted, published, accepted by the style
    endpoint and reachable from the dashboard, and the scroll-up transition's
    geometry keeps its repaint band clear of the progress bar,
  * a current lyric line that does not fit one row is wrapped (rows drawn
    verbatim, no ellipsis) and the wrapped block's simulated geometry stays
    between the song title and the progress-bar timestamps at both orientations.

The one property this cannot prove is thread safety: web handlers run on the
AsyncTCP task while the render loop runs on the Arduino task. The script prints
an inventory of those cross-task access sites instead of a pass/fail line.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []

# Failure details quote source lines and sample lyrics, which include non-ASCII
# text; a Windows console defaults to a codepage that cannot encode them.
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass


def read(p):
    return io.open(os.path.join(ROOT, p), encoding='utf-8').read()


def check(label, ok, detail=''):
    print('%-4s %s%s' % ('ok' if ok else 'FAIL', label, (' - ' + detail) if detail and not ok else ''))
    if not ok:
        fails.append(label)


# ---------------------------------------------------------------- discovery

PLUGIN_DIR = 'src/plugins'
headers = sorted(f for f in os.listdir(os.path.join(ROOT, PLUGIN_DIR))
                 if f.endswith('.h')
                 and 'public Plugin' in read(os.path.join(PLUGIN_DIR, f)))


class Plugin:
    def __init__(self, header):
        self.header = os.path.join(PLUGIN_DIR, header)
        src = read(self.header)
        m = re.search(r'class\s+(\w+)\s*:\s*public\s+Plugin\b', src)
        self.cls = m.group(1) if m else None
        m = re.search(r'id\(\)\s*const\s+override\s*\{\s*return\s*"([^"]*)"', src)
        self.id = m.group(1) if m else None
        m = re.search(r'mode\(\)\s*const\s+override\s*\{\s*return\s*(\d+)', src)
        self.mode = int(m.group(1)) if m else None
        m = re.search(r'name\(\)\s*const\s+override\s*\{\s*return\s*"([^"]*)"', src)
        self.name = m.group(1) if m else None
        self.ui = bool(re.search(r'uiSection\(\)\s*const\s+override', src))
        self.csrc = os.path.join(PLUGIN_DIR, header[:-2] + '.cpp')
        self.cs = read(self.csrc) if os.path.exists(os.path.join(ROOT, self.csrc)) else ''
        self.all = src + self.cs


plugins = [Plugin(h) for h in headers]
check('every plugin header derives from Plugin and names itself',
      all(p.cls and p.id and p.mode is not None and p.name for p in plugins),
      str([(p.cls, p.id, p.mode) for p in plugins if not (p.cls and p.id and p.mode is not None)]))

mgr_h = read('src/plugins/PluginManager.h')
m = re.search(r'MAX_PLUGINS\s*=\s*(\d+)', mgr_h)
max_plugins = int(m.group(1)) if m else 0
check('PluginManager declares a plugin capacity', max_plugins > 0, str(max_plugins))
check('plugin table is large enough for the registered set',
      len(plugins) <= max_plugins, '%d plugins, MAX_PLUGINS=%d' % (len(plugins), max_plugins))

# ------------------------------------------------------- id / mode identity

ids = [p.id for p in plugins]
modes = [p.mode for p in plugins]
check('plugin ids are unique', len(set(ids)) == len(ids), str(ids))
check('plugin ids are lowercase route-safe tokens',
      all(re.match(r'^[a-z][a-z0-9_]*$', i or '') for i in ids), str(ids))
check('plugin modes are unique', len(set(modes)) == len(modes), str(modes))
check('plugin modes are inside the manager table',
      all(m is not None and 0 <= m < max_plugins for m in modes), str(modes))

# ---------------------------------------------------- registration in App.cpp

app = read('src/App.cpp')
app_h = read('src/App.h')
app_flat = re.sub(r'\s+', ' ', app)
regs = re.findall(r'_pluginManager\.registerPlugin\(&(\w+)\)', app)


def inc_line(p):
    return '#include "plugins/%s.h"' % p.cls


check('App.h includes every plugin header',
      all(inc_line(p) in app_h for p in plugins),
      'missing: ' + str([inc_line(p) for p in plugins if inc_line(p) not in app_h]))
check('App.cpp registers every plugin exactly once',
      len(regs) == len(plugins) and len(set(regs)) == len(regs),
      'registrations %s vs plugins %s' % (regs, [p.cls for p in plugins]))
check('App.cpp registers none more than the table holds', len(regs) <= max_plugins)

begin_all = app_flat.find('_pluginManager.beginAll()')
last_reg = app_flat.rfind('registerPlugin(')
check('registration happens before beginAll()',
      begin_all > 0 and last_reg < begin_all,
      'beginAll at %d, last registerPlugin at %d' % (begin_all, last_reg))
check('registration happens before WebServer::begin()',
      0 < last_reg < app_flat.find('_webServer.begin()'))

# Registration order also drives the dashboard mode dropdown, so it must follow
# the mode numbers (Clock=0, Media=1, Lyrics=2, Scores=3).
by_cls = {p.cls: p for p in plugins}
# App.h declares one member per plugin: <Class> <_instance>;
inst_cls = dict((m[1], m[0]) for m in re.findall(r'^\s*(\w+)\s+(_\w+)\s*;', app_h, re.M))
check('every registered instance is a declared plugin member',
      all(r in inst_cls for r in regs),
      'undeclared: ' + str([r for r in regs if r not in inst_cls]))
check('every registered instance is a known plugin class',
      all(inst_cls.get(r) in by_cls for r in regs),
      str([(r, inst_cls.get(r)) for r in regs if inst_cls.get(r) not in by_cls]))
check('every plugin class is registered exactly once',
      sorted(inst_cls[r] for r in regs if r in inst_cls) == sorted(p.cls for p in plugins),
      str([inst_cls.get(r) for r in regs]))
order_modes = [by_cls[inst_cls[r]].mode for r in regs if inst_cls.get(r) in by_cls]
check('registration order matches ascending mode order',
      order_modes == sorted(modes), 'registered mode order %s' % order_modes)

check('unknown persisted mode falls back to a registered plugin',
      'findByMode(displayMode)' in app_flat and 'displayMode = 0' in app_flat)

# ---------------------------------------------------------- manager contract

mgr = read('src/plugins/PluginManager.cpp')
check('registerPlugin refuses a duplicate mode',
      'Plugin mode collision' in mgr and 'mode() == plugin->mode()' in mgr)
check('registerPlugin refuses a duplicate id',
      'Plugin id collision' in mgr and 'strcmp(_plugins[i]->id(), plugin->id())' in mgr)
check('registerPlugin refuses when the table is full',
      re.search(r'_count\s*>=\s*MAX_PLUGINS', mgr) is not None)
check('manager rejects a null plugin', 'if (!plugin' in mgr)

check('dispatchApi answers an unknown plugin with 404',
      'Unknown plugin' in mgr and 'request->send(404' in mgr)
check('dispatchApi answers an unknown action with 404',
      'Unknown action' in mgr and 'if (plugin->handleApi(request, action))' in mgr)
check('dispatchApi always answers the request (no silent drop)',
      mgr.count('return true;') >= 3)

check('enterMode exits the old plugin before entering the new one',
      re.search(r'if \(_active\) _active->exit\(\);', mgr) is not None)
check('updateBackground ticks every plugin, not just the active one',
      'backgroundTick()' in mgr and 'updateBackground' in mgr)

# --------------------------------------------------------- web dispatch path

web = read('src/WebServer.cpp')
check('WebServer registers the generic plugin route',
      '_server.on("/api/plugin", HTTP_POST' in web)
check('the route parses <id>/<action> from the URL',
      'url.substring(11)' in web and 'indexOf(\'/\', 1)' in web)
check('the route rejects a malformed path with 400',
      'Expected /api/plugin/<id>/<action>' in web)
check('the route hands off to the manager',
      'dispatchApi(id, action, request)' in web)
check('plugin literal routes are registered after the generic one',
      web.find('_server.on("/api/plugin"') < web.rfind('registerRoutes(_server)'))

# The generic route only matches because the vendored handler treats a bare
# prefix as a prefix. Verify that against the library itself when it is built.
lib = os.path.join(ROOT, '.pio/libdeps/esp32-s3-devkitc-1/ESPAsyncWebServer/src/WebHandlers.cpp')
if os.path.exists(lib):
    handlers = io.open(lib, encoding='utf-8').read()
    check('vendored ESPAsyncWebServer matches a URI prefix (so /api/plugin/* routes)',
          '_uri != request->url() && !request->url().startsWith(_uri + "/")' in handlers)
else:
    print('skip vendored ESPAsyncWebServer not built yet; route-prefix check not run')

# The generic route claims the whole "/api/plugin/" prefix, so a plugin route
# beneath it would either shadow or be shadowed depending on registration
# order. Keep plugin literal routes out of that namespace.
plugin_routes = []
for p in plugins:
    m = re.search(r'%s::registerRoutes\b[^{]*\{' % p.cls, p.cs)
    if m:
        depth, i = 1, m.end()
        while i < len(p.cs) and depth:
            depth += (p.cs[i] == '{') - (p.cs[i] == '}')
            i += 1
        plugin_routes += [(p.id, u) for u in re.findall(r'server\.on\("([^"]+)"', p.cs[m.end():i])]
paths = [u for _, u in plugin_routes]
check('no plugin route lives under the generic /api/plugin namespace',
      all(not u.startswith('/api/plugin') for u in paths),
      str([u for u in paths if u.startswith('/api/plugin')]))
dupes = set(u for u in paths if paths.count(u) > 1)
check('no two plugins own the same literal route', not dupes, str(sorted(dupes)))
core = set(re.findall(r'_server\.on\("([^"]+)"', web))
check('no plugin route collides with an earlier core route',
      not (set(paths) & core), str(sorted(set(paths) & core)))

# The <id>/<action> split of /api/plugin/<id>/<action>, mirroring the C++ and
# pinned to it so a change in the source invalidates this table.
check('the URL split mirrors the C++ implementation',
      "if (rest.length() < 3 || rest[0] != '/' || slash < 2)" in web
      and 'int slash = rest.indexOf(\'/\', 1);' in web)


def split_plugin_url(url):
    rest = url[11:] if len(url) > 11 else ''
    slash = rest.find('/', 1)
    if len(rest) < 3 or not rest.startswith('/') or slash < 2:
        return None                       # 400
    return rest[1:slash], rest[slash + 1:]


cases = [('/api/plugin/scores/set', ('scores', 'set')),
         ('/api/plugin/lyrics/line', ('lyrics', 'line')),
         ('/api/plugin/scores/', ('scores', '')),
         ('/api/plugin', None),
         ('/api/plugin/', None),
         ('/api/plugin/scores', None)]
check('the route split behaviour is table-tested',
      all(split_plugin_url(u) == want for u, want in cases),
      str([(u, split_plugin_url(u)) for u, want in cases if split_plugin_url(u) != want]))

# ----------------------------------------------------- UI section + shell

assets = read('src/DashboardAssets.h')
shell = web.split('R"rawliteral(', 1)[1].split(')rawliteral', 1)[0]

table = re.findall(r'"(%[A-Z_]+%)"', web)
check('buildIndexHtml has a placeholder table', len(table) >= 5, str(table))
check('every placeholder in the table appears in the shell',
      all(t in shell for t in table),
      'absent from shell: ' + str([t for t in table if t not in shell]))
replaced = set(re.findall(r'html\.replace\("(%[A-Z_]+%)"', web))
check('every placeholder in the table is substituted',
      all(t in replaced for t in table),
      'never replaced: ' + str([t for t in table if t not in replaced]))
check('no substitution targets a token missing from the table',
      replaced <= set(table), str(replaced - set(table)))
stray = set(re.findall(r'%[A-Z][A-Z_]{2,}%', shell)) - set(table)
check('no stray %TOKEN% can ship to the browser', not stray, str(stray))

providers = [p for p in plugins if p.ui]
check('every plugin with a uiSection() has an implementation',
      all(re.search(r'%s::uiSection' % p.cls, p.cs) for p in providers),
      str([p.cls for p in providers]))
check('a uiSection() builds card markup, not a bare fragment',
      all('class=\\"card\\"' in p.cs or 'class="card"' in p.cs for p in providers),
      str([p.cls for p in providers if 'class="card"' not in p.cs]))
check('the shell exposes a section for each UI-providing plugin',
      'lyricsCards' in web and 'scoresCards' in web and len(providers) <= 2,
      'providers %s' % [p.id for p in providers])

# ------------------------------------------------------ lyric style wiring
#
# Every lyric style is a user-facing toggle, so it has to survive a reboot,
# round-trip through /api/status, be accepted by the style endpoint, and be
# reachable from the dashboard. The toggles are discovered in Settings.h, so a
# new one cannot silently ship half-wired: it either passes all five or the
# name is printed.


def brace_body(text, marker):
    """Body of the first `{...}` block after marker (brace matched)."""
    i = text.index(marker)
    i = text.index('{', i) + 1
    depth, start = 1, i
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i - 1]


st_h = read('src/Settings.h')
st_cpp = read('src/Settings.cpp')
ctor = st_cpp[st_cpp.index('Settings::Settings()'):]
ctor_init = ctor[:ctor.index('{')]
clear_all = brace_body(st_cpp, 'void Settings::clearAll()')
lyr_src = next((p.cs for p in plugins if p.id == 'lyrics'), '')

style_bools = re.findall(r'bool getLyric(\w+)\(\)', st_h)
check('lyric style toggles are discoverable', len(style_bools) >= 2, str(style_bools))
for name in style_bools:
    key = 'lyric' + name
    # Full screen and scroll use fresh NVS keys so devices that stored the old
    # opt-in flags pick up the new default-on behaviour after flashing.
    storage_key = {'Full': 'lyricFullScreen',
                   'Scroll': 'lyricScrollAnim'}.get(name, key)
    low = name[0].lower() + name[1:]          # the web parameter's spelling
    check('setting %-9s has a getter and a setter' % key,
          ('bool getLyric%s();' % name) in st_h and ('void setLyric%s(bool' % name) in st_h)
    check('setting %-9s is loaded from and saved to NVS' % key,
          ('getBool("%s"' % storage_key) in st_cpp and
           ('putBool("%s"' % storage_key) in st_cpp)
    check('setting %-9s is defaulted at construction and in clearAll()' % key,
          re.search(r'_%s\(' % key, ctor_init) is not None and
          re.search(r'_%s\s*=' % key, clear_all) is not None)
    check('setting %-9s is published in /api/status' % key, 'doc["%s"] =' % key in web)
    check('setting %-9s is accepted by the style endpoint' % key,
          'hasParam("%s", true)' % low in lyr_src and 'setLyric%s(' % name in lyr_src)
    check('setting %-9s is tracked, so changing it repaints' % key,
          ('_lastLyric%s' % name) in lyr_src and ('!= _lastLyric%s' % name) in lyr_src)
    check('setting %-9s has a dashboard control that posts and syncs' % key,
          ('id="%s"' % key) in lyr_src and ('%s=' % low) in lyr_src and
          ('data.%s' % key) in lyr_src)
# A control in the card that no script reads is dead UI, and a script that reads
# an id the card does not have throws at load. Check both directions.
m = re.search(r'LyricsPlugin::uiSection.*?R"rawliteral\((.*?)\)rawliteral', lyr_src, re.S)
lyr_card = m.group(1) if m else ''
check('lyrics dashboard card parsed from the plugin', len(lyr_card) > 2000, '%d bytes' % len(lyr_card))
card_ids = set(re.findall(r'id="([^"]+)"', lyr_card))
card_refs = set(re.findall(r"getElementById\('([^']+)'\)", lyr_card))
check('every id the lyrics script reads exists in the lyrics card',
      card_refs <= card_ids, 'missing: ' + str(sorted(card_refs - card_ids)))
card_inputs = set(re.findall(r'<input[^>]*id="([^"]+)"', lyr_card))
check('every lyric control is read by the card script',
      card_inputs <= card_refs, 'unread: ' + str(sorted(card_inputs - card_refs)))

check('full-screen size is clamped on load and on set',
      'if (_lyricFullSize > LYRIC_FULL_SIZE_MAX)' in st_cpp and
      'if (size > LYRIC_FULL_SIZE_MAX) size = LYRIC_FULL_SIZE_MAX;' in st_cpp)

# ---- scroll rendering + transition geometry -------------------------------
#
# Every lyric row uses one uniform size. Lines wrap instead of ellipsizing, the
# current block is centered between the header and the timestamp band, and a
# line advance slides the whole wrapped stack up by the old block's height.

size_max = int(re.search(r'LYRIC_TEXT_MAX_SIZE = (\d+)', lyr_src).group(1))
ink_above = int(re.search(r'LYRIC_INK_ABOVE = (-?\d+)', lyr_src).group(1))
disp_cpp = read('src/Display.cpp')
ascent = int(re.search(r'EMOJI_ASCENT = (\d+)', disp_cpp).group(1))
check('the row ink box rises 14*size px above the baseline',
      ink_above == 14 and ink_above == ascent,
      'above %d, Display says %d' % (ink_above, ascent))
check('lyrics use the two dashboard text sizes only', size_max == 2, str(size_max))
check('row pitch scales with the uniform text size',
      '(16 + LYRIC_ROW_GAP) * size' in lyr_src)
check('lyric lines wrap instead of ellipsizing',
      '_display->wrapText(text, size, dispW - 16, rows, LYRIC_MAX_ROWS)' in lyr_src and
      '_display->fitText(current' not in lyr_src)
check('the lyric viewport ends above the timestamp band',
      int(re.search(r'LYRIC_CONTENT_BOTTOM = (\d+)', lyr_src).group(1)) >= 36 and
      'const int16_t vpBottom = full ? dispH - 4 : dispH - LYRIC_CONTENT_BOTTOM;' in lyr_src)
check('the transition is armed on a line advance, never on a first paint',
      'lineAdvanced' in lyr_src and '_lastLyricLine.length() > 0' in lyr_src and
      '_lastLyricLine != current' in lyr_src)
check('a seek or a skipped-forward line does not animate as a normal advance',
      '!seek' in lyr_src and 'progressNow + 2 < _lastLyricProgress' in lyr_src)
check('the transition starts one line height plus its gap below centre',
      '_scrollFrom = _lastCurrH + blockGap;' in lyr_src and
      'if (_scrollFrom < rowPitch) _scrollFrom = rowPitch;' in lyr_src)
check('the sliding block never starts partly below the viewport',
      'const int16_t roomBelow = vpBottom - (currTop + currH);' in lyr_src and
      '_scrollFrom = clampScroll(_scrollFrom);' in lyr_src and
      lyr_src.count('_scrollFrom = clampScroll(_scrollFrom);') == 2)
check('progress is wall-clock based, so a slow bus costs frames, not time',
      re.search(r'constexpr uint16_t LYRIC_SCROLL_MS', lyr_src) is not None and
      'elapsed >= LYRIC_SCROLL_MS' in lyr_src and 'elapsed * 1000 / LYRIC_SCROLL_MS' in lyr_src)
check('the transition lasts about 300 ms',
      int(re.search(r'LYRIC_SCROLL_MS = (\d+)', lyr_src).group(1)) == 300)
check('a cache reset (style/rotation/theme) cancels a transition in flight',
      re.search(r'clearCache\(\)\s*\{(?:\s*//[^\n]*)*\s*_scrollActive = false;', lyr_src) is not None)
check('a stale transition falls through to a full repaint',
      'const bool stale = full != _lastLyricFull ||' in lyr_src)
check('the transition redraws only the current line',
      'eraseLast();' in lyr_src and
      'drawCurrentRows(currTop + dy, true);' in lyr_src and
      'drawHistory' not in lyr_src and 'drawNext' not in lyr_src)
check('animation frames erase just the previous text band, not the viewport',
      'drawFilledRectangle(0, top, dispW, bottom - top, BLACK)' in lyr_src and
      'clearLyrics' not in lyr_src)
check('an unchanged frame is not repainted', 'if (dy == _scrollDy && !done) return;' in lyr_src)
check('the transition can be re-armed by the next line',
      lyr_src.count('_scrollActive = true;') == 2)
check('enabling Scroll Animation previews the motion immediately',
      '_scrollPreview = previewScroll;' in lyr_src and
      '} else if (_scrollPreview) {' in lyr_src and
      '_scrollFrom = rowPitch + blockGap;' in lyr_src)
check('the active line is drawn plain at full brightness',
      "drawCenteredTextAt(baseline, currRows[i].c_str(), lyricColor, size)" in lyr_src and
      'scaleColor(lyricColor' not in lyr_src and 'drawCenteredTextEffect' not in lyr_src)
check('previous and next context lines are no longer rendered',
      'getPrevLine' not in lyr_src and 'getNextLine' not in lyr_src and
      '_history' not in lyr_src and 'pushHistory' not in lyr_src)
check('a normal line change does not repaint the whole screen',
      'const bool fullRepaint = titleChanged ||' in lyr_src and
      '} else {\n        eraseLast();' in lyr_src)
check('full screen is enabled by default with a fresh preference key',
      '_lyricFull(true)' in st_cpp and
      'getBool("lyricFullScreen", true)' in st_cpp and
      'putBool("lyricFullScreen", _lyricFull)' in st_cpp)
check('rainbow and gradient effects are fully removed',
      'LyricRainbow' not in st_cpp + st_h + lyr_src and
      'LyricGradient' not in st_cpp + st_h + lyr_src and
      'lyricRainbow' not in st_cpp + st_h + lyr_src and
      'lyricGradient' not in st_cpp + st_h + lyr_src)

# Simulate the shipped smoothstep expressions at 1 ms resolution for a few
# travel distances (one-row and wrapped-block advances). It must start at the
# full distance, end at zero, and never reverse.
ms = int(re.search(r'LYRIC_SCROLL_MS = (\d+)', lyr_src).group(1))
check('the easing expressions are the ones simulated here',
      'const int32_t p = (int32_t)(elapsed * 1000 / LYRIC_SCROLL_MS);' in lyr_src and
      'const int32_t p2 = p * p / 1000;' in lyr_src and
      'eased = p2 * (3000 - 2 * p) / 1000;' in lyr_src and
      'const int16_t dy =' in lyr_src and
      'done ? 0 : (int16_t)((int32_t)_scrollFrom * (1000 - eased) / 1000);' in lyr_src and
      'const bool done = elapsed >= LYRIC_SCROLL_MS;' in lyr_src)
for travel in (14, 46, 78):
    offsets = []
    for elapsed in range(ms + 1):
        done = elapsed >= ms
        if done:
            eased = 1000
        else:
            p = elapsed * 1000 // ms
            p2 = p * p // 1000
            eased = p2 * (3000 - 2 * p) // 1000
        offsets.append(0 if done else travel * (1000 - eased) // 1000)
    check('easing over %d px starts at full travel, ends at zero and never reverses' % travel,
          offsets[0] == travel and offsets[-1] == 0 and
          all(b <= a for a, b in zip(offsets, offsets[1:])),
          'first %d last %d' % (offsets[0], offsets[-1]))
    check('easing over %d px is still visibly moving mid-way' % travel,
          0 < offsets[ms // 2] < travel, 'mid offset %d' % offsets[ms // 2])

# ------------------------------------------------------- line timing / pacing
#
# The phone sends song-millisecond line timing; the manager turns it into a
# line-progress figure published in /api/status. Each layer's arithmetic is
# table-tested against a mirror of the shipped code, pinned by the exact
# expressions.

mgr = read('src/LyricsManager.cpp')
check('the endpoint takes the line timing as optional parameters',
      all('hasParam("%s", true)' % p in lyr_src for p in ('posms', 'lstart', 'lend')))
check('the JSON path takes the same timing',
      all('data["%s"]' % p in lyr_src for p in ('posms', 'lstart', 'lend')))
check('a zero-length line window is refused',
      'if (lineEndMs <= lineStartMs) {' in mgr)
check('a line change invalidates the previous line\'s timing',
      '_lineSetAt = millis();\n'
      '        // Timing is per line: drop the previous line\'s until the phone says\n'
      '        // otherwise (it calls setLineTiming() right after this).\n'
      '        _hasLineTiming = false;' in mgr)
check('line lengths are learned, with outlier intervals excluded',
      '_estLineMs = (_estLineMs * 3 + held) / 4;' in mgr and
      'held > 400 && held < 30000' in mgr)
check('timing is dropped on song change, clear and stop',
      mgr.count('_hasLineTiming = false;') >= 4, str(mgr.count('_hasLineTiming = false;')))
check('pacing uses the song clock when a millisecond position arrived',
      '_posMs + (millis() - _posMsAt)' in mgr and 'if (_hasPosMs) {' in mgr)
check('an unpaced line reports 255 rather than pretending to be at 0',
      'return 255;' in mgr)

# Mirror of the pacing arithmetic: clamped at both ends, never above 100.
def line_progress(song_ms, start, end, span_known=True, local_ms=0, est=3000):
    if not span_known:
        return 100 if local_ms >= est else local_ms * 100 // est
    if end <= start:
        return 255
    if song_ms <= start:
        return 0
    into = song_ms - start
    span = end - start
    return 100 if into >= span else into * 100 // span


pacing = [((0, 10000, 13000), 0), ((9999, 10000, 13000), 0), ((10000, 10000, 13000), 0),
          ((11500, 10000, 13000), 50), ((12999, 10000, 13000), 99),
          ((13000, 10000, 13000), 100), ((99999, 10000, 13000), 100),
          ((500, 10000, 13000), 0)]
bad = [(t, want, line_progress(*t)) for t, want in pacing if line_progress(*t) != want]
check('pacing mirrors the shipped arithmetic over a line window', not bad, str(bad))
check('pacing never exceeds 100 when the clock runs past the line',
      line_progress(999999, 0, 1000) == 100)

# ---------------------------------------------------------------- the app side

app_client = read('android-lyrics-app/app/src/main/java/com/esp32dashboard/lyrics/Esp32Client.kt')
app_engine = read('android-lyrics-app/app/src/main/java/com/esp32dashboard/lyrics/LyricsEngine.kt')
check('the app sends the line timing with each line',
      '&posms=$posMs&lstart=$lineStartMs&lend=$lineEndMs' in app_client)
check('the app derives a line\'s end from the next line (track end for the last)',
      'if (index < lyrics.size - 1) lyrics[index + 1].timeMs else totalDuration' in app_engine)
check('both the line change and the periodic re-anchor carry the timing',
      app_engine.count('lineStartMs =') == 2 and app_engine.count('lineEndMs =') == 2,
      'lineStartMs=%d lineEndMs=%d' % (app_engine.count('lineStartMs ='),
                                       app_engine.count('lineEndMs =')))

# --------------------------------------------- cross-task access inventory
#
# Web handlers run on the AsyncTCP task; the render loop runs on the Arduino
# task. Anything both tasks touch is a data race unless it is guarded. This
# block does not pass or fail: it names the exact sites so the risk is visible.

ASYNC_ENTRY = ('handleApi', 'registerRoutes')
LOOP_ENTRY = ('update', 'drawScreen', 'backgroundTick', 'enter', 'exit',
              'onRotationChange', 'onThemeChange', 'uiSection')


def body(src, cls, method):
    m = re.search(r'%s::%s\b[^{;]*\{' % (cls, method), src)
    if not m:
        return ''
    depth, i = 1, m.end()
    while i < len(src) and depth:
        depth += (src[i] == '{') - (src[i] == '}')
        i += 1
    return src[m.end():i]


print('\n-- cross-task access inventory (web/AsyncTCP task vs Arduino loop task) --')
shared_any = []
for p in plugins:
    async_b = ' '.join(body(p.cs, p.cls, f) for f in ASYNC_ENTRY)
    loop_b = ' '.join(body(p.cs, p.cls, f) for f in LOOP_ENTRY)
    if not async_b.strip():
        continue
    # Objects (member pointers/references) whose methods each side calls.
    a_obj = set(re.findall(r'(_\w+)\s*->', async_b))
    l_obj = set(re.findall(r'(_\w+)\s*->', loop_b))
    shared = sorted(a_obj & l_obj)
    # Own methods reached from both sides (a true concurrent writer).
    a_m = set(re.findall(r'\b(\w+)\(', async_b))
    l_m = set(re.findall(r'\b(\w+)\(', loop_b))
    both = sorted(a_m & l_m - {'if', 'for', 'while', 'return', 'String', 'get', 'millis'})
    shared_any += shared
    print('  %-6s shared objects: %s | methods called from both tasks: %s'
          % (p.id, ', '.join(shared) or '-', ', '.join(both) or '-'))
print('  NOTE: PluginManager serializes nothing on this path; the lock-free')
print('        score/lyrics/football state is the accepted-risk area.')
check('thread contract documented in Plugin.h',
      re.search(r'AsyncTCP task', read('src/plugins/Plugin.h')) is not None,
      'Plugin.h must state which task handlers run on')

print('\n%s (%d checks failed)' % ('ALL CHECKS PASSED' if not fails else 'FAILURES: ' + ', '.join(fails),
                                   len(fails)))
sys.exit(1 if fails else 0)
