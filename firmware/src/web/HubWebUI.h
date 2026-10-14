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
// from the internet. All data in/out is JSON against /api/*, except the port
// history, which comes in binary pages from /api/history.
#pragma once
#include <Arduino.h>

static const char HUB_INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart USB Hub</title>
<!-- Favicon: the USB symbol used in the UI, in its blue on the dark surface.
     SVG for most browsers; PNGs (served from HubWebIcons.h) for Safari. -->
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 256 256'%3E%3Crect width='256' height='256' rx='56' fill='#151b23'/%3E%3Cg transform='translate(28 28) scale(.78)'%3E%3Cpath fill='#4493f8' d='M252.44,121.34l-48-32A8,8,0,0,0,192,96v24H72V72h33a32,32,0,1,0,0-16H72A16,16,0,0,0,56,72v48H8a8,8,0,0,0,0,16H56v48a16,16,0,0,0,16,16h32v8a16,16,0,0,0,16,16h32a16,16,0,0,0,16-16V176a16,16,0,0,0-16-16H120a16,16,0,0,0-16,16v8H72V136H192v24a8,8,0,0,0,12.44,6.66l48-32a8,8,0,0,0,0-13.32ZM136,48a16,16,0,1,1-16,16A16,16,0,0,1,136,48ZM120,176h32v32H120Zm88-30.95V111l25.58,17Z'/%3E%3C/g%3E%3C/svg%3E">
<link rel="icon" type="image/png" sizes="32x32" href="/favicon-32.png">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="stylesheet" href="/hub.css">
</head>
<body data-theme="dark">

<header class="topbar">
  <div class="ident">
    <span class="ident-name" id="dev-name">Smart USB Hub</span>
    <span class="ident-maker">by Unexpected Maker</span>
  </div>
  <div class="tools">
    <button id="view-history" class="tool" onclick="setViewMode('history')" title="History: clicking a port shows its graph"><i data-icon="graph"></i></button>
    <button id="view-schedule" class="tool" onclick="setViewMode('schedule')" title="Schedule: clicking a port shows its schedule"><i data-icon="clock"></i></button>
    <button id="theme-toggle" class="tool" onclick="toggleTheme()" title="Light / dark"><i data-icon="sun"></i></button>
    <button class="tool" onclick="openSettings()" title="Settings"><i data-icon="gear"></i></button>
  </div>
</header>

<div class="linkbar"><i data-icon="plug"></i> Hub not responding &mdash; reconnecting automatically&hellip;</div>

<main class="layout">
  <section>
    <div class="bezel"><div id="ports" class="sockets"></div></div>
    <div id="port-graph" class="detail graph-panel" style="display:none;"></div>
    <div id="port-detail" class="detail" style="display:none;"></div>
  </section>

  <aside class="readout">
    <div class="ro-group">
      <h3>Power</h3>
      <div class="ro-line"><span>Total draw</span><span id="s-total">--</span></div>
    </div>
    <div class="ro-group">
      <h3>Environment</h3>
      <div class="env-graph"><canvas id="env-canvas"></canvas></div>
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
.tool.on { background:var(--accent); border-color:var(--accent); color:var(--accent-on); }

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
.socket.graphing { box-shadow:inset 0 0 0 1px var(--accent); }
.sock-head { width:100%; display:flex; align-items:center; justify-content:space-between; gap:.3rem; }
.sock-num { font-size:1.5rem; font-weight:700; line-height:1; font-variant-numeric:tabular-nums; }
.sock-name { min-width:0; width:6.5rem; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;
  border:0; background:transparent; padding:.05rem .1rem; color:var(--text-muted);
  font-size:.74rem; font-weight:700; text-align:right; line-height:1.2; border-radius:4px; }
.sock-name::placeholder { color:var(--text-muted); opacity:1; }
.sock-name:hover, .sock-name:focus { color:var(--text); background:var(--surface-2); outline:none; }
.sock-name.named { color:var(--text); }
.sock-state { font-size:.68rem; font-weight:600; color:var(--text-muted); text-align:center; line-height:1.2; min-height:.85rem; }

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
.sock-read:hover { color:var(--accent); }
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
.env-graph { width:100%; height:6.4rem; margin-top:.35rem; }
.env-graph canvas { width:100%; height:100%; display:block; }

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

/* ---- Port trend graph: compact, self-contained canvas with segmented controls. ---- */
.graph-panel { padding:.8rem; }
.graph-panel.off { opacity:.68; }
.graph-head { display:flex; align-items:center; justify-content:space-between; gap:1rem; margin-bottom:.65rem; }
.graph-title { font-size:1rem; font-weight:700; }
/* HISTORICAL: .graph-note is no longer used (the status text beside the graph
   title was removed). Kept for now; to be removed. */
.graph-note { font-size:.7rem; font-weight:600; color:var(--text-dim); }
.graph-body { background:var(--bg); border:1px solid var(--border); border-radius:var(--radius); padding:.65rem; }
.graph-tools { display:flex; gap:.35rem; flex-wrap:wrap; align-items:center; margin-bottom:.55rem; }
/* One line until the items really don't fit: then the legend and Head move
   down together. The measurements box only takes space while it's showing
   (zoomed in). Nothing shrinks or wraps its text. */
.graph-tools > * { flex-shrink:0; }
.graph-seg button { white-space:nowrap; }
.graph-seg { display:flex; border:1px solid var(--border); border-radius:6px; overflow:hidden; background:var(--surface); }
.graph-seg button { border:0; border-right:1px solid var(--border); background:transparent; color:var(--text-muted); padding:.24rem .36rem; font-size:.68rem; font-weight:700; min-width:2.2rem; }
.graph-seg button:last-child { border-right:0; }
/* Measurements for what's in view, centred between the averaging buttons and
   the legend: a bordered box, a shade lighter than the panel, with a row per
   metric in its colour, columns Min / Max / Avg. */
.graph-measure { margin:0 auto; display:inline-flex; padding:.25rem .4rem;
  border:1px solid var(--border); border-radius:6px; background:var(--surface); }
.graph-measure.off { display:none; }  /* takes no space, so it never makes the row wrap */
.graph-stats { display:grid; grid-template-columns:auto repeat(3, auto); column-gap:.45rem; row-gap:.22rem;
  font-size:.64rem; line-height:1.15; font-variant-numeric:tabular-nums; text-align:right; }
.graph-stats b { font-weight:700; color:var(--text-dim); }
.graph-stats .gs-volts { color:var(--txt-blue); }
.graph-stats .gs-ma { color:var(--success); }
.graph-stats .gs-mw { color:var(--txt-pink); }
/* Legend and Head indicator, on one line at the right-hand end of the tools.
   The whole tools row is one line (see .graph-tools); these are kept compact
   so it fits with the measurements box showing. */
.graph-right { display:flex; align-items:center; gap:.5rem; flex-wrap:nowrap; margin-left:auto; white-space:nowrap; }
.graph-legend-row { display:flex; align-items:center; gap:.4rem; }
.graph-legend-key { display:inline-flex; align-items:center; gap:.2rem; font-size:.64rem; color:var(--text-muted); }
.graph-swatch { display:inline-block; width:.7rem; height:.6rem; border-radius:2px; }
.graph-swatch.v { background:var(--txt-blue); }
.graph-swatch.i { background:var(--success); }
.graph-swatch.p { background:var(--txt-pink); }
.graph-swatch.missing { background:var(--danger); opacity:.35; }
.graph-head-ind { display:inline-flex; align-items:center; gap:.35rem; font-size:.68rem; font-weight:700;
  color:var(--text-dim); cursor:default; user-select:none; }
.graph-head-dot { width:.55rem; height:.55rem; border-radius:50%; background:var(--border); }
.graph-head-ind.on { color:var(--text); }
.graph-head-ind.on .graph-head-dot { background:var(--success); box-shadow:0 0 0 2px color-mix(in srgb, var(--success) 30%, transparent); }
.graph-seg button.on { background:var(--accent); color:var(--accent-on); }
.graph-seg button.metric-v { color:var(--txt-blue); }
.graph-seg button.metric-i { color:var(--success); }
.graph-seg button.metric-p { color:var(--txt-pink); }
.graph-seg button.metric-v.on { background:var(--txt-blue); color:var(--bg); }
.graph-seg button.metric-i.on { background:var(--success); color:var(--bg); }
.graph-seg button.metric-p.on { background:var(--txt-pink); color:var(--bg); }
.graph-canvas-wrap { position:relative; width:100%; height:13rem; }
.graph-canvas { width:100%; height:100%; display:block; }
/* The brush: the whole history, with the part shown as a blue thumb that holds
   the length of time shown. Drag a thumb edge to move that end of the zoom, or
   the middle to move the zoom. */
.graph-brush { margin-top:.45rem; }
.graph-sb { position:relative; height:1.1rem; background:var(--border); border-radius:4px; overflow:hidden; }
.graph-sb-thumb { position:absolute; top:0; bottom:0; left:0; width:100%; background:var(--accent); border-radius:4px;
  display:flex; align-items:center; justify-content:center; }
.graph-sb-thumb::before, .graph-sb-thumb::after { content:''; position:absolute; top:3px; bottom:3px; width:2px;
  border-radius:1px; background:var(--bg); opacity:.55; }
.graph-sb-thumb::before { left:3px; }
.graph-sb-thumb::after { right:3px; }
.graph-sb-thumb span { font-size:.62rem; font-weight:700; color:var(--bg); white-space:nowrap; padding:0 .6rem; }
/* HISTORICAL: .graph-legend and .graph-dot are no longer used (the legend moved
   to the top right, with .graph-swatch). Kept for now; to be removed. */
.graph-legend { display:flex; gap:.85rem; flex-wrap:wrap; margin-top:.45rem; font-size:.68rem; color:var(--text-muted); }
.graph-dot { display:inline-block; width:.55rem; height:.55rem; border-radius:50%; margin-right:.25rem; vertical-align:-1px; }
.graph-dot.v { background:var(--txt-blue); }
.graph-dot.i { background:var(--success); }
.graph-dot.p { background:var(--txt-pink); }
.mini-graph { display:block; width:100%; height:2.4rem; border-top:1px solid var(--border-soft); padding-top:.35rem; cursor:pointer; }
.mini-canvas { width:100%; height:100%; display:block; }

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
.choice-seg { display:inline-flex; border:1px solid var(--border); border-radius:8px; overflow:hidden; background:var(--surface); }
.choice-seg button { border:0; border-right:1px solid var(--border); background:transparent; color:var(--text-muted);
  padding:.3rem .55rem; font-size:.78rem; font-weight:700; min-width:2.4rem; }
