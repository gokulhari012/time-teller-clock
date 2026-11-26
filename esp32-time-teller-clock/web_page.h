// Phone settings page, served by web_settings.ino at http://192.168.4.1
// Everything is in this one file (no internet on the hotspot, so no CDN files).
#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#1f6fb2">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="Time Teller">
<title>Time Teller</title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><circle cx='16' cy='16' r='14' fill='%231f6fb2'/><path d='M16 8v8l6 4' stroke='white' stroke-width='3' fill='none' stroke-linecap='round'/></svg>">
<style>
:root{--bg:#f2f4f7;--card:#fff;--text:#1b2430;--muted:#667085;--line:#dde2ea;--accent:#1f6fb2;--on-text:#fff;--off:#e8ecf1;--warn:#b86e00;--danger:#c0392b}
@media (prefers-color-scheme:dark){:root{--bg:#0f1319;--card:#19202a;--text:#e6ebf2;--muted:#98a2b3;--line:#2b3441;--accent:#4c9be8;--on-text:#08111d;--off:#262e39;--warn:#e3a44a;--danger:#ef7b6e}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding-bottom:90px}
header{position:sticky;top:0;z-index:5;background:var(--accent);color:var(--on-text);padding:10px 16px}
header h1{margin:0;font-size:18px}
#clock{font-size:13px;opacity:.9;min-height:18px}
main{max-width:640px;margin:0 auto;padding:12px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
h2{font-size:16px;margin:0 0 8px}
h3{font-size:15px;margin:0 0 6px}
p{margin:6px 0}
.muted{color:var(--muted);font-size:13px}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:8px 0;border-top:1px solid var(--line)}
.row.first,.sched .row{border-top:0}
.lbl{margin:10px 0 6px;font-size:13px;color:var(--muted)}
button{font:inherit;border:1px solid var(--line);background:var(--off);color:var(--text);border-radius:8px;padding:9px 12px;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:var(--on-text)}
button.danger{color:var(--danger)}
.btns{display:flex;flex-wrap:wrap;gap:8px}
select,input[type=text],input[type=number],input[type=date],input[type=time],input[type=password]{font:inherit;padding:8px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--text);max-width:100%}
input[type=range]{flex:1;max-width:55%;accent-color:var(--accent)}
.sw{position:relative;width:48px;height:28px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:var(--off);border:1px solid var(--line);border-radius:28px;transition:.2s}
.sw span:before{content:"";position:absolute;width:22px;height:22px;left:2px;top:2px;background:#fff;border-radius:50%;transition:.2s;box-shadow:0 1px 2px rgba(0,0,0,.3)}
.sw input:checked+span{background:var(--accent);border-color:var(--accent)}
.sw input:checked+span:before{transform:translateX(20px)}
.tabs{display:flex;gap:6px;margin-bottom:8px}
.tabs button{flex:1;padding:8px 4px}
.tabs button.sel{background:var(--accent);border-color:var(--accent);color:var(--on-text)}
.grid{display:grid;grid-template-columns:58px repeat(4,1fr);gap:4px;margin-top:8px}
.hr{font-size:13px;color:var(--muted);display:flex;align-items:center;cursor:pointer}
.cell{height:34px;padding:0;font-size:12px;border-radius:6px;color:var(--muted)}
.cell.on{background:var(--accent);border-color:var(--accent);color:var(--on-text)}
.cell.warn{outline:2px dashed var(--warn);outline-offset:-4px}
.divider{grid-column:1/-1;font-size:12px;font-weight:600;color:var(--muted);padding-top:6px}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{padding:7px 11px;border-radius:18px}
.chip.on{background:var(--accent);border-color:var(--accent);color:var(--on-text)}
.sched{border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin-bottom:8px}
.sched.off{opacity:.6}
.sched .t{font-weight:600}
.editor{border-color:var(--accent)}
table{border-collapse:collapse;width:100%;font-size:13px}
td{padding:4px 6px;border-top:1px solid var(--line)}
tr.empty td{color:var(--warn)}
#bar{position:fixed;left:0;right:0;bottom:0;z-index:6;background:var(--card);border-top:1px solid var(--line);padding:12px 16px;display:none;justify-content:space-between;align-items:center;gap:10px;box-shadow:0 -2px 8px rgba(0,0,0,.08)}
#bar.show{display:flex}
#login{position:fixed;inset:0;z-index:20;background:var(--bg);display:none;align-items:center;justify-content:center;padding:16px}
#login.show{display:flex}
#login section{width:100%;max-width:340px}
#login input{width:100%}
#toast{position:fixed;left:50%;bottom:96px;transform:translateX(-50%);z-index:30;background:var(--text);color:var(--bg);padding:9px 14px;border-radius:8px;display:none;max-width:90%;text-align:center}
</style>
</head>
<body>
<header><h1>Time Teller</h1><div id="clock">Connecting...</div></header>
<main>

<section>
  <h2>Play</h2>
  <p id="playing" class="muted">-</p>
  <div class="btns">
    <button onclick="act('test')">Test song</button>
    <button onclick="act('announce')">Announce now</button>
    <button class="danger" onclick="act('stop')">Stop</button>
  </div>
</section>

<section>
  <h2>Date &amp; time</h2>
  <p class="muted">Clock time: <b id="devtime">-</b></p>
  <div class="btns" style="margin:8px 0"><button class="primary" onclick="setPhoneTime()">Set to this phone's time</button></div>
  <div class="btns"><input type="date" id="dDate"><input type="time" id="dTime" step="1"><button onclick="setManualTime()">Set</button></div>
</section>

<section>
  <h2>Times</h2>
  <div class="row first"><span>The clock uses</span><select id="program"><option value="0">Program 1</option><option value="1">Program 2</option><option value="2">Program 3</option></select></div>
  <div class="lbl">Program to edit</div>
  <div class="tabs" id="progTabs"></div>
  <div class="tabs" id="timeTabs"><button data-k="teller">Teller</button><button data-k="happy">Happy songs</button><button data-k="sunday">Sunday songs</button><button data-k="blessing">Blessing</button></div>
  <p class="muted" id="gridHelp"></p>
  <div class="btns" id="gridBtns"></div>
  <div class="grid" id="grid"></div>
  <p class="muted">Tap an hour to turn its four times ON or OFF together.</p>
  <h3 style="margin-top:14px">Specific minutes</h3>
  <p class="muted">Extra times in this program at any minute, e.g. 6:05 AM. Tap the switches to choose what plays then. The spoken time only exists for :00, :15, :30 and :45, so other minutes play without it.</p>
  <div id="exact"></div>
  <div class="btns" id="exactAdd"><input type="time" id="xTime" value="06:05"><button onclick="addExact()">+ Add time</button></div>
</section>

<section>
  <h2>Language</h2>
  <div class="row first"><span>Announcement language<br><span class="muted">all audio comes from this Language folder on the SD card</span></span><select id="language"><option value="0">Language 1</option><option value="1">Language 2</option><option value="2">Language 3</option></select></div>
</section>

<section>
  <h2>Sunday Silence</h2>
  <div class="row first"><span>Silence on Sundays</span><label class="sw"><input type="checkbox" id="silenceOn"><span></span></label></div>
  <div class="row"><span>From</span><select id="silenceStart"></select></div>
  <div class="row"><span>Until</span><select id="silenceEnd"></select></div>
  <p class="muted">On Sundays nothing plays from the start time up to the end time (the end time itself plays). Schedules are silenced too.</p>
</section>

<section>
  <h2>Monthly Songs</h2>
  <p class="muted">In a month that is ON, a song from monthly_songs plays instead of the happy or Sunday song.</p>
  <div class="chips" id="months"></div>
</section>

<section>
  <h2>Announcement</h2>
  <div class="row first"><span>Church name<br><span class="muted">plays before the quote</span></span><label class="sw"><input type="checkbox" id="churchName"><span></span></label></div>
  <div class="row"><span>Extra quotes<br><span class="muted">plays after the quote</span></span><label class="sw"><input type="checkbox" id="extraQuotes"><span></span></label></div>
</section>

<section>
  <h2>Startup screen</h2>
  <p class="muted">Shown on the LCD for 2 seconds each when the clock starts. Up to 16 characters.</p>
  <div class="row first"><span>Show mobile number</span><label class="sw"><input type="checkbox" id="showPhone"><span></span></label></div>
  <div class="row"><span>Mobile number</span><input type="text" id="phone" maxlength="16" inputmode="tel"></div>
  <div class="row"><span>Name<br><span class="muted">shown after the number; empty = not shown</span></span><input type="text" id="startName" maxlength="16"></div>
</section>

<section>
  <h2>Sound</h2>
  <div class="row first"><span>Speaker output (relay)</span><label class="sw"><input type="checkbox" id="speaker"><span></span></label></div>
  <div class="row"><span>Morning volume: <b id="volMorningV"></b></span><input type="range" min="0" max="10" id="volMorning"></div>
  <div class="row"><span>Evening volume: <b id="volEveningV"></b></span><input type="range" min="0" max="10" id="volEvening"></div>
  <div class="row"><span>Morning volume from</span><select id="morningFrom"></select></div>
  <div class="row"><span>Evening volume from</span><select id="eveningFrom"></select></div>
</section>

<section>
  <h2>Schedules</h2>
  <p class="muted">Extra announcements at exact times, with a custom song after the song set for that time. They run whichever Program is used. Song and sub-folder numbers are in track-list.txt on the SD card, and are the same in every language.</p>
  <div id="scheds"></div>
  <div id="editor"></div>
  <button id="addSched" onclick="editSched(-1)">+ Add schedule</button>
</section>

<section>
  <h2>SD card</h2>
  <div id="folders" class="muted"></div>
  <div class="btns" style="margin-top:8px"><button onclick="act('rescan')">Rescan SD card</button></div>
  <p class="muted">Rescan after copying new songs onto the card.</p>
</section>

<section>
  <h2>Clock health</h2>
  <p id="health" class="muted">-</p>
  <p class="muted">Free memory should stay about the same from hour to hour. If the clock restarts by itself, <i>Running for</i> starts again from 0 and <i>Last start</i> says why: <i>low power</i> means the 5 V supply dips, <i>crash</i> or <i>watchdog</i> means a program fault. The daily restart at 3:55 AM shows as <i>restart</i>, and a restart from a computer over the USB cable as <i>USB</i>.</p>
</section>

</main>

<div id="bar"><span>Unsaved changes</span><div class="btns"><button onclick="undo()">Undo</button><button class="primary" onclick="save()">Save</button></div></div>

<div id="login"><section>
  <h2>Time Teller</h2>
  <p class="muted">Enter the settings password.</p>
  <input type="password" id="pw" onkeydown="if(event.key==='Enter')login()">
  <div class="btns" style="margin-top:10px"><button class="primary" onclick="login()">Open settings</button></div>
  <p class="muted" id="loginMsg"></p>
</section></div>

<div id="toast"></div>

<script>
const $ = id => document.getElementById(id);
const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
// SD card folders: each language has 33 (Language_1 = 01-33, Language_2 = 34-66, Language_3 = 67-99)
const FOLDERS = {1: 'Rythem', 2: 'Wishing', 3: 'Time', 4: 'Date', 5: 'Month', 6: 'Day', 7: 'church_name',
  8: 'Quotes', 9: 'extra_quotes', 10: 'happy_songs_morning', 11: 'happy_songs_evening', 12: 'sunday_songs',
  13: 'monthly_songs', 14: 'Custom_songs', 15: 'Test song', 16: 'Year', 17: 'Dongle', 18: 'Blessing_prayer',
  19: 'happy_songs_early_morning', 20: 'happy_songs_night'};
const folderName = f => {
  const n = (f - 1) % 33 + 1;
  return 'Language_' + (Math.floor((f - 1) / 33) + 1) + ' / ' + (FOLDERS[n] || 'Custom_songs sub-folder ' + (n - 20));
};
const HAPPY_PARTS = {0: 'Early morning songs (happy_songs_early_morning)', 6: 'Morning songs (happy_songs_morning)',
  12: 'Evening songs (happy_songs_evening)', 18: 'Night songs (happy_songs_night)'};
const HELP = {
  teller: 'The time teller runs at the blue times.',
  happy: 'Blue times end with a happy song: early morning songs from 12 AM, morning songs from 6 AM, evening songs from 12 PM and night songs from 6 PM. A dashed box means Teller is OFF at that time, so the song only plays if a schedule runs then.',
  sunday: 'On Sundays, blue times play a Sunday song instead of the happy song. A dashed box means Teller is OFF at that time.',
  blessing: 'The blessing prayer plays after the church name at the blue times. A dashed box means Teller is OFF at that time.'
};
// Specific minutes: what plays, as bits of f
const XFLAGS = [['Teller', 1], ['Happy song', 2], ['Sunday song', 4], ['Blessing', 8]];

let pw = '';
try { pw = localStorage.getItem('ttPassword') || ''; } catch (e) {}
let S = null;          // settings being edited
let scheds = [];       // schedules being edited
let dirtyS = false, dirtyK = false;
let tab = 'teller';
let prog = 0;          // Program being edited
let edit = null, draft = null;  // schedule editor: index (-1 = new) and its values
let offset = null;              // clock time minus phone time, in ms
let toastTimer;

const pad = n => String(n).padStart(2, '0');
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const timeText = (h, m) => (h % 12 || 12) + ':' + pad(m) + (h < 12 ? ' AM' : ' PM');
const slotLabel = i => timeText(Math.floor(i / 4), i % 4 * 15);

// 96 quarter-hours <-> 24 hex digits (slot 0 = lowest bit of the first byte)
function hexToBits(hex) {
  const bits = [];
  for (let i = 0; i < 12; i++) {
    const v = parseInt(hex.substr(i * 2, 2), 16);
    for (let j = 0; j < 8; j++) bits.push((v >> j & 1) === 1);
  }
  return bits;
}
function bitsToHex(bits) {
  let hex = '';
  for (let i = 0; i < 12; i++) {
    let v = 0;
    for (let j = 0; j < 8; j++) if (bits[i * 8 + j]) v |= 1 << j;
    hex += pad(v.toString(16));
  }
  return hex;
}

function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.style.display = 'none'; }, 2600);
}
function fail(e) { if (e.message) toast(e.message); }

