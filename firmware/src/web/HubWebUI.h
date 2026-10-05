// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubWebUI.h — the web UI, embedded in flash and served from PROGMEM:
//   HUB_INDEX_HTML at "/", HUB_STYLE_CSS at "/hub.css", HUB_SCRIPT_JS at "/hub.js".
// Layout: the hub's front panel (six sockets, LED state mirrored in each USB-C
// opening), a readout column beside it, and settings in a slide-in drawer.
// Colours are GitHub Primer (MIT). Everything is embedded — nothing is fetched
// from the internet. All data in/out is JSON against /api/*.
#pragma once
#include <Arduino.h>

static const char HUB_INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart USB Hub</title>
<link rel="stylesheet" href="/hub.css">
</head>
<body data-theme="dark">

<header class="topbar">
  <div class="ident">
    <span class="ident-name" id="dev-name">Smart USB Hub</span>
    <span class="ident-maker">by Unexpected Maker</span>
  </div>
  <div class="tools">
    <button id="theme-toggle" class="tool" onclick="toggleTheme()" title="Light / dark"><i data-icon="sun"></i></button>
    <button class="tool" onclick="openSettings()" title="Settings"><i data-icon="gear"></i></button>
  </div>
</header>

<div class="linkbar"><i data-icon="plug"></i> Hub not responding &mdash; reconnecting automatically&hellip;</div>

<main class="layout">
  <section>
    <div class="bezel"><div id="ports" class="sockets"></div></div>
    <div id="port-detail" class="detail" style="display:none;"></div>
  </section>

  <aside class="readout">
    <div class="ro-group">
      <h3>Power</h3>
      <div class="ro-line"><span>Total draw</span><span id="s-total">--</span></div>
    </div>
    <div class="ro-group">
      <h3>Environment</h3>
      <div class="ro-line"><span>Temperature</span><span id="s-temp">--</span></div>
      <div class="ro-line"><span>Humidity</span><span id="s-humid">--</span></div>
    </div>
    <div class="ro-group">
      <h3>Clock</h3>
      <div class="ro-line"><span>Time</span><span id="s-time">--:--</span></div>
      <div class="ro-line"><span>Date</span><span id="s-date">--</span></div>
    </div>
    <div class="ro-group">
      <h3>Network</h3>
      <div class="ro-line"><span>IP address</span><span id="s-ip">--</span></div>
    </div>
    <div class="ro-group">
      <h3>System</h3>
      <div class="ro-line"><span>Uptime</span><span id="s-uptime">--</span></div>
      <div class="ro-line"><span>Free heap</span><span id="s-heap">--</span></div>
      <div class="ro-line"><span>Largest block</span><span id="s-maxblk">--</span></div>
      <div class="ro-line"><span>Firmware</span><span id="fw-version">--</span></div>
      <div class="ro-line"><span>Last boot</span><span id="rst-reason">--</span></div>
    </div>
  </aside>
</main>

<div id="drawer" class="drawer" aria-hidden="true">
  <div class="drawer-scrim" onclick="closeSettings()"></div>
  <div class="drawer-panel" role="dialog" aria-label="Settings">
    <div class="drawer-head">
      <h2>Settings</h2>
      <button class="tool" onclick="closeSettings()" title="Close"><i data-icon="x"></i></button>
    </div>
    <div class="drawer-body" id="settings-panels"></div>
    <div class="drawer-foot">
      <span id="cfg-msg" class="cfg-msg"></span>
      <button class="btn" onclick="detectLocation()">Detect location</button>
      <button class="btn primary" id="cfg-save" onclick="saveAllSettings()">Save</button>
    </div>
  </div>
</div>

<script src="/hub.js"></script>
</body>
</html>
)rawliteral";

static const char HUB_STYLE_CSS[] PROGMEM = R"css(
/*! modern-normalize v3.0.1 | MIT License | https://github.com/sindresorhus/modern-normalize */
*,::before,::after{box-sizing:border-box}
html{font-family:system-ui,'Segoe UI',Roboto,Helvetica,Arial,sans-serif,'Apple Color Emoji','Segoe UI Emoji';line-height:1.15;-webkit-text-size-adjust:100%;tab-size:4}
body{margin:0}
b,strong{font-weight:bolder}
code,kbd,samp,pre{font-family:ui-monospace,SFMono-Regular,Consolas,'Liberation Mono',Menlo,monospace;font-size:1em}
small{font-size:80%}
sub,sup{font-size:75%;line-height:0;position:relative;vertical-align:baseline}
sub{bottom:-0.25em}
sup{top:-0.5em}
table{border-color:currentcolor}
button,input,optgroup,select,textarea{font-family:inherit;font-size:100%;line-height:1.15;margin:0}
button,[type='button'],[type='reset'],[type='submit']{-webkit-appearance:button}
legend{padding:0}
progress{vertical-align:baseline}
::-webkit-inner-spin-button,::-webkit-outer-spin-button{height:auto}
[type='search']{-webkit-appearance:textfield;outline-offset:-2px}
::-webkit-search-decoration{-webkit-appearance:none}
::-webkit-file-upload-button{-webkit-appearance:button;font:inherit}
summary{display:list-item}
/* end modern-normalize */

/* Base: form controls take the surrounding type size instead of their own. */
body { font-size:1rem; line-height:1.5; -webkit-tap-highlight-color:transparent; }
button, input, select, textarea { font-size:inherit; line-height:inherit; }
button:not(:disabled) { cursor:pointer; }

/* Icons: GitHub Octicons (MIT), inline SVG — sized to the text like a glyph. */
.oi { width:1em; height:1em; fill:currentColor; vertical-align:-.125em; display:inline-block; }

/* Theme: GitHub Primer (github.com/primer/primitives, MIT) — dark + light
   functional colour tokens. */
:root {
  --bg:#0d1117; --surface:#151b23; --surface-2:#262c36;
  --bezel:#212830;                      /* the hub front panel */
  --card:#151b23; --card-hover:#1b222c; /* each socket on the panel */
  --slot-off:#0d1117;                   /* USB-C opening with its LED dark */
  --drawer:#151b23;                     /* settings drawer, one step off the page */
  --txt-blue:#4493f8; --txt-pink:#f778ba;  /* LED hues as readable text */
  --border:#3d444d; --border-soft:#3d444db3;
  --text:#f0f6fc; --text-muted:#9198a1; --text-dim:#9198a1;
  --accent:#4493f8; --accent-2:#1f6feb; --accent-on:#ffffff;
  --success:#3fb950; --warning:#d29922; --danger:#f85149;
  /* LED-state colours — MUST match the RGB LED output exactly (renderLeds). */
  --led-blue:#0022ff; --led-green:#00dd00; --led-red:#dd0000;
  --led-pink:#ff5ac8;
  /* Schedule action tints, matched to the LED colours. */
  --tint-green:rgba(0,221,0,0.13); --tint-red:rgba(221,0,0,0.15);
  --radius:.5rem; --radius-sm:.4rem;
}
[data-theme="dark"]  { color-scheme:dark; }   /* native controls + scrollbars */
[data-theme="light"] { color-scheme:light; }
[data-theme="light"] {
  --bg:#ffffff; --surface:#f6f8fa; --surface-2:#eff2f5;
  --bezel:#eff2f5;
  --card:#ffffff; --card-hover:#f6f8fa;
  --slot-off:#d1d9e0;
  --drawer:#eff2f5;
  --txt-blue:#0969da; --txt-pink:#bf3989;
  --border:#d1d9e0; --border-soft:#d1d9e0b3;
  --text:#1f2328; --text-muted:#59636e; --text-dim:#59636e;
  --accent:#0969da; --accent-2:#0969da; --accent-on:#ffffff;
  --success:#1a7f37; --warning:#9a6700; --danger:#d1242f;
  --led-blue:#0022ff; --led-green:#00ff00; --led-red:#ff0000;
  --led-pink:#ff5ac8;
  --tint-green:rgba(0,150,0,0.14); --tint-red:rgba(220,0,0,0.12);
}

html { scrollbar-gutter:stable; }
body { background:var(--bg); color:var(--text); }

/* ---- Top bar: identity on the left, two round tools on the right. ---- */
.topbar { max-width:1180px; margin:0 auto; padding:1.1rem 1rem .9rem;
  display:flex; align-items:center; justify-content:space-between; gap:1rem; }
.ident { display:flex; align-items:baseline; gap:.6rem; flex-wrap:wrap; }
.ident-name { font-size:1.35rem; font-weight:600; letter-spacing:-.01em; }
.ident-maker { font-size:.8rem; color:var(--text-muted); }
.tools { display:flex; gap:.4rem; }
.tool { width:2.25rem; height:2.25rem; padding:0; display:inline-flex; align-items:center; justify-content:center;
  background:var(--surface); color:var(--text-muted); border:1px solid var(--border); border-radius:50%; }
.tool:hover { color:var(--text); border-color:var(--text-muted); }

/* ---- Shown only while the hub is unreachable. Everything below it stays on
   screen but can't be touched, and is greyed so it can't pass for live. ---- */