.choice-seg button:last-child { border-right:0; }
.choice-seg button.on { background:var(--accent); color:var(--accent-on); }
/* Per-port on/off: six small numbered bool buttons in a row. */
.port-bools { display:flex; gap:.25rem; }
.bool-btn.port-bool { min-width:1.9rem; padding:.3rem 0; }
/* "M" (manual) in the group heading, above each port that's on in no row. */
.cfg-group h4.has-marks { display:flex; align-items:center; justify-content:space-between; }
.port-marks { display:flex; gap:.25rem; }
.port-marks span { width:1.9rem; text-align:center; font-size:calc(.8rem + 1pt); font-weight:600; color:var(--accent); }

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
  clock: 'M8 0a8 8 0 1 1 0 16A8 8 0 0 1 8 0ZM1.5 8a6.5 6.5 0 1 0 13 0 6.5 6.5 0 0 0-13 0Zm7-3.25v2.992l2.028.812a.75.75 0 0 1-.557 1.392l-2.5-1A.751.751 0 0 1 7 8.25v-3.5a.75.75 0 0 1 1.5 0Z',
  graph: 'M1.75 13.5h12.5a.75.75 0 0 1 0 1.5H1.75A1.75 1.75 0 0 1 0 13.25V1.75a.75.75 0 0 1 1.5 0v11.5c0 .138.112.25.25.25Zm12.03-9.97a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06 0L7.25 5.62 4.03 8.84a.75.75 0 0 1-1.06-1.06l3.75-3.75a.75.75 0 0 1 1.06 0L10 6.25l2.72-2.72a.75.75 0 0 1 1.06 0Z',
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
let graphPort = null;
// The page's working copy of every port's schedule, loaded from the hub and
// sent back in full on every edit: schedules[n] = [entry, ...] for n = 1..6.
// Entries use the hub's fields (see settings/HubSettings.h, HubScheduleEntry).
const schedules = {};
const DOW = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];
const GRAPH_METRICS = ['volts', 'ma', 'mw'];
// The hub's port history: 100 readings a second, 60 minutes per port, sent 10
// minutes per page. Fixed by the firmware (history/PortHistory.h).
const HIST_HZ = 100;
const HIST_SLOTS = 360000;
// Raw reading steps to volts and mA: 8 mV per bus voltage step, and 40 uV per
// shunt voltage step across the 0.1 ohm shunt (INA_SHUNT_OHMS in main.cpp).
const BUS_V = 0.008;
const SHUNT_MA = 0.04 / 0.1;
// Port event codes (PortHistory::Event).
const EVENT_LABEL = { 1:'Power off', 2:'Power on', 3:'Removed', 4:'Inserted' };
// Readings fetched for a port that's only shown as a mini graph: 10 seconds.
const MINI_READINGS = 1000;
// Mini graphs: the height (px) kept clear at the top for the labels, and the
// lowest each scale's top can be.
const MINI_LABEL_H = 12;
const MINI_MIN_TOP = { volts:1, ma:10, mw:50 };
// Shortest zoom: 1 second (100 readings, so a span of 99 counts). And how
// close to a thumb edge (px) grabs the edge.
const GRAPH_MIN_ZOOM = HIST_HZ - 1;
const GRAPH_EDGE_W = 6;
// How far (px) the mouse must move with the button down before it's a drag. Less
// than that is a click, which moves the cursor there.
const GRAPH_DRAG_PX = 6;
// How close (px) either side of the cursor line grabs it.
const GRAPH_CURSOR_HIT = 12;
// Fitting a scale to what's in view leaves this much of the plot's height
// empty above and below the line.
const GRAPH_FIT_PAD = 0.125;
// The highest value each Y scale can reach. The ports are a fixed 5 V supply,
// so 6 V. Current: the most the history can store, ±1638 mA (13-bit shunt,
// 40 uV steps, 0.1 ohm shunt), plus 10%. Power: the two multiplied.
const GRAPH_Y_MAX = { volts:6, ma:1638 * 1.1, mw:6 * 1638 * 1.1 };
const graphCfg = { metric: 'volts,ma,mw', vzero: false };
let graphAveraging = 0;      // the profiler averaging setting: 0, 2, 4, 8 or 16
let graphAveragingSetAt = -1e9;
let graphZoom = null;        // null = everything held, or { a, b, head }: the capture counts shown,
                             // and whether it follows the newest reading
let graphCursor = null;      // the cursor's capture count
let graphCursorAge = 0;      // its distance back from the view's newest end, in readings
let graphDrag = null;        // a zoom selection, zoom arrow or scrollbar thumb being dragged
let graphLockedRanges = {};  // y ranges set on a scale column: { min, max } per metric
let graphVisible = {};       // per metric, what the last draw showed: { min, max, avg }
let graphScaleHover = '';    // the scale column under the pointer
let graphClickTimer = null;  // a single click on the V column, waiting to see if it's a double-click
let graphNoClick = false;    // the button-up just ended a drag: ignore the click that follows
let graphNoHistory = false;  // the hub has no PSRAM history
// What clicking a port shows: 'schedule' or 'history' (the top bar buttons).
let viewMode = 'schedule';
// Every port's history the browser has received, kept when another port is
// shown: hist[n] = { v, ma, lo, hi, events, ... } (histStore()).
const hist = {};
let liveOn = false;
let liveGen = 0;
let liveTimer = null;
let liveTurn = 0;
let miniMetric = 'ma';
const envHistory = [];
const ENV_WINDOW_MS = 5 * 60 * 1000;
let lastEnv = null;
let envHistoryLoaded = false;
let envHistoryLoading = false;
let envHistoryRequestSeq = 0;
let uiPrefsLoaded = false;

// True when the port is on, or before status has loaded and the UI should avoid
// drawing it as disabled.
function portEnabled(n) {
  const p = lastPorts.find((x) => x.n === n);
  return !p || p.enabled !== false;
}

// Display title for a port, including its saved name when it has one.
function portTitle(n) {
  const p = lastPorts.find((x) => x.n === n) || {};
  const name = (p.name || '').trim();
  return name ? name + ' · Port ' + n : 'Port ' + n;
}

// Click a socket to open or close its panel: its schedule or its history
// graph, whichever view is selected on the top bar.
function selectPort(n) {
  selectedPort = selectedPort === n ? null : n;
  document.querySelectorAll('.socket').forEach((c) => c.classList.remove('selected'));
  if (selectedPort) { const c = $('port-' + selectedPort); if (c) c.classList.add('selected'); }
  renderPortView();
}
// Show the selected port in the selected view.
function renderPortView() {
  const next = viewMode === 'history' ? selectedPort : null;
  if (next !== graphPort) {
    graphZoom = null;
    graphCursor = null;
    graphLockedRanges = {};
  }
  graphPort = next;
  $('view-history').classList.toggle('on', viewMode === 'history');
  $('view-schedule').classList.toggle('on', viewMode === 'schedule');
  renderGraphPanel();
  renderDetail();
  lastPorts.forEach(updatePort);
}
// Top bar: pick what clicking a port shows. The two buttons work as a pair.
function setViewMode(m) {
  viewMode = m;
  try { localStorage.setItem('hubView', m); } catch (e) {}
  renderPortView();
}

// Draw the selected port's schedule panel (or hide it when none is selected,
// or the history view is selected).
function renderDetail() {
  const host = $('port-detail');
  if (!selectedPort || viewMode !== 'schedule') { host.style.display = 'none'; host.innerHTML = ''; return; }
  const n = selectedPort;
  if (!schedules[n]) schedules[n] = [];
  host.style.display = 'block';
  host.innerHTML =
    '<div class="detail-bar">' +
      '<div class="detail-left">' +
        '<span class="detail-title" id="detail-title">' + esc(portTitle(n)) + '</span>' +
        '<span class="next-chip" id="next-chip"></span>' +
      '</div>' +
      '<button class="add-sched" onclick="addSchedule(' + n + ')">' + ic('plus') + ' Add Schedule</button>' +
    '</div>' +
    '<div id="timeline"></div>' +
    '<div class="sched-list" id="sched-list"></div>';
  renderSchedules();
  renderNextChip();
}

// Normalise the persisted metric set, preserving legacy single-metric values.
function normalizeGraphMetric(value) {
  if (value === '') return '';
  const saved = value === 'combined' ? GRAPH_METRICS : String(value || '').split(',');
  const active = GRAPH_METRICS.filter((m) => saved.includes(m));
  return active.length ? active.join(',') : GRAPH_METRICS.join(',');
}
// Active large-graph metrics in display order.
function graphMetrics() {
  return GRAPH_METRICS.filter((m) => String(graphCfg.metric || '').split(',').includes(m));
}
// Three independent metric toggle buttons for the large graph.
function graphMetricToggles() {
  const active = graphMetrics();
  let out = '<span class="graph-seg">';
  GRAPH_METRICS.forEach((m) => {
    const cls = (active.includes(m) ? 'on ' : '') + (m === 'volts' ? 'metric-v' : (m === 'ma' ? 'metric-i' : 'metric-p'));
    out += '<button class="' + cls + '" onclick="toggleGraphMetric(\'' + m + '\')">' + metricLabel(m) + '</button>';
  });
  return out + '</span>';
}
// ---- Port history, as received from the hub -----------------------------
// Each port's readings sit at count % HIST_SLOTS (count = the hub's capture
// count, one per 10 ms), as volts and mA, NaN where there's no reading. lo..hi
// is the range of counts held: at most the hub's 60 minutes.
function histStore(n) {
  if (!hist[n])
    hist[n] = { v:new Float32Array(HIST_SLOTS), ma:new Float32Array(HIST_SLOTS), lo:0, hi:-1,
      events:[], wallAt:0, busy:false, filling:false, filled:false,
      firstValid:Infinity, vMin:Infinity, vMax:-Infinity, iMin:Infinity, iMax:-Infinity, pMin:Infinity, pMax:-Infinity };
  return hist[n];
}
// Fetch one binary history page. Layout: PortHistory::Page in the firmware.
async function histFetch(n, query) {
  const ac = new AbortController();
  const timeout = setTimeout(() => ac.abort(), 10000);
  try {
    const r = await fetch('/api/history?port=' + n + query, { cache:'no-store', signal:ac.signal });
    if (!r.ok) { const e = new Error('http ' + r.status); e.status = r.status; throw e; }
    const buf = await r.arrayBuffer();
    const dv = new DataView(buf);
    if (buf.byteLength < 10) throw new Error('short page');
    const first = dv.getUint32(0, true), events = dv.getUint16(4, true), n2 = dv.getUint32(6, true);
    if (buf.byteLength !== 10 + events * 5 + n2 * 3) throw new Error('bad page');
    const ev = [];
    for (let i = 0, o = 10; i < events; i++, o += 5) ev.push({ c:dv.getUint32(o, true), code:dv.getUint8(o + 4) });
    return { first:first, n:n2, events:ev, bytes:new Uint8Array(buf, 10 + events * 5) };
  } finally {
    clearTimeout(timeout);
  }
}
// Unpack a page's readings into the store, skipping counts before minCount.
// Each reading is 3 bytes: valid bit, 10-bit bus voltage, 13-bit signed shunt.
// Also widens each metric's lowest/highest value, which sets the Y axis.
function histPut(h, p, minCount) {
  const b = p.bytes;
  for (let i = 0; i < p.n; i++) {
    const c = p.first + i;
    if (c < minCount) continue;
    const k = c % HIST_SLOTS, x = (b[i * 3] << 16) | (b[i * 3 + 1] << 8) | b[i * 3 + 2];
    if (x & 0x800000) {
      let s = x & 0x1fff;
      if (s & 0x1000) s -= 0x2000;
      if (c < h.firstValid) h.firstValid = c;
      const v = h.v[k] = ((x >> 13) & 0x3ff) * BUS_V;
      const ma = h.ma[k] = s * SHUNT_MA, mw = v * ma;
      if (v < h.vMin) h.vMin = v; if (v > h.vMax) h.vMax = v;
      if (ma < h.iMin) h.iMin = ma; if (ma > h.iMax) h.iMax = ma;
      if (mw < h.pMin) h.pMin = mw; if (mw > h.pMax) h.pMax = mw;
    } else {
      h.v[k] = NaN;
      h.ma[k] = NaN;
    }
  }
  p.events.forEach((e) => {
    if (e.c >= minCount && !h.events.some((x) => x.c === e.c && x.code === e.code)) h.events.push(e);
  });
  h.events.sort((a, b) => a.c - b.c);
}
// Add a page of the newest readings: the first page, or a live update.
function histAdd(h, p) {
  if (!p.n) return;
  if (h.hi < 0 || p.first - h.hi > HIST_SLOTS) {
    h.lo = p.first;
    h.events = [];
  } else {
    for (let c = h.hi + 1; c < p.first; c++) { h.v[c % HIST_SLOTS] = NaN; h.ma[c % HIST_SLOTS] = NaN; }
  }
  histPut(h, p, 0);
  h.hi = p.first + p.n - 1;
  h.lo = Math.max(h.lo, h.hi - HIST_SLOTS + 1);
  h.events = h.events.filter((e) => e.c >= h.lo);
  h.wallAt = Date.now();
}
// Fetch a port's new readings: the newest page the first time, then
// everything after the newest reading held.
async function histUpdate(n, max) {
  const h = histStore(n);
  if (h.busy) return;
  h.busy = true;
  try {
    const p = await histFetch(n, h.hi < 0 ? (max ? '&max=' + max : '') : '&after=' + h.hi);
    graphNoHistory = false;
    if (hist[n] !== h) return;
    if (h.hi >= 0 && p.n && p.first <= h.hi) {
      // The hub restarted: its capture counts started again from 0.
      delete hist[n];
      histAdd(histStore(n), p);
      return;
    }
    histAdd(h, p);
  } catch (e) {
    if (e.status === 503) graphNoHistory = true;
  } finally {
    h.busy = false;
  }
}
// Fill in a port's older history, 10 minutes per page, newest first, until a
// page comes back with no readings or the browser holds 60 minutes.
async function histBackfill(n) {
  const h = hist[n];
  if (!h || h.hi < 0 || h.filling || h.filled) return;
  h.filling = true;
  try {
    while (hist[n] === h && h.hi - h.lo + 1 < HIST_SLOTS) {
      const p = await histFetch(n, '&before=' + h.lo);
      if (hist[n] !== h) return;
      if (!p.n || p.first + p.n !== h.lo) break;
      histPut(h, p, h.hi - HIST_SLOTS + 1);
      h.lo = Math.max(p.first, h.hi - HIST_SLOTS + 1);
      if (n === graphPort) drawGraph();
    }
    if (hist[n] === h) h.filled = true;
  } catch (e) {
  } finally {
    h.filling = false;
  }
}
// ---- Live updates -----------------------------------------------------------
// Five times a second: new readings for the graph port, and for one other
// port's mini graph per round. Then carry on filling in the graph port's older
// history, and redraw.
function startLiveLoop() {
  if (liveOn) return;
  liveOn = true;
  liveTick(++liveGen);
}
// Stop the live updates, while the hub is offline.
function stopLiveLoop() {
  liveOn = false;
  liveGen++;
  if (liveTimer) clearTimeout(liveTimer);
  liveTimer = null;
}
// One round of live updates. `gen` stops a round left over from a loop that
// was stopped and started again.
async function liveTick(gen) {
  liveTimer = null;
  if (gen !== liveGen) return;
  if (graphPort) await histUpdate(graphPort);
  const others = [1, 2, 3, 4, 5, 6].filter((n) => n !== graphPort && portEnabled(n));
  if (others.length) await histUpdate(others[liveTurn++ % others.length], MINI_READINGS);
  if (gen !== liveGen) return;
  if (graphPort) {
    histBackfill(graphPort);
    drawGraph();
  }
  drawMiniGraphs();
  liveTimer = setTimeout(() => liveTick(gen), 200);
}

