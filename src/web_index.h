/**
 * @file web_index.h
 * Embedded configuration UI (served from flash, no filesystem upload needed).
 */
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PrintOrb</title>
<style>
  :root{--bg:#0d1117;--card:#161b22;--bd:#2b3340;--fg:#e6edf3;--mut:#9aa4ad;--ac:#22d3ee}
  *{box-sizing:border-box}
  body{margin:0;font-family:system-ui,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
  header{padding:18px 16px;text-align:center;border-bottom:1px solid var(--bd)}
  header h1{margin:0;font-size:20px;letter-spacing:.5px}
  header span{color:var(--ac)}
  .wrap{max-width:520px;margin:0 auto;padding:16px}
  .tabs{display:flex;gap:6px;margin-bottom:14px}
  .tabs button{flex:1;padding:10px;background:var(--card);color:var(--mut);border:1px solid var(--bd);
    border-radius:10px;cursor:pointer;font-size:14px}
  .tabs button.on{color:var(--fg);border-color:var(--ac)}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:16px;margin-bottom:14px}
  label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}
  input,select{width:100%;padding:10px;background:var(--bg);color:var(--fg);border:1px solid var(--bd);
    border-radius:9px;font-size:14px}
  button.primary{width:100%;padding:12px;margin-top:16px;background:var(--ac);color:#001317;border:0;
    border-radius:10px;font-weight:600;font-size:15px;cursor:pointer}
  .row{display:flex;gap:10px}.row>div{flex:1}
  button.ghost{padding:10px 12px;background:var(--bg);color:var(--ac);border:1px solid var(--bd);
    border-radius:9px;cursor:pointer;font-size:13px;white-space:nowrap}
  .hide{display:none}
  .gauge{font-size:54px;font-weight:700;text-align:center;margin:6px 0}
  .state{text-align:center;color:var(--ac);font-size:16px;margin-bottom:14px}
  .kv{display:flex;justify-content:space-between;padding:7px 0;border-bottom:1px solid var(--bd);font-size:14px}
  .kv:last-child{border:0}.kv span{color:var(--mut)}
  .hint{font-size:12px;color:var(--mut);margin-top:6px;line-height:1.5}
  .ok{color:#3fb950}.err{color:#f85149}
  #logbox{margin:0;max-height:62vh;overflow:auto;white-space:pre-wrap;word-break:break-word;
    font-family:ui-monospace,Consolas,monospace;font-size:12px;line-height:1.45;color:var(--mut);
    background:var(--bg);border:1px solid var(--bd);border-radius:9px;padding:10px}
  label.ck{display:flex;align-items:center;gap:8px;color:var(--fg);margin-top:14px}
  label.ck input{width:auto}
  .amshdr{display:flex;justify-content:space-between;align-items:center}
  .amshdr .hum{color:var(--mut);font-size:13px}
  .dryrow{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:12px}
  .dryrow .drystat{color:var(--mut);font-size:13px}
  .ghost[disabled]{opacity:.45;cursor:not-allowed}
  .amsgrid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:12px}
  .slot{aspect-ratio:1;border:2px solid var(--bd);border-radius:10px;display:flex;
    flex-direction:column;align-items:center;justify-content:center;gap:3px;padding:4px;
    font-size:12px;text-align:center;overflow:hidden;line-height:1.2}
  .slot .ty{font-weight:600;word-break:break-word}
  .slot.empty{background:var(--bg);color:var(--mut);border-style:dashed}
