#pragma once
// The web UI: one self-contained page (no internet needed on site), stored in flash.

#include <pgmspace.h>

static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ClimaControl</title>
<style>
:root{--bg:#f4f5f2;--card:#fff;--fg:#1d211c;--muted:#6b7268;--line:#dfe2dc;--accent:#3b7a3f;--warn:#b7791f;--bad:#c53030;--on:#2f855a}
@media (prefers-color-scheme:dark){:root{--bg:#151816;--card:#1f2420;--fg:#e8ebe6;--muted:#9aa396;--line:#333a33;--accent:#7cc47f;--warn:#e0a840;--bad:#f07070;--on:#68d391}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;align-items:center;gap:12px;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:1}
header h1{font-size:17px;margin:0;flex:1}#conn{font-size:13px;color:var(--muted)}
nav{display:flex;gap:4px;padding:8px 16px;overflow-x:auto;background:var(--card);border-bottom:1px solid var(--line)}
nav button{border:0;background:none;color:var(--muted);padding:6px 10px;border-radius:6px;font:inherit;cursor:pointer;white-space:nowrap}
nav button.active{background:var(--bg);color:var(--fg);font-weight:600}
main{padding:16px;max-width:1000px;margin:0 auto}section{display:none}section.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.card h3{margin:0 0 4px;font-size:12px;color:var(--muted);font-weight:500;text-transform:uppercase;letter-spacing:.04em}
.big{font-size:26px;font-weight:600;font-variant-numeric:tabular-nums}.unit{font-size:14px;color:var(--muted);font-weight:400}
.sub{font-size:12px;color:var(--muted)}h2{font-size:15px;margin:20px 0 8px}
.alarm{padding:8px 12px;border-radius:8px;margin-bottom:6px;border-left:4px solid var(--warn);background:var(--card)}
.alarm.critical{border-color:var(--bad)}.ok{color:var(--on)}.bad{color:var(--bad)}.warn{color:var(--warn)}
table{width:100%;border-collapse:collapse;background:var(--card);border-radius:10px;overflow:hidden}
td,th{padding:8px 10px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}th{font-size:12px;color:var(--muted);font-weight:500}
.pill{display:inline-block;min-width:44px;text-align:center;padding:2px 8px;border-radius:99px;font-size:13px;font-weight:600;background:var(--line)}
.pill.on{background:var(--on);color:#fff}
.btns{display:flex;gap:4px;flex-wrap:wrap}
button.b{border:1px solid var(--line);background:var(--bg);color:var(--fg);border-radius:6px;padding:5px 10px;font:inherit;font-size:13px;cursor:pointer}
button.b:hover{border-color:var(--accent)}button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
select,input{font:inherit;padding:4px 6px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg)}
textarea{width:100%;height:60vh;font:13px/1.4 ui-monospace,monospace;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg)}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:8px;display:none;max-width:90vw;white-space:pre-wrap}
.scroll{overflow-x:auto}dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 16px;margin:0}dt{color:var(--muted)}
</style>
</head>
<body>
<header><h1 id="title">ClimaControl</h1><span id="conn">connecting...</span></header>
<nav id="tabs">
<button data-tab="overview" class="active">Overview</button><button data-tab="outputs">Outputs</button>
<button data-tab="sensors">Sensors</button><button data-tab="config">Configuration</button><button data-tab="system">System</button>
</nav>
<main>
<section id="overview" class="active"><div id="alarms"></div><div id="groups"></div></section>
<section id="outputs">
<p class="sub">Manual on/off returns to automatic after
<select id="minutes"><option value="0">the default time</option><option value="15">15 min</option><option value="60">1 h</option><option value="240">4 h</option><option value="1440">24 h</option></select>.</p>
<div class="scroll"><table><thead><tr><th>Output</th><th>State</th><th>Mode</th><th>Why</th><th></th></tr></thead><tbody id="outTable"></tbody></table></div>
</section>
<section id="sensors"><div class="scroll"><table><thead><tr><th>Sensor</th><th>Status</th><th>Values</th></tr></thead><tbody id="sensorTable"></tbody></table></div></section>
<section id="config">
<p class="sub">Edit the JSON and save. It is checked before it is stored; WiFi/MQTT changes need a reboot. Passwords shown as ******** stay unchanged.</p>
<p class="btns"><button class="b" id="cfgLoad">Reload</button><button class="b primary" id="cfgSave">Save</button></p>
<textarea id="cfgText" spellcheck="false"></textarea>
</section>
<section id="system">
<div class="card"><dl id="sysInfo"></dl></div>
<h2>Clock</h2><p class="btns"><button class="b" id="setClock">Set clock from this device</button></p>
<h2>Firmware update</h2><p class="btns"><input type="file" id="fw" accept=".bin"><button class="b" id="fwUpload">Upload</button></p>
<h2>Restart</h2><p class="btns"><button class="b" id="reboot">Reboot controller</button></p>
</section>
</main>
<div id="msg"></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const UNITS={temperature:'°C',humidity:'%',co2:'ppm',pressure:'hPa',gas:'kΩ'};
let S=null;
function toast(t,ms=3000){const m=$('#msg');m.textContent=t;m.style.display='block';clearTimeout(m._t);m._t=setTimeout(()=>m.style.display='none',ms)}
async function post(url,body,isJson){
  const r=await fetch(url,{method:'POST',body,headers:isJson?{'Content-Type':'application/json'}:{}});
  const j=await r.json().catch(()=>({ok:r.ok,errors:[r.status+' '+r.statusText]}));
  if(!j.ok)toast((j.errors||['failed']).join('\n'),6000);
  else if(j.rebootRequired)toast('Saved. Reboot to apply network settings.',6000);
  else if(j.notes&&j.notes.length)toast(j.notes.join('\n'));
  return j;
}
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('#tabs button,section').forEach(e=>e.classList.remove('active'));
  b.classList.add('active');$('#'+b.dataset.tab).classList.add('active');
  if(b.dataset.tab==='config'&&!$('#cfgText').value)loadConfig();
});
function fmt(v,q){if(v==null)return '–';return q==='co2'||q==='gas'?Math.round(v):v.toFixed(1)}
function renderOverview(){
  $('#alarms').innerHTML=S.alarms.map(a=>`<div class="alarm ${a.critical?'critical':''}">${esc(a.message)}</div>`).join('')||'<div class="alarm" style="border-color:var(--on)">All good, no alarms.</div>';
  const groups={};
  for(const [name,sig] of Object.entries(S.signals)){const [g,q]=name.split('.');(groups[g]=groups[g]||[]).push([q,sig])}
  const order=['temperature','humidity','co2','pressure','gas'];
  $('#groups').innerHTML=Object.entries(groups).map(([g,list])=>`<h2>${esc(g)}</h2><div class="grid">`+
    list.sort((a,b)=>order.indexOf(a[0])-order.indexOf(b[0])).map(([q,s])=>{
      const cls=s.value==null?'bad':(s.sources<s.expected||s.disagree)?'warn':'';
      return `<div class="card"><h3>${esc(q)}</h3><div class="big ${cls}">${fmt(s.value,q)} <span class="unit">${UNITS[q]||''}</span></div>
      <div class="sub">${s.sources}/${s.expected} sensors${s.disagree?' · disagree':''}</div></div>`}).join('')+'</div>').join('');
}
function renderOutputs(){
  $('#outTable').innerHTML=S.outputs.map(o=>{
    const state=o.kind==='pwm'?Math.round(o.level*100)+'%':(o.on?'ON':'off');
    const left=o.manualRemainingS?` (${Math.ceil(o.manualRemainingS/60)} min left)`:'';
    const pwm=o.kind==='pwm'?`<input type="number" min="0" max="100" step="5" value="${Math.round(o.level*100)}" id="lv_${esc(o.name)}" style="width:64px"> <button class="b" data-o="${esc(o.name)}" data-m="level">Set %</button>`:'';
    return `<tr><td><b>${esc(o.label||o.name)}</b><div class="sub">${esc(o.name)}</div></td>
    <td><span class="pill ${o.on?'on':''}">${state}</span></td><td>${o.mode}${left}</td><td class="sub">${esc(o.reason)}</td>
    <td><div class="btns"><button class="b" data-o="${esc(o.name)}" data-m="auto">Auto</button><button class="b" data-o="${esc(o.name)}" data-m="on">On</button><button class="b" data-o="${esc(o.name)}" data-m="off">Off</button>${pwm}</div></td></tr>`}).join('');
}
$('#outTable').onclick=async e=>{
  const b=e.target.closest('button[data-o]');if(!b)return;
  const p=new URLSearchParams({name:b.dataset.o,mode:b.dataset.m,minutes:$('#minutes').value});
  if(b.dataset.m==='level')p.set('level',($('#lv_'+b.dataset.o).value||0)/100);
  await post('/api/output',p);setTimeout(refresh,300);
};
function renderSensors(){
  $('#sensorTable').innerHTML=S.sensors.map(s=>`<tr><td><b>${esc(s.id)}</b><div class="sub">${esc(s.type)} · ${esc(s.group)}</div></td>
  <td class="${s.ok?'ok':'bad'}">${s.ok?'ok':'failing ('+s.failures+')'}${s.ageS!=null?'<div class="sub">'+s.ageS+' s ago</div>':''}</td>
  <td>${Object.entries(s.values).map(([q,v])=>`${esc(q)} <b>${fmt(v,q)}</b> ${UNITS[q]||''}`).join('<br>')}</td></tr>`).join('');
}
function renderSystem(){
  const y=S.system,w=y.wifi||{};const up=S.uptimeS;
  const rows=[['Device',y.device],['Firmware',y.firmware+' ('+y.chip+')'],['Uptime',Math.floor(up/86400)+'d '+new Date(up*1000).toISOString().substr(11,8)],
  ['Last restart',y.resetReason],['Clock',S.epoch?new Date(S.epoch*1000).toLocaleString():'NOT SET (schedules run in failsafe)'],
  ['Access point',w.ap?`${w.ap.ssid} · ${w.ap.ip} · ${w.ap.clients} clients`:'off'],
  ['Farm WiFi',w.sta?(w.sta.connected?`${w.sta.ssid} · ${w.sta.ip} · ${w.sta.rssi} dBm`:`${w.sta.ssid} · not connected`):'not configured'],
  ['MQTT',y.mqttConnected?'connected':'not connected'],['Config',y.configSource],['Sensors',S.simulated?'SIMULATED':'real'],
  ['Free memory',Math.round(y.freeHeap/1024)+' kB (min '+Math.round(y.minFreeHeap/1024)+' kB)']];
  $('#sysInfo').innerHTML=rows.map(([k,v])=>`<dt>${k}</dt><dd>${esc(v)}</dd>`).join('');
}
async function refresh(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});S=await r.json();
    $('#title').textContent='ClimaControl · '+S.system.device;
    const crit=S.alarms.filter(a=>a.critical).length;
    $('#conn').innerHTML=crit?`<span class="bad">${crit} alarm${crit>1?'s':''}</span>`:'<span class="ok">online</span>';
    renderOverview();renderOutputs();renderSensors();renderSystem();
  }catch(e){$('#conn').innerHTML='<span class="bad">connection lost</span>'}
}
async function loadConfig(){const r=await fetch('/api/config',{cache:'no-store'});$('#cfgText').value=JSON.stringify(await r.json(),null,2)}
$('#cfgLoad').onclick=loadConfig;
$('#cfgSave').onclick=async()=>{
  let txt=$('#cfgText').value;try{JSON.parse(txt)}catch(e){return toast('Not valid JSON: '+e.message,6000)}
  const j=await post('/api/config',txt,true);if(j.ok&&!j.rebootRequired)toast('Saved and active.');
};
$('#setClock').onclick=()=>post('/api/time',new URLSearchParams({epoch:Math.floor(Date.now()/1000)})).then(j=>j.ok&&toast('Clock set.'));
$('#reboot').onclick=()=>confirm('Reboot the controller? Outputs go to their safe state for a few seconds.')&&post('/api/reboot');
$('#fwUpload').onclick=async()=>{
  const f=$('#fw').files[0];if(!f)return toast('Choose a firmware.bin first');
  if(!confirm('Install '+f.name+'? The controller reboots afterwards.'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);toast('Uploading...',60000);await post('/update',fd);
};
refresh();setInterval(refresh,2000);
</script>
</body>
</html>
)HTML";