// ---- Graph panel --------------------------------------------------------------
// The graph shows all the history held for the port, filling the width, unless
// it's zoomed in to part of it. Positions along the time axis are capture
// counts (one reading every 10 ms). The Y axis is set from all the history
// held, so it doesn't change with zooming or scrolling.

// A length of time: "1h 2m", "4m 12s", "12.5s", "0.35s".
function graphSpanLabel(seconds) {
  if (seconds >= 3600) return Math.floor(seconds / 3600) + 'h ' + Math.floor(seconds % 3600 / 60) + 'm';
  if (seconds >= 60) return Math.floor(seconds / 60) + 'm ' + Math.floor(seconds % 60) + 's';
  if (seconds >= 10) return seconds.toFixed(1) + 's';
  return seconds.toFixed(2) + 's';
}
// Plot area margins: room on the left for all three metrics' scales, kept the
// same whichever metrics are shown so the plot never changes width.
function graphPad() {
  return { l:112, r:12, t:14, b:24 };
}
// Where the graph's full view starts: the first real reading held. Time before
// it (the port off since the hub started, say) holds no readings, so showing
// everything shows from the first reading to the newest, not the whole hour.
function graphLo(h) {
  if (h.firstValid < h.lo) {
    // That reading has dropped out of the 60 minutes: find the next one.
    let c = h.lo;
    while (c <= h.hi && h.v[c % HIST_SLOTS] !== h.v[c % HIST_SLOTS]) c++;
    h.firstValid = c <= h.hi ? c : Infinity;
  }
  return h.firstValid <= h.hi ? h.firstValid : h.lo;
}
// The capture counts shown, first and last: everything held, or the zoomed
// part of it. A zoom locked to the head moves along with each new reading.
// Null before any readings.
function graphView(h) {
  if (!h || h.hi < 0) return null;
  const z = graphZoom;
  if (!z) return { a:graphLo(h), b:h.hi };
  const len = z.b - z.a;
  if (z.head) { z.b = h.hi; z.a = h.hi - len; }
  if (z.a < graphLo(h)) { z.a = graphLo(h); z.b = Math.min(h.hi, graphLo(h) + len); }
  if (z.b > h.hi) z.b = h.hi;
  if (z.b - z.a < GRAPH_MIN_ZOOM || (z.a <= graphLo(h) && z.b >= h.hi)) { graphZoom = null; return { a:graphLo(h), b:h.hi }; }
  return { a:z.a, b:z.b };
}
// Set the zoom to capture counts a..b. Reaching the newest reading locks it to
// the head.
function graphSetZoom(h, a, b) {
  graphZoom = { a:a, b:b, head:b >= h.hi };
}
// Right-hand end of the tools: the legend (voltage, current, power, missing
// data), then the Head indicator. Head isn't a control: it's lit while the view follows the
// newest reading, which happens when showing everything, or when the brush
// thumb's right edge is dragged to the end.
function graphNav() {
  return '<span class="graph-right"><span class="graph-legend-row">' +
    '<span class="graph-legend-key"><span class="graph-swatch v"></span>Voltage</span>' +
    '<span class="graph-legend-key"><span class="graph-swatch i"></span>Current</span>' +
    '<span class="graph-legend-key"><span class="graph-swatch p"></span>Power</span>' +
    '<span class="graph-legend-key"><span class="graph-swatch missing"></span>Missing</span></span>' +
    '<span class="graph-head-ind on" id="graph-head-btn" title="Lit while the graph follows the newest reading">' +
      '<span class="graph-head-dot"></span>Head</span></span>';
}
// Measurements for what's in view: the lowest, highest and average of each
// metric, one row each in its colour. Filled in by drawGraph(), and only shown
// while zoomed in; hidden, it takes no space.
function graphStatsBlock() {
  let out = '<span class="graph-measure off" id="graph-stats"><span class="graph-stats"><span></span><b>Min</b><b>Max</b><b>Avg</b>';
  GRAPH_METRICS.forEach((k) => {
    out += '<span class="gs-' + k + '">' + metricLabel(k) + '</span>' +
      ['min', 'max', 'avg'].map((f) => '<span class="gs-' + k + '" id="gs-' + k + '-' + f + '">--</span>').join('');
  });
  return out + '</span></span>';
}
// Fill in the measurements from what the last draw showed (graphVisible),
// and show the box only while zoomed in.
function updateGraphStats() {
  const box = $('graph-stats');
  if (box) box.classList.toggle('off', !graphZoom);
  GRAPH_METRICS.forEach((k) => {
    const seen = graphVisible[k];
    ['min', 'max', 'avg'].forEach((f) => {
      const el = $('gs-' + k + '-' + f);
      if (!el) return;
      const n = seen ? seen[f] : NaN;
      el.textContent = isFinite(n) ? (k === 'volts' ? n.toFixed(3) : n.toFixed(1)) : '--';
    });
  });
}
// Averaging: Off, or the average of each reading and the 1, 3, 7 or 15 before it.
// The same setting as the port cards' averaging.
function graphAveragingButtons() {
  let out = '<span class="graph-seg">';
  [0, 2, 4, 8, 16].forEach((a) => {
    out += '<button class="' + (graphAveraging === a ? 'on' : '') + '" onclick="setGraphAveraging(' + a + ')"' +
      ' title="Averaging">' + (a ? 'Avg ' + a : 'Off') + '</button>';
  });
  return out + '</span>';
}
// An averaging button: set it, and save it to the hub's profiler settings.
function setGraphAveraging(a) {
  graphAveraging = a;
  graphAveragingSetAt = performance.now();
  fetch('/api/settings', { method:'POST', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ profiler:{ averaging:a } }) }).catch(() => {});
  if (graphPort) renderGraphPanel();
}
// Draw the graph panel for the selected port.
function renderGraphPanel() {
  const host = $('port-graph');
  if (!host || !graphPort) {
    if (host) { host.style.display = 'none'; host.innerHTML = ''; }
    return;
  }
  host.classList.toggle('off', !portEnabled(graphPort));
  host.style.display = 'block';
  host.innerHTML =
    '<div class="graph-head"><div><span class="graph-title" id="graph-title">' + esc(portTitle(graphPort)) + ' trends</span></div>' +
      '<button class="tool" onclick="selectPort(' + graphPort + ')" title="Close">' + ic('x') + '</button></div>' +
    '<div class="graph-body">' +
      '<div class="graph-tools">' +
        graphMetricToggles() +
        graphAveragingButtons() +
        graphStatsBlock() +
        graphNav() +
      '</div>' +
      '<div class="graph-canvas-wrap"><canvas class="graph-canvas" id="graph-canvas"' +
        ' onmousedown="graphMouseDown(event)" onmousemove="graphHover(event)" onmouseleave="graphLeave()"' +
        ' ondblclick="graphDoubleClick(event)"' +
        ' onclick="graphCanvasClick(event)" onwheel="graphCanvasWheel(event)"></canvas></div>' +
      '<div class="graph-brush">' +
        '<div class="graph-sb" id="graph-sb"><div class="graph-sb-thumb" id="graph-sb-thumb"' +
          ' onmousedown="graphThumbDown(event)" onmousemove="graphThumbHover(event)">' +
          '<span id="graph-span"></span></div></div></div>' +
    '</div>';
  drawGraph();
  startLiveLoop();
}
// Toggle one large-graph metric on or off and persist the set.
function toggleGraphMetric(metric) {
  const active = graphMetrics();
  const next = active.includes(metric) ? active.filter((m) => m !== metric) : active.concat(metric);
  graphCfg.metric = GRAPH_METRICS.filter((m) => next.includes(m)).join(',');
  saveProfilerPrefs();
  if (graphPort) renderGraphPanel();
}