.linkbar { display:none; max-width:1148px; margin:0 auto .75rem; padding:.55rem 1rem;
  background:var(--danger); color:#fff; font-size:.85rem; font-weight:600; border-radius:var(--radius); }
body.offline .linkbar { display:block; }
body.offline .layout, body.offline .tools, body.offline .drawer {
  pointer-events:none; filter:grayscale(.8) brightness(.75); }

/* ---- Two columns: the hub on the left, its readout on the right. ---- */
.layout { max-width:1180px; margin:0 auto; padding:0 1rem 2rem;
  display:grid; grid-template-columns:minmax(0,1fr) 16rem; gap:1rem; align-items:start; }
@media (max-width:960px){ .layout { grid-template-columns:minmax(0,1fr); } }

/* ---- The hub's front panel. Port 6 on the left through port 1 on the right,
   the same way round as the hardware. ---- */
.bezel { background:var(--bezel); border:1px solid var(--border); border-radius:14px; padding:.9rem; }
.sockets { display:grid; grid-template-columns:repeat(6, minmax(0,1fr)); gap:.6rem; }
@media (max-width:1100px){ .sockets { grid-template-columns:repeat(3, minmax(0,1fr)); } }
@media (max-width:520px) { .sockets { grid-template-columns:repeat(2, minmax(0,1fr)); } }

.socket { background:var(--card); border:1px solid var(--border); border-radius:10px; padding:.7rem .6rem .6rem;
  display:flex; flex-direction:column; align-items:center; gap:.55rem; cursor:pointer; user-select:none;
  transition:background-color .15s, border-color .15s; }
.socket:hover { background:var(--card-hover); }
/* Selected = its detail is open below. Same 1px border, so nothing shifts. */
.socket.selected { border-color:var(--accent); }
.sock-head { width:100%; display:flex; align-items:center; justify-content:space-between; gap:.3rem; }
.sock-num { font-size:1.5rem; font-weight:700; line-height:1; font-variant-numeric:tabular-nums; }
.sock-state { font-size:.68rem; font-weight:600; color:var(--text-muted); text-align:right; line-height:1.2; }

/* The USB-C opening, filled with whatever that port's RGB LED is showing — it
   mirrors the hardware LED. Static states are set here; pulse and flash are
   driven from JS on one shared clock (animate()) so all six stay in phase. */
.slot { width:3.4rem; height:1.05rem; border-radius:999px; border:2px solid var(--border);
  background:var(--slot-off); }
.st-blue  .slot { background:var(--led-blue); }
.st-green .slot { background:var(--led-green); }
.st-pink  .slot { background:var(--led-pink); }
/* Waiting for a power change to come back from the hub. */
.st-wait  .slot { background:repeating-linear-gradient(45deg, var(--slot-off) 0 4px, var(--border) 4px 8px); }

/* State words use readable text colours in the LED's hue — the raw LED blue and
   red are far too dark to read as text on a dark background. */
.st-blue  .sock-state, .st-pulse .sock-state     { color:var(--txt-blue); }
.st-green .sock-state                            { color:var(--success); }
.st-fault .sock-state                            { color:var(--danger); }
.st-pink  .sock-state, .st-pinkpulse .sock-state { color:var(--txt-pink); }

/* Readings: current is the headline number, volts and power beneath it. */
.sock-read { text-align:center; }
.sock-amps { font-size:1.1rem; font-weight:600; line-height:1.15; font-variant-numeric:tabular-nums; }
.sock-amps small { font-size:.7rem; font-weight:500; color:var(--text-muted); }
.sock-sub { font-size:.72rem; color:var(--text-muted); font-variant-numeric:tabular-nums; white-space:nowrap; }
.sock-sched { width:100%; border-top:1px solid var(--border-soft); padding-top:.45rem; text-align:center;
  font-size:.72rem; color:var(--text-muted); line-height:1.3; }
.sock-sched b { color:var(--text); }
.sock-sched .at { display:block; font-size:.66rem; font-variant-numeric:tabular-nums; }

/* ---- Readout: the hub's own numbers, as label / value lines. ---- */
.readout { background:var(--surface); border:1px solid var(--border); border-radius:14px; padding:.3rem 1rem .8rem; }
@media (max-width:960px){
  .readout { display:grid; grid-template-columns:repeat(auto-fit, minmax(13rem,1fr)); column-gap:1.5rem; } }
/* Group heading is the strongest text; the line labels under it are quieter. */
.ro-group h3 { margin:.8rem 0 .25rem; padding-bottom:.3rem; border-bottom:1px solid var(--border);
  font-size:.9rem; font-weight:600; color:var(--text); }
.ro-line { display:flex; justify-content:space-between; gap:.75rem; padding:.1rem 0; font-size:.82rem; }
.ro-line span:first-child { color:var(--text-muted); }
.ro-line span:last-child { font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:.8rem;
  text-align:right; overflow-wrap:anywhere; }

/* ---- Detail for the selected port: next change, timeline, schedule rows. ---- */
.detail { background:var(--surface); border:1px solid var(--border); border-radius:14px; padding:1rem; margin-top:1rem; }
.detail-bar { display:flex; align-items:center; justify-content:space-between; gap:1rem; flex-wrap:wrap; }
.detail-left { display:flex; align-items:center; gap:1rem; }
.detail-title { font-size:1.1rem; font-weight:700; }
/* Sliding power toggle on each port card. Clicking it toggles power; clicking
   anywhere else on the card opens/closes the detail panel. */
.port-toggle { position:relative; width:4rem; height:2.1rem; border-radius:5px; background:var(--surface-2); border:1px solid var(--border); cursor:pointer; flex-shrink:0; transition:background-color .2s, border-color .2s; }
.toggle-knob { position:absolute; top:3px; bottom:3px; left:3px; width:1.8rem; border-radius:3px; background:var(--text-dim); transition:left .2s, background-color .2s; }
.port-toggle.on { background:var(--success); border-color:var(--success); }
.port-toggle.on .toggle-knob { left:calc(100% - 1.8rem - 4px); background:#fff; }
.add-sched { border:1px solid var(--accent); background:transparent; color:var(--accent); border-radius:var(--radius-sm); padding:.4rem .85rem; font-weight:600; font-size:.8rem; cursor:pointer; }
.add-sched:hover { background:var(--accent); color:var(--accent-on); }

.sched-list { margin-top:1rem; display:flex; flex-direction:column; gap:.5rem; }
.sched-empty { color:var(--text-dim); font-size:.85rem; padding:.4rem 0; }
.sched-row { display:flex; align-items:center; gap:.75rem; flex-wrap:wrap; background:var(--surface-2); border:1px solid var(--border); border-radius:var(--radius-sm); padding:.35rem .6rem .4rem; }
/* Per-row enable switch. Track stays dark in both states — only the knob
   changes: green when enabled, grey outline when disabled. */
.row-toggle { position:relative; margin-left:auto; width:2.2rem; height:1.15rem; border-radius:4px;
  background:var(--bg); border:1px solid var(--border); cursor:pointer; flex-shrink:0; }
.row-toggle .toggle-knob { position:absolute; top:2px; bottom:2px; left:2px; width:.9rem; border-radius:2px;
  background:transparent; border:1px solid var(--text-dim); transition:left .2s, background-color .2s, border-color .2s; }
.row-toggle.on .toggle-knob { left:calc(100% - .9rem - 3px); background:var(--success); border-color:var(--success); }
/* Disabled rows stay visible but clearly inert. */
.sched-row.off > *:not(.row-toggle) { opacity:.4; }

.day-chips { display:flex; gap:.2rem; }
.day-chip { width:1.7rem; height:1.7rem; border-radius:50%; border:1px solid var(--border); background:transparent; color:var(--text-dim); font-size:.7rem; font-weight:700; cursor:pointer; padding:0; }
.day-chip.on { background:var(--accent); border-color:var(--accent); color:var(--accent-on); }
/* Fixed width so the action button always lands in the same column, whether or
   not this row shows a start time (ON EVERY / all-day hides it). */
.sched-time { width:7.5rem; flex-shrink:0; background:var(--bg); border:1px solid var(--border); color:var(--text); border-radius:var(--radius-sm); padding:.25rem .4rem; font-size:.85rem; }
/* Empty stand-in that reserves the start-time slot. */
.sched-slot { width:7.5rem; flex-shrink:0; }
.onoff { border-radius:var(--radius-sm); border:1px solid var(--border); padding:.25rem .7rem; font-weight:700; font-size:.75rem; cursor:pointer; background-color:var(--surface-2); color:var(--text-muted); white-space:nowrap; min-width:4.6rem; }
.onoff.on    { background-image:linear-gradient(var(--tint-green),var(--tint-green)); color:var(--text); }
.onoff.off   { background-image:linear-gradient(var(--tint-red),var(--tint-red)); color:var(--text); }
.onoff.onfor,
.onoff.onuntil,
.onoff.onevery { background-image:linear-gradient(var(--tint-green),var(--tint-green)); color:var(--text); }
/* Inline connector words ("for", "to") between fields in a row. */
.sched-lbl { font-size:.7rem; color:var(--text-dim); padding:0 .1rem; }
/* Duration for "ON FOR" — HH:MM, scroll over hours/minutes to change. */
.sched-dur { width:4.2rem; text-align:center; background:var(--bg); border:1px solid var(--border);
  color:var(--text); border-radius:var(--radius-sm); padding:.25rem .3rem; font-size:.85rem;
  font-variant-numeric:tabular-nums; }
.sched-dur:focus { outline:none; border-color:var(--accent); }
/* Weekly / Once repeat mode. */
.repeat-btn { border-radius:var(--radius-sm); border:1px solid var(--border); padding:.25rem .6rem;
  font-weight:700; font-size:.7rem; letter-spacing:.03em; cursor:pointer; background-color:var(--surface-2);
  color:var(--text-dim); white-space:nowrap; min-width:4.4rem; }
.repeat-btn.once { border-color:var(--accent); color:var(--accent); }
.sched-dup { background:transparent; border:none; color:var(--text-dim); cursor:pointer; font-size:1rem; padding:0 .2rem; }
.sched-dup:hover { color:var(--accent); }
.sched-del { background:transparent; border:none; color:var(--text-dim); cursor:pointer; font-size:1rem; padding:0 .2rem; }
.sched-del:hover { color:var(--danger); }

/* Weekly timeline — one row per day, 24h wide, showing powered blocks like a
   non-linear editor track. Green = powered, red = an entry that clashes. */
.tl { margin:.75rem 0 1rem; }
.tl-head { display:flex; margin-left:2.2rem; border-bottom:1px solid var(--border-soft); margin-bottom:.25rem; }
.tl-hour { flex:1 1 0; font-size:.6rem; color:var(--text-dim); text-align:left; }
.tl-row { display:flex; align-items:center; gap:.4rem; margin-bottom:2px; }
.tl-day { width:1.8rem; font-size:.65rem; font-weight:700; color:var(--text-dim); text-transform:uppercase; }
.tl-track { position:relative; flex:1 1 auto; height:1.05rem; background:var(--bg);
  border:1px solid var(--border); border-radius:3px; overflow:hidden; }
/* Quarter-day gridlines, purely visual. */
.tl-track::after { content:''; position:absolute; inset:0; pointer-events:none;
  background:repeating-linear-gradient(to right, transparent 0, transparent calc(12.5% - 1px), var(--border-soft) calc(12.5% - 1px), var(--border-soft) 12.5%); }
.tl-on { position:absolute; top:0; bottom:0; background:var(--tint-green); }
.tl-on.starts { border-left:2px solid var(--led-green); }
.tl-clash { position:absolute; top:0; bottom:0; width:3px; background:var(--danger); z-index:2; }
/* "Now" cursor — one full-height line cutting through every day row. */
.tl-body { position:relative; }
.tl-now { position:absolute; top:0; bottom:0; width:2px; margin-left:-1px;
  background:var(--accent); z-index:3; pointer-events:none; }
/* Days other than today are genuinely DARKENED (not blended toward whatever is
   behind them, which would lighten them). */
.tl-row.dim { filter:brightness(.6); }
.tl-legend { display:flex; gap:1rem; margin-left:2.2rem; margin-top:.3rem; font-size:.65rem; color:var(--text-dim); }
.tl-key { display:inline-block; width:.7rem; height:.7rem; border-radius:2px; vertical-align:-1px; margin-right:.25rem; }
.tl-key.on { background:var(--tint-green); border:1px solid var(--led-green); }
.tl-key.bad { background:var(--danger); }
/* The entry that fires next. A clash (.bad) still wins — it can't fire at all. */
.sched-row.next { border-color:var(--success); }
.sched-row.bad { border-color:var(--danger); }
.sched-warn { font-size:.65rem; color:var(--danger); font-weight:700; width:100%; }

/* "Next scheduled change" chip, shown next to the port name in the panel. */
.next-chip { font-size:.7rem; color:var(--text-dim); font-weight:600; }
.next-chip b { color:var(--accent); font-weight:700; }

/* ---- Settings: a drawer that slides in over the page from the right. ---- */
.drawer { position:fixed; inset:0; z-index:200; visibility:hidden; transition:visibility 0s .2s; }
.drawer.open { visibility:visible; transition:none; }
.drawer-scrim { position:absolute; inset:0; background:rgba(0,0,0,.6); opacity:0; transition:opacity .2s; }
.drawer.open .drawer-scrim { opacity:1; }
.drawer-panel { position:absolute; top:0; right:0; bottom:0; width:min(30rem, 100%);
  background:var(--drawer); border-left:1px solid var(--border); display:flex; flex-direction:column;
  transform:translateX(100%); transition:transform .2s; }
.drawer.open .drawer-panel { transform:none; }
.drawer-head { display:flex; align-items:center; justify-content:space-between;
  padding:1rem 1.1rem; border-bottom:1px solid var(--border); }
.drawer-head h2 { margin:0; font-size:1.05rem; font-weight:600; }
.drawer-body { flex:1 1 auto; overflow-y:auto; padding:1rem 1.1rem;
  display:flex; flex-direction:column; gap:.9rem; }
.drawer-foot { display:flex; align-items:center; gap:.5rem; padding:.8rem 1.1rem; border-top:1px solid var(--border); }
/* Result of the last save / detect, beside the buttons. */
.cfg-msg { flex:1 1 auto; font-size:.8rem; font-weight:600; color:var(--success); }
.cfg-msg.err { color:var(--danger); }

/* One card per settings group: a clear heading, then "name ...... control"
   lines. Heading is the strongest text in the card; field names are quieter. */
.cfg-group { background:var(--bg); border:1px solid var(--border); border-radius:10px; padding:.75rem .9rem .35rem; }
.cfg-group h4 { margin:0 0 .35rem; padding-bottom:.5rem; border-bottom:1px solid var(--border);
  font-size:.95rem; font-weight:600; color:var(--text); }
.cfg-row { display:flex; align-items:center; justify-content:space-between; gap:1rem;
  padding:.4rem 0; border-bottom:1px solid var(--border-soft); }
.cfg-row:last-child { border-bottom:none; }
.cfg-key { font-size:.82rem; color:var(--text-muted); }
/* Optional one-line hint under a field's name. */
.cfg-help { display:block; font-size:.72rem; line-height:1.3; margin-top:.1rem; max-width:15rem; }
.cfg-ctl { display:flex; align-items:center; gap:.3rem; }
.cfg-in { width:13rem; background:var(--surface); color:var(--text); border:1px solid var(--border);
  border-radius:6px; padding:.3rem .5rem; font-size:.85rem; }
.cfg-in.num { width:6rem; text-align:right; }
.cfg-in:focus { outline:none; border-color:var(--accent); }
@media (max-width:440px){ .cfg-in { width:10rem; } }
/* Reveal button sits inside the password box, so it lines up with the others. */
.cfg-ctl { position:relative; }
.cfg-in.secret { padding-right:2rem; }
.cfg-eye { position:absolute; right:.3rem; top:50%; transform:translateY(-50%);
  background:none; border:none; padding:.2rem .3rem; color:var(--text-muted); line-height:1; }
.cfg-eye:hover { color:var(--text); }

/* Bool = a button that IS the checkbox: filled when on, outlined when off. */
.bool-btn { min-width:6.5rem; border-radius:6px; padding:.3rem .7rem; font-size:.8rem; font-weight:600;
  background:transparent; border:1px solid var(--accent); color:var(--accent);
  transition:background-color .15s, color .15s; }
.bool-btn.on { background:var(--accent); color:var(--accent-on); }

.btn { border-radius:6px; padding:.4rem .9rem; font-size:.85rem; font-weight:600;
  border:1px solid var(--border); background:var(--surface); color:var(--text); white-space:nowrap; }
.btn:hover { border-color:var(--text-muted); }
.btn.primary { background:var(--accent-2); border-color:var(--accent-2); color:#fff; }
.btn:disabled { cursor:default; }
)css";

static const char HUB_SCRIPT_JS[] PROGMEM = R"js(
const $ = (id) => document.getElementById(id);

// GitHub Octicons v19.37.0 (MIT, github.com/primer/octicons) — 16px path data,
// embedded so the UI needs nothing from the internet. Multi-path icons are
// joined with '|'.
const ICONS = {
  sun: 'M8 12a4 4 0 1 1 0-8 4 4 0 0 1 0 8Zm0-1.5a2.5 2.5 0 1 0 0-5 2.5 2.5 0 0 0 0 5Zm5.657-8.157a.75.75 0 0 1 0 1.061l-1.061 1.06a.749.749 0 0 1-1.275-.326.749.749 0 0 1 .215-.734l1.06-1.06a.75.75 0 0 1 1.06 0Zm-9.193 9.193a.75.75 0 0 1 0 1.06l-1.06 1.061a.75.75 0 1 1-1.061-1.06l1.06-1.061a.75.75 0 0 1 1.061 0ZM8 0a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-1.5 0V.75A.75.75 0 0 1 8 0ZM3 8a.75.75 0 0 1-.75.75H.75a.75.75 0 0 1 0-1.5h1.5A.75.75 0 0 1 3 8Zm13 0a.75.75 0 0 1-.75.75h-1.5a.75.75 0 0 1 0-1.5h1.5A.75.75 0 0 1 16 8Zm-8 5a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-1.5 0v-1.5A.75.75 0 0 1 8 13Zm3.536-1.464a.75.75 0 0 1 1.06 0l1.061 1.06a.75.75 0 0 1-1.06 1.061l-1.061-1.06a.75.75 0 0 1 0-1.061ZM2.343 2.343a.75.75 0 0 1 1.061 0l1.06 1.061a.751.751 0 0 1-.018 1.042.751.751 0 0 1-1.042.018l-1.06-1.06a.75.75 0 0 1 0-1.06Z',
  moon: 'M9.598 1.591a.749.749 0 0 1 .785-.175 7.001 7.001 0 1 1-8.967 8.967.75.75 0 0 1 .961-.96 5.5 5.5 0 0 0 7.046-7.046.75.75 0 0 1 .175-.786Zm1.616 1.945a7 7 0 0 1-7.678 7.678 5.499 5.499 0 1 0 7.678-7.678Z',
  gear: 'M8 0a8.2 8.2 0 0 1 .701.031C9.444.095 9.99.645 10.16 1.29l.288 1.107c.018.066.079.158.212.224.231.114.454.243.668.386.123.082.233.09.299.071l1.103-.303c.644-.176 1.392.021 1.82.63.27.385.506.792.704 1.218.315.675.111 1.422-.364 1.891l-.814.806c-.049.048-.098.147-.088.294.016.257.016.515 0 .772-.01.147.038.246.088.294l.814.806c.475.469.679 1.216.364 1.891a7.977 7.977 0 0 1-.704 1.217c-.428.61-1.176.807-1.82.63l-1.102-.302c-.067-.019-.177-.011-.3.071a5.909 5.909 0 0 1-.668.386c-.133.066-.194.158-.211.224l-.29 1.106c-.168.646-.715 1.196-1.458 1.26a8.006 8.006 0 0 1-1.402 0c-.743-.064-1.289-.614-1.458-1.26l-.289-1.106c-.018-.066-.079-.158-.212-.224a5.738 5.738 0 0 1-.668-.386c-.123-.082-.233-.09-.299-.071l-1.103.303c-.644.176-1.392-.021-1.82-.63a8.12 8.12 0 0 1-.704-1.218c-.315-.675-.111-1.422.363-1.891l.815-.806c.05-.048.098-.147.088-.294a6.214 6.214 0 0 1 0-.772c.01-.147-.038-.246-.088-.294l-.815-.806C.635 6.045.431 5.298.746 4.623a7.92 7.92 0 0 1 .704-1.217c.428-.61 1.176-.807 1.82-.63l1.102.302c.067.019.177.011.3-.071.214-.143.437-.272.668-.386.133-.066.194-.158.211-.224l.29-1.106C6.009.645 6.556.095 7.299.03 7.53.01 7.764 0 8 0Zm-.571 1.525c-.036.003-.108.036-.137.146l-.289 1.105c-.147.561-.549.967-.998 1.189-.173.086-.34.183-.5.29-.417.278-.97.423-1.529.27l-1.103-.303c-.109-.03-.175.016-.195.045-.22.312-.412.644-.573.99-.014.031-.021.11.059.19l.815.806c.411.406.562.957.53 1.456a4.709 4.709 0 0 0 0 .582c.032.499-.119 1.05-.53 1.456l-.815.806c-.081.08-.073.159-.059.19.162.346.353.677.573.989.02.03.085.076.195.046l1.102-.303c.56-.153 1.113-.008 1.53.27.161.107.328.204.501.29.447.222.85.629.997 1.189l.289 1.105c.029.109.101.143.137.146a6.6 6.6 0 0 0 1.142 0c.036-.003.108-.036.137-.146l.289-1.105c.147-.561.549-.967.998-1.189.173-.086.34-.183.5-.29.417-.278.97-.423 1.529-.27l1.103.303c.109.029.175-.016.195-.045.22-.313.411-.644.573-.99.014-.031.021-.11-.059-.19l-.815-.806c-.411-.406-.562-.957-.53-1.456a4.709 4.709 0 0 0 0-.582c-.032-.499.119-1.05.53-1.456l.815-.806c.081-.08.073-.159.059-.19a6.464 6.464 0 0 0-.573-.989c-.02-.03-.085-.076-.195-.046l-1.102.303c-.56.153-1.113.008-1.53-.27a4.44 4.44 0 0 0-.501-.29c-.447-.222-.85-.629-.997-1.189l-.289-1.105c-.029-.11-.101-.143-.137-.146a6.6 6.6 0 0 0-1.142 0ZM11 8a3 3 0 1 1-6 0 3 3 0 0 1 6 0ZM9.5 8a1.5 1.5 0 1 0-3.001.001A1.5 1.5 0 0 0 9.5 8Z',
  plug: 'M4 8H2.5a1 1 0 0 0-1 1v5.25a.75.75 0 0 1-1.5 0V9a2.5 2.5 0 0 1 2.5-2.5H4V5.133a1.75 1.75 0 0 1 1.533-1.737l2.831-.353.76-.913c.332-.4.825-.63 1.344-.63h.782c.966 0 1.75.784 1.75 1.75V4h2.25a.75.75 0 0 1 0 1.5H13v4h2.25a.75.75 0 0 1 0 1.5H13v.75a1.75 1.75 0 0 1-1.75 1.75h-.782c-.519 0-1.012-.23-1.344-.63l-.761-.912-2.83-.354A1.75 1.75 0 0 1 4 9.867Zm6.276-4.91-.95 1.14a.753.753 0 0 1-.483.265l-3.124.39a.25.25 0 0 0-.219.248v4.734c0 .126.094.233.219.249l3.124.39a.752.752 0 0 1 .483.264l.95 1.14a.25.25 0 0 0 .192.09h.782a.25.25 0 0 0 .25-.25v-8.5a.25.25 0 0 0-.25-.25h-.782a.25.25 0 0 0-.192.09Z',
  plus: 'M7.75 2a.75.75 0 0 1 .75.75V7h4.25a.75.75 0 0 1 0 1.5H8.5v4.25a.75.75 0 0 1-1.5 0V8.5H2.75a.75.75 0 0 1 0-1.5H7V2.75A.75.75 0 0 1 7.75 2Z',
  copy: 'M0 6.75C0 5.784.784 5 1.75 5h1.5a.75.75 0 0 1 0 1.5h-1.5a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h7.5a.25.25 0 0 0 .25-.25v-1.5a.75.75 0 0 1 1.5 0v1.5A1.75 1.75 0 0 1 9.25 16h-7.5A1.75 1.75 0 0 1 0 14.25Z|M5 1.75C5 .784 5.784 0 6.75 0h7.5C15.216 0 16 .784 16 1.75v7.5A1.75 1.75 0 0 1 14.25 11h-7.5A1.75 1.75 0 0 1 5 9.25Zm1.75-.25a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h7.5a.25.25 0 0 0 .25-.25v-7.5a.25.25 0 0 0-.25-.25Z',
  trash: 'M11 1.75V3h2.25a.75.75 0 0 1 0 1.5H2.75a.75.75 0 0 1 0-1.5H5V1.75C5 .784 5.784 0 6.75 0h2.5C10.216 0 11 .784 11 1.75ZM4.496 6.675l.66 6.6a.25.25 0 0 0 .249.225h5.19a.25.25 0 0 0 .249-.225l.66-6.6a.75.75 0 0 1 1.492.149l-.66 6.6A1.748 1.748 0 0 1 10.595 15h-5.19a1.75 1.75 0 0 1-1.741-1.575l-.66-6.6a.75.75 0 1 1 1.492-.15ZM6.5 1.75V3h3V1.75a.25.25 0 0 0-.25-.25h-2.5a.25.25 0 0 0-.25.25Z',
  alert: 'M6.457 1.047c.659-1.234 2.427-1.234 3.086 0l6.082 11.378A1.75 1.75 0 0 1 14.082 15H1.918a1.75 1.75 0 0 1-1.543-2.575ZM8 5a.75.75 0 0 0-.75.75v2.5a.75.75 0 0 0 1.5 0v-2.5A.75.75 0 0 0 8 5Zm1 6a1 1 0 1 0-2 0 1 1 0 0 0 2 0Z',
  eye: 'M8 2c1.981 0 3.671.992 4.933 2.078 1.27 1.091 2.187 2.345 2.637 3.023a1.62 1.62 0 0 1 0 1.798c-.45.678-1.367 1.932-2.637 3.023C11.67 13.008 9.981 14 8 14c-1.981 0-3.671-.992-4.933-2.078C1.797 10.83.88 9.576.43 8.898a1.62 1.62 0 0 1 0-1.798c.45-.677 1.367-1.931 2.637-3.022C4.33 2.992 6.019 2 8 2ZM1.679 7.932a.12.12 0 0 0 0 .136c.411.622 1.241 1.75 2.366 2.717C5.176 11.758 6.527 12.5 8 12.5c1.473 0 2.825-.742 3.955-1.715 1.124-.967 1.954-2.096 2.366-2.717a.12.12 0 0 0 0-.136c-.412-.621-1.242-1.75-2.366-2.717C10.824 4.242 9.473 3.5 8 3.5c-1.473 0-2.825.742-3.955 1.715-1.124.967-1.954 2.096-2.366 2.717ZM8 10a2 2 0 1 1-.001-3.999A2 2 0 0 1 8 10Z',
  eyeClosed: 'M.143 2.31a.75.75 0 0 1 1.047-.167l14.5 10.5a.75.75 0 1 1-.88 1.214l-2.248-1.628C11.346 13.19 9.792 14 8 14c-1.981 0-3.67-.992-4.933-2.078C1.797 10.832.88 9.577.43 8.9a1.619 1.619 0 0 1 0-1.797c.353-.533.995-1.42 1.868-2.305L.31 3.357A.75.75 0 0 1 .143 2.31Zm1.536 5.622A.12.12 0 0 0 1.657 8c0 .021.006.045.022.068.412.621 1.242 1.75 2.366 2.717C5.175 11.758 6.527 12.5 8 12.5c1.195 0 2.31-.488 3.29-1.191L9.063 9.695A2 2 0 0 1 6.058 7.52L3.529 5.688a14.207 14.207 0 0 0-1.85 2.244ZM8 3.5c-.516 0-1.017.09-1.499.251a.75.75 0 1 1-.473-1.423A6.207 6.207 0 0 1 8 2c1.981 0 3.67.992 4.933 2.078 1.27 1.091 2.187 2.345 2.637 3.023a1.62 1.62 0 0 1 0 1.798c-.11.166-.248.365-.41.587a.75.75 0 1 1-1.21-.887c.148-.201.272-.382.371-.53a.119.119 0 0 0 0-.137c-.412-.621-1.242-1.75-2.366-2.717C10.825 4.242 9.473 3.5 8 3.5Z',
  x: 'M3.72 3.72a.75.75 0 0 1 1.06 0L8 6.94l3.22-3.22a.749.749 0 0 1 1.275.326.749.749 0 0 1-.215.734L9.06 8l3.22 3.22a.749.749 0 0 1-.326 1.275.749.749 0 0 1-.734-.215L8 9.06l-3.22 3.22a.751.751 0 0 1-1.042-.018.751.751 0 0 1-.018-1.042L6.94 8 3.72 4.78a.75.75 0 0 1 0-1.06Z',
  // Octicons has no USB symbol — this one is Phosphor Icons v2.1.1 (MIT,
  // github.com/phosphor-icons/core), "usb" regular weight, 256px grid.
  usb: 'M252.44,121.34l-48-32A8,8,0,0,0,192,96v24H72V72h33a32,32,0,1,0,0-16H72A16,16,0,0,0,56,72v48H8a8,8,0,0,0,0,16H56v48a16,16,0,0,0,16,16h32v8a16,16,0,0,0,16,16h32a16,16,0,0,0,16-16V176a16,16,0,0,0-16-16H120a16,16,0,0,0-16,16v8H72V136H192v24a8,8,0,0,0,12.44,6.66l48-32a8,8,0,0,0,0-13.32ZM136,48a16,16,0,1,1-16,16A16,16,0,0,1,136,48ZM120,176h32v32H120Zm88-30.95V111l25.58,17Z',
};
// Icons not drawn on Octicons' 16px grid.
const ICON_VB = { usb: '0 0 256 256' };
// An icon from ICONS as inline SVG markup, sized to the surrounding text.
const ic = (n) => '<svg class="oi" viewBox="' + (ICON_VB[n] || '0 0 16 16') + '" aria-hidden="true"><path d="' +
  ICONS[n].split('|').join('"/><path d="') + '"/></svg>';
// Swap the static <i data-icon="…"> placeholders in the page for real SVGs.
document.querySelectorAll('[data-icon]').forEach((el) => { el.outerHTML = ic(el.dataset.icon); });

let built = false;     // sockets drawn yet?
let lastPorts = [];    // ports from the latest status reply
// Ports whose power switch was clicked and whose reply hasn't come back. Their
// socket shows a neutral "waiting" state and status polls leave it alone, so
// the page never shows a guessed result — only what the hub reports.
const inflight = new Set();

// ---- Port detail: timeline + schedule editor ---------------------------
let selectedPort = null;
// The page's working copy of every port's schedule, loaded from the hub and
// sent back in full on every edit: schedules[n] = [entry, ...] for n = 1..6.
// Entries use the hub's fields (see settings/HubSettings.h, HubScheduleEntry).
const schedules = {};
const DOW = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];

// Click a socket to open or close its detail panel.
function selectPort(n) {
  selectedPort = (selectedPort === n) ? null : n;
  document.querySelectorAll('.socket').forEach((c) => c.classList.remove('selected'));
  if (selectedPort) { const c = $('port-' + selectedPort); if (c) c.classList.add('selected'); }
  renderDetail();
}

// Draw the selected port's detail panel (or hide it when none is selected).
function renderDetail() {
  const host = $('port-detail');
  if (!selectedPort) { host.style.display = 'none'; host.innerHTML = ''; return; }
  const n = selectedPort;
  if (!schedules[n]) schedules[n] = [];
  host.style.display = 'block';
  host.innerHTML =
    '<div class="detail-bar">' +
      '<div class="detail-left">' +
        '<span class="detail-title">Port ' + n + '</span>' +
        '<span class="next-chip" id="next-chip"></span>' +
      '</div>' +
      '<button class="add-sched" onclick="addSchedule(' + n + ')">' + ic('plus') + ' Add Schedule</button>' +
    '</div>' +
    '<div id="timeline"></div>' +
    '<div class="sched-list" id="sched-list"></div>';
  renderSchedules();
  renderNextChip();
}

// "Next: ON in 2h 15m (Mon 08:00)" beside the port name — straight from the
// device's own schedule evaluation.
function renderNextChip() {
  const el = $('next-chip');
  if (!el || !selectedPort) return;
  const p = lastPorts.find((x) => x.n === selectedPort);
  if (!p || !p.next) { el.innerHTML = '<span>No upcoming change</span>'; return; }
  el.innerHTML = 'Next: <b>' + (p.next.on ? 'ON' : 'OFF') + '</b> in ' + fmtCountdown(p.next.in) + ' &middot; ' + p.next.at;
}

// Weekly timeline for the selected port. Each entry is a switch EVENT, so the
// powered blocks are derived by walking the week's events in order — exactly the
// model the firmware uses. Blocks carry over midnight and across the week wrap.
function renderTimeline() {
  const host = $('timeline');
  if (!host || !selectedPort) return;
  const rows = (schedules[selectedPort] || []);

  // Expand entries into events: one per (entry, day-bit).
  const ev = [];
  rows.forEach((r, i) => {
    const t = hhmmToMin(r.time);
    if (t < 0 || !r.days || r.enabled === false) return;   // disabled rows don't draw
    // "ON EVERY" — a repeating pulse across a daily window.
    if (r.on && r.mode === 3) {
      const iv = Math.max(r.every | 0, MIN_EVERY);
      const pulse = r.dur > 0 ? r.dur : 1;
      if (pulse >= iv) return;
      let from = 0, span = 1440;
      if (r.allday === false) {
        const u = hhmmToMin(r.until);
        if (u < 0) return;
        from = t; span = (u - t + 1440) % 1440;
        if (!span) return;
      }
      for (let d = 0; d < 7; d++) {
        if (!((r.days >> d) & 1)) continue;
        for (let off = 0; off < span; off += iv) {
          const a = (d * 1440 + from + off) % 10080;
          const b = (a + pulse) % 10080;
          ev.push({ at: a, day: Math.floor(a / 1440), min: a % 1440, on: true,  bad: !!r.invalid, i: i });
          ev.push({ at: b, day: Math.floor(b / 1440), min: b % 1440, on: false, bad: !!r.invalid, i: i });
        }
      }
      return;
    }
    for (let d = 0; d < 7; d++) {
      if (!((r.days >> d) & 1)) continue;
      ev.push({ at: d * 1440 + t, day: d, min: t, on: !!r.on, bad: !!r.invalid, i: i });
      // "ON FOR" / "ON UNTIL" add the OFF that closes the window (week wraps).
      const win = effDur(r);
      if (r.on && win > 0) {
        const off = (d * 1440 + t + win) % 10080;
        ev.push({ at: off, day: Math.floor(off / 1440), min: off % 1440, on: false, bad: !!r.invalid, i: i });
      }
    }
  });
  if (!ev.length) { host.innerHTML = ''; return; }
  // Same instant: ON before OFF, so a clashing pair cancels to a zero-length
  // block instead of leaving the port latched on for the rest of the week.
  ev.sort((a, b) => (a.at - b.at) || (b.on - a.on));

  // Drawn from ALL entries, clashing ones included: a clash is a local problem
  // (flagged red on its own day) and must not repaint days it never touched.
  let state = ev[ev.length - 1].on;

  // Walk the week, emitting powered spans; split them at day boundaries so each
  // day's track can render its own pieces.
  const spans = [];
  let openAt = state ? 0 : -1;
  ev.forEach((e) => {
    if (e.on && openAt < 0) openAt = e.at;
    else if (!e.on && openAt >= 0) { spans.push([openAt, e.at]); openAt = -1; }
  });
  if (openAt >= 0) spans.push([openAt, 7 * 1440]);   // still on at week's end

  const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  let hdr = '<div class="tl-head">';
  for (let h = 0; h < 8; h++) hdr += '<div class="tl-hour">' + (h * 3) + ':00</div>';
  hdr += '</div>';

  // The "now" line is a single element spanning every day row, so it reads as
  // one cut through the whole week.
  let out = hdr + '<div class="tl-body"><div class="tl-now" style="display:none"></div>';
  for (let d = 0; d < 7; d++) {
    const dayStart = d * 1440, dayEnd = dayStart + 1440;
    let bars = '';
    spans.forEach(([s, e]) => {
      const a = Math.max(s, dayStart), b = Math.min(e, dayEnd);
      if (b <= a) return;
      // Only the piece containing the REAL span start gets a start marker —
      // otherwise every day drew a phantom tick at 00:00 where a block merely
      // continued across midnight.
      const isStart = (a === s);
      bars += '<div class="tl-on' + (isStart ? ' starts' : '') + '" style="left:' +
              ((a - dayStart) / 1440 * 100) + '%;width:' + ((b - a) / 1440 * 100) + '%"></div>';
    });
    ev.filter((x) => x.bad && x.day === d).forEach((x) => {
      bars += '<div class="tl-clash" title="Clashing entry — ignored" style="left:' +
              (x.min / 1440 * 100) + '%"></div>';
    });
    out += '<div class="tl-row" data-day="' + d + '"><span class="tl-day">' + DAYS[d] + '</span>' +
           '<div class="tl-track">' + bars + '</div></div>';
  }
  out += '</div>' +
         '<div class="tl-legend"><span><span class="tl-key on"></span>Powered</span>' +
         '<span><span class="tl-key bad"></span>Clash (ignored)</span></div>';
  host.innerHTML = '<div class="tl">' + out + '</div>';
  updateTimeCursor();
}

// Position the "now" cursor on the current day's track. Called on every poll,
// so it creeps across the row in real time without redrawing the timeline.
let lastClock = null;
function updateTimeCursor() {
  const cur = document.querySelector('.tl-now');
  if (!cur) return;
  const ok = lastClock && lastClock.synced;
  cur.style.display = ok ? '' : 'none';
  if (ok) {
    // Offset past the day-label column, then a fraction of the track width.
    const f = lastClock.secs / 86400;
    cur.style.left = 'calc(' + TL_LABEL + ' + (100% - ' + TL_LABEL + ') * ' + f + ')';
  }
  // Everything except today is knocked back so the current day stands out.
  document.querySelectorAll('.tl-row').forEach((row) => {
    row.classList.toggle('dim', ok && (+row.dataset.day) !== lastClock.wday);
  });
}
// Width of the day-label column + its gap — keep in step with .tl-day/.tl-row.
const TL_LABEL = '2.2rem';

// "HH:MM" -> minutes since midnight, or -1 if it isn't a valid time.
function hhmmToMin(s) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(s || '');
  if (!m) return -1;
  return (+m[1]) * 60 + (+m[2]);
}

