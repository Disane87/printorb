/**
 * @file web_index.h
 * Embedded configuration UI (served from flash, no filesystem upload needed).
 *
 * Four tabs — Status, Printers, Settings, System. The printer list is edited
 * client-side and POSTed to /api/config as a whole array; switching the
 * displayed printer goes to /api/printer/select and does not reboot.
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
  :root{--bg:#0b0f14;--card:#141b24;--card2:#1b2430;--bd:#2b3644;--fg:#e9eff5;--mut:#98a5b3;
        --fnt:#5d6875;--ac:#22d3ee;--ok:#3ddc84;--warn:#ffb020;--err:#ff5a5a}
  *{box-sizing:border-box}
  body{margin:0;font-family:system-ui,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg);
       font-size:15px;line-height:1.45;-webkit-text-size-adjust:100%}
  header{padding:16px;text-align:center;border-bottom:1px solid var(--bd);background:var(--bg)}
  header h1{margin:0;font-size:19px;letter-spacing:.6px;font-weight:600}
  header span{color:var(--ac)}
  .wrap{max-width:560px;margin:0 auto;padding:14px 14px 40px}
  .tabs{display:flex;gap:6px;margin-bottom:14px;position:sticky;top:0;z-index:5;
        background:var(--bg);padding:8px 0}
  .tabs button{flex:1;padding:9px 4px;background:var(--card);color:var(--mut);border:1px solid var(--bd);
    border-radius:10px;cursor:pointer;font-size:13px}
  .tabs button.on{color:var(--fg);border-color:var(--ac);background:var(--card2)}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:16px;margin-bottom:14px}
  .card>b:first-child{display:block;margin-bottom:6px;font-size:15px}
  h3{margin:0 0 10px;font-size:14px;color:var(--mut);font-weight:600;letter-spacing:.4px;
     text-transform:uppercase}
  label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}
  input,select{width:100%;padding:10px;background:var(--bg);color:var(--fg);border:1px solid var(--bd);
    border-radius:9px;font-size:14px}
  input[type=range]{padding:0}
  button.primary{width:100%;padding:12px;margin-top:16px;background:var(--ac);color:#00161b;border:0;
    border-radius:10px;font-weight:600;font-size:15px;cursor:pointer}
  button.danger{background:var(--warn);color:#1a0e00}
  button.ghost{padding:9px 12px;background:var(--bg);color:var(--ac);border:1px solid var(--bd);
    border-radius:9px;cursor:pointer;font-size:13px;white-space:nowrap}
  button.ghost.rm{color:var(--err)}
  .ghost[disabled]{opacity:.45;cursor:not-allowed}
  .row{display:flex;gap:10px}.row>div{flex:1}
  .hide{display:none}
  .hint{font-size:12px;color:var(--mut);margin-top:6px;line-height:1.5}
  .ok{color:var(--ok)}.err{color:var(--err)}
  .kv{display:flex;justify-content:space-between;gap:12px;padding:7px 0;
      border-bottom:1px solid var(--bd);font-size:14px}
  .kv:last-child{border:0}.kv span{color:var(--mut);flex:0 0 auto}
  .kv b{text-align:right;word-break:break-word;font-weight:500}

  /* --- live progress ring (Status tab) --- */
  .ring{width:168px;height:168px;border-radius:50%;margin:6px auto 14px;display:flex;
        align-items:center;justify-content:center;
        background:conic-gradient(var(--rc,var(--ac)) calc(var(--p,0)*3.6deg),var(--card2) 0);
        transition:background .4s}
  .ringin{width:140px;height:140px;border-radius:50%;background:var(--card);display:flex;
          flex-direction:column;align-items:center;justify-content:center;gap:2px}
  .ringin .pct{font-size:38px;font-weight:700;line-height:1}
  .ringin .st{font-size:14px;color:var(--rc,var(--ac))}

  /* --- printer switcher chips --- */
  .chips{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}
  .chip{display:flex;align-items:center;gap:7px;padding:7px 12px 7px 8px;border-radius:999px;
        border:1px solid var(--bd);background:var(--card);color:var(--mut);cursor:pointer;font-size:13px}
  .chip.on{border-color:var(--ac);color:var(--fg);background:var(--card2)}
  .badge{width:20px;height:20px;border-radius:50%;display:grid;place-items:center;
         font-size:11px;font-weight:700;color:#00161b;flex:0 0 auto}
  .badge.k{background:#ff8c42}.badge.b{background:var(--ac)}

  /* --- printer editor --- */
  .phead{display:flex;align-items:center;gap:10px;margin-bottom:4px}
  .phead b{flex:1}
  .empty{text-align:center;color:var(--mut);padding:18px 0;font-size:14px}

  /* --- AMS --- */
  .amshdr{display:flex;justify-content:space-between;align-items:center;gap:8px}
  .amshdr .hum{color:var(--mut);font-size:13px}
  .amsgrid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:12px}
  .slot{aspect-ratio:1;border:2px solid var(--bd);border-radius:10px;display:flex;
    flex-direction:column;align-items:center;justify-content:center;gap:3px;padding:4px;
    font-size:12px;text-align:center;overflow:hidden;line-height:1.2}
  .slot .ty{font-weight:600;word-break:break-word}
  .slot.empty{background:var(--bg);color:var(--mut);border-style:dashed;padding:4px}
  .dryrow{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:12px}
  .dryrow .drystat{color:var(--mut);font-size:13px}

  label.ck{display:flex;align-items:center;gap:8px;color:var(--fg);margin-top:14px}
  label.ck input{width:auto}
  #logbox{margin:0;max-height:58vh;overflow:auto;white-space:pre-wrap;word-break:break-word;
    font-family:ui-monospace,Consolas,monospace;font-size:12px;line-height:1.45;color:var(--mut);
    background:var(--bg);border:1px solid var(--bd);border-radius:9px;padding:10px}
  .bar{height:8px;background:var(--bg);border:1px solid var(--bd);border-radius:6px;
       overflow:hidden;margin-top:12px}
  .bar>div{height:100%;width:0;background:var(--ac);transition:width .2s}