async function api(path, data, timeout) {
  const opt = {headers: {Authorization: 'Basic ' + btoa('admin:' + pw)}};
  if (data) { opt.method = 'POST'; opt.body = new URLSearchParams(data); }
  if (timeout && window.AbortController) {  // give up on an answer that never comes (weak Wi-Fi)
    const ctl = new AbortController();
    opt.signal = ctl.signal;
    setTimeout(() => ctl.abort(), timeout);
  }
  let r;
  try { r = await fetch(path, opt); }
  catch (e) { throw new Error('Clock not reachable. Is this phone on the TimeTeller Wi-Fi?'); }
  if (r.status === 401) { showLogin(pw ? 'Wrong password' : ''); throw new Error(''); }
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || 'Error ' + r.status);
  return j;
}

function showLogin(msg) { $('login').classList.add('show'); $('loginMsg').textContent = msg; }
async function login() {
  pw = $('pw').value;
  try {
    await load();
    try { localStorage.setItem('ttPassword', pw); } catch (e) {}
  } catch (e) { fail(e); }
}

async function load() {
  const st = await api('/api/state');
  const s = st.settings;
  S = Object.assign({}, s, {programs: s.programs.map(p => ({
    teller: hexToBits(p.t), happy: hexToBits(p.h), sunday: hexToBits(p.s), blessing: hexToBits(p.b),
    exact: p.x ? p.x.split(',').map(e => ({h: +e.slice(0, 2), m: +e.slice(2, 4), f: parseInt(e[4], 16)})) : []
  }))});
  prog = S.program;
  scheds = st.schedules;
  dirtyS = dirtyK = false;
  edit = null;
  $('login').classList.remove('show');
  render();
  showStatus(st.status);
}
function undo() { load().catch(fail); }