// Mirror the firmware's clash rule so the UI flags problems immediately, before
// a save round-trip: same time + a shared day => ON wins, the OFF (or the later
// duplicate) is invalid.
function markClashes(rows) {
  const live = (r) => r.enabled !== false;
  rows.forEach((r) => { r.invalid = false; });

  // 1) Same time on a shared day.
  for (let i = 0; i < rows.length; i++) {
    if (!live(rows[i])) continue;
    for (let k = i + 1; k < rows.length; k++) {
      if (!live(rows[k])) continue;
      if (rows[i].time !== rows[k].time) continue;
      if ((rows[i].days & rows[k].days) === 0) continue;
      if (rows[i].invalid || rows[k].invalid) continue;
      const bad = (rows[i].on !== rows[k].on) ? (rows[i].on ? k : i) : k;
      rows[bad].invalid = true;
    }
  }

  // 1b) Per-entry sanity — mirrors validate() on the device.
  rows.forEach((r) => {
    if (!live(r) || !r.on) return;
    if (r.mode === 2 && hhmmToMin(r.until) === hhmmToMin(r.time)) r.invalid = true;
    if (r.mode === 3) {
      if (!(r.every >= MIN_EVERY)) r.invalid = true;
      else if (!(r.dur > 0) || r.dur >= r.every) r.invalid = true;
      else if (r.allday === false && hhmmToMin(r.until) === hhmmToMin(r.time)) r.invalid = true;
    }
  });

  // 2) An entry firing INSIDE an ON window ("ON FOR"/"ON UNTIL") contradicts it.
  for (let a = 0; a < rows.length; a++) {
    const A = rows[a];
    // Periodic entries are evaluated on the timeline, not as one window.
    if (!live(A) || A.invalid || !A.on || A.mode === 3) continue;
    const win = effDur(A);
    if (!win) continue;
    const ta = hhmmToMin(A.time);
    if (ta < 0) continue;
    for (let b = 0; b < rows.length; b++) {
      if (b === a) continue;
      const B = rows[b];
      if (!live(B) || B.invalid) continue;
      const tb = hhmmToMin(B.time);
      if (tb < 0) continue;
      let hit = false;
      for (let da = 0; da < 7 && !hit; da++) {
        if (!((A.days >> da) & 1)) continue;
        const start = da * 1440 + ta;
        for (let db = 0; db < 7 && !hit; db++) {
          if (!((B.days >> db) & 1)) continue;
          const rel = ((db * 1440 + tb) - start + 10080) % 10080;
          if (rel > 0 && rel < win) hit = true;   // strictly inside the window
        }
      }
      if (hit) B.invalid = true;
    }
  }
}