// ---- Zoom, brush and cursor -----------------------------------------------------
// Drag across the plot to zoom to that span (time only: the selection is
// always the full height). Double-click to zoom back out. Dragging either edge
// of the brush's thumb moves that end of the zoom, and dragging the middle of
// the thumb moves the zoomed span through the history.
// The cursor is a vertical line with an arrow on top, always in view. Drag it
// to read the values at any reading; dragging it past either side of the plot
// moves the view along.
function graphPlotPos(ev) {
  const c = $('graph-canvas');
  const r = c.getBoundingClientRect();
  const pad = graphPad();
  const w = Math.max(320, c.clientWidth), ht = Math.max(160, c.clientHeight);
  return { x:ev.clientX - r.left, y:ev.clientY - r.top, pad:pad, gw:w - pad.l - pad.r, gh:ht - pad.t - pad.b };
}
// True when the pointer is over the plot, or just above it where the
// cursor's arrow is.
function graphInPlot(p) {
  return p.x >= p.pad.l && p.x <= p.pad.l + p.gw && p.y >= p.pad.t - 12 && p.y <= p.pad.t + p.gh;
}
// X position of a capture count in the view, and the count at an x position.
function graphXOf(v, c, p) {
  return p.pad.l + (v.b > v.a ? (c - v.a) * p.gw / (v.b - v.a) : p.gw / 2);
}
// The capture count at an x position on the plot.
function graphCountAt(v, x, p) {
  return Math.round(v.a + (x - p.pad.l) / p.gw * (v.b - v.a));
}
// True when the pointer is on the cursor line or its arrow.
function graphOnCursor(p) {
  const v = graphView(hist[graphPort]);
  if (!v || graphCursor === null || !graphInPlot(p)) return false;
  return Math.abs(p.x - graphXOf(v, graphCursor, p)) <= GRAPH_CURSOR_HIT;
}
// Pointer shape over the plot: a resize arrow on the cursor line, a crosshair
// elsewhere on the plot.
function graphHover(ev) {
  if (graphDrag) return;
  const c = $('graph-canvas');
  const p = graphPlotPos(ev);
  const scale = graphScaleMetricAt(ev);
  c.style.cursor = scale ? 'ns-resize' : (graphOnCursor(p) ? 'ew-resize' : (graphInPlot(p) ? 'crosshair' : 'default'));
  if (scale !== graphScaleHover) { graphScaleHover = scale; drawGraph(); }
}
// Pointer left the canvas: no scale column is hovered.
function graphLeave() {
  if (graphScaleHover && !graphDrag) { graphScaleHover = ''; drawGraph(); }
}
// Double-click: on a scale column, fit that metric to what's in view; on the
// plot, show all the history held again.
function graphDoubleClick(ev) {
  if (graphClickTimer) { clearTimeout(graphClickTimer); graphClickTimer = null; }
  const metric = graphScaleMetricAt(ev);
  if (metric) graphFitScale(metric);
  else graphZoomOut();
}
// Button down on the plot: on the cursor line, start dragging it; anywhere
// else, start what becomes a click or a zoom selection.
function graphMouseDown(ev) {
  const h = hist[graphPort];
  const v = graphView(h);
  if (!v || ev.button !== 0) return;
  const p = graphPlotPos(ev);
  const scale = graphScaleMetricAt(ev);
  if (scale) {
    const range = metricRange(h, scale);
    if (!range) return;
    graphDrag = { kind:'yshift', key:scale, y0:p.y, dragging:false, min0:range.min, max0:range.max, gh:p.gh };
  }
  else if (graphOnCursor(p)) graphDrag = { kind:'cursor', x:p.x, timer:setInterval(graphCursorEdge, 30) };
  else if (graphInPlot(p)) graphDrag = { kind:'select', x0:p.x, y0:p.y, x1:p.x, dragging:false, v:v, p:p };
  else return;
  ev.preventDefault();
  window.addEventListener('mousemove', graphMouseMove);
  window.addEventListener('mouseup', graphMouseUp);
}
// Mouse moved with the button down: grow the zoom selection, once it has
// moved far enough to be a drag; move a scale up or down; or move the cursor.
function graphMouseMove(ev) {
  const d = graphDrag, h = hist[graphPort];
  if (!d || !h) return;
  const p = graphPlotPos(ev);
  if (d.kind === 'yshift') {
    // Dragging a scale column moves that metric's line with the pointer: both
    // ends of its scale shift together, keeping its span.
    if (!d.dragging && Math.abs(p.y - d.y0) < GRAPH_DRAG_PX) return;
    d.dragging = true;
    const shift = (p.y - d.y0) * (d.max0 - d.min0) / d.gh;
    graphLockedRanges[d.key] = graphClampRange(d.key, d.min0 + shift, d.max0 + shift, true);
  }
  else if (d.kind === 'select') {
    if (!d.dragging && Math.hypot(p.x - d.x0, p.y - d.y0) < GRAPH_DRAG_PX) return;
    d.dragging = true;
    d.x1 = Math.max(p.pad.l, Math.min(p.pad.l + p.gw, p.x));
  }
  else {
    d.x = p.x;
    const v = graphView(h);
    if (v) {
      graphCursor = Math.max(v.a, Math.min(v.b, graphCountAt(v, p.x, p)));
      graphCursorAge = v.b - graphCursor;
    }
  }
  drawGraph();
}
// While the cursor is dragged past either side of the plot, move the view
// along: faster the further past it is.
function graphCursorEdge() {
  const d = graphDrag, h = hist[graphPort];
  const c = $('graph-canvas');
  if (!d || d.kind !== 'cursor' || !h || !c) return;
  const v = graphView(h);
  if (!v) return;
  const p = graphPlotPos({ clientX:0, clientY:0 });
  const over = d.x < p.pad.l ? d.x - p.pad.l : (d.x > p.pad.l + p.gw ? d.x - p.pad.l - p.gw : 0);
  if (!over) return;
  const len = v.b - v.a;
  let shift = Math.round(over / p.gw * len * 0.1);
  if (!shift) shift = over < 0 ? -1 : 1;
  const a = Math.max(graphLo(h), Math.min(h.hi - len, v.a + shift));
  graphSetZoom(h, a, a + len);
  graphCursor = over < 0 ? a : a + len;
  graphCursorAge = a + len - graphCursor;
  drawGraph();
}
// Button up: a click moves the cursor there; a drag zooms to the selection.
function graphMouseUp(ev) {
  const d = graphDrag, h = hist[graphPort];
  graphDrag = null;
  window.removeEventListener('mousemove', graphMouseMove);
  window.removeEventListener('mouseup', graphMouseUp);
  if (d && d.timer) clearInterval(d.timer);
  if (d && d.dragging) graphNoClick = true;
  // A click: move the cursor to where the button was let go.
  if (d && d.kind === 'select' && !d.dragging && h) {
    const p = graphPlotPos(ev);
    const v = graphView(h);
    if (v) {
      graphCursor = Math.max(v.a, Math.min(v.b, graphCountAt(v, Math.max(p.pad.l, Math.min(p.pad.l + p.gw, p.x)), p)));
      graphCursorAge = v.b - graphCursor;
    }
  }
  if (d && d.kind === 'select' && d.dragging && h) {
    let a = graphCountAt(d.v, Math.min(d.x0, d.x1), d.p), b = graphCountAt(d.v, Math.max(d.x0, d.x1), d.p);
    // A selection under 1 second zooms to 1 second around its middle.
    if (b - a < GRAPH_MIN_ZOOM) {
      a = Math.max(graphLo(h), Math.round((a + b - GRAPH_MIN_ZOOM) / 2));
      b = Math.min(h.hi, a + GRAPH_MIN_ZOOM);
      a = Math.max(graphLo(h), b - GRAPH_MIN_ZOOM);
    }
    if (b - a >= GRAPH_MIN_ZOOM) graphSetZoom(h, a, b);
  }
  drawGraph();
}
// Scroll wheel over the plot: zoom in or out, moving both ends of the zoom
// together, like dragging both edges of the brush's thumb. It never locks the
// zoom to the newest reading, even when it reaches it: only dragging the
// thumb's right edge there does that.
function graphWheelZoom(ev) {
  const h = hist[graphPort];
  const v = graphView(h);
  if (!v || !graphInPlot(graphPlotPos(ev))) return;
  ev.preventDefault();
  const len = v.b - v.a;
  const next = Math.max(GRAPH_MIN_ZOOM, Math.min(h.hi - graphLo(h), Math.round(len * Math.exp(ev.deltaY * 0.002))));
  if (next === len) return;
  const a = Math.max(graphLo(h), Math.min(h.hi - next, Math.round((v.a + v.b - next) / 2)));
  graphZoom = { a:a, b:a + next, head:false };
  drawGraph();
}
// Double-click on the plot: show all the history held again.
function graphZoomOut() {
  graphZoom = null;
  drawGraph();
}
// Which part of the brush's thumb is under the pointer: its left edge ('a'),
// its right edge ('b') or its middle ('').
function graphThumbPart(ev) {
  const thumb = $('graph-sb-thumb');
  const r = thumb.getBoundingClientRect();
  const x = ev.clientX - r.left;
  return x <= GRAPH_EDGE_W ? 'a' : (x >= r.width - GRAPH_EDGE_W ? 'b' : '');
}
// Pointer shape over the thumb: a resize arrow on an edge, a hand in the
// middle while zoomed.
function graphThumbHover(ev) {
  if (graphDrag) return;
  const part = graphThumbPart(ev);
  $('graph-sb-thumb').style.cursor = part ? 'ew-resize' : (graphZoom ? 'grab' : 'default');
}
// Drag the thumb: an edge moves that end of the zoom; the middle moves the
// zoomed span, keeping its length. The brush covers all the history held.
function graphThumbDown(ev) {
  const h = hist[graphPort];
  const v = graphView(h);
  const sb = $('graph-sb'), thumb = $('graph-sb-thumb');
  if (!v || !sb || !thumb || ev.button !== 0) return;
  const part = graphThumbPart(ev);
  if (!part && !graphZoom) return;
  const total = h.hi - graphLo(h) + 1, len = v.b - v.a;
  const room = sb.clientWidth - thumb.offsetWidth;
  graphDrag = { kind:part ? 'edge' : 'thumb', part:part, x0:ev.clientX, a0:v.a, b0:v.b, len:len,
    head:!graphZoom || !!graphZoom.head,
    perPx:part ? total / sb.clientWidth : (room > 0 ? (total - 1 - len) / room : 0) };
  ev.preventDefault();
  window.addEventListener('mousemove', graphThumbMove);
  window.addEventListener('mouseup', graphDragEnd);
}
// The thumb's right edge locks to the newest end when it's dragged to within
// GRAPH_EDGE_W pixels of it, and stays locked there while the left edge is
// dragged, so the zoom keeps following the newest readings.
function graphThumbMove(ev) {
  const d = graphDrag, h = hist[graphPort];
  if (!d || !h) return;
  const shift = Math.round((ev.clientX - d.x0) * d.perPx);
  const snap = Math.ceil(GRAPH_EDGE_W * (h.hi - graphLo(h) + 1) / Math.max(1, $('graph-sb').clientWidth));
  if (d.kind === 'thumb') {
    let a = Math.max(graphLo(h), Math.min(h.hi - d.len, d.a0 + shift));
    if (a + d.len >= h.hi - snap) a = h.hi - d.len;
    graphSetZoom(h, a, a + d.len);
  } else if (d.part === 'a') {
    const b = d.head ? h.hi : d.b0;
    graphSetZoom(h, Math.max(graphLo(h), Math.min(b - GRAPH_MIN_ZOOM, d.a0 + shift)), b);
  } else {
    let b = Math.min(h.hi, Math.max(d.a0 + GRAPH_MIN_ZOOM, d.b0 + shift));
    if (b >= h.hi - snap) b = h.hi;
    graphSetZoom(h, d.a0, b);
  }
  drawGraph();
}
// Button up after dragging the thumb or one of its edges.
function graphDragEnd() {
  graphDrag = null;
  window.removeEventListener('mousemove', graphThumbMove);
  window.removeEventListener('mouseup', graphDragEnd);
}
// Size and place the brush's thumb: the part of the history shown, never
// narrower than the length of time printed inside it.
function updateGraphBrush(h, v) {
  const sb = $('graph-sb'), thumb = $('graph-sb-thumb'), span = $('graph-span');
  if (!sb || !thumb || !span) return;
  span.textContent = v ? graphSpanLabel((v.b - v.a + 1) / HIST_HZ) : '';
  const track = sb.clientWidth;
  const total = v ? h.hi - graphLo(h) + 1 : 1, len = v ? v.b - v.a + 1 : 1;
  const w = Math.min(track, Math.max(len / total * track, span.offsetWidth + 16));
  const left = v && total > len ? (v.a - graphLo(h)) / (total - len) * (track - w) : 0;
  thumb.style.left = left + 'px';
  thumb.style.width = w + 'px';
  thumb.classList.toggle('zoomed', !!graphZoom);
  const head = $('graph-head-btn');
  if (head) head.classList.toggle('on', !graphZoom || !!graphZoom.head);
}

