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
  .hide{display:none}
  .gauge{font-size:54px;font-weight:700;text-align:center;margin:6px 0}
  .state{text-align:center;color:var(--ac);font-size:16px;margin-bottom:14px}
  .kv{display:flex;justify-content:space-between;padding:7px 0;border-bottom:1px solid var(--bd);font-size:14px}
  .kv:last-child{border:0}.kv span{color:var(--mut)}
  .hint{font-size:12px;color:var(--mut);margin-top:6px;line-height:1.5}
  .ok{color:#3fb950}.err{color:#f85149}
</style>
</head>
<body>
<header><h1>Print<span>Orb</span></h1></header>
<div class="wrap">
  <div class="tabs">
    <button id="tStatus" class="on" onclick="tab('status')">Status</button>
    <button id="tSettings" onclick="tab('settings')">Settings</button>
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
  </div>

  <!-- SETTINGS -->
  <div id="settings" class="hide">
    <form id="form">
      <div class="card">
        <b>WiFi</b>
        <label>SSID</label><input name="wifiSsid" id="wifiSsid">
        <label>Password</label><input name="wifiPass" id="wifiPass" type="password" placeholder="(unchanged if blank)">
      </div>
      <div class="card">
        <b>Printer</b>
        <label>Type</label>
        <select name="printerType" id="printerType" onchange="ptype()">
          <option value="klipper">Klipper (Moonraker)</option>
          <option value="bambu">Bambu Lab</option>
        </select>
        <label>Display name</label><input name="printerName" id="printerName">
        <label>Printer IP</label><input name="printerIp" id="printerIp" placeholder="192.168.1.50">

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
      </div>
      <button class="primary" type="submit">Save &amp; Reboot</button>
      <div id="msg" class="hint"></div>
    </form>
  </div>
</div>

<script>
function tab(t){
  document.getElementById('status').classList.toggle('hide',t!='status');
  document.getElementById('settings').classList.toggle('hide',t!='settings');
  document.getElementById('tStatus').classList.toggle('on',t=='status');
  document.getElementById('tSettings').classList.toggle('on',t=='settings');
}
function ptype(){
  var b=document.getElementById('printerType').value=='bambu';
  document.getElementById('bamb').classList.toggle('hide',!b);
  document.getElementById('klip').classList.toggle('hide',b);
}
function eta(s){ if(s<0)return'—';var h=(s/3600)|0,m=((s%3600)/60)|0;return h?h+'h '+m+'m':m+'m';}
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
  }catch(e){}
}
async function loadConfig(){
  try{
    var r=await fetch('/api/config'); var d=await r.json();
    for(var k in d){var el=document.getElementById(k); if(el&&k!='wifiPass') el.value=d[k];}
    document.getElementById('brVal').textContent=d.brightness;
    ptype();
  }catch(e){}
}
document.getElementById('form').addEventListener('submit',async function(e){
  e.preventDefault();
  var fd=new FormData(e.target), o={};
  fd.forEach((v,k)=>o[k]=v);
  o.moonrakerPort=parseInt(o.moonrakerPort||'7125');
  o.brightness=parseInt(o.brightness||'100');
  var m=document.getElementById('msg');
  m.textContent='Saving…';m.className='hint';
  try{
    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
    if(r.ok){m.textContent='Saved. Rebooting…';m.className='hint ok';}
    else{m.textContent='Save failed.';m.className='hint err';}
  }catch(e){m.textContent='Error: '+e;m.className='hint err';}
});
loadConfig();loadStatus();setInterval(loadStatus,2000);
</script>
</body>
</html>
)HTML";