// Rebuild the selected port's schedule rows and its timeline.
function renderSchedules() {
  const list = $('sched-list');
  if (!list) return;
  const rows = schedules[selectedPort] || [];
  markClashes(rows);
  if (!rows.length) {
    list.innerHTML = '<div class="sched-empty">No schedules. Add one to switch this port on/off automatically.</div>';
    renderTimeline();
    return;
  }
  list.innerHTML = rows.map((r, i) => schedRow(selectedPort, i, r)).join('');
  renderTimeline();
  refreshNextRow();
}

// Action cycles OFF -> ON -> ON FOR <duration> -> ON UNTIL <time> -> OFF.
// `dur` and `until` are mutually exclusive; `until` stores the END TIME so
// editing the start doesn't drag the end with it.
const MIN_EVERY = 30;   // matches SCHED_MIN_EVERY on the device
// An entry's action as a short name: off / on / onfor / onuntil / onevery.
function schedAction(r) {
  if (!r.on) return 'off';
  if (r.mode === 1) return 'onfor';
  if (r.mode === 2) return 'onuntil';
  if (r.mode === 3) return 'onevery';
  return 'on';
}
const ACTION_LABEL = { off: 'OFF', on: 'ON', onfor: 'ON FOR', onuntil: 'ON UNTIL', onevery: 'ON EVERY' };

