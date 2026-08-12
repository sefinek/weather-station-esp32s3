#pragma once

// Main dashboard page (/) - polls /api/data every 5s. See CLAUDE.md's
// "Adding a new sensor" note: new fields must be added here (FIELDS/DOM ids)
// as well as in console.cpp's printStatus() and app_webserver.cpp's handleApiData().
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Stacja Pogodowa</title>
<link rel="icon" href="data:,">
<meta name="color-scheme" content="dark">
<style>
:root{
  --mono:ui-monospace,'Cascadia Code','SF Mono',Consolas,'Liberation Mono',monospace;
  --bg:#08090a;--surface:#131418;--border:#24262c;
  --text:#eef0f2;--muted:#7b818c;
  --accent:#ffb020;
  --c-outside:#ffb020;--c-inside:#ffd93d;--c-gas:#ff4d4d;
  --c-light:#3ddc84;--c-system:#b8bec7;--c-network:#a855f7;--c-memory:#ff5fa2
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;z-index:20;background:rgba(8,9,10,.72);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border-bottom:1px solid var(--border);padding:14px 20px}
.header-inner{max-width:1100px;margin:0 auto;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px}
h1{font-size:1.4rem;font-weight:800;letter-spacing:-.02em;color:var(--text)}
.hdr-actions{display:flex;align-items:center;flex-wrap:wrap;justify-content:flex-end;gap:14px}
.refresh-info{font-size:.78rem;color:var(--muted);white-space:nowrap}
.refresh-info b{color:var(--accent);font-weight:600}
.console-link{color:var(--muted);text-decoration:none;font-size:.85rem;border:1px solid var(--border);border-radius:999px;padding:6px 14px;transition:color .2s ease,border-color .2s ease}
.console-link:hover{color:var(--accent);border-color:var(--accent)}
.conn{display:inline-flex;align-items:center;justify-content:center;width:28px;height:28px;background:var(--surface);border:1px solid var(--border);border-radius:999px}
.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}
.dot{width:8px;height:8px;border-radius:50%;background:var(--c-gas);flex-shrink:0;transition:background .5s}
.dot.online{background:var(--c-light);animation:pulse 2s ease-in-out infinite}
@keyframes pulse{0%,100%{box-shadow:0 0 0 0 #3ddc8444}50%{box-shadow:0 0 0 5px #3ddc8411}}
main.page{max-width:1100px;margin:0 auto;padding:28px 20px}
.section-label{font-size:.85rem;text-transform:uppercase;letter-spacing:.16em;color:var(--muted);margin:28px 0 12px}
.section-label:first-child{margin-top:0}
.hero{position:relative;overflow:hidden;background:var(--surface);border:1px solid var(--border);border-top:3px solid var(--c-outside);border-radius:14px;padding:26px;margin-bottom:16px;display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:22px}
.hero::before{content:"";position:absolute;inset:0;background:radial-gradient(circle at 10% 20%,rgba(255,176,32,.16),transparent 55%);pointer-events:none}
.hero-top{display:flex;align-items:center;gap:18px;position:relative}
.hero-icon{font-size:2.8rem;line-height:1}
.hero-label{font-size:.78rem;text-transform:uppercase;letter-spacing:.14em;color:var(--muted);margin-bottom:4px}
.hero-temp{font-family:var(--mono);font-size:3.1rem;font-weight:700;line-height:1;color:var(--text)}
.hero-deg{font-size:1.5rem;color:var(--muted);margin-left:2px;font-weight:400}
.hero-stats{display:flex;gap:26px;flex-wrap:wrap;position:relative}
.hstat{display:flex;flex-direction:column;gap:3px;min-width:82px}
.hstat-label{font-size:.7rem;text-transform:uppercase;letter-spacing:.1em;color:var(--muted)}
.hstat-val{font-family:var(--mono);font-size:1.1rem;font-weight:600;color:var(--text)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:16px}
.card{background:var(--surface);border-radius:10px;border:1px solid var(--border);border-left:3px solid var(--accent-card,var(--border));overflow:hidden;transition:transform .15s ease}
.card:hover{transform:translateY(-2px)}
.card-body{padding:20px}
.card-title{font-size:.92rem;font-weight:700;text-transform:uppercase;letter-spacing:.14em;color:var(--muted);margin-bottom:6px}
.row{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:4px 12px;padding:9px 0;border-bottom:1px solid rgba(255,255,255,.05)}
.row:last-child{border:none}
.label{font-size:.9rem;color:var(--muted);flex-shrink:0}
.val{font-family:var(--mono);font-size:1.15rem;font-weight:600;color:var(--text);text-align:right}
.val-sm{font-size:.8rem;font-weight:500}
@keyframes flash{0%{color:var(--accent)}100%{color:var(--text)}}
.flash{animation:flash .7s ease-out forwards}
.unit{font-size:.8rem;color:var(--muted);margin-left:6px;font-weight:400}
.badge{display:inline-block;padding:3px 12px;border-radius:999px;font-size:.78rem;font-weight:700;letter-spacing:.06em;font-family:system-ui,-apple-system,sans-serif}
.badge-rain{background:rgba(255,176,32,.14);color:#ffc966;border:1px solid rgba(255,176,32,.3)}
.badge-dry{background:rgba(61,220,132,.14);color:#7bedab;border:1px solid rgba(61,220,132,.3)}
.badge-gas{background:rgba(255,77,77,.16);color:#ff9494;border:1px solid rgba(255,77,77,.32)}
.badge-clean{background:rgba(61,220,132,.14);color:#7bedab;border:1px solid rgba(61,220,132,.3)}
.badge-warm{background:rgba(255,176,32,.14);color:#ffc966;border:1px solid rgba(255,176,32,.3)}
</style>
</head>
<body>
<header>
  <div class="header-inner">
    <h1><span aria-hidden="true">🌥️</span> Stacja Pogodowa</h1>
    <div class="hdr-actions">
      <span class="refresh-info">Odświeżanie za <b><span id="cd">5</span>s</b> &nbsp;·&nbsp; Zaktualizowano <b><span id="ts">-</span></b></span>
      <a class="console-link" href="/console">Konsola</a>
      <div class="conn" role="status" aria-live="polite" title="Łączenie..."><div class="dot" id="dot"></div><span class="sr-only" id="eth-status">Łączenie...</span></div>
    </div>
  </div>
</header>
<main class="page">
  <div class="section-label">Warunki</div>
  <div class="hero">
    <div class="hero-top">
      <span class="hero-icon" aria-hidden="true">🌤️</span>
      <div>
        <div class="hero-label">Na zewnątrz</div>
        <div class="hero-temp"><span id="ot">-</span><span class="hero-deg">°C</span></div>
      </div>
    </div>
    <div class="hero-stats">
      <div class="hstat"><span class="hstat-label">Wilgotność</span><span class="hstat-val"><span id="oh">-</span><span class="unit">%</span></span></div>
      <div class="hstat"><span class="hstat-label">Ciśnienie</span><span class="hstat-val"><span id="op">-</span><span class="unit">hPa</span></span></div>
      <div class="hstat"><span class="hstat-label">Poziom morza</span><span class="hstat-val"><span id="osl">-</span><span class="unit">hPa</span></span></div>
    </div>
  </div>
  <div class="grid">
    <div class="card" style="--accent-card:var(--c-inside)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">🏠</span> Wewnątrz</h2>
        <div class="row"><span class="label">Temperatura</span><span class="val"><span id="it">-</span><span class="unit">°C</span></span></div>
        <div class="row"><span class="label">Wilgotność</span><span class="val"><span id="ih">-</span><span class="unit">%</span></span></div>
        <div class="row"><span class="label">Ciśnienie</span><span class="val"><span id="ip">-</span><span class="unit">hPa</span></span></div>
        <div class="row"><span class="label">Różnica temp.</span><span class="val"><span id="dt">-</span><span class="unit">°C</span></span></div>
      </div>
    </div>
    <div class="card" style="--accent-card:var(--c-gas)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">💨</span> Gaz / Dym (MQ-2)</h2>
        <div class="row"><span class="label">Status</span><span class="val" id="gas-s">-</span></div>
        <div class="row"><span class="label">Natężenie</span><span class="val"><span id="gas-p">-</span><span class="unit">%</span></span></div>
        <div class="row"><span class="label">ADC (surowe)</span><span class="val"><span id="gas-r">-</span><span class="unit">/ 4095</span></span></div>
      </div>
    </div>
    <div class="card" style="--accent-card:var(--c-light)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">☀️</span> Światło i deszcz</h2>
        <div class="row"><span class="label">Natężenie światła</span><span class="val"><span id="lx">-</span><span class="unit">lx</span></span></div>
        <div class="row"><span class="label">Status</span><span class="val" id="rs">-</span></div>
        <div class="row"><span class="label">Natężenie</span><span class="val"><span id="rp">-</span><span class="unit">%</span></span></div>
        <div class="row"><span class="label">ADC (surowe)</span><span class="val"><span id="rr">-</span><span class="unit">/ 4095</span></span></div>
      </div>
    </div>
  </div>

  <div class="section-label">Urządzenie</div>
  <div class="grid">
    <div class="card" style="--accent-card:var(--c-system)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">⚙️</span> System</h2>
        <div class="row"><span class="label">Temperatura ESP</span><span class="val"><span id="esp-t">-</span><span class="unit">°C</span></span></div>
        <div class="row"><span class="label">Częstotliwość CPU</span><span class="val"><span id="cpu-freq">-</span><span class="unit">MHz</span></span></div>
        <div class="row"><span class="label">Powód resetu</span><span class="val"><span id="reset-reason">-</span></span></div>
        <div class="row"><span class="label">Czas pracy</span><span class="val"><span id="uptime">-</span></span></div>
        <div class="row"><span class="label">Liczba uruchomień</span><span class="val"><span id="boot-count">-</span></span></div>
        <div class="row"><span class="label">Łączny czas pracy</span><span class="val"><span id="cum-uptime">-</span></span></div>
        <div class="row"><span class="label">Błędy czujników</span><span class="val"><span id="sensor-err">-</span></span></div>
        <div class="row"><span class="label">Czas uruchomienia</span><span class="val val-sm"><span id="boot-time">-</span></span></div>
      </div>
    </div>
    <div class="card" style="--accent-card:var(--c-network)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">🌐</span> <span id="net-type">Sieć</span></h2>
        <div class="row"><span class="label">Adres IP</span><span class="val"><span id="eth-ip">-</span></span></div>
        <div class="row"><span class="label">Łącze</span><span class="val"><span id="eth-spd">-</span></span></div>
        <div class="row"><span class="label">Brama</span><span class="val"><span id="eth-gw">-</span></span></div>
        <div class="row"><span class="label">Maska podsieci</span><span class="val"><span id="eth-sub">-</span></span></div>
        <div class="row"><span class="label">DNS</span><span class="val"><span id="eth-dns">-</span></span></div>
        <div class="row"><span class="label">Adres MAC</span><span class="val"><span id="mac">-</span></span></div>
      </div>
    </div>
    <div class="card" style="--accent-card:var(--c-memory)">
      <div class="card-body">
        <h2 class="card-title"><span aria-hidden="true">🧠</span> Pamięć</h2>
        <div class="row"><span class="label">Wolna sterta</span><span class="val"><span id="heap">-</span><span class="unit">KB</span></span></div>
        <div class="row"><span class="label">Min. wolna sterta</span><span class="val"><span id="heap-min">-</span><span class="unit">KB</span></span></div>
        <div class="row"><span class="label">Maks. alokacja sterty</span><span class="val"><span id="heap-max-alloc">-</span><span class="unit">KB</span></span></div>
        <div class="row"><span class="label">Wolny PSRAM</span><span class="val"><span id="psram">-</span><span class="unit">KB</span></span></div>
        <div class="row"><span class="label">Rozmiar flash</span><span class="val"><span id="flash">-</span><span class="unit">MB</span></span></div>
        <div class="row"><span class="label">Rozmiar szkicu</span><span class="val"><span id="sketch-size">-</span><span class="unit">KB</span></span></div>
        <div class="row"><span class="label">Wolne miejsce na szkic</span><span class="val"><span id="sketch-free">-</span><span class="unit">KB</span></span></div>
      </div>
    </div>
  </div>
</main>
<script>
const f1 = v => (+v).toFixed(1);
const connEl = document.querySelector('.conn');

const FIELDS = {
  ot: ({ outside }) => f1(outside.temp),
  oh: ({ outside }) => f1(outside.humidity),
  op: ({ outside }) => f1(outside.pressure),
  osl: ({ outside }) => f1(outside.pressureSL),
  it: ({ inside }) => f1(inside.temp),
  ih: ({ inside }) => inside.humidity === null ? 'n/d' : f1(inside.humidity),
  ip: ({ inside }) => f1(inside.pressure),
  dt: ({ outside, inside }) => {
    const d = inside.temp - outside.temp;
    return (d >= 0 ? '+' : '') + f1(d);
  },
  lx: ({ light }) => f1(light.lux),
  rp: ({ rain }) => rain.intensity,
  rr: ({ rain }) => rain.raw,
};

const fmtUptime = s => {
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  const hms = [h, m, sec].map(v => String(v).padStart(2, '0')).join(':');
  return d > 0 ? `${d}d ${hms}` : hms;
};

const setVal = (id, value) => {
  const el = document.getElementById(id);
  if (!el || el.textContent === String(value)) return;
  el.textContent = value;
  el.classList.remove('flash');
  void el.offsetWidth;
  el.classList.add('flash');
};

const RESET_REASONS = ['Nieznany','Włączenie zasilania','Reset zewnętrzny','Reset programowy','Panika','Int WDT','Task WDT','WDT','Głębokie uśpienie','Spadek napięcia','SDIO'];

const applyData = data => {
  for (const [id, fn] of Object.entries(FIELDS)) setVal(id, fn(data));

  document.getElementById('rs').innerHTML = data.rain.detected
    ? '<span class="badge badge-rain">PADA</span>'
    : '<span class="badge badge-dry">SUCHO</span>';

  if (!data.gas.ready) {
    document.getElementById('gas-s').innerHTML = '<span class="badge badge-warm">NAGRZEWANIE</span>';
    setVal('gas-p', '-');
    setVal('gas-r', '-');
  } else {
    document.getElementById('gas-s').innerHTML = data.gas.detected
      ? '<span class="badge badge-gas">WYKRYTO</span>'
      : '<span class="badge badge-clean">CZYSTO</span>';
    setVal('gas-p', data.gas.intensity);
    setVal('gas-r', data.gas.raw);
  }

  const { espTemp, heap, minHeap, maxAllocHeap, freePsram, cpuFreq, flashSize,
          sketchSize, freeSketchSpace, resetReason, uptime,
          bootCount, cumulativeUptime, sensorErrors, bootTime } = data.system;
  setVal('esp-t', f1(espTemp));
  setVal('heap', (heap / 1024).toFixed(0));
  setVal('heap-min', (minHeap / 1024).toFixed(0));
  setVal('heap-max-alloc', (maxAllocHeap / 1024).toFixed(0));
  setVal('psram', (freePsram / 1024).toFixed(0));
  setVal('cpu-freq', cpuFreq);
  setVal('flash', (flashSize / (1024 * 1024)).toFixed(0));
  setVal('sketch-size', (sketchSize / 1024).toFixed(0));
  setVal('sketch-free', (freeSketchSpace / 1024).toFixed(0));
  setVal('reset-reason', RESET_REASONS[resetReason] ?? `#${resetReason}`);
  setVal('uptime', fmtUptime(uptime));
  setVal('boot-count', bootCount);
  setVal('cum-uptime', fmtUptime(cumulativeUptime));
  setVal('sensor-err', sensorErrors);
  setVal('boot-time', bootTime ? new Date(bootTime).toLocaleString('pl-PL') : 'jeszcze niezsynchronizowany');

  const { type, ip, speed, fullDuplex, rssi, gateway, subnet, dns, mac } = data.network;
  document.getElementById('net-type').textContent = type === 'ethernet' ? 'Ethernet' : 'WiFi';
  setVal('eth-ip', ip);
  setVal('eth-spd', type === 'ethernet' ? `${speed} Mb/s · ${fullDuplex ? 'full duplex' : 'half duplex'}` : `WiFi · ${rssi} dBm`);
  setVal('eth-gw', gateway);
  setVal('eth-sub', subnet);
  setVal('eth-dns', dns);
  setVal('mac', mac);

  document.getElementById('dot').classList.add('online');
  document.getElementById('eth-status').textContent = `Połączono - ${ip}`;
  connEl.title = ip;
  document.getElementById('ts').textContent = new Date().toLocaleTimeString('pl-PL');
};

let cd = 5;
setInterval(() => {
  document.getElementById('cd').textContent = cd > 0 ? --cd : (cd = 5);
}, 1000);

const poll = async () => {
  try {
    const res = await fetch('/api/data');
    applyData(await res.json());
    cd = 5;
  } catch {
    document.getElementById('dot').classList.remove('online');
    document.getElementById('eth-status').textContent = 'Rozłączono';
    connEl.title = 'Rozłączono';
  }
};

poll();
setInterval(poll, 5000);
</script>
</body>
</html>
)rawliteral";