// ---- Scales -------------------------------------------------------------------
// Metric represented by the left y-scale column under a canvas event.
function graphScaleMetricAt(ev) {
  const p = graphPlotPos(ev);
  const series = graphMetrics();
  if (p.x < 0 || p.y < p.pad.t || p.y > p.pad.t + p.gh || p.x > p.pad.l) return '';
  if (series.length === 1) return series[0];
  const colW = 34;
  for (let i = 0; i < series.length; i++) {
    const colX = 6 + i * colW;
    if (p.x >= colX - 4 && p.x <= colX + colW - 4) return series[i];
  }
  return '';
}
// Scroll over a scale column to stretch or squash that metric's scale. The
// top and bottom move together, around the metric's average over what's in
// view, so the line stays put at that average. Scroll over the plot to zoom.
function graphCanvasWheel(ev) {
  const metric = graphScaleMetricAt(ev);
  if (!metric) { graphWheelZoom(ev); return; }
  ev.preventDefault();
  const range = metricRange(hist[graphPort], metric);
  const seen = graphVisible[metric];
  if (!range) return;
  const mid = seen && isFinite(seen.avg) ? seen.avg : (range.min + range.max) / 2;
  const f = Math.exp(ev.deltaY * 0.002);
  const min = mid - (mid - range.min) * f, max = mid + (range.max - mid) * f;
  if (max - min < 1e-6) return;
  graphLockedRanges[metric] = graphClampRange(metric, min, max, false);
  drawGraph();
}
// Fit a metric's scale to what's in view: its lowest and highest values sit
// GRAPH_FIT_PAD of the plot's height in from the bottom and top.
function graphFitScale(metric) {
  const seen = graphVisible[metric];
  if (!seen || !isFinite(seen.min) || !isFinite(seen.max)) return;
  let span = seen.max - seen.min;
  if (span <= 0) span = Math.max(Math.abs(seen.max) * 0.02, metric === 'volts' ? 0.01 : 0.1);
  const total = span / (1 - 2 * GRAPH_FIT_PAD);
  const mid = (seen.min + seen.max) / 2;
  graphLockedRanges[metric] = graphClampRange(metric, mid - total / 2, mid + total / 2, false);
  drawGraph();
}
// Keep a Y scale between 0 and GRAPH_Y_MAX, with GRAPH_FIT_PAD of the plot's
// height to spare below 0 and above the top, so a line at a limit never sits
// on the edge of the plot. keepSpan (moving a scale up or down) stops it at
// either limit without changing its span; otherwise each end is cut back to
// its limit.
function graphClampRange(key, min, max, keepSpan) {
  const top = GRAPH_Y_MAX[key];
  const most = top / (1 - 2 * GRAPH_FIT_PAD);
  if (keepSpan) {
    const span = Math.min(most, max - min);
    const spare = span * GRAPH_FIT_PAD;
    if (min < -spare) { min = -spare; max = min + span; }
    if (max > top + spare) { max = top + spare; min = max - span; }
  } else {
    const spare = Math.min(most, max - min) * GRAPH_FIT_PAD;
    min = Math.max(-spare, min);
    max = Math.min(top + spare, max);
  }
  if (max <= min) max = Math.min(top, min + 1e-3);
  if (max <= min) min = max - 1e-3;
  return { min:min, max:max };
}
// A single click on the voltage scale toggles auto-zero for voltage. It waits
// to see whether a second click makes it a double-click, and is ignored
// straight after a drag.
function graphCanvasClick(ev) {
  if (graphNoClick) { graphNoClick = false; return; }
  if (ev.detail > 1 || graphScaleMetricAt(ev) !== 'volts') return;
  if (graphClickTimer) clearTimeout(graphClickTimer);
  graphClickTimer = setTimeout(() => {
    graphClickTimer = null;
    graphCfg.vzero = !graphCfg.vzero;
    delete graphLockedRanges.volts;
    drawGraph();
    saveProfilerPrefs();
  }, 300);
}
// Short unit label for a graph metric.
function metricLabel(key) {
  return key === 'volts' ? 'V' : (key === 'ma' ? 'mA' : 'mW');
}
// CSS variable holding the line colour for a metric.
function metricColor(key) {
  return key === 'volts' ? '--txt-blue' : (key === 'ma' ? '--success' : '--txt-pink');
}
// Format a metric value tightly enough for the graph scales.
function metricDigits(key, v) {
  if (key === 'volts') return v.toFixed(2);
  return Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(1);
}
// Round a raw scale step to 1, 2, 5 or 10 times a power of ten.
function niceStep(raw) {
  if (!isFinite(raw) || raw <= 0) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const n = raw / p;
  const m = n <= 1 ? 1 : (n <= 2 ? 2 : (n <= 5 ? 5 : 10));
  return m * p;
}
// Lowest and highest value of a metric over all the history held.
function graphDataRange(h, key) {
  if (!h) return null;
  return key === 'volts' ? { min:h.vMin, max:h.vMax } : (key === 'ma' ? { min:h.iMin, max:h.iMax } : { min:h.pMin, max:h.pMax });
}
// Sensible y-axis range for one metric, from its lowest and highest value.
function metricAutoRange(mm, key) {
  if (!mm || !isFinite(mm.min) || !isFinite(mm.max)) return null;
  let min = mm.min, max = mm.max;
  if ((key !== 'volts' || graphCfg.vzero) && min >= 0) min = 0;
  if (min === max) {
    const pad = key === 'volts' ? 0.05 : Math.max(1, Math.abs(max) * 0.1);
    min -= pad; max += pad;
    if ((key !== 'volts' || graphCfg.vzero) && min < 0) min = 0;
  }
  const step = niceStep((max - min) / 4);
  min = (key === 'volts' && !graphCfg.vzero) ? Math.floor(min / step) * step : Math.max(0, Math.floor(min / step) * step);
  max = Math.ceil(max / step) * step;
  if (min === max) max = min + step;
  return { min:min, max:max, step:step };
}
// Y-axis range for one metric: from all the history held, or as set by
// scrolling over its scale.
function metricRange(h, key) {
  if (graphLockedRanges[key]) return graphLockedRanges[key];
  const auto = metricAutoRange(graphDataRange(h, key), key);
  return auto ? graphClampRange(key, auto.min, auto.max, false) : null;
}
// Time-axis label: how long before the newest reading.
function graphAgeLabel(seconds) {
  if (seconds < 0.005) return 'now';
  return '-' + graphSpanLabel(seconds);
}