// Window length in minutes — mirrors hubEntryWindow() on the device.
function effDur(r) {
  if (!r.on) return 0;
  if (r.mode === 1) return r.dur > 0 ? r.dur : 0;
  if (r.mode === 2) {
    const u = hhmmToMin(r.until), s = hhmmToMin(r.time);
    if (u < 0 || s < 0) return 0;
    return (u - s + 1440) % 1440;
  }
  return 0;
}

// Markup for one schedule row: days, start time, action, its extra fields,
// weekly/once, enable switch, duplicate and delete.
function schedRow(n, i, r) {
  let days = '';
  for (let d = 0; d < 7; d++) {
    const active = (r.days >> d) & 1;
    days += '<button class="day-chip ' + (active ? 'on' : '') + '" onclick="toggleDay(' + n + ',' + i + ',' + d + ')">' + DOW[d] + '</button>';
  }
  const act = schedAction(r);
  const lbl = (t) => '<span class="sched-lbl">' + t + '</span>';
  const durField = (title) => ' <input type="text" class="sched-dur" value="' + minToHHMM(r.dur > 0 ? r.dur : 60) + '" maxlength="5"' +
    ' title="' + title + '"' +
    ' onwheel="durWheel(event,this,' + n + ',' + i + ')"' +
    ' onkeydown="durKey(event,this,' + n + ',' + i + ')"' +
    ' onchange="setSchedDur(' + n + ',' + i + ',this.value)">';
  const untilField = ' <input type="time" class="sched-time" value="' + (r.until || '17:00') + '"' +
    ' title="End time — scroll over the hours or minutes to change"' +
    ' onwheel="untilWheel(event,this,' + n + ',' + i + ')"' +
    ' onchange="setSchedUntil(' + n + ',' + i + ',this.value)">';
  const en = (r.enabled !== false);
  // "ON EVERY" all-day hides the start time — the repeat covers the whole day.
  const hideStart = (act === 'onevery' && r.allday !== false);
  return '<div class="sched-row' + (r.invalid ? ' bad' : '') + (en ? '' : ' off') + '">' +
    '<div class="day-chips">' + days + '</div>' +
    (hideStart ? '<span class="sched-slot"></span>' :
      '<input type="time" class="sched-time" value="' + r.time + '"' +
      ' title="Scroll over the hours or minutes to change"' +
      ' onwheel="timeWheel(event,this,' + n + ',' + i + ')"' +
      ' oninput="setSchedTimeLive(' + n + ',' + i + ',this.value)"' +
      ' onchange="setSchedTime(' + n + ',' + i + ',this.value)">') +
    '<button class="onoff ' + act + '" onclick="cycleSchedAction(' + n + ',' + i + ')">' + ACTION_LABEL[act] + '</button>' +
    (act === 'onfor' ? durField('Duration (HH:MM) — scroll over hours or minutes') : '') +
    (act === 'onuntil' ? untilField : '') +
    (act === 'onevery'
      ? ' <input type="text" class="sched-dur" value="' + minToHHMM(r.every >= MIN_EVERY ? r.every : MIN_EVERY) + '" maxlength="5"' +
        ' title="Repeat interval (HH:MM), minimum 00:30"' +
        ' onwheel="everyWheel(event,this,' + n + ',' + i + ')"' +
        ' onkeydown="everyKey(event,this,' + n + ',' + i + ')"' +
        ' onchange="setSchedEvery(' + n + ',' + i + ',this.value)">' +
        lbl('for') + durField('On-time per cycle (HH:MM)') +
        ' <button class="repeat-btn ' + (r.allday !== false ? '' : 'once') + '"' +
        ' onclick="toggleAllDay(' + n + ',' + i + ')"' +
        ' title="All day, or only between a start and stop time">' +
        (r.allday !== false ? 'ALL DAY' : 'WINDOW') + '</button>' +
        (r.allday !== false ? '' : lbl('to') + untilField)
      : '') +
    '<button class="repeat-btn ' + (r.once ? 'once' : 'weekly') + '" onclick="toggleSchedOnce(' + n + ',' + i + ')"' +
      ' title="Weekly repeats every week; Once fires next time then deletes itself">' +
      (r.once ? 'ONCE' : 'WEEKLY') + '</button>' +
    '<div class="row-toggle' + (en ? ' on' : '') + '" title="Enable / disable this row"' +
      ' onclick="toggleSchedEnabled(' + n + ',' + i + ')"><span class="toggle-knob"></span></div>' +
    '<button class="sched-dup" onclick="dupSchedule(' + n + ',' + i + ')" title="Duplicate">' + ic('copy') + '</button>' +
    '<button class="sched-del" onclick="delSchedule(' + n + ',' + i + ')" title="Delete">' + ic('trash') + '</button>' +
    (r.invalid ? '<div class="sched-warn">' + ic('alert') + ' Clashes with another entry at the same time — this one is ignored.</div>' : '') +
    '</div>';
}

