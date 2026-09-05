// lib/webservice/html_pages.h
// PROGMEM-stored HTML/CSS/JS control page. Vanilla JS, no external framework.
#ifndef HTML_PAGES_H
#define HTML_PAGES_H

#include <Arduino.h>

static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Mesh Control</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--fg:#e2e8f0;--muted:#94a3b8;--acc:#38bdf8;--ok:#22c55e;--bad:#ef4444;--inp:#0b1120;--line:#334155}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
.wrap{max-width:920px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}
.sub{color:var(--muted);font-size:13px;margin-bottom:14px}
.tabs{display:flex;gap:8px;margin-bottom:14px}
.tab{flex:1;text-align:center;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px;cursor:pointer;font-size:13px;color:var(--muted)}
.tab.active{color:var(--acc);border-color:var(--acc)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:720px){.grid{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.card h2{font-size:14px;margin:0 0 10px;color:var(--acc)}
.row{display:flex;justify-content:space-between;align-items:center;margin:6px 0;font-size:13px}
.row .k{color:var(--muted)}
.pill{padding:2px 8px;border-radius:999px;font-size:11px;background:var(--inp);border:1px solid var(--line)}
.pill.ok{color:var(--ok);border-color:var(--ok)}
.pill.bad{color:var(--bad);border-color:var(--bad)}
.pill.info{color:var(--acc)}
label{display:block;font-size:12px;color:var(--muted);margin:8px 0 4px}
input,select{width:100%;background:var(--inp);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:8px;font-size:13px}
button{background:var(--acc);color:#062033;border:0;border-radius:8px;padding:9px 12px;font-size:13px;cursor:pointer;font-weight:600;margin-top:10px}
button.ghost{background:transparent;color:var(--acc);border:1px solid var(--line)}
button.danger{background:var(--bad);color:#fff}
button:disabled{opacity:.5;cursor:not-allowed}
table{width:100%;border-collapse:collapse;font-size:12px;margin-top:8px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--muted)}
.hidden{display:none}
#scanList{list-style:none;padding:0;margin:8px 0;font-size:13px}
#scanList li{padding:6px;border-bottom:1px solid var(--line);display:flex;justify-content:space-between}
.note{font-size:12px;color:var(--muted);margin-top:8px}
.full{grid-column:1/-1}
</style>
</head>
<body>
<div class="wrap">
<h1>ESP32 Master/Slave Mesh</h1>
<div class="sub" id="subline">connecting...</div>

<div class="tabs">
  <div class="tab active" data-p="status">Status</div>
  <div class="tab" data-p="config">Settings</div>
  <div class="tab" data-p="scan">Scan</div>
  <div class="tab" data-p="data">Telemetry</div>
</div>

<div class="grid">
  <div class="card" id="p-status">
    <h2>Live status</h2>
    <div id="statusRows"></div>
  </div>

  <div class="card hidden" id="p-config">
    <h2>Settings</h2>
    <label>Role</label>
    <select id="role">
      <option value="master">Master</option>
      <option value="slave">Slave</option>
    </select>
    <label>Node ID (8 chars)</label>
    <input id="nodeId" maxlength="8">
    <div id="masterFields">
      <label>Upstream SSID</label>
      <input id="upSsid" maxlength="32">
      <label>Upstream Password</label>
      <input id="upPsk" maxlength="64">
      <label>IP Mode</label>
      <select id="ipMode">
        <option value="dhcp">DHCP</option>
        <option value="static">Static</option>
      </select>
      <div id="staticFields" class="hidden">
        <label>IP</label><input id="sIp" placeholder="192.168.1.50">
        <label>Gateway</label><input id="sGw" placeholder="192.168.1.1">
        <label>Subnet</label><input id="sMask" placeholder="255.255.255.0">
      </div>
    </div>
    <div id="slaveFields" class="hidden">
      <label>Target Node ID</label>
      <input id="targetId" maxlength="8">
    </div>
    <label>Relay (slave)</label>
    <select id="relay">
      <option value="0">Off</option>
      <option value="1">On (manual)</option>
      <option value="2">Auto</option>
    </select>
    <label>Relay threshold (auto)</label>
    <input id="relayThr" type="number" min="1" max="8">
    <button id="saveBtn">Save &amp; Apply</button>
    <div class="note">Wi-Fi role changes take effect after reboot.</div>
    <button id="rebootBtn" class="danger">Reboot device</button>
  </div>

  <div class="card hidden" id="p-scan">
    <h2>Scan for target</h2>
    <button id="scanBtn" class="ghost">Scan</button>
    <ul id="scanList"></ul>
  </div>

  <div class="card hidden" id="p-data">
    <h2>Soil telemetry</h2>
    <table>
      <thead><tr><th>Node</th><th>Path</th><th>pH</th><th>Light</th><th>Moist</th></tr></thead>
      <tbody id="dataRows"></tbody>
    </table>
  </div>

  <div class="card full">
    <h2>Advanced (password-gated)</h2>
    <div id="advLocked">
      <label>Advanced password</label>
      <input id="advPw" type="password">
      <button id="unlockBtn">Unlock</button>
    </div>
    <div id="advOpen" class="hidden">
      <label>New hotspot password</label>
      <input id="apPsk" maxlength="64">
      <button id="hotspotBtn">Set hotspot password</button>
      <button id="provBtn">Batch provision to all nodes</button>
      <label style="display:flex;align-items:center;gap:8px">
        <input type="checkbox" id="applyAll" checked style="width:auto"> Propagate to whole tree
      </label>
      <button id="advSetBtn" class="ghost">Set new advanced password</button>
    </div>
  </div>
</div>
</div>

<script>
const $=id=>document.getElementById(id);
let advUnlocked=false;
let ws=null;

async function jget(u){const r=await fetch(u);return r.json();}
async function jpost(u,o){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o||{})});return r.json();}