</style>
</head>
<body>
<header><h1>Print<span>Orb</span></h1></header>
<div class="wrap">
  <div class="tabs">
    <button id="tStatus" class="on" onclick="tab('status')">Status</button>
    <button id="tPrinters" onclick="tab('printers')">Printers</button>
    <button id="tSettings" onclick="tab('settings')">Settings</button>
    <button id="tSystem" onclick="tab('system')">System</button>
  </div>

  <!-- ============================ STATUS ============================ -->
  <div id="status">
    <div id="switcher" class="chips hide"></div>
    <div class="card">
      <div class="ring" id="gRing"><div class="ringin">
        <div class="pct" id="gPct">--%</div><div class="st" id="gState">connecting…</div>
      </div></div>
      <div class="kv"><span>Printer</span><b id="gName">—</b></div>
      <div class="kv"><span>File</span><b id="gFile">—</b></div>
      <div class="kv"><span>Nozzle</span><b id="gNoz">—</b></div>
      <div class="kv"><span>Bed</span><b id="gBed">—</b></div>
      <div class="kv"><span>Remaining</span><b id="gEta">—</b></div>
      <div class="kv"><span>Layer</span><b id="gLay">—</b></div>
    </div>
    <div id="amsBody"></div>
    <button type="button" class="primary danger" onclick="reboot()">&#9211; Reboot device</button>
    <div id="rmsg" class="hint"></div>
  </div>

  <!-- =========================== PRINTERS =========================== -->
  <div id="printers" class="hide">
    <div class="card">
      <h3>Add from the network</h3>
      <div class="row">
        <div><select id="discSel"><option value="">&mdash; discover &mdash;</option></select></div>
        <div style="flex:0 0 auto"><button type="button" class="ghost" onclick="discover()">&#8635; Find</button></div>
      </div>
      <div class="hint">mDNS discovery finds Moonraker and Bambu printers on the same LAN.
        Pick one and it is added as a new entry below.</div>
      <button type="button" class="ghost" style="margin-top:10px" onclick="addFromDisc()">+ Add selected</button>
    </div>

    <div id="plist"></div>
    <button type="button" class="ghost" id="addBtn" onclick="addPrinter()">+ Add printer</button>
    <button class="primary" type="button" onclick="savePrinters()">Save &amp; Reboot</button>
    <div id="pmsg" class="hint"></div>
  </div>

  <!-- =========================== SETTINGS =========================== -->
  <div id="settings" class="hide">
    <form id="form">
      <div class="card">
        <h3>WiFi</h3>
        <div class="kv"><span>Device IP</span><b id="wifiIp">—</b></div>
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
        <h3>Display</h3>
        <label>Brightness: <span id="brVal">100</span>%</label>
        <input type="range" min="10" max="100" name="brightness" id="brightness" value="100"
          oninput="document.getElementById('brVal').textContent=this.value">

        <label class="ck"><input type="checkbox" id="screenSleepEnabled" onchange="toggleSleep()"> Display auto-off (power-save)</label>
        <div id="sleepRow">
          <label>Sleep after (s) of inactivity</label>
          <input type="number" min="5" max="3600" name="screenTimeoutSec" id="screenTimeoutSec" value="120">
        </div>

        <label>Timezone (clock &amp; scheduled dimming)</label>
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
        <h3>Updates &amp; security</h3>
        <label class="ck"><input type="checkbox" id="autoUpdateCheck"> Check GitHub for new releases</label>
        <label>Update / OTA password</label>
        <input name="adminPassword" id="adminPassword" type="password" placeholder="(unchanged if blank)">
        <div class="hint">Required to enable firmware updates (web upload &amp; ArduinoOTA).
          Leave blank to keep the current one. <b>No password = OTA disabled.</b></div>
      </div>
      <button class="primary" type="submit">Save &amp; Reboot</button>
      <div id="msg" class="hint"></div>
    </form>
  </div>

  <!-- ============================ SYSTEM ============================ -->
  <div id="system" class="hide">
    <div class="card">
      <div class="phead"><b>System info</b>
        <button type="button" class="ghost" onclick="loadInfo()">&#8635; Refresh</button></div>
      <div id="infoBody"><div class="hint">Loading…</div></div>
    </div>

    <div class="card">
      <b>Firmware update (OTA)</b>
      <div class="hint">Upload a compiled <b>firmware.bin</b>. The device flashes it
        and reboots. Do not power off during the update. You'll be asked for the
        <b>OTA password</b> (set it in Settings first).</div>
      <label>Firmware file (.bin)</label>
      <input type="file" id="fwFile" accept=".bin">
      <button type="button" class="primary" onclick="uploadFw()">Upload &amp; flash</button>
      <div id="updBar" class="bar hide"><div id="updFill"></div></div>
      <div id="updMsg" class="hint"></div>
    </div>

    <div class="card">
      <div class="phead"><b>Device log</b>
        <button type="button" class="ghost" onclick="loadLog()">&#8635; Refresh</button></div>
      <pre id="logbox"></pre>
    </div>
  </div>
