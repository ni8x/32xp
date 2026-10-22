#pragma once
// Shared web assets for the whole dashboard: the design-system stylesheet plus
// the reusable card markup builders.
//
// The CSS lives here, in one place, instead of being embedded in the page
// literal in WebServer.cpp. The shell splices it in via the %DASHBOARD_CSS%
// placeholder, and every uiSection() builds its markup from these same classes
// (.card, .row, .value, .btn-inline, .hint, ...) instead of repeating inline
// styles. The scores section's live-feed cards come from ESPN_CARD_HTML below,
// so cricket and football cannot drift apart either.
#include <Arduino.h>

inline constexpr const char DASHBOARD_CSS[] = R"css(
    :root {
      --bg:       #ece0c8;
      --surface:  #f8f1e2;
      --raised:   #efe4cf;
      --border:   #d8c6a4;
      --accent:   #8b5e34;
      --accent2:  #b07d3f;
      --text:     #3d2f21;
      --muted:    #8a765c;
      --success:  #6d8b4a;
      --warning:  #c08a2e;
      --danger:   #a8482f;
      --radius:   12px;
      --radius-sm:8px;
      --shadow:   0 2px 16px rgba(93,68,40,0.16);
      --nav-h:    56px;
    }
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    html { scroll-behavior: smooth; }
    body {
      font-family: system-ui, -apple-system, 'Segoe UI', sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      padding-bottom: calc(var(--nav-h) + 16px);
    }
    /* ── Typography ── */
    h1 { font-size: 1.1rem; font-weight: 700; letter-spacing: .03em; }
    h2 { font-size: .78rem; font-weight: 600; letter-spacing: .12em; text-transform: uppercase; color: var(--muted); margin-bottom: 14px; }
    p  { font-size: .82rem; color: var(--muted); line-height: 1.55; margin-top: 6px; }
    /* ── Top bar ── */
    .topbar {
      position: sticky; top: 0; z-index: 100;
      background: var(--surface);
      border-bottom: 1px solid var(--border);
      padding: 0 16px;
      height: var(--nav-h);
      display: flex; align-items: center; justify-content: space-between;
    }
    .topbar-title { display: flex; align-items: center; gap: 10px; }
    .topbar-dot {
      width: 8px; height: 8px; border-radius: 50%;
      background: var(--muted); flex-shrink: 0;
      transition: background .4s;
    }
    .topbar-dot.online  { background: var(--success); box-shadow: 0 0 6px var(--success); }
    .topbar-dot.offline { background: var(--danger); }
    /* Polls are failing: the page is showing what the device last said. */
    .topbar-dot.stale   { background: var(--warning); }
    .conn-warn {
      background: rgba(192,138,46,.18); color: var(--warning);
      font-size: .75rem; font-weight: 600; letter-spacing: .02em;
      padding: 8px 16px; text-align: center;
      border-bottom: 1px solid var(--border);
    }
    .status-chips { display: flex; gap: 6px; flex-wrap: wrap; }
    .chip {
      font-size: .7rem; font-weight: 500; letter-spacing: .04em;
      background: var(--raised); border: 1px solid var(--border);
      border-radius: 20px; padding: 3px 10px; color: var(--muted);
    }
    /* ── Bottom nav ── */
    .nav {
      position: fixed; bottom: 0; left: 0; right: 0; z-index: 100;
      height: var(--nav-h);
      background: var(--surface);
      border-top: 1px solid var(--border);
      display: flex;
    }
    .nav-btn {
      flex: 1; display: flex; flex-direction: column;
      align-items: center; justify-content: center;
      gap: 3px; border: none; background: transparent;
      color: var(--muted); cursor: pointer;
      font-size: .65rem; font-weight: 500; letter-spacing: .06em;
      text-transform: uppercase; transition: color .2s;
      -webkit-tap-highlight-color: transparent;
    }
    .nav-btn svg { width: 20px; height: 20px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
    .nav-btn.active, .nav-btn:hover { color: var(--accent); }
    /* ── Sections ── */
    .section { display: none; padding: 16px; max-width: 560px; margin: auto; }
    .section.active { display: block; }
    /* ── Cards ── */
    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 18px;
      margin-bottom: 14px;
      box-shadow: var(--shadow);
    }
    .card + .card { }
    /* ── Row ── */
    .row {
      display: flex; justify-content: space-between; align-items: center;
      padding: 7px 0; border-bottom: 1px solid var(--border);
      font-size: .85rem;
    }
    .row:last-child { border-bottom: none; }
    .row span:first-child { color: var(--muted); }
    .value { font-family: 'SF Mono', 'Fira Code', monospace; font-size: .82rem; color: var(--text); }
    /* ── Form controls ── */
    label { display: block; font-size: .8rem; font-weight: 600; color: var(--muted); margin-bottom: 5px; margin-top: 12px; letter-spacing: .05em; text-transform: uppercase; }
    label:first-child { margin-top: 0; }
    input[type="text"],
    input[type="password"],
    input[type="number"],
    input[type="date"],
    select {
      width: 100%; padding: 10px 12px;
      background: var(--raised); border: 1px solid var(--border);
      border-radius: var(--radius-sm); color: var(--text);
      font-size: .88rem; outline: none;
      transition: border-color .2s;
      -webkit-appearance: none; appearance: none;
    }
    input:focus, select:focus { border-color: var(--accent); }
    select { background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%238a765c' d='M6 8L1 3h10z'/%3E%3C/svg%3E"); background-repeat: no-repeat; background-position: right 12px center; padding-right: 32px; }
    /* ── Range sliders ── */
    .slider-row { display: flex; align-items: center; gap: 10px; margin-top: 6px; }
    .slider-row input[type="range"] { flex: 1; -webkit-appearance: none; appearance: none; height: 4px; border-radius: 2px; background: var(--border); outline: none; }
    .slider-row input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; }
    .slider-row input[type="range"]::-moz-range-thumb { width: 18px; height: 18px; border-radius: 50%; background: var(--accent); border: none; cursor: pointer; }
    .slider-val { min-width: 44px; text-align: right; font-size: .82rem; font-family: monospace; color: var(--text); }
    /* ── Buttons ── */
    .btn {
      display: inline-flex; align-items: center; justify-content: center; gap: 6px;
      padding: 9px 18px; border-radius: var(--radius-sm);
      border: none; cursor: pointer; font-size: .84rem; font-weight: 600;
      transition: opacity .15s, transform .1s; -webkit-tap-highlight-color: transparent;
    }
    .btn:active { transform: scale(.97); }
    .btn-primary { background: var(--accent); color: #fdf6e8; }
    .btn-primary:hover { opacity: .88; }
    .btn-secondary { background: var(--raised); color: var(--text); border: 1px solid var(--border); }
    .btn-secondary:hover { border-color: var(--accent); color: var(--accent); }
    .btn-danger { background: var(--danger); color: #fdf6e8; }
    .btn-danger:hover { opacity: .85; }
    .btn-sm { padding: 6px 12px; font-size: .78rem; }
    .btn-row { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 12px; }
    /* ── Color pickers ── */
    .color-row { display: flex; gap: 8px; align-items: center; }
    input[type="color"] { width: 44px; height: 36px; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--raised); padding: 2px; cursor: pointer; }
    /* ── Toggles (checkboxes) ── */
    .toggle-group { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 8px; }
    .toggle-label {
      display: flex; align-items: center; gap: 7px;
      background: var(--raised); border: 1px solid var(--border);
      border-radius: 20px; padding: 6px 14px; cursor: pointer;
      font-size: .82rem; font-weight: 500;
      transition: border-color .2s, color .2s;
      user-select: none;
    }
    .toggle-label input[type="checkbox"] { display: none; }
    .toggle-label .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--border); transition: background .2s; flex-shrink: 0; }
    .toggle-label:has(input:checked) { border-color: var(--accent); color: var(--accent); }
    .toggle-label:has(input:checked) .dot { background: var(--accent); }
    /* ── Clock face picker ── */
    .face-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(92px, 1fr)); gap: 10px; margin: 8px 0 2px; }
    .face-card {
      background: var(--raised); border: 1px solid var(--border);
      border-radius: var(--radius-sm); padding: 7px; cursor: pointer;
      font-family: inherit; color: var(--text); text-align: center;
      transition: border-color .15s, background .15s, box-shadow .15s;
    }
    .face-card:hover { border-color: var(--accent2); }
    .face-card.active { border-color: var(--accent); background: var(--surface); box-shadow: 0 0 0 2px rgba(139,94,52,.22); }
    .face-thumb { position: relative; display: block; height: 62px; border-radius: 6px; background: #16130f; overflow: hidden; margin-bottom: 6px; }
    .face-name { display: block; font-size: .76rem; font-weight: 600; }
    .face-desc { display: block; font-size: .66rem; line-height: 1.25; color: var(--muted); margin-top: 2px; }
    /* mini previews — these mimic what the panel actually paints */
    .t-digital .big { position: absolute; left: 50%; top: 27px; width: 52px; height: 11px; margin-left: -26px; border-radius: 2px; background: #efe4cf; }
    .t-digital .small { position: absolute; left: 50%; top: 44px; width: 30px; height: 4px; margin-left: -15px; border-radius: 2px; background: var(--muted); }
    .t-digital .heart { position: absolute; left: 50%; top: 7px; width: 18px; height: 16px; margin-left: -9px; background: var(--accent2); clip-path: polygon(50% 100%, 6% 58%, 0 33%, 12% 13%, 33% 13%, 50% 32%, 67% 13%, 88% 13%, 100% 33%, 94% 58%); }
    .t-analogue .ring { position: absolute; left: 50%; top: 50%; width: 42px; height: 42px; margin: -21px 0 0 -21px; border: 2px solid var(--accent2); border-radius: 50%; }
    .t-analogue .hand { position: absolute; left: 50%; bottom: 50%; transform-origin: 50% 100%; border-radius: 2px; background: #efe4cf; }
    .t-analogue .h { width: 3px; height: 12px; margin-left: -1.5px; transform: rotate(55deg); }
    .t-analogue .m { width: 2px; height: 17px; margin-left: -1px; transform: rotate(-35deg); background: var(--accent2); }
    .t-analogue .s { width: 1px; height: 19px; margin-left: -0.5px; transform: rotate(155deg); background: var(--danger); }
    .t-analogue .pin { position: absolute; left: 50%; top: 50%; width: 5px; height: 5px; margin: -2.5px 0 0 -2.5px; border-radius: 50%; background: var(--accent2); }
    .t-8bit::before { content: ""; position: absolute; inset: 5px; border: 2px solid var(--accent2); }
    .t-8bit::after { content: ""; position: absolute; inset: 10px; border: 1px solid var(--muted); }
    .t-8bit .big { position: absolute; left: 50%; top: 22px; width: 44px; height: 13px; margin-left: -22px; background: repeating-linear-gradient(90deg, #efe4cf 0 5px, #16130f 5px 7px); }
    .t-8bit .small { position: absolute; left: 50%; top: 42px; width: 14px; height: 8px; margin-left: -7px; background: var(--accent2); }
    .t-anime .eye { position: absolute; top: 14px; width: 13px; height: 13px; border-radius: 50%; background: #efe4cf; }
    .t-anime .eye.l { left: 17px; }
    .t-anime .eye.r { right: 17px; }
    .t-anime .smile { position: absolute; left: 50%; top: 30px; width: 22px; height: 12px; margin-left: -11px; border: 2px solid transparent; border-bottom-color: #efe4cf; border-radius: 50%; }
    .t-anime .sp { position: absolute; width: 3px; height: 9px; background: var(--accent2); border-radius: 1px; }
    .t-anime .sp.x { width: 9px; height: 3px; }
    .t-anime .big { position: absolute; left: 50%; bottom: 5px; width: 40px; height: 7px; margin-left: -20px; border-radius: 3px; background: linear-gradient(90deg, var(--accent2), #efe4cf, var(--accent2)); }
    .t-minimal .big { position: absolute; left: 50%; top: 24px; width: 36px; height: 10px; margin-left: -18px; background: #efe4cf; border-radius: 1px; }
    .t-minimal .rule { position: absolute; left: 50%; top: 39px; width: 58px; height: 1px; margin-left: -29px; background: var(--muted); }
    .t-minimal .small { position: absolute; left: 50%; top: 45px; width: 24px; height: 5px; margin-left: -12px; background: var(--muted); border-radius: 2px; }
    /* ── Emoji picker ── */
    .emoji-row { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0 2px; }
    .emoji-btn {
      background: var(--raised); border: 1px solid var(--border);
      border-radius: var(--radius-sm); padding: 4px 7px; cursor: pointer;
      font-size: 1.05rem; line-height: 1.1;
      transition: border-color .15s, transform .1s;
    }
    .emoji-btn:hover { border-color: var(--accent); }
    .emoji-btn:active { transform: scale(.94); }
    /* ── File list ── */
    .file-item {
      display: flex; justify-content: space-between; align-items: center;
      padding: 9px 0; border-bottom: 1px solid var(--border); gap: 8px;
    }
    .file-item:last-child { border-bottom: none; }
    .file-name { font-size: .83rem; font-family: monospace; word-break: break-all; color: var(--text); flex: 1; }
    /* ── Status badges ── */
    .badge { display: inline-block; font-size: .72rem; font-weight: 600; padding: 2px 9px; border-radius: 10px; letter-spacing: .04em; }
    .badge-success { background: rgba(109,139,74,.18); color: var(--success); }
    .badge-muted   { background: var(--raised); color: var(--muted); }
    .badge-warn    { background: rgba(192,138,46,.18); color: var(--warning); }
    /* ── Lyric now-playing ── */
    .nowplaying { background: var(--raised); border-radius: var(--radius-sm); padding: 12px 14px; margin: 10px 0; }
    .nowplaying .song-title { font-size: .95rem; font-weight: 600; }
    .nowplaying .song-artist { font-size: .8rem; color: var(--muted); margin-top: 2px; }
    .nowplaying .lyric-line { font-size: .85rem; color: var(--accent2); margin-top: 8px; font-style: italic; }
    .nowplaying .progress-bar { height: 3px; background: var(--border); border-radius: 2px; margin-top: 10px; overflow: hidden; }
    .nowplaying .progress-fill { height: 100%; background: var(--accent); border-radius: 2px; width: 0%; transition: width .4s; }
    /* ── Upload zone ── */
    .upload-zone { border: 2px dashed var(--border); border-radius: var(--radius); padding: 24px; text-align: center; cursor: pointer; transition: border-color .2s; }
    .upload-zone:hover { border-color: var(--accent); }
    .upload-zone input[type="file"] { display: none; }
    .upload-zone .upload-icon { font-size: 2rem; margin-bottom: 8px; }
    .upload-zone p { margin-top: 6px; font-size: .82rem; }
    /* ── Dividers ── */
    hr { border: none; border-top: 1px solid var(--border); margin: 16px 0; }
    /* ── Collapsible ── */
    details summary { cursor: pointer; font-size: .82rem; font-weight: 600; color: var(--muted); letter-spacing: .06em; text-transform: uppercase; list-style: none; display: flex; align-items: center; gap: 6px; }
    details summary::before { content: '›'; font-size: 1.1rem; transition: transform .2s; }
    details[open] summary::before { transform: rotate(90deg); }
    details > *:not(summary) { margin-top: 14px; }
    /* ── Desktop layout ── */
    @media (min-width: 700px) {
      body { padding-bottom: calc(var(--nav-h) + 24px); }
      .topbar { padding: 0 24px; }
      .main-content { padding: 24px; }
      .section { max-width: 520px; padding: 0; }
    }
    .btn-inline { display: flex; gap: 10px; }
    .hint { color: var(--muted); font-size: 0.85em; margin-bottom: 0; }
)css";

// One skeleton for the live-feed cards in the scores section. Cricket and
// football differ only in their labels, element-id prefix, option list and
// trailing buttons, so the shared shape lives here and espnCardHtml() fills in
// the blanks -- %P% is the element-id prefix ("cric"/"fb"), %S% the sport slug.
inline constexpr const char ESPN_CARD_HTML[] = R"scorecard(
    <div class="card">
      <h2>%T%</h2>
      <div class="row"><span>Displayed match:</span> <span class="value" id="%P%Match">-</span></div>
      <div class="row"><span>Feed status:</span> <span class="value" id="%P%FeedStatus">-</span></div>
      <label>%L%</label>
      <select id="%P%League" onchange="document.getElementById('%P%LeagueCustom').value=''; browseMatches('%S%')">%O%      </select>
      <label>%C%</label>
      <input type="%Y%" id="%P%LeagueCustom" placeholder="%H%">
      <label>Match date (optional, for finished scores)</label>
      <input type="date" id="%P%Date" onchange="document.getElementById('%P%Event').value=''">
      <button id="%P%BrowseBtn" onclick="browseMatches('%S%')">Browse matches</button>
      <label>Match</label>
      <select id="%P%Event"><option value="">Automatic: live, then next match</option></select>
      <label>Refresh interval (seconds)</label>
      <input type="number" id="%P%Interval" min="30" max="3600" placeholder="30">
%X%      <p class="hint">%D%</p>
    </div>
)scorecard";

inline String espnCardHtml(const char* title, const char* prefix, const char* sport,
                           const char* leagueLabel, const char* customLabel,
                           const char* customType, const char* customPlaceholder,
                           const char* options, const char* buttons, const char* help) {
    String card;
    card.reserve(sizeof(ESPN_CARD_HTML) + strlen(options) + strlen(buttons) +
                 strlen(customLabel) + strlen(customPlaceholder) + strlen(help) + 32);
    card = ESPN_CARD_HTML;
    card.replace("%T%", title);
    card.replace("%P%", prefix);
    card.replace("%S%", sport);
    card.replace("%L%", leagueLabel);
    card.replace("%C%", customLabel);
    card.replace("%Y%", customType);
    card.replace("%H%", customPlaceholder);
    card.replace("%O%", options);
    card.replace("%X%", buttons);
    card.replace("%D%", help);
    return card;
}