function renderStatus(s){
  $('subline').textContent='role: '+s.role+' | '+(s.sta_connected?'uplink: '+s.sta_rssi+' dBm':'no uplink')+' | AP '+(s.ap_running?'on':'off')+' | '+s.ip;
  const add=(k,v,pill)=>$('statusRows').insertAdjacentHTML('beforeend',
    '<div class="row"><span class="k">'+k+'</span><span class="pill '+(pill||'info')+'">'+v+'</span></div>');
  $('statusRows').innerHTML='';
  add('Role',s.role);
  add('State',s.state);
  add('Node ID',s.node_id);
  add('STA connected',s.sta_connected?'yes':'no',s.sta_connected?'ok':'bad');
  add('RSSI',(s.sta_rssi||'-')+' dBm');
  add('AP running',s.ap_running?'yes':'no',s.ap_running?'ok':'bad');
  add('AP SSID',s.ap_ssid||'-');
  add('Children',s.children);
  add('Relay',s.relay_active?'active':'off',s.relay_active?'ok':'info');
  add('IP',s.ip);
  add('Free heap',s.free_heap+' B');
  add('Uptime',s.uptime+' s');
}

async function refreshStatus(){try{renderStatus(await jget('/api/status'));}catch(e){}}

async function loadConfig(){
  const c=await jget('/api/config');
  $('role').value=c.role;
  $('nodeId').value=c.node_id;
  $('targetId').value=c.target_id;
  $('upSsid').value=c.upstream_ssid;
  $('upPsk').value=c.upstream_psk;
  $('ipMode').value=c.ip_mode;
  $('sIp').value=c.ip; $('sGw').value=c.gateway; $('sMask').value=c.subnet;
  $('relay').value=String(c.relay);
  $('relayThr').value=c.relay_threshold;
  $('apPsk').value=c.ap_psk;
  onRole(); onIp();
}