function changed() { dirtyS = true; bar(); }
function bar() { $('bar').classList.toggle('show', dirtyS || dirtyK); }
function render() { renderGrid(); renderExact(); renderForm(); renderMonths(); renderScheds(); bar(); }

// ---- time grids ----
const P = () => S.programs[prog];  // the Program being edited
function renderGrid() {
  $('progTabs').innerHTML = [0, 1, 2].map(i => '<button class="' + (i === prog ? 'sel' : '') + '" onclick="pickProg(' + i + ')">Program ' +
    (i + 1) + (i === S.program ? ' &#10003;' : '') + '</button>').join('');
  document.querySelectorAll('#timeTabs button').forEach(b => b.classList.toggle('sel', b.dataset.k === tab));
  $('gridHelp').textContent = HELP[tab];
  $('gridBtns').innerHTML = '<button onclick="fill(0)">All ON</button><button onclick="fill(1)">All OFF</button>' +
    '<button onclick="fill(2)">Every hour</button>' + (tab === 'teller' ? '' : '<button onclick="fill(3)">Same as Teller</button>');
  const list = P()[tab], teller = P().teller;
  let html = '';
  for (let hr = 0; hr < 24; hr++) {
    if (tab === 'happy' && HAPPY_PARTS[hr]) html += '<div class="divider">' + HAPPY_PARTS[hr] + '</div>';
    html += '<div class="hr" onclick="row(' + hr + ')">' + (hr % 12 || 12) + (hr < 12 ? ' AM' : ' PM') + '</div>';
    for (let q = 0; q < 4; q++) {
      const i = hr * 4 + q;
      const warn = tab !== 'teller' && list[i] && !teller[i];
      html += '<button class="cell' + (list[i] ? ' on' : '') + (warn ? ' warn' : '') + '" onclick="cell(' + i + ')">:' + pad(q * 15) + '</button>';
    }
  }
  $('grid').innerHTML = html;
}
function pickProg(i) { prog = i; renderGrid(); renderExact(); }
function cell(i) { P()[tab][i] = !P()[tab][i]; changed(); renderGrid(); }
function row(hr) {
  const list = P()[tab];
  const allOn = [0, 1, 2, 3].every(q => list[hr * 4 + q]);
  for (let q = 0; q < 4; q++) list[hr * 4 + q] = !allOn;
  changed(); renderGrid();
}
function fill(mode) {  // 0 all ON, 1 all OFF, 2 every hour, 3 same as Teller
  const list = P()[tab];
  for (let i = 0; i < 96; i++) list[i] = mode === 0 ? true : mode === 1 ? false : mode === 2 ? i % 4 === 0 : P().teller[i];
  changed(); renderGrid();
}