</div>

<script>
var TABS=['status','printers','settings','system'];
var P=[];               // printer list being edited on the Printers tab
var MAXP=4, ACTIVE=0, DISC=[];

function $(id){return document.getElementById(id);}
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,function(m){
  return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[m];});}

function tab(t){
  TABS.forEach(function(x){$(x).classList.toggle('hide',x!=t);});
  $('tStatus').classList.toggle('on',t=='status');
  $('tPrinters').classList.toggle('on',t=='printers');
  $('tSettings').classList.toggle('on',t=='settings');
  $('tSystem').classList.toggle('on',t=='system');
  if(t=='settings'&&!window._scanned){window._scanned=1;scan();}
  window._sysOn=(t=='system');
  if(t=='system'){loadLog();loadInfo();}
}

// ------------------------------- helpers -------------------------------
function kib(b){return (b/1024).toFixed(1)+' KiB';}
function mib(b){return (b/1048576).toFixed(2)+' MiB';}
function dur(s){var d=(s/86400)|0,h=((s%86400)/3600)|0,m=((s%3600)/60)|0;return (d?d+'d ':'')+(h?h+'h ':'')+m+'m';}
function kv(k,v){return '<div class="kv"><span>'+k+'</span><b>'+v+'</b></div>';}
function eta(s){if(s==null||s<0)return'—';var h=(s/3600)|0,m=((s%3600)/60)|0;return h?h+'h '+m+'m':m+'m';}
function bars(r){return r>=-55?'▮▮▮':r>=-65?'▮▮':r>=-75?'▮':'▫';}
function stateColor(s){
  return s=='Printing'?'var(--ac)':s=='Paused'?'var(--warn)':s=='Complete'?'var(--ok)':
         s=='Error'?'var(--err)':'var(--mut)';
}