// ---- Drawing ------------------------------------------------------------------
// The capture-count ranges [first, last] the port was switched off, from its
// power off / power on events. Readings missing while it was off aren't
// missing data: nothing was expected. Before the first power event, the port's
// state is taken from that event (a power on means it was off before); with no
// power events at all, from whether it's on now.
function graphOffRanges(h, n) {
  const ev = h.events.filter((e) => e.code === 1 || e.code === 2);
  const wasOn = ev.length ? ev[0].code === 1 : portEnabled(n);
  const out = [];
  let from = wasOn ? null : -Infinity;
  ev.forEach((e) => {
    if (e.code === 1 && from === null) from = e.c;
    else if (e.code === 2 && from !== null) { out.push([from, e.c - 1]); from = null; }
  });
  if (from !== null) out.push([from, Infinity]);
  return out;
}
// The points to draw for the readings from capture count a to b, across gw
// pixels. With averaging on, each reading is first averaged with the ones
// before it, like the port cards.
//  - More readings than pixels: one point per block of readings about a pixel
//    wide, with their average, lowest and highest, so spikes show. A block with
//    no readings breaks the line.
//  - Fewer: one point per reading. A missing reading breaks the line.
// Runs of missing readings come back in `gaps`, except while the port was
// switched off (`off`, from graphOffRanges()); `count` and the sums give the
// exact average of the readings in view.
function graphPoints(h, a, b, gw, off) {
  const cols = Math.max(1, Math.round(gw));
  const span = b - a + 1;
  const dense = span > cols;
  // Dense: readings are grouped in blocks of a whole number of readings, lined
  // up on the capture count, so a block holds the same readings from one draw
  // to the next as the view moves. Its point sits at the block's middle.
  const per = dense ? Math.ceil(span / cols) : 1;
  const max = dense ? Math.ceil(span / per) + 2 : span;
  // gaps: [start x, end x] of each run of missing readings in view, from the
  // reading before it to the reading after it.
  // count/sum: every reading in view, for the exact average.
  const out = { n:0, dense:dense, x:new Float32Array(max), brk:new Uint8Array(max), gaps:[],
    count:0, sumV:0, sumI:0, sumP:0 };
  ['v', 'i', 'p'].forEach((m) => {
    out[m] = new Float32Array(max); out[m + 'Lo'] = new Float32Array(max); out[m + 'Hi'] = new Float32Array(max);
  });
  const xScale = b > a ? gw / (b - a) : 0;
  const V = h.v, I = h.ma;
  const avg = Math.max(1, graphAveraging);
  const bv = new Float64Array(avg), bi = new Float64Array(avg), bp = new Float64Array(avg);
  let nb = 0, at = 0, sv = 0, si = 0, sp = 0, gap = true, gapFrom = -1;
  let offAt = 0;  // the first off range that hasn't ended before c
  let blk = -1, cn = 0, cv = 0, ci = 0, cp = 0, lastBlk = -2;
  let vLo = 0, vHi = 0, iLo = 0, iHi = 0, pLo = 0, pHi = 0;
  const flush = () => {
    if (!cn) return;
    const k = out.n++;
    out.x[k] = (blk * per + (per - 1) / 2 - a) * xScale;
    out.v[k] = cv / cn; out.i[k] = ci / cn; out.p[k] = cp / cn;
    out.vLo[k] = vLo; out.vHi[k] = vHi; out.iLo[k] = iLo; out.iHi[k] = iHi; out.pLo[k] = pLo; out.pHi[k] = pHi;
    out.brk[k] = blk !== lastBlk + 1 ? 1 : 0;
    lastBlk = blk;
    cn = 0; cv = 0; ci = 0; cp = 0;
  };
  // Dense: start at the first reading of the block the view starts in, so the
  // left-most block is complete too.
  const from = dense ? Math.floor(a / per) * per : a;
  for (let c = Math.max(h.lo, from - avg + 1); c <= b; c++) {
    const k = c % HIST_SLOTS;
    const v = V[k];
    if (v !== v) {
      nb = 0; at = 0; sv = 0; si = 0; sp = 0; gap = true;
      while (off && offAt < off.length && off[offAt][1] < c) offAt++;
      const portOff = off && offAt < off.length && off[offAt][0] <= c;
      if (portOff) {
        // Switched off: not missing data. Close any missing run before it.
        if (gapFrom >= 0) { out.gaps.push([Math.max(0, (gapFrom - 1 - a) * xScale), (c - a) * xScale]); gapFrom = -1; }
      } else if (c >= a && gapFrom < 0) gapFrom = c;
      continue;
    }
    if (gapFrom >= 0) { out.gaps.push([Math.max(0, (gapFrom - 1 - a) * xScale), (c - a) * xScale]); gapFrom = -1; }
    const i = I[k], p = v * i;
    let mv = v, mi = i, mp = p;
    if (avg > 1) {
      if (nb === avg) { sv -= bv[at]; si -= bi[at]; sp -= bp[at]; } else nb++;
      bv[at] = v; bi[at] = i; bp[at] = p;
      sv += v; si += i; sp += p;
      if (++at === avg) at = 0;
      mv = sv / nb; mi = si / nb; mp = sp / nb;
    }
    if (c < from) continue;
    if (c >= a) { out.count++; out.sumV += mv; out.sumI += mi; out.sumP += mp; }
    if (dense) {
      const bb = Math.floor(c / per);
      if (bb !== blk) {
        flush();
        blk = bb;
        vLo = vHi = mv; iLo = iHi = mi; pLo = pHi = mp;
      } else {
        if (mv < vLo) vLo = mv; else if (mv > vHi) vHi = mv;
        if (mi < iLo) iLo = mi; else if (mi > iHi) iHi = mi;
        if (mp < pLo) pLo = mp; else if (mp > pHi) pHi = mp;
      }
      cn++; cv += mv; ci += mi; cp += mp;
    } else {
      const k2 = out.n++;
      out.x[k2] = (c - a) * xScale;
      out.v[k2] = mv; out.i[k2] = mi; out.p[k2] = mp;
      out.brk[k2] = gap ? 1 : 0;
    }
    gap = false;
  }
  if (dense) flush();
  if (gapFrom >= 0) out.gaps.push([Math.max(0, (gapFrom - 1 - a) * xScale), (b - a) * xScale]);
  return out;
}
// One metric's value at a capture count, averaged the same way as the graph.
// Null when there's no reading there.
function graphValueAt(h, c) {
  if (!h || c < h.lo || c > h.hi) return null;
  const k = c % HIST_SLOTS;
  if (h.v[k] !== h.v[k]) return null;
  const avg = Math.max(1, graphAveraging);
  let n = 0, v = 0, i = 0, p = 0;
  for (let x = c; x >= h.lo && n < avg; x--) {
    const kk = x % HIST_SLOTS;
    if (h.v[kk] !== h.v[kk]) break;
    v += h.v[kk]; i += h.ma[kk]; p += h.v[kk] * h.ma[kk]; n++;
  }
  return { volts:v / n, ma:i / n, mw:p / n };
}
// The time of a capture count: the browser's clock when the newest reading
// arrived, less 10 ms per reading back from it.
function graphClockLabel(h, c) {
  const t = new Date(h.wallAt - (h.hi - c) * 1000 / HIST_HZ);
  const two = (n) => String(n).padStart(2, '0');
  return two(t.getHours()) + ':' + two(t.getMinutes()) + ':' + two(t.getSeconds()) + '.' + two(Math.floor(t.getMilliseconds() / 10));
}
// Render the large graph: scales, grid, the selected metrics, events, the
// zoom selection, the cursor and its values, then the brush below.
function drawGraph() {
  const c = $('graph-canvas');
  if (!c || !graphPort) return;
  const h = hist[graphPort];
  const host = $('port-graph');
  if (host) host.classList.toggle('off', !portEnabled(graphPort));
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(320, c.clientWidth), ht = Math.max(160, c.clientHeight);
  c.width = Math.round(w * dpr); c.height = Math.round(ht * dpr);
  const ctx = c.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const cs = getComputedStyle(document.body);
  const text = cs.getPropertyValue('--text-muted').trim();
  const grid = cs.getPropertyValue('--border-soft').trim();
  ctx.fillStyle = cs.getPropertyValue('--bg').trim(); ctx.fillRect(0, 0, w, ht);
  const series = graphMetrics().map((m) => ({k:m}));
  const pad = graphPad();
  const gw = w - pad.l - pad.r, gh = ht - pad.t - pad.b;
  const pp = { pad:pad, gw:gw, gh:gh };
  const v = graphView(h);
  updateGraphBrush(h, v);
  // While the zoom follows the newest reading, the cursor moves with it, staying
  // at the same place on the plot. Otherwise it stays on its reading, and goes
  // to the middle if the view moves away from it.
  if (v && graphCursor !== null && graphZoom && graphZoom.head) graphCursor = v.b - graphCursorAge;
  if (v && (graphCursor === null || graphCursor < v.a || graphCursor > v.b)) graphCursor = Math.round((v.a + v.b) / 2);
  if (v) graphCursorAge = v.b - graphCursor;

  // Grid, and time labels at 5 points across the view.
  ctx.strokeStyle = grid; ctx.lineWidth = 1; ctx.fillStyle = text; ctx.font = '10px system-ui';
  for (let i = 0; i <= 4; i++) {
    const x = pad.l + gw * i / 4, y = pad.t + gh * i / 4;
    ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, pad.t + gh); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(pad.l + gw, y); ctx.stroke();
    if (v) {
      const label = graphAgeLabel((h.hi - (v.a + (v.b - v.a) * i / 4)) / HIST_HZ);
      const tw = ctx.measureText(label).width;
      ctx.fillText(label, Math.max(0, Math.min(w - tw, x - tw / 2)), ht - 7);
    }
  }
  const say = (msg) => ctx.fillText(msg, pad.l + 8, pad.t + 20);
  if (graphNoHistory) { say('No history on the hub (PSRAM unavailable)'); return; }
  if (!series.length) { say('Select a metric'); return; }
  if (!v) { say('Waiting for readings'); return; }

  // Each metric: the lowest-to-highest range of each pixel column, faint, then
  // the average line over it.
  const pts = graphPoints(h, v.a, v.b, gw, graphOffRanges(h, graphPort));
  // What's in view, per metric: lowest, highest and average. Shown in the
  // measurements, and used to fit a scale (double-click) and to centre scale
  // stretching (scroll).
  graphVisible = {};
  GRAPH_METRICS.forEach((key) => {
    const m = key === 'volts' ? 'v' : (key === 'ma' ? 'i' : 'p');
    let min = Infinity, max = -Infinity;
    for (let k = 0; k < pts.n; k++) {
      const lo = pts.dense ? pts[m + 'Lo'][k] : pts[m][k], hi = pts.dense ? pts[m + 'Hi'][k] : pts[m][k];
      if (lo < min) min = lo;
      if (hi > max) max = hi;
    }
    const sum = m === 'v' ? pts.sumV : (m === 'i' ? pts.sumI : pts.sumP);
    graphVisible[key] = { min:min, max:max, avg:pts.count ? sum / pts.count : NaN };
  });
  updateGraphStats();
  // The hovered scale column gets a tint.
  if (graphScaleHover && series.some((s) => s.k === graphScaleHover)) {
    const idx = series.findIndex((s) => s.k === graphScaleHover);
    const x0 = series.length === 1 ? 0 : 2 + idx * 34;
    ctx.globalAlpha = 0.12;
    ctx.fillStyle = cs.getPropertyValue(metricColor(graphScaleHover)).trim();
    ctx.fillRect(x0, pad.t, series.length === 1 ? pad.l - 4 : 34, gh);
    ctx.globalAlpha = 1;
  }
  ctx.save();
  ctx.beginPath(); ctx.rect(pad.l, pad.t, gw, gh); ctx.clip();
  // Missing readings: a faint red background across each run, at least a pixel
  // wide, so a gap in the line reads as missing data. Time the port was
  // switched off isn't shaded.
  if (pts.gaps.length) {
    ctx.globalAlpha = 0.18;
    ctx.fillStyle = cs.getPropertyValue('--danger').trim();
    pts.gaps.forEach((g) => ctx.fillRect(pad.l + g[0], pad.t, Math.max(1, g[1] - g[0]), gh));
    ctx.globalAlpha = 1;
  }
  series.forEach((s) => {
    const range = metricRange(h, s.k);
    if (!range) return;
    const m = s.k === 'volts' ? 'v' : (s.k === 'ma' ? 'i' : 'p');
    const vals = pts[m], lo = pts[m + 'Lo'], hi = pts[m + 'Hi'];
    const scale = gh / (range.max - range.min);
    const yOf = (val) => pad.t + gh - (val - range.min) * scale;
    const color = cs.getPropertyValue(metricColor(s.k)).trim();
    ctx.strokeStyle = color;
    if (pts.dense) {
      ctx.globalAlpha = 0.35;
      ctx.lineWidth = Math.max(1, gw / Math.max(1, pts.n));
      ctx.beginPath();
      for (let k = 0; k < pts.n; k++) {
        const x = pad.l + pts.x[k];
        ctx.moveTo(x, yOf(hi[k]) - 0.5); ctx.lineTo(x, yOf(lo[k]) + 0.5);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let k = 0; k < pts.n; k++) {
      const x = pad.l + pts.x[k], y = yOf(vals[k]);
      if (pts.brk[k]) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    s.range = range;
  });

  // Events: a dashed line with an arrow and a label where each one happened.
  // Labels that would overlap stack downwards, each in the first row where it
  // fits, so close events stay readable.
  const evColor = cs.getPropertyValue('--warning').trim();
  ctx.strokeStyle = evColor; ctx.fillStyle = evColor; ctx.lineWidth = 1;
  const rowEnds = [];
  h.events.forEach((e) => {
    if (e.c < v.a || e.c > v.b) return;
    const x = graphXOf(v, e.c, pp);
    ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(x, pad.t + 6); ctx.lineTo(x, pad.t + gh); ctx.stroke();
    ctx.setLineDash([]);
    ctx.beginPath(); ctx.moveTo(x - 4, pad.t); ctx.lineTo(x + 4, pad.t); ctx.lineTo(x, pad.t + 6); ctx.closePath(); ctx.fill();
    const label = EVENT_LABEL[e.code] || ('Event ' + e.code);
    const tw = ctx.measureText(label).width;
    const lx = x + 6 + tw > pad.l + gw ? x - 6 - tw : x + 6;
    let row = rowEnds.findIndex((end) => lx > end + 4);
    if (row < 0) row = rowEnds.length;
    rowEnds[row] = lx + tw;
    ctx.fillText(label, lx, pad.t + 9 + row * 12);
  });

  // The span being dragged out to zoom to.
  const accent = cs.getPropertyValue('--accent').trim();
  if (graphDrag && graphDrag.kind === 'select' && graphDrag.dragging) {
    const x0 = Math.min(graphDrag.x0, graphDrag.x1), x1 = Math.max(graphDrag.x0, graphDrag.x1);
    ctx.globalAlpha = 0.18; ctx.fillStyle = accent; ctx.fillRect(x0, pad.t, x1 - x0, gh);
    ctx.globalAlpha = 1; ctx.strokeStyle = accent; ctx.strokeRect(x0 + 0.5, pad.t + 0.5, x1 - x0 - 1, gh - 1);
  }
  ctx.restore();

  // The cursor: a line with an arrow on top, and a box with its time and the
  // value of each metric shown.
  const fg = cs.getPropertyValue('--text').trim();
  const cx = Math.round(graphXOf(v, graphCursor, pp)) + 0.5;
  ctx.strokeStyle = fg; ctx.fillStyle = fg; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(cx, pad.t); ctx.lineTo(cx, pad.t + gh); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(cx - 6, pad.t - 10); ctx.lineTo(cx + 6, pad.t - 10); ctx.lineTo(cx, pad.t); ctx.closePath(); ctx.fill();
  const val = graphValueAt(h, graphCursor);
  // A dot on each metric's line where the cursor crosses it.
  if (val) {
    const bg = cs.getPropertyValue('--bg').trim();
    series.forEach((s) => {
      if (!s.range) return;
      const y = pad.t + gh - (val[s.k] - s.range.min) * gh / (s.range.max - s.range.min);
      if (y < pad.t || y > pad.t + gh) return;
      ctx.beginPath(); ctx.arc(cx, y, 3.5, 0, Math.PI * 2);
      ctx.fillStyle = cs.getPropertyValue(metricColor(s.k)).trim(); ctx.fill();
      ctx.lineWidth = 1.5; ctx.strokeStyle = bg; ctx.stroke();
    });
    ctx.lineWidth = 1;
  }
  const lines = [{ t:graphClockLabel(h, graphCursor) + '  ' + graphAgeLabel((h.hi - graphCursor) / HIST_HZ), c:fg }];
  series.forEach((s) => {
    const n = val ? val[s.k] : null;
    lines.push({ t:n === null ? 'no reading' : (s.k === 'volts' ? n.toFixed(3) : n.toFixed(1)) + ' ' + metricLabel(s.k),
      c:cs.getPropertyValue(metricColor(s.k)).trim() });
  });
  ctx.font = '11px system-ui';
  const bw = Math.max(...lines.map((l) => ctx.measureText(l.t).width)) + 12, bh = lines.length * 15 + 8;
  const bx = cx + 8 + bw > pad.l + gw ? cx - 8 - bw : cx + 8, by = pad.t + 4;
  ctx.fillStyle = cs.getPropertyValue('--surface').trim(); ctx.strokeStyle = cs.getPropertyValue('--border').trim();
  ctx.beginPath(); ctx.roundRect(bx + 0.5, by + 0.5, bw - 1, bh - 1, 4); ctx.fill(); ctx.stroke();
  lines.forEach((l, i) => { ctx.fillStyle = l.c; ctx.fillText(l.t, bx + 6, by + 16 + i * 15); });
  ctx.font = '10px system-ui';

  // Y scales.
  series.forEach((s, idx) => {
    const range = s.range;
    if (!range) return;
    const zero = s.k === 'volts' && graphCfg.vzero ? ' 0' : '';
    if (series.length === 1) {
      ctx.fillStyle = text;
      ctx.fillText(metricLabel(s.k) + zero, 6, pad.t + 10);
      for (let i = 0; i <= 4; i++)
        ctx.fillText(metricDigits(s.k, range.max - (range.max - range.min) * i / 4), 6, pad.t + gh * i / 4 + 3);
    } else {
      const x = 6 + idx * 34;
      ctx.fillStyle = cs.getPropertyValue(metricColor(s.k)).trim();
      ctx.fillText(metricLabel(s.k) + zero, x, pad.t + 10);
      ctx.fillText(metricDigits(s.k, range.max), x, pad.t + 23);
      ctx.fillText(metricDigits(s.k, (range.max + range.min) / 2), x, pad.t + gh / 2 + 3);
      ctx.fillText(metricDigits(s.k, range.min), x, pad.t + gh);
    }
  });
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

function beginPortNameEdit(el) {
  el.dataset.editing = '1';
  el.select();
}
function portNameKey(el, e) {
  e.stopPropagation();
  if (e.key === 'Enter') { e.preventDefault(); el.blur(); }
  if (e.key === 'Escape') {
    e.preventDefault();
    el.value = el.dataset.saved || '';
    delete el.dataset.editing;
    el.blur();
  }
}
// Rename a port in place. Blank names are allowed and restore the default "Port".
async function savePortName(n, el) {
  const name = el.value.trim().slice(0, 24);
  delete el.dataset.editing;
  if (name === (el.dataset.saved || '')) {
    el.value = name;
    return;
  }
  el.dataset.saving = '1';
  // Names are part of the "ports" settings group, which takes all six at
  // once, so the other five are sent back unchanged.
  const names = [1, 2, 3, 4, 5, 6].map((i) => {
    const p = lastPorts.find((x) => x.n === i);
    return (p && p.name) || '';
  });
  names[n - 1] = name;
  try {
    const r = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ports: { names } })
    });
    if (!r.ok) throw new Error('http ' + r.status);
    el.dataset.saved = name;
    delete el.dataset.saving;
    poll();   // show the name as the hub stored it
  } catch (e) {
    delete el.dataset.saving;
    setOffline(true);
  }
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
  // Ports goes first; the rest keep the hub's order.
  const shown = groups.filter((g) => (g.fields || []).length)
    .sort((a, b) => (b.name === 'ports') - (a.name === 'ports'));
  $('settings-panels').innerHTML = shown.length
    ? shown.map((g) =>
        '<section class="cfg-group" data-group="' + esc(g.name) + '">' +
          (hasPortRows(g)
            ? '<h4 class="has-marks">' + esc(g.label) +
              '<span class="port-marks">' + '<span></span>'.repeat(6) + '</span></h4>'
            : '<h4>' + esc(g.label) + '</h4>') +
          (g.fields || []).filter((f) => f.type !== 'hidden').map((f) => cfgRow(g, f)).join('') +
        '</section>').join('')
    : '<p class="sched-empty">No settings.</p>';
  document.querySelectorAll('.cfg-group').forEach(updatePortMarks);
}

