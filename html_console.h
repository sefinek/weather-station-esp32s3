#pragma once

// Terminal-style console page (/console) - talks to the WebSocket console
// on port 81. Same commands as Serial/BLE, handled identically by
// console.cpp's consoleHandleLine() regardless of transport.
static const char CONSOLE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Konsola - Stacja Pogodowa</title>
<link rel="icon" href="data:,">
<meta name="color-scheme" content="dark">
<style>
:root{
  --mono:ui-monospace,'Cascadia Code','SF Mono',Consolas,'Liberation Mono',monospace;
  --bg:#08090b;--surface:#0c0e11;--border:#1c1f24;
  --text:#d6dbe1;--muted:#69707b;--dim:#3a3f47;
  --accent:#4dc3e0;--err:#f0605a;--warn:#d8a53d;--ok:#3fb964
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{font-family:var(--mono);background:var(--bg);color:var(--text);display:flex;flex-direction:column;overflow:hidden;font-size:.85rem}
header{flex:0 0 auto;display:flex;align-items:baseline;padding:9px 18px;background:var(--surface);border-bottom:1px solid var(--border)}
.title{display:flex;align-items:baseline;gap:6px;letter-spacing:.01em}
.title .host{color:var(--muted);text-decoration:none}
.title .host:hover{color:var(--accent)}
.title .sep{color:var(--dim)}
.title .app{color:var(--text);font-weight:600}
.term{flex:1 1 auto;min-height:0;display:flex;flex-direction:column;background:var(--bg)}
.out{flex:1;min-height:0;overflow-y:auto;padding:14px 18px;line-height:1.6;white-space:pre-wrap;word-break:break-word}
.out div{padding:1px 0}
.out .cmd{color:var(--text)}
.out .cmd .p{color:var(--accent)}
.out .log{color:var(--muted)}
.out .tag-err{color:var(--err)}
.out .tag-warn{color:var(--warn)}
.out .tag-ok{color:var(--ok)}
.in{flex:0 0 auto;display:flex;align-items:center;gap:8px;border-top:1px solid var(--border);background:var(--surface);padding:11px 18px}
.in .p{color:var(--accent)}
.inputline{position:relative;flex:1;display:flex;align-items:center;min-width:0}
.inputline .mirror{visibility:hidden;white-space:pre;position:absolute;left:0;top:0}
.inputline .cursor{position:absolute;top:50%;transform:translateY(-50%);width:2px;height:1.15em;background:var(--accent);opacity:.85;animation:blink 1.05s steps(1) infinite;pointer-events:none}
.inputline .cursor.idle{animation:none;opacity:0}
@keyframes blink{0%,49%{opacity:.85}50%,100%{opacity:0}}
.in input{flex:1;background:transparent;border:none;color:var(--text);font:inherit;caret-color:transparent;outline:none;position:relative;z-index:1}
.in input::placeholder{color:var(--dim)}
footer{flex:0 0 auto;display:flex;justify-content:space-between;padding:7px 18px;font-size:.72rem;color:var(--dim);background:var(--surface);border-top:1px solid var(--border)}
#fstatus{color:var(--warn)}
#fstatus.live{color:var(--ok)}
#fstatus.down{color:var(--err)}
#fstatus::before{content:"[ "}
#fstatus::after{content:" ]"}
.out::-webkit-scrollbar{width:8px}
.out::-webkit-scrollbar-track{background:transparent}
.out::-webkit-scrollbar-thumb{background:var(--border)}
.out::-webkit-scrollbar-thumb:hover{background:var(--dim)}
</style>
</head>
<body>
<header>
  <div class="title"><a class="host" href="/">weather-station</a><span class="sep">/</span><span class="app">konsola</span></div>
</header>
<div class="term">
  <div class="out" id="out"></div>
  <div class="in">
    <span class="p">&#10095;</span>
    <div class="inputline">
      <span class="mirror" id="mirror"></span>
      <span class="cursor" id="cursor"></span>
      <input id="in" autofocus autocomplete="off" spellcheck="false" placeholder="help">
    </div>
  </div>
</div>
<footer>
  <span>&uarr;/&darr; historia &middot; Tab uzupełnianie &middot; Ctrl+L wyczyść &middot; Ctrl+C anuluj</span>
  <span id="fstatus">ŁĄCZENIE</span>
</footer>
<script>
const out = document.getElementById('out');
const input = document.getElementById('in');
const mirror = document.getElementById('mirror');
const cursor = document.getElementById('cursor');
const fstatus = document.getElementById('fstatus');

const MAX_LINES = 1000;

const classify = text => {
  if (/\bERROR\b/.test(text)) return 'log tag-err';
  if (/\bWARN\b/.test(text)) return 'log tag-warn';
  if (/\bOK\b/.test(text)) return 'log tag-ok';
  return 'log';
};

const appendLine = line => {
  const atBottom = out.scrollHeight - out.scrollTop - out.clientHeight < 40;
  out.appendChild(line);
  while (out.childNodes.length > MAX_LINES) out.removeChild(out.firstChild);
  if (atBottom) out.scrollTop = out.scrollHeight;
};

const append = (cls, text) => {
  const line = document.createElement('div');
  line.className = cls;
  line.textContent = text;
  appendLine(line);
};

const appendCmd = text => {
  const line = document.createElement('div');
  line.className = 'cmd';
  line.innerHTML = '<span class="p">&#10095;</span> ';
  line.appendChild(document.createTextNode(text));
  appendLine(line);
};

const clearScreen = () => { out.innerHTML = ''; };

const updateCursor = () => {
  mirror.textContent = input.value.slice(0, input.selectionStart);
  cursor.style.left = mirror.offsetWidth + 'px';
  cursor.classList.remove('idle');
  cursor.style.animation = 'none';
  void cursor.offsetWidth;
  cursor.style.animation = '';
};

['input', 'keyup', 'click', 'focus'].forEach(ev => input.addEventListener(ev, updateCursor));
input.addEventListener('blur', () => cursor.classList.add('idle'));
updateCursor();

let ws = null;
let wsCarry = '';
let cmdQueue = [];
let reconnectDelay = 1000;

const handleWsData = data => {
  const lines = (wsCarry + data).split('\n');
  wsCarry = lines.pop();
  lines.forEach(l => { if (l) append(classify(l), l); });
};

const connectWs = () => {
  ws = new WebSocket(`ws://${location.hostname}:81/`);
  ws.onopen = () => {
    fstatus.className = 'live';
    fstatus.textContent = 'POŁĄCZONO';
    reconnectDelay = 1000;
    if (cmdQueue.length) {
      cmdQueue.forEach(c => ws.send(c));
      cmdQueue = [];
    }
  };
  ws.onclose = () => {
    fstatus.className = 'down';
    fstatus.textContent = 'ROZŁĄCZONO - PONAWIANIE';
    setTimeout(connectWs, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 10000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = e => handleWsData(e.data);
};

let history = [];
let historyIndex = 0;
let draft = '';

const runCmd = cmd => {
  appendCmd(cmd);
  history.push(cmd);
  historyIndex = history.length;

  if (cmd.toLowerCase() === 'clear' || cmd.toLowerCase() === 'cls') {
    clearScreen();
    return;
  }

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(cmd);
  } else {
    cmdQueue.push(cmd);
    append('log', 'Brak połączenia - komenda w kolejce, zostanie wysłana po ponownym połączeniu.');
  }
};

const COMMANDS = [
  'help', 'status', 'net', 'reconnect', 'reboot', 'i2c', 'gpio ', 'wifiscan',
  'mem', 'nvs', 'nvs reset', 'beep', 'melody ok', 'melody error',
  'version', 'login ', 'clear',
];
let tabBase = null;
let tabMatches = [];
let tabIndex = 0;

input.addEventListener('keydown', e => {
  if (e.ctrlKey && e.key.toLowerCase() === 'l') {
    e.preventDefault();
    clearScreen();
    return;
  }

  if (e.ctrlKey && e.key.toLowerCase() === 'c') {
    e.preventDefault();
    appendCmd(input.value + '^C');
    input.value = '';
    historyIndex = history.length;
    updateCursor();
    return;
  }

  if (e.key === 'Enter') {
    const cmd = input.value.trim();
    input.value = '';
    updateCursor();
    if (cmd) runCmd(cmd);
    return;
  }

  if (e.key === 'ArrowUp') {
    e.preventDefault();
    if (historyIndex === history.length) draft = input.value;
    if (historyIndex > 0) {
      historyIndex--;
      input.value = history[historyIndex];
      input.setSelectionRange(input.value.length, input.value.length);
      updateCursor();
    }
    return;
  }

  if (e.key === 'ArrowDown') {
    e.preventDefault();
    if (historyIndex < history.length) {
      historyIndex++;
      input.value = historyIndex === history.length ? draft : history[historyIndex];
      input.setSelectionRange(input.value.length, input.value.length);
      updateCursor();
    }
    return;
  }

  if (e.key === 'Tab') {
    e.preventDefault();
    if (input.value !== tabBase) {
      tabBase = input.value;
      tabMatches = tabBase ? COMMANDS.filter(c => c.startsWith(tabBase)) : [];
      tabIndex = 0;
    } else if (tabMatches.length > 1) {
      tabIndex = (tabIndex + 1) % tabMatches.length;
    }
    if (tabMatches.length > 0) {
      input.value = tabMatches[tabIndex];
      tabBase = input.value;
      input.setSelectionRange(input.value.length, input.value.length);
      updateCursor();
    }
    return;
  }

  tabBase = null;
});

input.addEventListener('paste', e => {
  const text = (e.clipboardData || window.clipboardData).getData('text');
  if (!text.includes('\n')) return;
  e.preventDefault();
  text.split('\n').map(l => l.trim()).filter(Boolean).forEach(runCmd);
});

document.querySelector('.term').addEventListener('click', () => input.focus());

connectWs();
</script>
</body>
</html>
)rawliteral";