// ------------------------------- status --------------------------------
function renderSwitcher(){
  var box=$('switcher');
  if(P.length<2){box.classList.add('hide');box.innerHTML='';return;}
  box.classList.remove('hide');
  box.innerHTML=P.map(function(p,i){
    var b=p.type=='bambu';
    return '<div class="chip'+(i==ACTIVE?' on':'')+'" onclick="selectPrinter('+i+')">'
      +'<span class="badge '+(b?'b':'k')+'">'+(b?'B':'K')+'</span>'
      +esc(p.name||p.ip)+'</div>';
  }).join('');
}
async function selectPrinter(i){
  if(i==ACTIVE)return;
  ACTIVE=i;renderSwitcher();
  try{await fetch('/api/printer/select?index='+i,{method:'POST'});}catch(e){}
  setTimeout(loadStatus,400);
}
async function loadStatus(){
  try{
    var d=await (await fetch('/api/status')).json();
    var pct=(d.state=='Offline')?0:Math.round(d.progress||0);
    var ring=$('gRing');
    ring.style.setProperty('--p',pct);
    ring.style.setProperty('--rc',stateColor(d.state));
    $('gPct').textContent=(d.state=='Offline'?'--':pct)+'%';
    $('gState').textContent=d.state;
    $('gName').textContent=d.printer||'—';
    $('gFile').textContent=d.file||'—';
    $('gNoz').textContent=Math.round(d.nozzle)+' / '+Math.round(d.nozzleTarget)+'°C';
    $('gBed').textContent=Math.round(d.bed)+' / '+Math.round(d.bedTarget)+'°C';
    $('gEta').textContent=eta(d.remaining);
    $('gLay').textContent=(d.totalLayer>0)?(d.layer+' / '+d.totalLayer):'—';
    if(d.activePrinter!=null&&d.activePrinter!=ACTIVE){ACTIVE=d.activePrinter;renderSwitcher();}
    renderAms(d.ams);
  }catch(e){}
}
async function reboot(){
  if(!confirm('Reboot PrintOrb now?'))return;
  var m=$('rmsg');m.textContent='Rebooting…';m.className='hint ok';
  try{await fetch('/api/restart',{method:'POST'});}catch(e){}
}