// True when a group has per-port rows that share a key (one choice per port).
const hasPortRows = (g) => (g.fields || []).some((f) => f.type === 'port_bools' && f.value);

// Put "M" in the heading above each port that's on in none of the group's
// rows, blank above the rest.
function updatePortMarks(sec) {
  const marks = sec.querySelector('.port-marks');
  if (!marks) return;
  const rows = [...sec.querySelectorAll('.port-bools')].filter((r) => r.dataset.value);
  [...marks.children].forEach((m, i) => {
    m.textContent = rows.some((r) => r.children[i].dataset.bool === '1') ? '' : 'M';
  });
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
  } else if (f.type === 'port_bools') {
    // One numbered on/off button per port; the row saves as an array of six.
    const a = Array.isArray(v) ? v : [];
    ctl = '<span class="port-bools" id="' + id + (f.value ? '-' + esc(f.value) : '') + '" data-key="' + esc(f.key) + '"' +
      ' data-value="' + esc(f.value || '') + '" data-ports="1">' +
      [1, 2, 3, 4, 5, 6].map((n) => {
        const on = f.value ? a[n - 1] === f.value : !!a[n - 1];
        return '<button type="button" class="bool-btn port-bool' + (on ? ' on' : '') + '"' +
          ' data-bool="' + (on ? 1 : 0) + '" data-on="' + n + '" data-off="' + n + '"' +
          ' title="Port ' + n + '" onclick="flipPortBool(this)">' + n + '</button>';
      }).join('') + '</span>';
  } else if (f.type === 'choice') {
    const options = Array.isArray(f.options) ? f.options : [];
    const current = String(v);
    const numeric = options.every((x) => /^-?\d+(\.\d+)?$/.test(String(x)));
    ctl = '<span class="choice-seg" id="' + id + '" data-key="' + esc(f.key) + '"' +
      ' data-choice="' + (numeric ? 'number' : 'string') + '" data-value="' + esc(current) + '">' +
      options.map((opt) => {
        const value = String(opt);
        const on = value === current;
        return '<button type="button" class="' + (on ? 'on' : '') + '" data-value="' + esc(value) + '"' +
          ' onclick="chooseSetting(this)">' + esc(value) + '</button>';
      }).join('') + '</span>';
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

// A per-port button was clicked: flip it. Rows that share a key are one
// choice per port, so turning a port on turns the same port off in the others.
function flipPortBool(btn) {
  flipBool(btn);
  const row = btn.parentElement;
  const sec = row.closest('.cfg-group');
  if (btn.dataset.bool === '1' && row.dataset.value) {
    const i = [...row.children].indexOf(btn);
    sec.querySelectorAll('.port-bools').forEach((r) => {
      if (r !== row && r.dataset.key === row.dataset.key && r.children[i].dataset.bool === '1') flipBool(r.children[i]);
    });
  }
  updatePortMarks(sec);
}

// A segmented choice was clicked: select one value in the group.
function chooseSetting(btn) {
  const seg = btn.parentElement;
  seg.dataset.value = btn.dataset.value;
  [...seg.children].forEach((b) => b.classList.toggle('on', b === btn));
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
      if (el.dataset.ports !== undefined) {
        const bools = [...el.querySelectorAll('.port-bool')].map((b) => b.dataset.bool === '1');
        if (el.dataset.value) {
          // Rows sharing a key: each port takes the value of the row it's on
          // in, or "manual" if it's on in none.
          out[k] = out[k] || bools.map(() => 'manual');
          bools.forEach((on, i) => { if (on) out[k][i] = el.dataset.value; });
        } else {
          out[k] = bools;
        }
      }
      else if (el.dataset.bool !== undefined) out[k] = el.dataset.bool === '1';
      else if (el.dataset.choice !== undefined) {
        out[k] = el.dataset.choice === 'number' ? parseFloat(el.dataset.value) : el.dataset.value;
      }
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
// Send the graph preferences to the hub's profiler settings. The hub saves them
// to flash once they've gone unchanged for a few seconds.
function saveProfilerPrefs() {
  if (!uiPrefsLoaded) return;
  const body = {
    mini_metric: miniMetric,
    graph_metric: graphCfg.metric,
    graph_voltage_zero: graphCfg.vzero
  };
  fetch('/api/settings', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ profiler: body }) }).catch(() => {});
}
// Apply hidden graph UI preferences from the first /api/status response.
function applyUiPrefs(ui) {
  if (uiPrefsLoaded || !ui) return;
  graphCfg.metric = normalizeGraphMetric(ui.graph_metric);
  graphCfg.vzero = !!ui.graph_voltage_zero;
  miniMetric = ['ma','mw','volts'].includes(ui.mini_metric) ? ui.mini_metric : miniMetric;
  uiPrefsLoaded = true;
}
// Clicking a mini graph cycles its displayed metric: mA, mW, V.
function cycleMiniMetric() {
  const order = ['ma', 'mw', 'volts'];
  miniMetric = order[(order.indexOf(miniMetric) + 1) % order.length];
  drawMiniGraphs();
  saveProfilerPrefs();
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
      '<div class="sock-head"><span class="sock-num">' + i + '</span>' +
        '<input class="sock-name" type="text" maxlength="24" placeholder="Port" title="Rename port"' +
          ' onclick="event.stopPropagation()" onfocus="beginPortNameEdit(this)"' +
          ' onkeydown="portNameKey(this,event)" onblur="savePortName(' + i + ',this)"></div>' +
      '<div class="slot"></div>' +
      '<div class="sock-state">--</div>' +
      '<div class="port-toggle" onclick="event.stopPropagation(); togglePower(' + i + ')"><span class="toggle-knob"></span></div>' +
      '<div class="sock-read">' +
        '<div class="sock-amps"><span class="mma">--</span> <small>mA</small></div>' +
        '<div class="sock-sub"><span class="mv">--</span> &middot; <span class="mmw">--</span></div>' +
      '</div>' +
      '<div class="mini-graph" onclick="event.stopPropagation(); cycleMiniMetric()" title="Cycle mini graph metric"><canvas class="mini-canvas"></canvas></div>' +
      '<div class="sock-sched">--</div>';
    host.appendChild(c);
  }
  built = true;
}

// What each LED state means, in words, under the port number.
const STATE_LABEL = { off:'Off', blue:'No power', green:'Active', pulse:'No device', fault:'Fault', pink:'No 5V', pinkpulse:'No 5V' };
// States whose slot colour is animated in JS rather than set by CSS.
const ANIMATED = ['pulse', 'fault', 'pinkpulse'];

// Render one compact trend line inside a port card: the port's newest 10
// seconds of readings.
function drawMiniGraph(n) {
  const c = document.querySelector('#port-' + n + ' .mini-canvas');
  const off = !portEnabled(n);
  const h = hist[n];
  const pts = [];
  if (off) pts.push(0, 0);
  else if (h && h.hi >= 0) {
    for (let k, cnt = Math.max(h.lo, h.hi - MINI_READINGS + 1); cnt <= h.hi; cnt++) {
      k = cnt % HIST_SLOTS;
      const v = h.v[k];
      if (v !== v) continue;
      pts.push(miniMetric === 'volts' ? v : (miniMetric === 'ma' ? h.ma[k] : v * h.ma[k]));
    }
  }
  if (!c || pts.length < 2) return;
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(80, c.clientWidth), ht = Math.max(28, c.clientHeight);
  c.width = Math.round(w * dpr); c.height = Math.round(ht * dpr);
  const ctx = c.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, ht);
  const cs = getComputedStyle(document.body);
  const line = off ? cs.getPropertyValue('--text-muted').trim() : cs.getPropertyValue(metricColor(miniMetric)).trim();
  const grid = cs.getPropertyValue('--border-soft').trim();
  let min = Infinity, max = -Infinity;
  pts.forEach((v) => {
    min = Math.min(min, v);
    max = Math.max(max, v);
  });
  if (off) { min = 0; max = miniMetric === 'volts' ? 5 : 1; }
  if (min >= 0) min = 0;
  // A minimum top, so a tiny reading (a fraction of a mA or mW) draws as a low
  // line instead of being stretched to the full height.
  max = Math.max(max, MINI_MIN_TOP[miniMetric]);
  if (min === max) {
    const pad = miniMetric === 'volts' ? 0.05 : Math.max(1, Math.abs(max) * 0.1);
    min -= pad; max += pad;
    if (min < 0) min = 0;
  }
  const step = niceStep((max - min) / 4);
  min = Math.max(0, Math.floor(min / step) * step);
  max = Math.ceil(max / step) * step;
  if (min === max) max = min + step;
  ctx.strokeStyle = grid; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, ht - 1); ctx.lineTo(w, ht - 1); ctx.stroke();
  // The line is drawn below the labels' row, so it never runs through them.
  const top = MINI_LABEL_H, bottom = ht - 2;
  ctx.strokeStyle = line; ctx.lineWidth = 1.5; ctx.beginPath();
  pts.forEach((v, i) => {
    const x = w * i / (pts.length - 1);
    const y = bottom - (bottom - top) * ((v - min) / (max - min));
    if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
  });
  ctx.stroke();
  ctx.fillStyle = line;
  ctx.font = '9px system-ui';
  ctx.fillText(metricLabel(miniMetric), 3, 9);
  ctx.textAlign = 'right';
  ctx.fillText(metricDigits(miniMetric, max), w - 3, 9);
  ctx.textAlign = 'left';
}
// Redraw every visible mini graph.
function drawMiniGraphs() {
  for (let n = 1; n <= 6; n++) drawMiniGraph(n);
}