function onRole(){
  const r=$('role').value;
  $('masterFields').classList.toggle('hidden',r!=='master');
  $('slaveFields').classList.toggle('hidden',r!=='slave');
}
function onIp(){$('staticFields').classList.toggle('hidden',$('ipMode').value!=='static');}

async function saveConfig(){
  const body={
    role:$('role').value,
    node_id:$('nodeId').value,
    target_id:$('targetId').value,
    upstream_ssid:$('upSsid').value,
    upstream_psk:$('upPsk').value,
    ip_mode:$('ipMode').value,
    ip:$('sIp').value,
    gateway:$('sGw').value,
    subnet:$('sMask').value,
    relay:parseInt($('relay').value),
    relay_threshold:parseInt($('relayThr').value)
  };
  const r=await jpost('/api/config',body);
  alert(r.ok?'Saved. Reboot to apply Wi-Fi changes.':'Save failed');
}

async function doScan(){
  const ul=$('scanList'); ul.innerHTML='<li>scanning...</li>';
  const s=await jget('/api/scan');
  ul.innerHTML='';
  if(!s.items||!s.items.length){ul.innerHTML='<li>No matching NODE_* AP found</li>';return;}
  s.items.forEach(it=>{
    const li=document.createElement('li');
    li.innerHTML='<span>'+it.ssid+'</span><span>'+it.rssi+' dBm</span>';
    ul.appendChild(li);
  });
}

async function unlock(){
  const r=await jpost('/api/advanced',{password:$('advPw').value});
  if(r.ok){advUnlocked=true;$('advLocked').classList.add('hidden');$('advOpen').classList.remove('hidden');}
  else alert('Wrong advanced password');
}
async function setHotspot(){
  const r=await jpost('/api/hotspot',{password:$('advPw').value,psk:$('apPsk').value});
  alert(r.ok?'Hotspot password updated':'Failed (min 8 chars / wrong password)');
}
async function doProvision(){
  const r=await jpost('/api/provision',{password:$('advPw').value,psk:$('apPsk').value,apply_to_all:$('applyAll').checked});
  alert(r.ok?'Provisioning sent':'Failed');
}
async function setAdvPw(){
  const np=prompt('New advanced password (min 1 char):');
  if(!np)return;
  const r=await jpost('/api/advanced',{password:$('advPw').value,new_password:np});
  alert(r.ok?'Advanced password updated':'Failed');
}
async function doReboot(){await jpost('/api/reboot',{});}

function startWs(){
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
  ws.onmessage=e=>{
    const d=JSON.parse(e.data);
    if(d.type==='status'){renderStatus(d);return;}
    const tr=document.createElement('tr');
    tr.innerHTML='<td>'+d.node_id+'</td><td>'+d.path+'</td><td>'+
      Number(d.ph).toFixed(2)+'</td><td>'+Number(d.light).toFixed(0)+
      '</td><td>'+Number(d.moisture).toFixed(1)+'%</td>';
    const tb=$('dataRows');
    tb.insertBefore(tr,tb.firstChild);
    while(tb.children.length>30)tb.removeChild(tb.lastChild);
  };
  ws.onclose=()=>setTimeout(startWs,2000);
}

document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{
  document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
  t.classList.add('active');
  ['status','config','scan','data'].forEach(p=>{
    $('p-'+p).classList.toggle('hidden',p!==t.dataset.p);
  });
});

$('role').onchange=onRole;
$('ipMode').onchange=onIp;
$('saveBtn').onclick=saveConfig;
$('scanBtn').onclick=doScan;
$('unlockBtn').onclick=unlock;
$('hotspotBtn').onclick=setHotspot;
$('provBtn').onclick=doProvision;
$('advSetBtn').onclick=setAdvPw;
$('rebootBtn').onclick=doReboot;

loadConfig(); refreshStatus(); startWs();
setInterval(refreshStatus,5000);
</script>
</body>
</html>
)rawliteral";

#endif // HTML_PAGES_H