const pad2 = (v) => String(v).padStart(2, '0');
const minToHHMM = (m) => pad2(Math.floor(m / 60)) + ':' + pad2(m % 60);
// A typed duration ("HH:MM" or "HHMM") -> minutes, clamped to 00:01..23:59.
function hhmmToDur(s) {
  const m = /^(\d{1,2}):?(\d{2})$/.exec((s || '').trim());
  if (!m) return 1;
  return clamp(Math.min(23, +m[1]) * 60 + Math.min(59, +m[2]), 1, 23 * 60 + 59);
}

// Which segment is the pointer over? The browser won't tell us, so we MEASURE
// the rendered text with the field's own font and work out the real pixel spans
// of HH, the colon and MM. Returns 'h', 'm', or null over the colon / trailing
// am-pm — so hovering the separator changes nothing.
function segAt(ev, el) {
  const cs = getComputedStyle(el);
  const ctx = segAt._c || (segAt._c = document.createElement('canvas').getContext('2d'));
  ctx.font = cs.fontStyle + ' ' + cs.fontWeight + ' ' + cs.fontSize + ' ' + cs.fontFamily;
  const r = el.getBoundingClientRect();
  const padL = parseFloat(cs.paddingLeft) + parseFloat(cs.borderLeftWidth);
  const padR = parseFloat(cs.paddingRight) + parseFloat(cs.borderRightWidth);
  const wHH = ctx.measureText('00').width;
  const wCol = ctx.measureText(':').width;
  // Centred fields (the duration box) start their text part-way in.
  let start = padL;
  if (cs.textAlign === 'center') {
    const inner = r.width - padL - padR;
    start = padL + Math.max(0, (inner - ctx.measureText('00:00').width) / 2);
  }
  const x = ev.clientX - r.left;
  if (x >= start && x < start + wHH) return 'h';
  if (x >= start + wHH + wCol && x < start + wHH + wCol + wHH) return 'm';
  return null;   // the colon, or past the minutes (am/pm) — dead zone
}

// Wheel step: scrolling UP increases, matching the arrow keys.
const wheelStep = (ev) => (ev.deltaY > 0 ? 1 : -1);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

// Scroll wheel over a start time: step the hour or minute under the pointer.
function timeWheel(ev, el, n, i) {
  const seg = segAt(ev, el);
  if (!seg) return;                 // over the colon — leave it alone
  ev.preventDefault();
  const p = (el.value || '00:00').split(':');
  let h = +p[0] || 0, m = +p[1] || 0;
  const step = wheelStep(ev);
  // Time of day legitimately wraps (23:59 -> 00:00).
  if (seg === 'h') h = (h + step + 24) % 24; else m = (m + step + 60) % 60;
  el.value = pad2(h) + ':' + pad2(m);
  setSchedTimeLive(n, i, el.value);
  clearTimeout(el._t); el._t = setTimeout(() => saveSchedules(), 400);
}

// Duration is a length, not a clock — CLAMP to 00:01..23:59, never wrap.
function bumpDur(el, hours, step, n, i) {
  let v = hhmmToDur(el.value);
  v = clamp(v + (hours ? step * 60 : step), 1, 23 * 60 + 59);
  el.value = minToHHMM(v);
  applyDur(n, i, el.value);
  clearTimeout(el._t); el._t = setTimeout(() => saveSchedules(), 400);
}

// Scroll wheel over a duration: step the hours or minutes under the pointer.
function durWheel(ev, el, n, i) {
  const seg = segAt(ev, el);
  if (!seg) return;                 // over the colon — leave it alone
  ev.preventDefault();
  bumpDur(el, seg === 'h', wheelStep(ev), n, i);
}

// Up/down arrows on the duration field, matching the time field's behaviour.
function durKey(ev, el, n, i) {
  if (ev.key !== 'ArrowUp' && ev.key !== 'ArrowDown') return;
  ev.preventDefault();
  bumpDur(el, (el.selectionStart || 0) <= 2, ev.key === 'ArrowUp' ? 1 : -1, n, i);
}

// Update the duration without rebuilding the row (keeps focus in the field).
function applyDur(n, i, v) {
  schedules[n][i].dur = hhmmToDur(v) || 1;
  markClashes(schedules[n]);
  renderTimeline();
  refreshRowFlags(n);
}
// Duration typed and committed: store, redraw, save.
function setSchedDur(n, i, v) { schedules[n][i].dur = hhmmToDur(v) || 1; renderSchedules(); saveSchedules(); }

// Action button: cycle OFF -> ON -> ON FOR -> ON UNTIL -> ON EVERY -> OFF.
function cycleSchedAction(n, i) {
  const r = schedules[n][i];
  // Only `on` and `mode` change — dur and until are BOTH kept, so cycling
  // through the modes never loses what you'd already typed.
  const next = { off: 'on', on: 'onfor', onfor: 'onuntil', onuntil: 'onevery', onevery: 'off' }[schedAction(r)];
  if (next === 'off')          { r.on = false; r.mode = 0; }
  else if (next === 'on')      { r.on = true;  r.mode = 0; }
  else if (next === 'onfor')   { r.on = true;  r.mode = 1; if (!(r.dur > 0)) r.dur = 60; }
  else if (next === 'onuntil') { r.on = true;  r.mode = 2; if (!r.until) r.until = '17:00'; }
  else /* onevery */           { r.on = true;  r.mode = 3;
                                 if (!(r.every >= MIN_EVERY)) r.every = MIN_EVERY;
                                 if (!(r.dur > 0) || r.dur >= r.every) r.dur = 5; }
  renderSchedules();
  saveSchedules();
}