// Format a sidebar environment graph scale value.
function envDigits(key, v) {
  return key === 'temp' ? v.toFixed(0) + 'C' : v.toFixed(0) + '%';
}
// Sidebar environment live value shown beside its lane label.
function envValue(key) {
  if (!lastEnv) return '--';
  return key === 'temp' ? lastEnv.temp.toFixed(1) + 'C' : lastEnv.humid.toFixed(0) + '%';
}
// Integer y-axis bounds for a sidebar environment lane, with a useful minimum span.
function envRange(pts, key) {
  let min = Infinity, max = -Infinity;
  pts.forEach((p) => {
    const v = p[key];
    if (isFinite(v)) { min = Math.min(min, v); max = Math.max(max, v); }
  });
  if (!isFinite(min) || !isFinite(max)) return null;
  const span = 5;
  let lo = Math.floor(min);
  let hi = Math.ceil(max);
  if (hi - lo < span) {
    const mid = (min + max) / 2;
    lo = Math.floor(mid - span / 2);
    hi = lo + span;
    if (hi < max) { hi = Math.ceil(max); lo = hi - span; }
    if (lo > min) { lo = Math.floor(min); hi = lo + span; }
  }
  if (key === 'humid') {
    if (lo < 0) { hi -= lo; lo = 0; }
    if (hi > 100) { lo -= hi - 100; hi = 100; }
    if (lo < 0) lo = 0;
  }
  return { min:lo, max:hi };
}
// Compact label for the environment graph's visible time range.
function envTimeLabel(ms) {
  const s = Math.round(ms / 1000);
  return s >= 60 ? Math.round(s / 60) + 'm' : s + 's';
}
// Reticule tick spacing for the environment graph's time window.
function envTickMs() {
  if (ENV_WINDOW_MS <= 90000) return 15000;
  if (ENV_WINDOW_MS <= 300000) return 60000;
  return 300000;
}
// Draw the environment graph reticule, with time ticks sliding under the data.
function drawEnvReticule(ctx, w, h, now, color) {
  const tick = envTickMs();
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.globalAlpha = 0.45;
  ctx.lineWidth = 1;
  for (let lane = 0; lane < 2; lane++) {
    const y0 = lane * h / 2;
    for (let i = 1; i <= 2; i++) {
      const y = y0 + (h / 2) * i / 3;
      ctx.beginPath(); ctx.moveTo(0, Math.floor(y) + .5); ctx.lineTo(w, Math.floor(y) + .5); ctx.stroke();
    }
  }
  for (let t = now - (now % tick); t >= now - ENV_WINDOW_MS; t -= tick) {
    const x = w * (1 - (now - t) / ENV_WINDOW_MS);
    ctx.beginPath(); ctx.moveTo(Math.floor(x) + .5, 0); ctx.lineTo(Math.floor(x) + .5, h); ctx.stroke();
  }
  ctx.font = '9px system-ui';
  ctx.textAlign = 'left';
  ctx.fillText(envTimeLabel(ENV_WINDOW_MS), 3, h - 3);
  ctx.restore();
}
// Draw one lane of the sidebar temperature/humidity graph.
function drawEnvLane(ctx, pts, key, y0, h, w, now, color, label, labelColor, valueColor) {
  const range = envRange(pts, key);
  if (!range) return;
  const min = range.min, max = range.max;
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  pts.forEach((p, i) => {
    const age = Math.max(0, Math.min(ENV_WINDOW_MS, now - p.t));
    const x = w * (1 - age / ENV_WINDOW_MS);
    const pos = Math.max(0, Math.min(1, (p[key] - min) / (max - min)));
    const y = y0 + h - 2 - (h - 10) * pos;
    if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
  });
  ctx.stroke();
  ctx.font = '10.5px system-ui';
  ctx.textAlign = 'left';
  ctx.fillStyle = labelColor;
  ctx.fillText(label, 3, y0 + 9);
  const labelW = ctx.measureText(label + ' ').width;
  ctx.font = '10.5px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace';
  ctx.fillStyle = valueColor;
  ctx.fillText(envValue(key), 3 + labelW, y0 + 9);
  ctx.font = '9px system-ui';
  ctx.fillStyle = color;
  ctx.textAlign = 'right';
  ctx.fillText(envDigits(key, max), w - 3, y0 + 9);
  ctx.fillText(envDigits(key, min), w - 3, y0 + h - 3);
  ctx.textAlign = 'left';
}
// Add one status temperature/humidity sample to the sidebar graph.
function pushEnvPoint(temp) {
  lastEnv = temp && temp.valid ? { temp:temp.c, humid:temp.h } : null;
  if (!lastEnv) { drawEnvGraph(); return; }
  const now = Date.now();
  envHistory.push({ t: now, temp: temp.c, humid: temp.h });
  while (envHistory.length && now - envHistory[0].t > ENV_WINDOW_MS)
    envHistory.shift();
  drawEnvGraph();
}
// Fetch the hub's retained temperature/humidity samples after a page load.
async function loadEnvHistory() {
  if (envHistoryLoaded || envHistoryLoading) return;
  const requestId = ++envHistoryRequestSeq;
  envHistoryLoading = true;
  try {
    const r = await fetch('/api/environment/history', { cache: 'no-store' });
    if (!r.ok) throw new Error('http ' + r.status);
    const d = await r.json();
    if (requestId !== envHistoryRequestSeq)
      return;
    envHistoryLoaded = true;
    if (d.ok && Array.isArray(d.points)) {
      const clientNow = Date.now();
      envHistory.length = 0;
      d.points.forEach((p) => {
        if (p.length >= 3)
          envHistory.push({ t:clientNow + p[0], temp:p[1], humid:p[2] });
      });
      drawEnvGraph();
    }
  } catch (e) {
  } finally {
    if (requestId === envHistoryRequestSeq)
      envHistoryLoading = false;
  }
}
// Render the sidebar temperature and humidity mini graph.
function drawEnvGraph() {
  const c = $('env-canvas');
  if (!c) return;
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(120, c.clientWidth), h = Math.max(75, c.clientHeight);
  c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
  const ctx = c.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const cs = getComputedStyle(document.body);
  const grid = cs.getPropertyValue('--border-soft').trim();
  const reticule = cs.getPropertyValue('--border').trim();
  const text = cs.getPropertyValue('--text').trim();
  const muted = cs.getPropertyValue('--text-muted').trim();
  const blue = cs.getPropertyValue('--txt-blue').trim();
  const green = cs.getPropertyValue('--success').trim();
  ctx.strokeStyle = grid; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, Math.floor(h / 2) + .5); ctx.lineTo(w, Math.floor(h / 2) + .5); ctx.stroke();
  if (envHistory.length < 1) return;
  const now = Date.now();
  drawEnvReticule(ctx, w, h, now, reticule);
  drawEnvLane(ctx, envHistory, 'temp', 0, h / 2, w, now, blue, 'Temp', muted, text);
  drawEnvLane(ctx, envHistory, 'humid', h / 2, h / 2, w, now, green, 'Humidity', muted, text);
}

// Refresh one socket from its entry in the status reply.
function updatePort(p) {
  const c = $('port-' + p.n);
  if (!c) return;
  if (inflight.has(p.n)) return;   // waiting on this port's own switch result — hold
  c.className = 'socket st-' + p.state + (p.n === selectedPort ? ' selected' : '') + (p.n === graphPort ? ' graphing' : '');
  if (!ANIMATED.includes(p.state)) c.querySelector('.slot').style.background = '';
  const nameBtn = c.querySelector('.sock-name');
  const portName = (p.name || '').trim();
  if (!nameBtn.dataset.editing && !nameBtn.dataset.saving) {
    nameBtn.value = portName;
    nameBtn.dataset.saved = portName;
  }
  nameBtn.classList.toggle('named', !!portName);
  nameBtn.title = portName ? 'Rename ' + portName : 'Rename port';
  c.querySelector('.sock-state').textContent = STATE_LABEL[p.state] || p.state;
  c.querySelector('.port-toggle').className = 'port-toggle' + (p.enabled ? ' on' : '');

  const mon = p.mon !== false;      // false = this port's power monitor isn't fitted
  c.querySelector('.mma').textContent = mon ? p.ma.toFixed(1) : '--';
  c.querySelector('.mv').textContent  = mon ? p.v.toFixed(3) + ' V' : '-- V';
  c.querySelector('.mmw').textContent = mon ? p.mw.toFixed(1) + ' mW' : '-- mW';

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
  applyUiPrefs(d.ui);
  // The averaging setting, unless the graph's buttons changed it in the last
  // 2 s and the hub's reply may not show it yet.
  if (d.ui && [0, 2, 4, 8, 16].includes(+d.ui.averaging) && performance.now() - graphAveragingSetAt > 2000 &&
      +d.ui.averaging !== graphAveraging) {
    graphAveraging = +d.ui.averaging;
    if (graphPort) renderGraphPanel();
  }
  if (d.name && $('dev-name').textContent !== d.name) {   // the "Device name" setting
    $('dev-name').textContent = d.name;
    document.title = d.name;
  }
  if (d.fw) $('fw-version').textContent = d.fw;
  if (d.rst) $('rst-reason').textContent = d.rst;
  $('s-ip').textContent     = d.ip || '--';
  $('s-uptime').textContent = fmtUptime(d.uptime_s || 0);
  if (!envHistoryLoaded) loadEnvHistory();
  pushEnvPoint(d.temp);
  $('s-heap').textContent   = d.heap ? Math.round(d.heap/1024) + ' KB' : '--';
  $('s-maxblk').textContent = d.maxblk ? Math.round(d.maxblk/1024) + ' KB' : '--';
  if (d.total) {
    const a = d.total.ma, w = d.total.mw;
    $('s-total').textContent = (a >= 1000 ? (a/1000).toFixed(4) + ' A' : a.toFixed(1) + ' mA') +
                               ' / ' + (w >= 1000 ? (w/1000).toFixed(4) + ' W' : w.toFixed(1) + ' mW');
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
  if (selectedPort && $('detail-title')) $('detail-title').textContent = portTitle(selectedPort);
  if (graphPort && $('graph-title')) $('graph-title').textContent = portTitle(graphPort) + ' trends';
  startLiveLoop();
  renderNextChip();
  refreshNextRow();
}

// Connection state. A few missed polls in a row means the hub is gone — say so
// loudly and block interaction rather than leaving a live-looking UI showing
// stale data that you can still click on.
let pollFails = 0;
let lastHubUptime = null;
function setOffline(on) {
  document.body.classList.toggle('offline', on);
  if (on) stopLiveLoop();
}

// Clear browser-side trend data after the hub reconnects or reboots. The port
// history is only cleared after a reboot: otherwise the hub still has it, and
// the live updates carry on from the newest reading the browser holds.
function resetTrendStateForReconnect(rebooted) {
  if (rebooted) {
    for (const n in hist) delete hist[n];
    graphZoom = null;
    graphLockedRanges = {};
    drawGraph();
    drawMiniGraphs();
  }
  envHistoryLoaded = false;
  envHistoryLoading = false;
  envHistoryRequestSeq++;
  envHistory.length = 0;
  drawEnvGraph();
}

// Fetch /api/status once a second. Three failures in a row = offline.
async function poll() {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), 4000);   // don't let a hung socket stall us
  try {
    const r = await fetch('/api/status', { cache: 'no-store', signal: ac.signal });
    if (!r.ok) throw new Error('http ' + r.status);
    const d = await r.json();
    const wasOffline = document.body.classList.contains('offline') || pollFails >= 3;
    const uptime = +d.uptime_s || 0;
    const rebooted = lastHubUptime !== null && uptime + 5 < lastHubUptime;
    if (wasOffline || rebooted)
      resetTrendStateForReconnect(rebooted);
    lastHubUptime = uptime;
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
window.addEventListener('resize', () => { drawGraph(); drawMiniGraphs(); drawEnvGraph(); });

try { if (localStorage.getItem('hubView') === 'history') viewMode = 'history'; } catch (e) {}
renderPortView();
loadSchedules();
poll();
setInterval(poll, 1000);
)js";