// ---- specific minutes ----
function renderExact() {
  const x = P().exact;
  $('exact').innerHTML = x.length ? x.map((e, i) =>
    '<div class="sched"><div class="row"><span class="t">' + timeText(e.h, e.m) + '</span>' +
    '<button class="danger" onclick="delExact(' + i + ')">Delete</button></div><div class="chips">' +
    XFLAGS.map(([name, bit]) => chip(name, e.f & bit, 'flagExact(' + i + ',' + bit + ')')).join('') + '</div></div>'
  ).join('') : '<p class="muted">No specific minutes in Program ' + (prog + 1) + '.</p>';
  $('exactAdd').style.display = x.length >= 20 ? 'none' : '';
}
function addExact() {
  const t = $('xTime').value.split(':');
  if (t.length < 2) return toast('Pick a time');
  const h = +t[0], m = +t[1], x = P().exact;
  if (x.some(e => e.h === h && e.m === m)) return toast(timeText(h, m) + ' is already in the list');
  x.push({h: h, m: m, f: 1 | 2 | 8});  // Teller, happy song and blessing, like the default times
  x.sort((a, b) => a.h * 60 + a.m - (b.h * 60 + b.m));
  changed(); renderExact();
}
function delExact(i) { P().exact.splice(i, 1); changed(); renderExact(); }
function flagExact(i, bit) { P().exact[i].f ^= bit; changed(); renderExact(); }