// "ON EVERY" repeat interval — a length, clamped to 00:30..23:59.
function bumpEvery(el, hours, step, n, i) {
  let v = hhmmToDur(el.value);
  v = clamp(v + (hours ? step * 60 : step), MIN_EVERY, 23 * 60 + 59);
  el.value = minToHHMM(v);
  schedules[n][i].every = v;
  markClashes(schedules[n]);
  renderTimeline();
  refreshRowFlags(n);
  clearTimeout(el._t); el._t = setTimeout(() => saveSchedules(), 400);
}
// Scroll wheel / arrow keys over an ON EVERY interval.
function everyWheel(ev, el, n, i) {
  const seg = segAt(ev, el);
  if (!seg) return;
  ev.preventDefault();
  bumpEvery(el, seg === 'h', wheelStep(ev), n, i);
}
function everyKey(ev, el, n, i) {
  if (ev.key !== 'ArrowUp' && ev.key !== 'ArrowDown') return;
  ev.preventDefault();
  bumpEvery(el, (el.selectionStart || 0) <= 2, ev.key === 'ArrowUp' ? 1 : -1, n, i);
}
// Interval typed and committed: clamp to 00:30..23:59, store, redraw, save.
function setSchedEvery(n, i, v) {
  schedules[n][i].every = clamp(hhmmToDur(v), MIN_EVERY, 23 * 60 + 59);
  renderSchedules();
  saveSchedules();
}
// ON EVERY: switch between all day and a start..stop window.
function toggleAllDay(n, i) {
  const r = schedules[n][i];
  r.allday = (r.allday === false);
  renderSchedules();
  saveSchedules();
}

// End time for "ON UNTIL". A time of day, so it wraps like the start field.
function setSchedUntil(n, i, v) { schedules[n][i].until = v; renderSchedules(); saveSchedules(); }
// Scroll wheel over an end time.
function untilWheel(ev, el, n, i) {
  const seg = segAt(ev, el);
  if (!seg) return;
  ev.preventDefault();
  const p = (el.value || '00:00').split(':');
  let h = +p[0] || 0, m = +p[1] || 0;
  const step = wheelStep(ev);
  if (seg === 'h') h = (h + step + 24) % 24; else m = (m + step + 60) % 60;
  el.value = pad2(h) + ':' + pad2(m);
  schedules[n][i].until = el.value;
  markClashes(schedules[n]);
  renderTimeline();
  refreshRowFlags(n);
  clearTimeout(el._t); el._t = setTimeout(() => saveSchedules(), 400);
}

// Row buttons: weekly / once, enable, add, delete, and a day chip.
function toggleSchedOnce(n, i) { schedules[n][i].once = !schedules[n][i].once; renderSchedules(); saveSchedules(); }
function toggleSchedEnabled(n, i) {
  const r = schedules[n][i];
  r.enabled = (r.enabled === false);
  renderSchedules();
  saveSchedules();
}

function addSchedule(n) { (schedules[n] = schedules[n] || []).push({ days: 127, time: '08:00', on: true, mode: 0, dur: 60, until: '17:00', every: MIN_EVERY, allday: true, once: false, enabled: true }); renderSchedules(); saveSchedules(); }
function delSchedule(n, i) { schedules[n].splice(i, 1); renderSchedules(); saveSchedules(); }
// Copy a row and insert it directly beneath the one it came from.
function dupSchedule(n, i) {
  const r = schedules[n][i];
  schedules[n].splice(i + 1, 0, { days: r.days, time: r.time, on: r.on, mode: r.mode | 0, dur: r.dur > 0 ? r.dur : 60, until: r.until || '17:00', every: r.every >= MIN_EVERY ? r.every : MIN_EVERY, allday: r.allday !== false, once: !!r.once, enabled: r.enabled !== false });
  renderSchedules();
  saveSchedules();
}
function toggleDay(n, i, d) { schedules[n][i].days ^= (1 << d); renderSchedules(); saveSchedules(); }
// While the time field is being edited: update the timeline + clash flags live,
// but DON'T rebuild the row list (that would steal focus from the input).
function setSchedTimeLive(n, i, v) {
  schedules[n][i].time = v;
  markClashes(schedules[n]);
  renderTimeline();
  refreshRowFlags(n);
}
// On commit (blur/enter): full re-render + persist.
function setSchedTime(n, i, v) { schedules[n][i].time = v; renderSchedules(); saveSchedules(); }

// Mark the row whose event fires next. Runs on every poll, so the highlight
// moves along on its own as each entry comes due.
function refreshNextRow() {
  if (!selectedPort) return;
  const p = lastPorts.find((x) => x.n === selectedPort);
  const idx = (p && p.next && p.next.i >= 0) ? p.next.i : -1;
  document.querySelectorAll('#sched-list .sched-row').forEach((el, i) => {
    el.classList.toggle('next', i === idx);
  });
}

// Update just the clash styling/warning on each row, leaving inputs untouched.
function refreshRowFlags(n) {
  const rows = schedules[n] || [];
  document.querySelectorAll('#sched-list .sched-row').forEach((el, i) => {
    const bad = rows[i] && rows[i].invalid;
    el.classList.toggle('bad', !!bad);
    let w = el.querySelector('.sched-warn');
    if (bad && !w) {
      w = document.createElement('div');
      w.className = 'sched-warn';
      w.innerHTML = ic('alert') + ' Clashes with another entry at the same time — this one is ignored.';
      el.appendChild(w);
    } else if (!bad && w) w.remove();
  });
}

// Persist all schedules to the device (LittleFS), sent as {"ports":[[...],...]}
// with 6 arrays (port 1..6). Fired on every edit.
function saveSchedules() {
  const ports = [];
  for (let n = 1; n <= 6; n++) ports.push((schedules[n] || []).map((r) => ({ days: r.days, time: r.time, on: r.on, mode: r.mode | 0, dur: r.dur > 0 ? r.dur : 60, until: r.until || '17:00', every: r.every >= MIN_EVERY ? r.every : MIN_EVERY, allday: r.allday !== false, once: !!r.once, enabled: r.enabled !== false })));
  fetch('/api/schedules', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ports }) })
    .catch(() => { setOffline(true); });
}

// Load persisted schedules from the device at startup.
async function loadSchedules() {
  try {
    const r = await fetch('/api/schedules');
    const d = await r.json();
    if (d && Array.isArray(d.ports)) {
      for (let i = 0; i < d.ports.length; i++) schedules[i + 1] = d.ports[i] || [];
    }
    if (selectedPort) renderSchedules();
  } catch (e) {}
}

// Toggle a port's power via its sliding switch. Neutral waiting state (the
// switch holds position until the real device status returns — no guess).
function togglePower(n) {
  if (inflight.has(n)) return;
  const p = lastPorts.find((x) => x.n === n);
  if (!p) return;
  const on = !p.enabled;
  inflight.add(n);
  const c = $('port-' + n);
  if (c) {
    c.className = 'socket st-wait' + (n === selectedPort ? ' selected' : '');
    c.querySelector('.slot').style.background = '';
  }
  fetch('/api/port?n=' + n + '&on=' + (on ? 1 : 0), { method: 'POST' })
    .then((r) => r.json())
    .then((d) => { inflight.delete(n); applyStatus(d); })
    .catch(() => { inflight.delete(n); setOffline(true); });
}

// ---- Settings drawer ----------------------------------------------------
// The form is built from /api/settings/schema: one section per group, one
// "name ...... control" line per field. A single Save sends every group.
function openSettings() {
  cfgMsg('');
  $('drawer').classList.add('open');
  $('drawer').setAttribute('aria-hidden', 'false');
  loadSettings();
}
// Slide the drawer away (also on Esc or a click outside it).
function closeSettings() {
  $('drawer').classList.remove('open');
  $('drawer').setAttribute('aria-hidden', 'true');
}
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && $('drawer').classList.contains('open')) closeSettings();
});

// Fetch the form structure + current values and draw the drawer's contents.
async function loadSettings() {
  try {
    const r = await fetch('/api/settings/schema');
    const d = await r.json();
    renderSettings(d.groups || []);
  } catch (e) {
    $('settings-panels').innerHTML = '<p class="sched-empty">Could not load settings.</p>';
  }
}

// Status line beside the drawer's buttons. Empty string clears it.
let cfgTimer = null;
function cfgMsg(text, bad) {
  const el = $('cfg-msg');
  el.textContent = text;
  el.className = 'cfg-msg' + (bad ? ' err' : '');
  clearTimeout(cfgTimer);
  if (text) cfgTimer = setTimeout(() => { el.textContent = ''; }, 3000);
}

// Escape text for use inside HTML or an attribute value.
const esc = (s) => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
// Fields that must never be shown as plain text until asked.
const isSecret = (k) => /pass|secret|api_key|token/i.test(k);

// One section per settings group.
function renderSettings(groups) {
  // Groups with nothing editable would be empty headings — leave them out.
  const shown = groups.filter((g) => (g.fields || []).length);
  $('settings-panels').innerHTML = shown.length
    ? shown.map((g) =>
        '<section class="cfg-group" data-group="' + esc(g.name) + '">' +
          '<h4>' + esc(g.label) + '</h4>' +
          (g.fields || []).filter((f) => f.type !== 'hidden').map((f) => cfgRow(g, f)).join('') +
        '</section>').join('')
    : '<p class="sched-empty">No settings.</p>';
}

// One "name ...... control" line for a field, with its optional hint.
function cfgRow(g, f) {
  const v = (g.values || {})[f.key];
  const id = 'cfg-' + g.name + '-' + f.key;
  let key = f.label, ctl;

  if (f.type === 'bool') {
    // The button is the checkbox. A field labelled "X enabled" reads as
    // "X ...... [Enabled]"; anything else as "Label ...... [On]".
    const m = /^(.*?)\s*enabled$/i.exec(f.label || '');
    if (m) key = m[1].trim() || g.label;
    const lOn = m ? 'Enabled' : 'On', lOff = m ? 'Disabled' : 'Off';
    const on = !!v;
    ctl = '<button type="button" class="bool-btn' + (on ? ' on' : '') + '" id="' + id + '"' +
      ' data-key="' + esc(f.key) + '" data-bool="' + (on ? 1 : 0) + '"' +
      ' data-on="' + lOn + '" data-off="' + lOff + '" onclick="flipBool(this)">' +
      (on ? lOn : lOff) + '</button>';
  } else if (f.type === 'int') {
    ctl = '<input class="cfg-in num" type="number" id="' + id + '" data-key="' + esc(f.key) + '"' +
      ' value="' + (v != null ? v : 0) + '" min="' + f.min + '" max="' + f.max + '" step="' + f.step + '">';
  } else {
    const secret = isSecret(f.key);
    ctl = '<input class="cfg-in' + (secret ? ' secret' : '') + '" type="' + (secret ? 'password' : 'text') + '" id="' + id + '"' +
      ' data-key="' + esc(f.key) + '" value="' + esc(v) + '" autocomplete="off">' +
      (secret ? '<button type="button" class="cfg-eye" title="Show / hide" onclick="reveal(\'' + id + '\', this)">' +
                ic('eye') + '</button>' : '');
  }
  return '<div class="cfg-row"><span class="cfg-key">' + esc(key) +
         (f.help ? '<span class="cfg-help">' + esc(f.help) + '</span>' : '') + '</span>' +
         '<span class="cfg-ctl">' + ctl + '</span></div>';
}