// --------------------------------- AMS ---------------------------------
function amsLum(c){return 0.299*parseInt(c.substr(1,2),16)+0.587*parseInt(c.substr(3,2),16)+0.114*parseInt(c.substr(5,2),16);}
function renderAms(a){
  var box=$('amsBody');
  if(!a||!a.present){box.innerHTML='';return;}
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
        h+='<div class="slot" style="'+st+'"><span class="ty">'+esc(s.type||'?')+'</span><span>'+(s.remain!=null?s.remain+'%':'')+'</span></div>';
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
          +'<button class="ghost rm" onclick="dry(\'stop\')">Stop drying</button></div>';
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

// ------------------------------ printers -------------------------------
function renderPrinters(){
  var box=$('plist');
  if(!P.length){box.innerHTML='<div class="card empty">No printer configured yet.<br>Add one below.</div>';}
  else{
    box.innerHTML=P.map(function(p,i){
      var b=p.type=='bambu';
      return '<div class="card">'
       +'<div class="phead"><span class="badge '+(b?'b':'k')+'">'+(b?'B':'K')+'</span>'
       +'<b>'+esc(p.name||p.ip||('Printer '+(i+1)))+'</b>'
       +(i==ACTIVE?'<span class="hint" style="margin:0;color:var(--ac)">shown on device</span>':'')
       +'<button type="button" class="ghost rm" onclick="rmPrinter('+i+')">Remove</button></div>'
       +'<label>Type</label>'
       +'<select onchange="setP('+i+',\'type\',this.value);renderPrinters()">'
       +'<option value="klipper"'+(b?'':' selected')+'>Klipper (Moonraker)</option>'
       +'<option value="bambu"'+(b?' selected':'')+'>Bambu Lab</option></select>'
       +'<label>Display name</label>'
       +'<input value="'+esc(p.name)+'" oninput="setP('+i+',\'name\',this.value)" placeholder="Workshop X1C">'
       +'<label>IP / hostname</label>'
       +'<input value="'+esc(p.ip)+'" oninput="setP('+i+',\'ip\',this.value)" placeholder="192.168.1.50 or printer.local">'
       +(b
         ? '<label>Serial number</label>'
          +'<input value="'+esc(p.bambuSerial)+'" oninput="setP('+i+',\'bambuSerial\',this.value)" placeholder="01P00A…">'
          +'<label>LAN access code</label>'
          +'<input type="password" value="'+esc(p.bambuAccessCode||'')+'" oninput="setP('+i+',\'bambuAccessCode\',this.value)"'
          +' placeholder="'+(p.bambuCodeSet?'•••••••• (unchanged if blank)':'from the printer screen')+'">'
          +'<div class="hint">Enable <b>LAN Mode</b> on the printer (Settings → WLAN); '
          +'serial &amp; access code are shown there.</div>'
         : '<label>Moonraker port</label>'
          +'<input type="number" value="'+(p.moonrakerPort||7125)+'" oninput="setP('+i+',\'moonrakerPort\',this.value)">'
          +'<label>API key (optional)</label>'
          +'<input value="'+esc(p.moonrakerApiKey)+'" oninput="setP('+i+',\'moonrakerApiKey\',this.value)">')
       +'</div>';
    }).join('');
  }
  $('addBtn').disabled=(P.length>=MAXP);
  $('addBtn').textContent=P.length>=MAXP?('Maximum of '+MAXP+' printers'):'+ Add printer';
  renderSwitcher();
}
function setP(i,k,v){P[i][k]=(k=='moonrakerPort')?(parseInt(v)||7125):v;}
function addPrinter(){
  if(P.length>=MAXP)return;
  P.push({type:'klipper',name:'',ip:'',moonrakerPort:7125,moonrakerApiKey:'',
          bambuSerial:'',bambuAccessCode:'',bambuCodeSet:false,codeFrom:-1});
  renderPrinters();
}
function rmPrinter(i){
  if(!confirm('Remove this printer?'))return;
  P.splice(i,1);
  if(ACTIVE>=P.length)ACTIVE=0;
  renderPrinters();
}
function addFromDisc(){
  var v=$('discSel').value;if(!v||P.length>=MAXP)return;
  var d=DISC[parseInt(v)];
  P.push({type:d.type,name:d.name||d.ip,ip:d.name||d.ip,
          moonrakerPort:(d.type=='klipper'&&d.port)?d.port:7125,
          moonrakerApiKey:'',bambuSerial:'',bambuAccessCode:'',bambuCodeSet:false,codeFrom:-1});
  renderPrinters();
}
async function savePrinters(){
  var m=$('pmsg');
  for(var i=0;i<P.length;i++){
    if(!(P[i].ip||'').trim()){m.textContent='Printer '+(i+1)+' needs an IP or hostname.';m.className='hint err';return;}
    if(P[i].type=='bambu'&&!P[i].bambuSerial.trim()){
      m.textContent='Printer '+(i+1)+' needs a Bambu serial number.';m.className='hint err';return;}
    if(P[i].type=='bambu'&&!P[i].bambuAccessCode&&!P[i].bambuCodeSet){
      m.textContent='Printer '+(i+1)+' needs a LAN access code.';m.className='hint err';return;}
  }
  m.textContent='Saving…';m.className='hint';
  var body={printers:P.map(function(p){return {
    type:p.type,name:p.name,ip:p.ip,moonrakerPort:p.moonrakerPort,
    moonrakerApiKey:p.moonrakerApiKey,bambuSerial:p.bambuSerial,
    bambuAccessCode:p.bambuAccessCode||'',codeFrom:p.codeFrom};}),activePrinter:ACTIVE};
  try{
    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
                                     body:JSON.stringify(body)});
    if(r.ok){m.textContent='Saved. Rebooting…';m.className='hint ok';}
    else{m.textContent='Save failed.';m.className='hint err';}
  }catch(e){m.textContent='Error: '+e;m.className='hint err';}
}
async function discover(){
  var sel=$('discSel');sel.innerHTML='<option>finding…</option>';
  try{
    var d=await (await fetch('/api/discover')).json();
    DISC=d.printers||[];
    sel.innerHTML='<option value="">'+(DISC.length?'— select —':'— none found —')+'</option>';
    DISC.forEach(function(p,i){
      var o=document.createElement('option');o.value=i;
      o.textContent=p.type+': '+(p.name||p.ip)+' ('+p.ip+')';
      sel.appendChild(o);
    });
  }catch(e){sel.innerHTML='<option value="">— error —</option>';}
}