// ---- switches, selects and sliders ----
const SWITCHES = ['silenceOn', 'churchName', 'extraQuotes', 'speaker', 'showPhone'];
const TEXTS = ['phone', 'startName'];
const cleanText = v => v.replace(/[^\x20-\x7e]|["\\]/g, '');  // what the LCD can show
const NUMBERS = ['silenceStart', 'silenceEnd', 'morningFrom', 'eveningFrom', 'volMorning', 'volEvening', 'program', 'language'];
function renderForm() {
  SWITCHES.forEach(k => { $(k).checked = !!S[k]; });
  NUMBERS.forEach(k => { $(k).value = S[k]; });
  TEXTS.forEach(k => { if (document.activeElement !== $(k)) $(k).value = S[k] || ''; });
  $('volMorningV').textContent = S.volMorning;
  $('volEveningV').textContent = S.volEvening;
}

function renderMonths() {
  $('months').innerHTML = MONTHS.map((m, i) =>
    '<button class="chip' + (S.months >> i & 1 ? ' on' : '') + '" onclick="month(' + i + ')">' + m + '</button>').join('');
}
function month(i) { S.months ^= 1 << i; changed(); renderMonths(); }

// ---- schedules ----
const daysText = d => d === 127 ? 'Every day' : DAYS.filter((_, i) => d >> i & 1).join(', ');
const monthsText = m => m === 4095 ? 'all months' : MONTHS.filter((_, i) => m >> i & 1).join(', ');
const songText = s => s.t == 1 ? 'then song ' + s.k + ' from Custom_songs' : s.t == 2 ? 'then a random song from Custom_songs sub-folder ' + s.k : 'no custom song';

function renderScheds() {
  $('scheds').innerHTML = scheds.length ? scheds.map((s, i) =>
    '<div class="sched' + (s.e ? '' : ' off') + '"><div class="row"><div><div class="t">' + esc(s.n) + ' &middot; ' + timeText(s.h, s.m) + '</div>' +
    '<div class="muted">' + daysText(s.d) + ' &middot; ' + monthsText(s.mo) + '<br>' + songText(s) + '</div></div>' +
    '<label class="sw"><input type="checkbox"' + (s.e ? ' checked' : '') + ' onchange="schedOn(' + i + ',this.checked)"><span></span></label></div>' +
    '<div class="btns"><button onclick="editSched(' + i + ')">Edit</button><button class="danger" onclick="delSched(' + i + ')">Delete</button></div></div>'
  ).join('') : '<p class="muted">No schedules.</p>';
  $('addSched').style.display = edit !== null || scheds.length >= 20 ? 'none' : '';
  renderEditor();
}
function schedOn(i, on) { scheds[i].e = on ? 1 : 0; dirtyK = true; bar(); renderScheds(); }
function delSched(i) {
  if (!confirm('Delete the schedule "' + scheds[i].n + '"?')) return;
  scheds.splice(i, 1);
  edit = null; dirtyK = true; bar(); renderScheds();
}
function editSched(i) {
  edit = i;
  draft = i < 0 ? {n: '', h: 9, m: 0, d: 127, mo: 4095, t: 0, k: 0, e: 1} : Object.assign({}, scheds[i]);
  renderScheds();
  $('editor').scrollIntoView({behavior: 'smooth', block: 'center'});
}
const chip = (text, on, fn) => '<button class="chip' + (on ? ' on' : '') + '" onclick="' + fn + '">' + text + '</button>';
function renderEditor() {
  const e = $('editor');
  if (edit === null) { e.innerHTML = ''; return; }
  const d = draft;
  e.innerHTML = '<div class="sched editor"><h3>' + (edit < 0 ? 'New schedule' : 'Edit schedule') + '</h3>' +
    '<div class="row"><span>Name</span><input type="text" id="eName" maxlength="16" value="' + esc(d.n) + '"></div>' +
    '<div class="row"><span>Time</span><input type="time" id="eTime" value="' + pad(d.h) + ':' + pad(d.m) + '"></div>' +
    '<div class="lbl">Days</div><div class="chips">' + DAYS.map((x, i) => chip(x, d.d >> i & 1, 'dayChip(' + i + ')')).join('') +
    chip('Every day', d.d === 127, 'dayChip(-1)') + '</div>' +
    '<div class="lbl">Months</div><div class="chips">' + MONTHS.map((x, i) => chip(x, d.mo >> i & 1, 'monthChip(' + i + ')')).join('') +
    chip('All', d.mo === 4095, 'monthChip(-1)') + '</div>' +
    '<div class="row"><span>Custom song</span><select id="eType" onchange="typeChanged()"><option value="0">None</option>' +
    '<option value="1">One song (Custom_songs)</option><option value="2">Random from a sub-folder</option></select></div>' +
    '<div class="row" id="eNumRow"><span id="eNumLabel"></span><input type="number" id="eNum" min="1" max="255" value="' + d.k + '"></div>' +
    '<div class="row"><span>Enabled</span><label class="sw"><input type="checkbox" id="eOn"' + (d.e ? ' checked' : '') + '><span></span></label></div>' +
    '<div class="btns"><button class="primary" onclick="applySched()">OK</button><button onclick="cancelEdit()">Cancel</button></div></div>';
  $('eType').value = d.t;
  showNumber();
}
function grab() {  // copy the editor inputs into draft
  draft.n = $('eName').value.replace(/[^\x20-\x7e]|["\\]/g, '').trim();
  const t = $('eTime').value.split(':');
  if (t.length >= 2) { draft.h = +t[0]; draft.m = +t[1]; }
  draft.t = +$('eType').value;
  draft.k = +$('eNum').value || 0;
  draft.e = $('eOn').checked ? 1 : 0;
}
function showNumber() {
  const t = +$('eType').value;
  $('eNumRow').style.display = t ? '' : 'none';
  $('eNumLabel').textContent = t === 1 ? 'Song number in Custom_songs' : 'Sub-folder number (1-13)';
}
function typeChanged() { grab(); showNumber(); }
function dayChip(i) { grab(); draft.d = i < 0 ? (draft.d === 127 ? 0 : 127) : draft.d ^ (1 << i); renderEditor(); }
function monthChip(i) { grab(); draft.mo = i < 0 ? (draft.mo === 4095 ? 0 : 4095) : draft.mo ^ (1 << i); renderEditor(); }
function applySched() {
  grab();
  const d = draft;
  if (!d.n) return toast('Enter a name');
  if (!d.d) return toast('Pick at least one day');
  if (!d.mo) return toast('Pick at least one month');
  if (d.t === 1 && !(d.k >= 1 && d.k <= 255)) return toast('Song number must be 1-255');
  if (d.t === 2 && !(d.k >= 1 && d.k <= 13)) return toast('Sub-folder number must be 1-13');
  if (d.t === 0) d.k = 0;
  if (edit < 0) scheds.push(d); else scheds[edit] = d;
  edit = null; dirtyK = true; bar(); renderScheds();
}
function cancelEdit() { edit = null; renderScheds(); }

// ---- save ----
async function save() {
  if (edit !== null) return toast('Press OK or Cancel on the schedule first');
  try {
    if (dirtyS) {
      const d = {program: S.program, language: S.language};
      S.programs.forEach((p, i) => {
        d['p' + i + 't'] = bitsToHex(p.teller); d['p' + i + 'h'] = bitsToHex(p.happy);
        d['p' + i + 's'] = bitsToHex(p.sunday); d['p' + i + 'b'] = bitsToHex(p.blessing);
        d['p' + i + 'x'] = p.exact.map(e => pad(e.h) + pad(e.m) + e.f.toString(16)).join(',');
      });
      await api('/api/settings', Object.assign(d, {
        silenceOn: S.silenceOn ? 1 : 0, silenceStart: S.silenceStart, silenceEnd: S.silenceEnd, months: S.months,
        churchName: S.churchName ? 1 : 0, extraQuotes: S.extraQuotes ? 1 : 0, speaker: S.speaker ? 1 : 0,
        volMorning: S.volMorning, volEvening: S.volEvening, morningFrom: S.morningFrom, eveningFrom: S.eveningFrom,
        showPhone: S.showPhone ? 1 : 0, phone: S.phone.trim(), startName: S.startName.trim()
      }));
    }
    if (dirtyK) {
      const d = {count: scheds.length};
      scheds.forEach((s, i) => {
        d['n' + i] = s.n; d['h' + i] = s.h; d['m' + i] = s.m; d['d' + i] = s.d;
        d['mo' + i] = s.mo; d['t' + i] = s.t; d['k' + i] = s.k; d['e' + i] = s.e;
      });
      await api('/api/schedules', d);
    }
    await load();
    toast('Saved');
  } catch (e) { fail(e); }
}

// ---- buttons that act at once ----
async function act(name) {
  try {
    const r = await api('/api/' + name, {});
    if (r.status) showStatus(r.status);
    toast(r.message || 'Done');
    refresh();
  } catch (e) { fail(e); }
}

const stamp = d => d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + 'T' +
  pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
function setPhoneTime() { sendTime(stamp(new Date())); }
function setManualTime() {
  const d = $('dDate').value, t = $('dTime').value;
  if (!d || !t) return toast('Pick a date and a time');
  sendTime(d + 'T' + (t.length === 5 ? t + ':00' : t));
}
async function sendTime(t) {
  try { showStatus(await api('/api/time', {t: t})); toast('Clock set'); } catch (e) { fail(e); }
}

// ---- status ----
function showStatus(st) {
  const p = st.time.split(/[-T:]/).map(Number);
  offset = new Date(p[0], p[1] - 1, p[2], p[3], p[4], p[5]) - Date.now();
  tick();
  $('playing').textContent = !st.mp3 ? 'MP3 module not found' : st.playing ? 'Playing' + (st.label ? ': ' + st.label : '...') : 'Not playing';
  $('folders').innerHTML = st.folders.length ? '<table>' + st.folders.map(f =>
    '<tr' + (f[1] ? '' : ' class="empty"') + '><td>' + pad(f[0]) + '</td><td>' + folderName(f[0]) + '</td><td>' +
    (f[1] ? f[1] + ' files' : 'empty') + '</td></tr>').join('') + '</table>' : 'No folders counted (is the MP3 module connected?).';
  const kb = n => Math.round(n / 1024) + ' KB', h = Math.floor(st.uptime / 3600), m = Math.floor(st.uptime % 3600 / 60);
  $('health').innerHTML = 'Free memory: <b>' + kb(st.heap) + '</b> (lowest ' + kb(st.minHeap) + ')<br>' +
    'Running for: <b>' + (h ? h + ' h ' : '') + m + ' min</b><br>Last start: <b>' + esc(st.reset) + '</b>';
}
function tick() {
  if (offset === null) return;
  const d = new Date(Date.now() + offset);
  const text = DAYS[d.getDay()] + ' ' + d.getDate() + ' ' + MONTHS[d.getMonth()] + ' ' + d.getFullYear() + ', ' +
    timeText(d.getHours(), d.getMinutes()).replace(' ', ':' + pad(d.getSeconds()) + ' ');
  $('clock').textContent = text;
  $('devtime').textContent = text;
}
async function refresh() {
  if (!S) return;
  try { showStatus(await api('/api/status', null, 8000)); } catch (e) {}
}
// The status every 5 s: one request at a time, and none while the page is hidden
async function poll() {
  if (!document.hidden) await refresh();
  setTimeout(poll, 5000);
}

// ---- start ----
(function init() {
  const options = Array.from({length: 96}, (_, i) => '<option value="' + i + '">' + slotLabel(i) + '</option>').join('');
  ['silenceStart', 'silenceEnd', 'morningFrom', 'eveningFrom'].forEach(k => { $(k).innerHTML = options; });
  document.querySelectorAll('#timeTabs button').forEach(b => { b.onclick = () => { tab = b.dataset.k; renderGrid(); }; });
  SWITCHES.forEach(k => { $(k).onchange = e => { S[k] = e.target.checked ? 1 : 0; changed(); }; });
  NUMBERS.forEach(k => { $(k).oninput = e => { S[k] = +e.target.value; changed(); renderForm(); if (k === 'program') renderGrid(); }; });
  TEXTS.forEach(k => { $(k).oninput = e => { S[k] = cleanText(e.target.value); changed(); }; });
  const now = stamp(new Date());
  $('dDate').value = now.slice(0, 10);
  $('dTime').value = now.slice(11, 19);
  window.onbeforeunload = () => (dirtyS || dirtyK) ? true : undefined;
  setInterval(tick, 1000);
  setTimeout(poll, 5000);
  load().catch(fail);
})();
</script>
</body>
</html>
)rawliteral";