</style>
</head>
<body>
<header><h1>Print<span>Orb</span></h1></header>
<div class="wrap">
  <div class="tabs">
    <button id="tStatus" class="on" onclick="tab('status')">Status</button>
    <button id="tAms" onclick="tab('ams')">AMS</button>
    <button id="tSettings" onclick="tab('settings')">Settings</button>
    <button id="tInfo" onclick="tab('info')">Info</button>
    <button id="tLog" onclick="tab('log')">Log</button>
    <button id="tUpd" onclick="tab('upd')">Update</button>
  </div>

  <!-- STATUS -->
  <div id="status">
    <div class="card">
      <div class="gauge" id="gPct">--%</div>
      <div class="state" id="gState">connecting…</div>
      <div class="kv"><span>Printer</span><b id="gName">—</b></div>
      <div class="kv"><span>File</span><b id="gFile">—</b></div>
      <div class="kv"><span>Nozzle</span><b id="gNoz">—</b></div>
      <div class="kv"><span>Bed</span><b id="gBed">—</b></div>
      <div class="kv"><span>Remaining</span><b id="gEta">—</b></div>
      <div class="kv"><span>Layer</span><b id="gLay">—</b></div>
    </div>
    <button type="button" class="primary" style="background:#f0883e;color:#1a0e00" onclick="reboot()">&#9211; Reboot device</button>
    <div id="rmsg" class="hint"></div>
  </div>

  <!-- AMS -->
  <div id="ams" class="hide">
    <div id="amsBody"><div class="card">No AMS connected.</div></div>
  </div>

  <!-- SETTINGS -->
  <div id="settings" class="hide">
    <form id="form">
      <div class="card">
        <b>WiFi</b>
        <div class="kv" style="margin-top:8px"><span>Device IP</span><b id="wifiIp">—</b></div>
        <div class="kv"><span>mDNS</span><b id="wifiMdns">—</b></div>
        <div class="kv"><span>Signal</span><b id="wifiRssi">—</b></div>
        <label>Nearby networks</label>
        <div class="row">
          <div><select id="ssidSel" onchange="pickSsid()"><option value="">&mdash; scan &mdash;</option></select></div>
          <div style="flex:0 0 auto"><button type="button" class="ghost" onclick="scan()">&#8635; Scan</button></div>
        </div>
        <label>SSID</label><input name="wifiSsid" id="wifiSsid" placeholder="network name">
        <label>Password</label><input name="wifiPass" id="wifiPass" type="password" placeholder="(unchanged if blank)">
        <label>Hostname</label><input name="hostname" id="hostname" placeholder="printorb">
        <div class="hint">Network name &amp; reachable as <b>&lt;hostname&gt;.local</b>.</div>
      </div>
      <div class="card">
        <b>Printer</b>
        <label>Discover (mDNS)</label>
        <div class="row">
          <div><select id="discSel" onchange="pickDisc()"><option value="">&mdash; discover &mdash;</option></select></div>
          <div style="flex:0 0 auto"><button type="button" class="ghost" onclick="discover()">&#8635; Find</button></div>
        </div>
        <label>Type</label>
        <select name="printerType" id="printerType" onchange="ptype()">
          <option value="klipper">Klipper (Moonraker)</option>
          <option value="bambu">Bambu Lab</option>
        </select>
        <label>Display name</label><input name="printerName" id="printerName">
        <label>Printer IP / hostname</label><input name="printerIp" id="printerIp" placeholder="192.168.1.50 or printer.local">

        <div id="klip">
          <div class="row">
            <div><label>Moonraker port</label><input name="moonrakerPort" id="moonrakerPort" value="7125"></div>
          </div>
          <label>API key (optional)</label><input name="moonrakerApiKey" id="moonrakerApiKey">
        </div>

        <div id="bamb" class="hide">
          <label>Serial number</label><input name="bambuSerial" id="bambuSerial" placeholder="01P00A…">
          <label>LAN access code</label><input name="bambuAccessCode" id="bambuAccessCode">
          <div class="hint">Bambu: enable <b>LAN Mode</b> on the printer
            (Settings → WLAN). Serial &amp; access code are shown there.</div>
        </div>
      </div>
      <div class="card">
        <b>Display</b>
        <label>Brightness: <span id="brVal">100</span>%</label>
        <input type="range" min="10" max="100" name="brightness" id="brightness" value="100"
          oninput="document.getElementById('brVal').textContent=this.value">

        <label class="ck"><input type="checkbox" id="screenSleepEnabled" onchange="toggleSleep()"> Display auto-off (power-save)</label>
        <div id="sleepRow">
          <label>Sleep after (s) of inactivity</label>
          <input type="number" min="5" max="3600" name="screenTimeoutSec" id="screenTimeoutSec" value="120">
        </div>

        <label>Timezone (for scheduled dimming)</label>
        <select name="timezone" id="timezone">
          <option value="">UTC</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe (Berlin, Paris, Madrid)</option>
          <option value="GMT0BST,M3.5.0/1,M10.5.0">UK / Ireland (London, Dublin)</option>
          <option value="EET-2EET,M3.5.0/3,M10.5.0/4">Eastern Europe (Athens, Helsinki)</option>
          <option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
          <option value="CST6CDT,M3.2.0,M11.1.0">US Central</option>
          <option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
          <option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
          <option value="JST-9">Japan (Tokyo)</option>
          <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia Eastern (Sydney)</option>
        </select>

        <label class="ck"><input type="checkbox" id="dimSchedEnabled" onchange="toggleDim()"> Enable night dimming</label>
        <div id="dimRows">
          <div class="row">
            <div><label>From</label><input type="time" id="dimStart" value="22:00"></div>
            <div><label>To</label><input type="time" id="dimEnd" value="07:00"></div>
          </div>
          <label>Night brightness: <span id="dbVal">20</span>%</label>
          <input type="range" min="0" max="100" id="dimBrightness" value="20"
            oninput="document.getElementById('dbVal').textContent=this.value">
        </div>
      </div>
      <div class="card">
        <b>Security</b>
        <label>Update / OTA password</label>
        <input name="adminPassword" id="adminPassword" type="password" placeholder="(unchanged if blank)">
        <div class="hint" id="pwHint">Required to enable firmware updates (web upload &amp; ArduinoOTA).
          Leave blank to keep the current one. <b>No password = OTA disabled.</b></div>
      </div>
      <button class="primary" type="submit">Save &amp; Reboot</button>
      <div id="msg" class="hint"></div>
    </form>
  </div>

  <!-- INFO -->
  <div id="info" class="hide">
    <div class="card">
      <div class="row" style="align-items:center;margin-bottom:4px">
        <div><b>System info</b></div>
        <div style="flex:0 0 auto"><button type="button" class="ghost" onclick="loadInfo()">&#8635; Refresh</button></div>
      </div>
      <div id="infoBody"><div class="hint">Loading…</div></div>
    </div>
  </div>

  <!-- LOG -->
  <div id="log" class="hide">
    <div class="card">
      <div class="row" style="align-items:center;margin-bottom:8px">
        <div><b>Device log</b></div>
        <div style="flex:0 0 auto"><button type="button" class="ghost" onclick="loadLog()">&#8635; Refresh</button></div>
      </div>
      <pre id="logbox"></pre>
    </div>
  </div>

  <!-- UPDATE -->
  <div id="upd" class="hide">
    <div class="card">
      <b>Firmware update (OTA)</b>
      <div class="hint">Upload a compiled <b>firmware.bin</b>. The device flashes it
        and reboots. Do not power off during the update. You'll be asked for the
        <b>OTA password</b> (set it in Settings → Security first).</div>
      <label>Firmware file (.bin)</label>
      <input type="file" id="fwFile" accept=".bin">
      <button type="button" class="primary" onclick="uploadFw()">Upload &amp; flash</button>
      <div id="updBar" style="height:8px;background:var(--bg);border:1px solid var(--bd);
        border-radius:6px;overflow:hidden;margin-top:12px;display:none">
        <div id="updFill" style="height:100%;width:0;background:var(--ac);transition:width .2s"></div>
      </div>
      <div id="updMsg" class="hint"></div>
    </div>
  </div>