// ------------------------------- settings ------------------------------
function toggleSleep(){$('sleepRow').classList.toggle('hide',!$('screenSleepEnabled').checked);}
function toggleDim(){$('dimRows').classList.toggle('hide',!$('dimSchedEnabled').checked);}
function hm2min(v){var p=(v||'0:0').split(':');return (parseInt(p[0])||0)*60+(parseInt(p[1])||0);}
function min2hm(m){m=m||0;var h=(m/60)|0,i=m%60;return (h<10?'0':'')+h+':'+(i<10?'0':'')+i;}
async function scan(){
  var sel=$('ssidSel');
  sel.innerHTML='<option>scanning…</option>';
  for(var i=0;i<20;i++){
    var d=await (await fetch('/api/scan')).json();
    if(d.scanning){await new Promise(function(r){setTimeout(r,1000);});continue;}
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
function pickSsid(){var v=$('ssidSel').value;if(v)$('wifiSsid').value=v;}

// -------------------------------- system -------------------------------
async function loadInfo(){
  try{
    var d=await (await fetch('/api/sysinfo')).json();
    var n=d.net||{},m=d.mem||{},f=d.flash||{},c=d.chip||{},t=d.time||{},p=d.printer||{};
    $('wifiIp').textContent=n.ip||'—';
    $('wifiMdns').textContent=n.mdns||'—';
    $('wifiRssi').textContent=(n.rssi!=null?n.rssi+' dBm':'—');
    var h='';
    h+=kv('Firmware',esc(d.firmware||'—')+(d.updateAvailable?' <span class="ok">→ '+esc(d.latestVersion)+'</span>':''));
    h+=kv('SDK',esc(d.sdk||'—'));
    h+=kv('Uptime',dur(d.uptimeSec||0));
    h+=kv('Reset reason',esc(d.resetReason||'—'));
    h+=kv('Printer',esc(p.name||'—')+' · '+esc(p.type||'—')+' · '+esc(p.state||'—')
        +(p.count>1?' ('+(p.index+1)+'/'+p.count+')':''));
    h+=kv('Mode',esc(n.mode||'—'));
    h+=kv('IP',esc(n.ip||'—'));
    h+=kv('mDNS',esc(n.mdns||'—'));
    h+=kv('MAC',esc(n.mac||'—'));
    h+=kv('SSID',esc(n.ssid||'—')+(n.channel?' (ch '+n.channel+')':''));
    h+=kv('RSSI',(n.rssi!=null?n.rssi+' dBm':'—'));
    h+=kv('Heap free',kib(m.heapFree||0)+' / '+kib(m.heapSize||0));
    h+=kv('Heap min ever',kib(m.heapMin||0));
    h+=kv('Heap max block',kib(m.heapMaxBlk||0));
    h+=kv('PSRAM free',kib(m.psramFree||0)+' / '+kib(m.psramSize||0));
    h+=kv('Flash size',mib(f.flashSize||0));
    h+=kv('Sketch',mib(f.sketchSize||0)+' used, '+mib(f.sketchFree||0)+' free');
    h+=kv('Chip',esc(c.model||'—')+' rev '+(c.rev!=null?c.rev:'?')+', '+(c.cores||'?')+' cores @ '+(c.cpuMhz||'?')+' MHz');
    h+=kv('NTP',(t.synced?('synced'+(t.local?' · '+t.local:'')):'not synced'));
    $('infoBody').innerHTML=h;
  }catch(e){$('infoBody').innerHTML='<div class="hint err">Failed to load.</div>';}
}
async function loadLog(){
  try{
    var t=await (await fetch('/api/log')).text();
    var box=$('logbox');
    var atBottom=box.scrollTop+box.clientHeight>=box.scrollHeight-20;
    box.textContent=t;
    if(atBottom)box.scrollTop=box.scrollHeight;
  }catch(e){}
}
function uploadFw(){
  var f=$('fwFile').files[0];
  if(!f){alert('Pick a .bin file first');return;}
  var pw=prompt('Update password (the OTA password set in Settings):');
  if(pw===null)return;
  var bar=$('updBar'),fill=$('updFill'),m=$('updMsg');
  bar.classList.remove('hide');m.className='hint';m.textContent='Uploading…';
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

// --------------------------------- boot --------------------------------
async function loadConfig(){
  try{
    var d=await (await fetch('/api/config')).json();
    MAXP=d.maxPrinters||4;
    ACTIVE=d.activePrinter||0;
    P=(d.printers||[]).map(function(p,i){
      return {type:p.type||'klipper',name:p.name||'',ip:p.ip||'',
              moonrakerPort:p.moonrakerPort||7125,moonrakerApiKey:p.moonrakerApiKey||'',
              bambuSerial:p.bambuSerial||'',bambuAccessCode:'',bambuCodeSet:!!p.bambuCodeSet,
              codeFrom:i};   // slot this entry came from, so a blank code keeps its own
    });
    renderPrinters();
    ['wifiSsid','hostname','screenTimeoutSec','brightness'].forEach(function(k){
      if(d[k]!=null)$(k).value=d[k];});
    $('brVal').textContent=d.brightness;
    $('adminPassword').placeholder=d.adminPwSet?'•••••••• (set — blank to keep)':'set a password to enable OTA';
    $('screenSleepEnabled').checked=(d.screenSleepEnabled!==false);
    $('autoUpdateCheck').checked=(d.autoUpdateCheck!==false);
    if(d.timezone!=null)$('timezone').value=d.timezone;
    $('dimSchedEnabled').checked=!!d.dimSchedEnabled;
    $('dimStart').value=min2hm(d.dimStartMin);
    $('dimEnd').value=min2hm(d.dimEndMin);
    var db=(d.dimBrightness!=null?d.dimBrightness:20);
    $('dimBrightness').value=db;$('dbVal').textContent=db;
    toggleSleep();toggleDim();
  }catch(e){}
}
$('form').addEventListener('submit',async function(e){
  e.preventDefault();
  var fd=new FormData(e.target),o={};
  fd.forEach(function(v,k){o[k]=v;});
  o.brightness=parseInt(o.brightness||'100');
  o.screenTimeoutSec=parseInt(o.screenTimeoutSec||'120');
  o.screenSleepEnabled=$('screenSleepEnabled').checked;
  o.autoUpdateCheck=$('autoUpdateCheck').checked;
  o.dimSchedEnabled=$('dimSchedEnabled').checked;
  o.dimStartMin=hm2min($('dimStart').value);
  o.dimEndMin=hm2min($('dimEnd').value);
  o.dimBrightness=parseInt($('dimBrightness').value||'20');
  var m=$('msg');
  m.textContent='Saving…';m.className='hint';
  try{
    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
    if(r.ok){m.textContent='Saved. Rebooting…';m.className='hint ok';}
    else{m.textContent='Save failed.';m.className='hint err';}
  }catch(e){m.textContent='Error: '+e;m.className='hint err';}
});
loadConfig();loadStatus();loadInfo();
setInterval(loadStatus,2000);
setInterval(function(){if(window._sysOn){loadLog();loadInfo();}},3000);
</script>
</body>
</html>
)HTML";