// A bool button was clicked: flip its value and its label.
function flipBool(btn) {
  const on = btn.dataset.bool !== '1';
  btn.dataset.bool = on ? '1' : '0';
  btn.classList.toggle('on', on);
  btn.textContent = on ? btn.dataset.on : btn.dataset.off;
}

// Show or hide a password field's text.
function reveal(id, btn) {
  const el = $(id);
  const show = el.type === 'password';
  el.type = show ? 'text' : 'password';
  btn.innerHTML = ic(show ? 'eyeClosed' : 'eye');
}

// Gather every group's fields and send them in one request.
async function saveAllSettings() {
  const body = {};
  document.querySelectorAll('.cfg-group').forEach((sec) => {
    const out = {};
    sec.querySelectorAll('[data-key]').forEach((el) => {
      const k = el.dataset.key;
      if (el.dataset.bool !== undefined) out[k] = el.dataset.bool === '1';
      else if (el.type === 'number') out[k] = parseFloat(el.value) || 0;
      else out[k] = el.value;
    });
    body[sec.dataset.group] = out;
  });
  const btn = $('cfg-save');
  btn.disabled = true;
  btn.textContent = 'Saving…';
  try {
    const r = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!r.ok) throw new Error('http ' + r.status);
    await loadSettings();   // show what the hub actually stored
    cfgMsg('Saved');
  } catch (e) {
    cfgMsg('Save failed', true);
  }
  btn.disabled = false;
  btn.textContent = 'Save';
}

// Ask the hub to fill city / country / UTC offset from its public IP, then
// re-read the form once the lookup has had time to finish.
async function detectLocation() {
  try {
    await fetch('/api/location/detect', { method: 'POST' });
    cfgMsg('Detecting location…');
    setTimeout(async () => { await loadSettings(); cfgMsg('Location updated'); }, 2500);
  } catch (e) { cfgMsg('Detect failed', true); }
}

// The hub's stored theme is applied ONCE, from the first status reply. After
// that the page owns it: a click changes it immediately and tells the hub,
// and later status polls never touch it.
let themeSet = false;
function applyTheme(t) {
  document.body.setAttribute('data-theme', t);
  $('theme-toggle').innerHTML = ic(t === 'dark' ? 'sun' : 'moon');
}
function toggleTheme() {
  themeSet = true;
  const next = (document.body.getAttribute('data-theme') === 'dark') ? 'light' : 'dark';
  applyTheme(next);
  fetch('/api/theme?value=' + next, { method: 'POST' }).catch(() => {});
}

// Seconds -> "3d 4h" / "2h 15m" / "5m" / "12s".
function fmtUptime(s) {
  s = Math.floor(s);
  const d = Math.floor(s/86400); s %= 86400;
  const h = Math.floor(s/3600);  s %= 3600;
  const m = Math.floor(s/60);
  if (d) return d + 'd ' + h + 'h';
  if (h) return h + 'h ' + m + 'm';
  if (m) return m + 'm';
  return Math.floor(s) + 's';
}

// Build the six sockets once. Port 6 is drawn first so the row reads 6..1 left
// to right, matching the hardware.
function buildPorts(n) {
  const host = $('ports');
  host.innerHTML = '';
  for (let i = n; i >= 1; i--) {
    const c = document.createElement('div');
    c.className = 'socket st-off';
    c.id = 'port-' + i;
    c.title = 'Port ' + i + ' — click for schedule';
    c.onclick = () => selectPort(i);
    c.innerHTML =
      '<div class="sock-head"><span class="sock-num">' + i + '</span><span class="sock-state">--</span></div>' +
      '<div class="slot"></div>' +
      '<div class="port-toggle" onclick="event.stopPropagation(); togglePower(' + i + ')"><span class="toggle-knob"></span></div>' +
      '<div class="sock-read">' +
        '<div class="sock-amps"><span class="mma">--</span> <small>mA</small></div>' +
        '<div class="sock-sub"><span class="mv">--</span> &middot; <span class="mmw">--</span></div>' +
      '</div>' +
      '<div class="sock-sched">--</div>';
    host.appendChild(c);
  }
  built = true;
}

// What each LED state means, in words, under the port number.
const STATE_LABEL = { off:'Off', blue:'No power', green:'Active', pulse:'No device', fault:'Fault', pink:'No 5V', pinkpulse:'No 5V' };
// States whose slot colour is animated in JS rather than set by CSS.
const ANIMATED = ['pulse', 'fault', 'pinkpulse'];

// Refresh one socket from its entry in the status reply.
function updatePort(p) {
  const c = $('port-' + p.n);
  if (!c) return;
  if (inflight.has(p.n)) return;   // waiting on this port's own switch result — hold
  c.className = 'socket st-' + p.state + (p.n === selectedPort ? ' selected' : '');
  if (!ANIMATED.includes(p.state)) c.querySelector('.slot').style.background = '';
  c.querySelector('.sock-state').textContent = STATE_LABEL[p.state] || p.state;
  c.querySelector('.port-toggle').className = 'port-toggle' + (p.enabled ? ' on' : '');

  const mon = p.mon !== false;      // false = this port's power monitor isn't fitted
  c.querySelector('.mma').textContent = mon ? p.ma.toFixed(0) : '--';
  c.querySelector('.mv').textContent  = mon ? p.v.toFixed(2) + ' V' : '-- V';
  c.querySelector('.mmw').textContent = mon ? p.mw.toFixed(0) + ' mW' : '-- mW';

  // Next scheduled change, readable without opening the port.
  const s = c.querySelector('.sock-sched');
  if (!p.sched)      s.innerHTML = 'No schedule';
  else if (!p.next)  s.innerHTML = p.sched + ' set &middot; waiting for time';
  else               s.innerHTML = '<b>' + (p.next.on ? 'ON' : 'OFF') + '</b> in ' + fmtCountdown(p.next.in) +
                                   '<span class="at">' + p.next.at + '</span>';
}

// Shared countdown formatting — seconds under a minute, then m / h / d.
function fmtCountdown(s) {
  const m = Math.floor(s / 60);
  if (s < 60)    return s + 's';
  if (m >= 1440) return Math.floor(m / 1440) + 'd ' + Math.floor((m % 1440) / 60) + 'h';
  if (m >= 60)   return Math.floor(m / 60) + 'h ' + (m % 60) + 'm';
  return m + 'm';
}

// Render everything from an authoritative status object — used by both the
// periodic poll and the /api/port POST response. The ONLY place cards are drawn,
// always from real device state.
function applyStatus(d) {
  if (!d) return;
  if (!themeSet) { themeSet = true; applyTheme(d.theme || 'dark'); }
  if (d.name && $('dev-name').textContent !== d.name) {   // the "Device name" setting
    $('dev-name').textContent = d.name;
    document.title = d.name;
  }
  if (d.fw) $('fw-version').textContent = d.fw;
  if (d.rst) $('rst-reason').textContent = d.rst;
  $('s-ip').textContent     = d.ip || '--';
  $('s-uptime').textContent = fmtUptime(d.uptime_s || 0);
  $('s-temp').textContent   = d.temp && d.temp.valid ? d.temp.c.toFixed(1) + '°C' : '--';
  $('s-humid').textContent  = d.temp && d.temp.valid ? d.temp.h.toFixed(0) + '%' : '--';
  $('s-heap').textContent   = d.heap ? Math.round(d.heap/1024) + ' KB' : '--';
  $('s-maxblk').textContent = d.maxblk ? Math.round(d.maxblk/1024) + ' KB' : '--';
  if (d.total) {
    const a = d.total.ma, w = d.total.mw;
    $('s-total').textContent = (a >= 1000 ? (a/1000).toFixed(2) + ' A' : Math.round(a) + ' mA') +
                               ' / ' + (w >= 1000 ? (w/1000).toFixed(2) + ' W' : Math.round(w) + ' mW');
  }
  if (d.clock) {
    lastClock = d.clock;
    $('s-time').textContent = d.clock.synced ? d.clock.time : '--:--';
    $('s-date').textContent = d.clock.synced ? d.clock.date : 'Not synced';
    updateTimeCursor();
  }
  if (!built) buildPorts(d.ports.length);
  lastPorts = d.ports;
  d.ports.forEach(updatePort);
  renderNextChip();
  refreshNextRow();
}

// Connection state. A few missed polls in a row means the hub is gone — say so
// loudly and block interaction rather than leaving a live-looking UI showing
// stale data that you can still click on.
let pollFails = 0;
function setOffline(on) {
  document.body.classList.toggle('offline', on);
}

// Fetch /api/status once a second. Three failures in a row = offline.
async function poll() {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), 4000);   // don't let a hung socket stall us
  try {
    const r = await fetch('/api/status', { cache: 'no-store', signal: ac.signal });
    if (!r.ok) throw new Error('http ' + r.status);
    const d = await r.json();
    pollFails = 0;
    setOffline(false);
    applyStatus(d);
  } catch (e) {
    if (++pollFails >= 3) setOffline(true);       // ~3s of silence
  } finally {
    clearTimeout(t);
  }
}

// Drive the animated pulse/fault borders from ONE clock so every card is in
// phase (the device pulses all ports together off millis(); per-element CSS
// animations drift). Matches renderLeds(): pulse = 2s sine, 10-100%; fault =
// 250ms on/off flash.
// Pulse and flash the slots from ONE clock so every port stays in phase, the
// way the device drives all its LEDs off one millis(). Same shapes as
// renderLeds(): pulse = 2 s sine from 10% to 100%; fault = 250 ms on / off.
function animate(t) {
  const ph = (t % 2000) / 2000;
  const f = (Math.sin(ph * 2 * Math.PI - Math.PI / 2) + 1) / 2;   // 0..1
  const a = (0.1 + 0.9 * f).toFixed(3);
  const lit = (rgb, alpha) => 'linear-gradient(rgba(' + rgb + ',' + alpha + '),rgba(' + rgb + ',' + alpha + ')),var(--slot-off)';
  const fill = {
    pulse:     lit('0,34,255', a),
    pinkpulse: lit('255,90,200', a),
    fault:     (Math.floor(t / 250) % 2 === 0) ? lit('255,0,0', 1) : 'var(--slot-off)',
  };
  ANIMATED.forEach((st) => {
    document.querySelectorAll('.st-' + st + ' .slot').forEach((s) => { s.style.background = fill[st]; });
  });
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);

loadSchedules();
poll();
setInterval(poll, 1000);
)js";