</div>

<script>
function tab(t){
  ['status','ams','settings','info','log','upd'].forEach(function(x){
    document.getElementById(x).classList.toggle('hide',x!=t);
  });
  document.getElementById('tStatus').classList.toggle('on',t=='status');
  document.getElementById('tAms').classList.toggle('on',t=='ams');
  document.getElementById('tSettings').classList.toggle('on',t=='settings');
  document.getElementById('tInfo').classList.toggle('on',t=='info');
  document.getElementById('tLog').classList.toggle('on',t=='log');
  document.getElementById('tUpd').classList.toggle('on',t=='upd');
  if(t=='settings'&&!window._scanned){window._scanned=1;scan();}
  window._logOn=(t=='log');
  window._infoOn=(t=='info');
  if(t=='log')loadLog();
  if(t=='info')loadInfo();
}
function kib(b){return (b/1024).toFixed(1)+' KiB';}
function mib(b){return (b/1048576).toFixed(2)+' MiB';}
function dur(s){var d=(s/86400)|0,h=((s%86400)/3600)|0,m=((s%3600)/60)|0;return (d?d+'d ':'')+(h?h+'h ':'')+m+'m';}
function kv(k,v){return '<div class="kv"><span>'+k+'</span><b>'+v+'</b></div>';}
async function loadInfo(){
  try{
    var d=await (await fetch('/api/sysinfo')).json();
    var n=d.net||{},m=d.mem||{},f=d.flash||{},c=d.chip||{},t=d.time||{},p=d.printer||{};
    document.getElementById('wifiIp').textContent=n.ip||'—';
    document.getElementById('wifiMdns').textContent=n.mdns||'—';
    document.getElementById('wifiRssi').textContent=(n.rssi!=null?n.rssi+' dBm':'—');
    var h='';
    h+=kv('Firmware',d.firmware||'—');
    h+=kv('SDK',d.sdk||'—');
    h+=kv('Uptime',dur(d.uptimeSec||0));
    h+=kv('Reset reason',d.resetReason||'—');
    h+=kv('Mode',n.mode||'—');
    h+=kv('IP',n.ip||'—');
    h+=kv('mDNS',n.mdns||'—');
    h+=kv('MAC',n.mac||'—');
    h+=kv('SSID',(n.ssid||'—')+(n.channel?' (ch '+n.channel+')':''));
    h+=kv('RSSI',(n.rssi!=null?n.rssi+' dBm':'—'));
    h+=kv('Heap free',kib(m.heapFree||0)+' / '+kib(m.heapSize||0));
    h+=kv('Heap min ever',kib(m.heapMin||0));
    h+=kv('Heap max block',kib(m.heapMaxBlk||0));
    h+=kv('PSRAM free',kib(m.psramFree||0)+' / '+kib(m.psramSize||0));
    h+=kv('Flash size',mib(f.flashSize||0));
    h+=kv('Sketch',mib(f.sketchSize||0)+' used, '+mib(f.sketchFree||0)+' free');
    h+=kv('Chip',(c.model||'—')+' rev '+(c.rev!=null?c.rev:'?')+', '+(c.cores||'?')+' cores @ '+(c.cpuMhz||'?')+' MHz');
    h+=kv('NTP',(t.synced?('synced'+(t.local?' · '+t.local:'')):'not synced'));
    h+=kv('Printer',(p.type||'—')+' · '+(p.state||'—'));
    document.getElementById('infoBody').innerHTML=h;
  }catch(e){document.getElementById('infoBody').innerHTML='<div class="hint err">Failed to load.</div>';}
}
function uploadFw(){
  var f=document.getElementById('fwFile').files[0];
  if(!f){alert('Pick a .bin file first');return;}
  var pw=prompt('Update password (the OTA password set in Settings):');
  if(pw===null)return;
  var bar=document.getElementById('updBar'),fill=document.getElementById('updFill'),m=document.getElementById('updMsg');
  bar.style.display='block';m.className='hint';m.textContent='Uploading…';
  var x=new XMLHttpRequest();
  x.upload.onprogress=function(e){if(e.lengthComputable){var p=(e.loaded/e.total*100)|0;fill.style.width=p+'%';m.textContent='Uploading '+p+'%';}};
  x.onload=function(){
    if(x.status==200){fill.style.width='100%';m.className='hint ok';m.textContent='Flashed. Rebooting…';}
    else if(x.status==401){m.className='hint err';m.textContent='Wrong password.';}
    else{m.className='hint err';m.textContent='Update failed ('+x.status+').';}
  };
  x.onerror=function(){m.className='hint err';m.textContent='Upload error.';};
  // Raw body (octet-stream), not multipart -> lighter on the device.
  x.open('POST','/api/update');
  x.setRequestHeader('Authorization','Basic '+btoa('admin:'+pw));
  x.setRequestHeader('Content-Type','application/octet-stream');
  x.send(f);
}
function toggleSleep(){document.getElementById('sleepRow').classList.toggle('hide',!document.getElementById('screenSleepEnabled').checked);}
function toggleDim(){document.getElementById('dimRows').classList.toggle('hide',!document.getElementById('dimSchedEnabled').checked);}
function hm2min(v){var p=(v||'0:0').split(':');return (parseInt(p[0])||0)*60+(parseInt(p[1])||0);}
function min2hm(m){m=m||0;var h=(m/60)|0,i=m%60;return (h<10?'0':'')+h+':'+(i<10?'0':'')+i;}
function amsLum(c){return 0.299*parseInt(c.substr(1,2),16)+0.587*parseInt(c.substr(3,2),16)+0.114*parseInt(c.substr(5,2),16);}
function renderAms(a){
  var box=document.getElementById('amsBody');
  if(!a||!a.present){box.innerHTML='<div class="card">No AMS connected.</div>';return;}
  var h='';
  (a.unit||[]).forEach(function(u){
    h+='<div class="card"><div class="amshdr"><b>'+(u.model||'AMS')+' '+(u.index+1)+'</b>';
    var meta=[];
    if(u.humidityPct!=null)meta.push('RH '+u.humidityPct+'%');
    else if(u.humidity!=null)meta.push('Humidity '+u.humidity+'/5');
    if(u.temp!=null)meta.push(u.temp.toFixed(1)+'&deg;C');
    if(meta.length)h+='<span class="hum">'+meta.join(' &middot; ')+'</span>';
    h+='</div><div class="amsgrid">';
    (u.slots||[]).forEach(function(s,i){
      var active=(a.activeUnit===u.index&&a.activeSlot===i);
      if(s.used){
        var col=s.color||'#808080';
        var txt=amsLum(col)>140?'#000':'#fff';
        var st='background:'+col+';color:'+txt+';border-color:'+(active?'var(--ac)':col)+';border-width:'+(active?'3px':'2px');
        h+='<div class="slot" style="'+st+'"><span class="ty">'+(s.type||'?')+'</span><span>'+(s.remain!=null?s.remain+'%':'')+'</span></div>';
      }else{
        h+='<div class="slot empty"'+(active?' style="border-color:var(--ac);border-width:3px;border-style:solid"':'')+'><span class="ty">empty</span></div>';
      }
    });
    h+='</div>';
    if(u.ht){
      if(u.drying){
        var info='Drying'+(u.dryTargetC!=null?' '+u.dryTargetC+'&deg;C':'')
                +(u.dryRemainMin!=null?' &middot; '+u.dryRemainMin+' min':'');
        h+='<div class="dryrow"><span class="drystat">'+info+'</span>'
          +'<button class="ghost" style="color:#f85149;border-color:#f85149" onclick="dry(\'stop\')">Stop drying</button></div>';
      }else{
        var canDry=!!(u.slots&&u.slots[0]&&u.slots[0].used);
        h+='<div class="dryrow"><span class="drystat">'+(canDry?'':'Load filament to dry')+'</span>'
          +'<button class="ghost" onclick="dry(\'start\')"'+(canDry?'':' disabled')+'>Dry</button></div>';
      }
    }
    h+='</div>';
  });
  box.innerHTML=h;
}
async function dry(action){
  try{await fetch('/api/dry?action='+action,{method:'POST'});}catch(e){}
  setTimeout(loadStatus,600);
}
async function loadLog(){
  try{
    var t=await (await fetch('/api/log')).text();
    var box=document.getElementById('logbox');
    var atBottom=box.scrollTop+box.clientHeight>=box.scrollHeight-20;
    box.textContent=t;
    if(atBottom)box.scrollTop=box.scrollHeight;
  }catch(e){}
}
function ptype(){
  var b=document.getElementById('printerType').value=='bambu';
  document.getElementById('bamb').classList.toggle('hide',!b);
  document.getElementById('klip').classList.toggle('hide',b);
}
function bars(r){return r>=-55?'█▆▄':r>=-65?'▆▄':r>=-75?'▄':'▂';}
async function scan(){
  var sel=document.getElementById('ssidSel');
  sel.innerHTML='<option>scanning…</option>';
  for(var i=0;i<20;i++){
    var d=await (await fetch('/api/scan')).json();
    if(d.scanning){await new Promise(r=>setTimeout(r,1000));continue;}
    var nets=d.networks||[];
    sel.innerHTML='<option value="">'+(nets.length?'— select —':'— none —')+'</option>';
    nets.forEach(function(n){
      var o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+'  '+bars(n.rssi)+(n.secure?' 🔒':'');
      sel.appendChild(o);
    });
    return;
  }
  sel.innerHTML='<option value="">— timeout —</option>';
}
function pickSsid(){var v=document.getElementById('ssidSel').value;if(v)document.getElementById('wifiSsid').value=v;}
async function discover(){
  var sel=document.getElementById('discSel');sel.innerHTML='<option>finding…</option>';
  try{
    var d=await (await fetch('/api/discover')).json();
    var ps=d.printers||[];
    sel.innerHTML='<option value="">'+(ps.length?'— select —':'— none found —')+'</option>';
    ps.forEach(function(p){
      var o=document.createElement('option');o.value=JSON.stringify(p);
      o.textContent=p.type+': '+(p.name||p.ip)+' ('+p.ip+')';
      sel.appendChild(o);
    });
  }catch(e){sel.innerHTML='<option value="">— error —</option>';}
}
function pickDisc(){
  var v=document.getElementById('discSel').value;if(!v)return;
  var p=JSON.parse(v);
  document.getElementById('printerType').value=p.type;ptype();
  document.getElementById('printerIp').value=p.name||p.ip;
  if(p.type=='klipper'&&p.port)document.getElementById('moonrakerPort').value=p.port;
}
function eta(s){ if(s<0)return'—';var h=(s/3600)|0,m=((s%3600)/60)|0;return h?h+'h '+m+'m':m+'m';}
async function reboot(){
  if(!confirm('Reboot PrintOrb now?'))return;
  var m=document.getElementById('rmsg');m.textContent='Rebooting…';m.className='hint ok';
  try{await fetch('/api/restart',{method:'POST'});}catch(e){}
}
async function loadStatus(){
  try{
    var r=await fetch('/api/status'); var d=await r.json();
    document.getElementById('gPct').textContent=(d.state=='Offline'?'--':Math.round(d.progress))+'%';
    document.getElementById('gState').textContent=d.state;
    document.getElementById('gName').textContent=d.printer||'—';
    document.getElementById('gFile').textContent=d.file||'—';
    document.getElementById('gNoz').textContent=Math.round(d.nozzle)+' / '+Math.round(d.nozzleTarget)+'°C';
    document.getElementById('gBed').textContent=Math.round(d.bed)+' / '+Math.round(d.bedTarget)+'°C';
    document.getElementById('gEta').textContent=eta(d.remaining);
    document.getElementById('gLay').textContent=(d.totalLayer>0)?(d.layer+' / '+d.totalLayer):'—';
    renderAms(d.ams);
  }catch(e){}
}
async function loadConfig(){
  try{
    var r=await fetch('/api/config'); var d=await r.json();
    for(var k in d){var el=document.getElementById(k); if(el&&k!='wifiPass'&&el.type!='checkbox') el.value=d[k];}
    document.getElementById('brVal').textContent=d.brightness;
    document.getElementById('adminPassword').placeholder=d.adminPwSet?'•••••• (set — blank to keep)':'set a password to enable OTA';
    document.getElementById('screenSleepEnabled').checked=(d.screenSleepEnabled!==false);
    if(d.timezone!=null)document.getElementById('timezone').value=d.timezone;
    document.getElementById('dimSchedEnabled').checked=!!d.dimSchedEnabled;
    document.getElementById('dimStart').value=min2hm(d.dimStartMin);
    document.getElementById('dimEnd').value=min2hm(d.dimEndMin);
    var db=(d.dimBrightness!=null?d.dimBrightness:20);
    document.getElementById('dimBrightness').value=db;
    document.getElementById('dbVal').textContent=db;
    toggleSleep();toggleDim();
    ptype();
  }catch(e){}
}
document.getElementById('form').addEventListener('submit',async function(e){
  e.preventDefault();
  var fd=new FormData(e.target), o={};
  fd.forEach((v,k)=>o[k]=v);
  o.moonrakerPort=parseInt(o.moonrakerPort||'7125');
  o.brightness=parseInt(o.brightness||'100');
  o.screenTimeoutSec=parseInt(o.screenTimeoutSec||'120');
  o.screenSleepEnabled=document.getElementById('screenSleepEnabled').checked;
  o.dimSchedEnabled=document.getElementById('dimSchedEnabled').checked;
  o.dimStartMin=hm2min(document.getElementById('dimStart').value);
  o.dimEndMin=hm2min(document.getElementById('dimEnd').value);
  o.dimBrightness=parseInt(document.getElementById('dimBrightness').value||'20');
  var m=document.getElementById('msg');
  m.textContent='Saving…';m.className='hint';
  try{
    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
    if(r.ok){m.textContent='Saved. Rebooting…';m.className='hint ok';}
    else{m.textContent='Save failed.';m.className='hint err';}
  }catch(e){m.textContent='Error: '+e;m.className='hint err';}
});
loadConfig();loadStatus();loadInfo();setInterval(loadStatus,2000);
setInterval(function(){if(window._logOn)loadLog();},1500);
setInterval(function(){if(window._infoOn)loadInfo();},3000);
</script>
</body>
</html>
)HTML";
