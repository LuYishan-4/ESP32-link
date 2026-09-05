// lib/webservice/html_pages.h
// PROGMEM-stored single-file HTML/CSS/JS control page (adapted from the
// "hackweb" control-panel design). Talks to the ESP32 web API:
//   GET  /api/config    -> fill the form / show preview
//   POST /api/config    -> persist settings (role, node id, WiFi, host upload)
//   GET  /api/status    -> live status (STA, AP, children, ip)
//   POST /api/reboot    -> reboot the device
#ifndef HTML_PAGES_H
#define HTML_PAGES_H

#include <Arduino.h>

static const char HTML_PAGE[] PROGMEM = R"htmlstr(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP NODE 設定中心</title>
<style>
:root {
  color-scheme: light;
  --ink: #171715; --muted: #6e716d; --soft: #a2a49f; --paper: #f3f4f1;
  --white: #fff; --line: #dedfda; --dark: #1b1d1a; --red: #ff5149;
  --red-dark: #dd372f; --red-soft: #fff0ee; --green: #19a974; --amber: #d99118;
  --shadow: 0 18px 45px rgba(22, 23, 20, .08);
  font-family: Inter, Arial, "Noto Sans TC", "Microsoft JhengHei", sans-serif;
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; min-width: 320px; background: var(--paper); color: var(--ink); font-size: 16px; }
button, input, select { font: inherit; } button, summary, label { -webkit-tap-highlight-color: transparent; } button { cursor: pointer; }
.app-shell { min-height: 100vh; display: grid; grid-template-columns: 245px minmax(0, 1fr); }
.sidebar { position: sticky; top: 0; height: 100vh; display: flex; flex-direction: column; padding: 32px 22px 22px; color: #fff; background: var(--dark); z-index: 20; }
.brand { display: flex; align-items: center; gap: 12px; color: inherit; text-decoration: none; padding: 0 8px 48px; }
.brand strong, .brand small { display: block; } .brand strong { font-size: 17px; letter-spacing: .04em; } .brand small { margin-top: 4px; color: #8c918b; font-size: 10px; letter-spacing: .19em; }
.brand-mark { width: 37px; height: 37px; display: grid; grid-template-columns: repeat(3, 1fr); align-items: end; gap: 3px; transform: skew(-10deg); }
.brand-mark i { display: block; background: var(--red); border-radius: 2px; } .brand-mark i:nth-child(1){height:52%}.brand-mark i:nth-child(2){height:100%}.brand-mark i:nth-child(3){height:73%}
nav { display: grid; gap: 7px; } .nav-link { display: flex; align-items: center; gap: 13px; padding: 13px 14px; border-radius: 8px; color: #a7aaa4; text-decoration: none; font-size: 14px; font-weight: 700; transition: .2s ease; }
.nav-link span { color: #666b65; font-size: 11px; font-variant-numeric: tabular-nums; } .nav-link:hover,.nav-link.active { color: #fff; background: rgba(255,255,255,.07); } .nav-link.active { box-shadow: inset 3px 0 var(--red); } .nav-link.active span { color: var(--red); }
.device-card { margin-top: auto; display: flex; align-items: center; gap: 11px; padding: 14px; border: 1px solid #353834; border-radius: 10px; background: #232522; }
.device-card small,.device-card strong { display: block; } .device-card small { color: #777c76; font-size: 11px; } .device-card strong { margin-top: 3px; font-size: 13px; } .version { margin: 15px 8px 0; color: #666a65; font-size: 10px; }
.status-light { width: 9px; height: 9px; flex: 0 0 auto; border-radius: 50%; background: var(--green); box-shadow: 0 0 0 5px rgba(25,169,116,.12); } .status-light.idle { background: #878b86; box-shadow: none; } .status-light.error { background: var(--red); box-shadow: 0 0 0 5px rgba(255,81,73,.12); }
main { min-width: 0; }
.topbar { position: sticky; top: 0; z-index: 15; min-height: 99px; display: flex; align-items: center; justify-content: space-between; gap: 22px; padding: 22px 34px; border-bottom: 1px solid var(--line); background: rgba(255,255,255,.9); backdrop-filter: blur(18px); }
.topbar p,.section-heading p,.panel-heading p { margin: 0 0 5px; color: #888b86; font-size: 10px; font-weight: 850; letter-spacing: .19em; } .topbar h1 { margin: 0; font-size: 25px; letter-spacing: -.035em; }
.top-status { display: flex; align-items: center; gap: 10px; padding: 9px 13px; border: 1px solid var(--line); border-radius: 9px; background: #fff; } .top-status small,.top-status strong { display:block; } .top-status small { color: var(--muted); font-size: 9px; } .top-status strong { margin-top: 2px; font-size: 12px; } .menu-button { display: none; border: 0; background: none; font-size: 22px; }
.workspace { width: min(1480px, 100%); margin: 0 auto; display: grid; grid-template-columns: minmax(560px, 1fr) 390px; align-items: start; gap: 22px; padding: 28px 34px 48px; }
.content,.status-panel,.preview-panel { border: 1px solid var(--line); background: var(--white); box-shadow: var(--shadow); } .content { padding: 27px 30px 30px; }
.section-heading { display: flex; justify-content: space-between; align-items: center; gap: 20px; padding-bottom: 24px; border-bottom: 1px solid var(--line); } .section-heading > div:first-child { display: flex; align-items: center; gap: 15px; }
.section-number { display:grid; place-items:center; width: 42px; height:42px; border-radius:50%; background:var(--red-soft); color:var(--red); font-size:12px; font-weight:900; } .section-heading h2,.panel-heading h2 { margin:0; font-size:21px; letter-spacing:-.025em; }
fieldset { padding:0; margin:0; border:0; } .mode-fieldset { margin: 25px 0 2px; } .mode-fieldset legend { margin-bottom: 10px; color:var(--muted); font-size:12px; font-weight:800; }
.mode-selector { display:grid; grid-template-columns:1fr 1fr; gap:11px; } .mode-selector label { cursor:pointer; } .mode-selector input { position:absolute; opacity:0; }
.mode-selector span { display:flex; align-items:center; justify-content:space-between; gap:10px; min-height:67px; padding:14px 16px; border:1px solid var(--line); border-radius:9px; transition:.18s ease; }
.mode-selector b,.mode-selector small { display:block; } .mode-selector b { font-size:16px; } .mode-selector small { color:var(--muted); font-size:11px; font-weight:600; text-align:right; }
.mode-selector input:checked + span { border-color:var(--red); background:var(--red-soft); box-shadow:0 0 0 2px rgba(255,81,73,.08); } .mode-selector input:focus-visible + span { outline:3px solid rgba(255,81,73,.24); outline-offset:2px; }
.form-section { margin-top: 27px; } .form-section-title { display:flex; align-items:baseline; gap:10px; margin-bottom:14px; } .form-section-title span { font-size:15px; font-weight:850; } .form-section-title small { color:var(--soft); font-size:11px; }
.fields { display:grid; gap:16px; } .two-columns { grid-template-columns:1fr 1fr; } .span-2 { grid-column:1 / -1; } .field { display:grid; align-content:start; gap:7px; min-width:0; }
.field > span { font-size:13px; font-weight:750; } .field > span i { color:var(--soft); font-size:10px; font-style:normal; font-weight:600; } .field > small { color:#92958f; font-size:10px; line-height:1.4; }
.field input,.field select { width:100%; height:43px; border:1px solid #d6d8d2; border-radius:7px; outline:0; background:#fbfcfa; color:var(--ink); padding:0 12px; font-size:14px; transition:.18s; }
.field input:focus,.field select:focus { border-color:var(--red); box-shadow:0 0 0 3px rgba(255,81,73,.1); background:#fff; }
.field-error { min-height:0; color:var(--red-dark); font-size:10px; font-style:normal; } .password-field { position:relative; } .password-field input { padding-right:56px; }
.reveal-button { position:absolute; right:6px; top:6px; height:31px; padding:0 8px; border:0; border-radius:5px; background:#eceeea; color:#5f635d; font-size:10px; font-weight:800; }
.form-actions { display:flex; justify-content:flex-end; gap:10px; margin-top:27px; padding-top:22px; border-top:1px solid var(--line); } .button { min-height:43px; border-radius:7px; padding:0 17px; font-size:13px; font-weight:850; transition:.18s ease; }
.button:disabled { opacity:.55; cursor:wait; } .button.primary { border:1px solid var(--red); background:var(--red); color:#fff; } .button.primary:hover { background:var(--red-dark); } .button.dark { border:1px solid var(--dark); background:var(--dark); color:#fff; }
.button.secondary { margin-right:auto; border:1px solid var(--line); background:#fff; color:var(--ink); } .button.secondary span { margin-right:5px; color:var(--red); }
.inspector { position:sticky; top:127px; display:grid; gap:17px; } .status-panel,.preview-panel { padding:21px; } .panel-heading { display:flex; align-items:flex-start; justify-content:space-between; gap:14px; } .panel-heading h2 { font-size:18px; }
.status-badge { padding:5px 8px; border-radius:5px; font-size:10px; font-weight:850; } .status-badge.idle { color:#676b65; background:#eeefec; } .status-badge.testing { color:#98630a; background:#fff4dc; } .status-badge.success { color:#087b52; background:#e4f7ef; } .status-badge.error { color:#bc302a; background:var(--red-soft); }
.connection-steps { list-style:none; margin:22px 0 17px; padding:0; } .connection-steps li { position:relative; display:grid; grid-template-columns:auto 1fr; gap:11px; min-height:60px; }
.connection-steps li:not(:last-child)::after { content:""; position:absolute; left:14px; top:30px; width:1px; height:30px; background:#dfe1dc; } .step-icon { position:relative; z-index:1; width:29px; height:29px; display:grid; place-items:center; border-radius:50%; background:#eeefec; color:#878b85; font-size:10px; font-weight:900; }
.connection-steps strong,.connection-steps small { display:block; } .connection-steps strong { padding-top:1px; font-size:13px; } .connection-steps small { margin-top:4px; color:var(--muted); font-size:10px; }
.connection-steps li.active .step-icon { color:#fff; background:var(--amber); box-shadow:0 0 0 4px rgba(217,145,24,.12); } .connection-steps li.done .step-icon { color:#fff; background:var(--green); } .connection-steps li.error .step-icon { color:#fff; background:var(--red); }
.latency { display:flex; align-items:baseline; justify-content:space-between; padding:13px 14px; border-radius:7px; background:#f5f6f3; } .latency span { color:var(--muted); font-size:11px; } .latency strong { font-size:18px; font-variant-numeric:tabular-nums; }
.preview-panel pre { max-height:225px; overflow:auto; margin:18px 0 14px; padding:15px; border-radius:7px; background:#1f211e; color:#cfd5ca; font:11px/1.55 "Cascadia Mono",Consolas,monospace; scrollbar-width:thin; }
.icon-text-button { border:0; background:none; color:var(--red-dark); font-size:11px; font-weight:850; } .preview-actions { display:grid; gap:11px; } .preview-actions button { width:100%; height:37px; border:1px solid var(--line); border-radius:6px; background:#fff; font-size:11px; font-weight:800; }
.saved-note { display:flex; gap:11px; align-items:center; padding:15px 17px; border:1px solid #d7e9df; border-radius:8px; background:#f0faf5; } .saved-note > span { width:27px; height:27px; display:grid; place-items:center; border-radius:50%; background:var(--green); color:#fff; font-size:12px; }
.saved-note p { margin:0; } .saved-note strong,.saved-note small { display:block; } .saved-note strong { font-size:12px; } .saved-note small { margin-top:3px; color:#6f867a; font-size:9px; }
.toast { position:fixed; right:24px; bottom:24px; z-index:100; max-width:360px; padding:13px 16px; border-radius:8px; background:var(--dark); color:#fff; box-shadow:var(--shadow); font-size:12px; opacity:0; transform:translateY(12px); pointer-events:none; transition:.22s; }
.toast.show { opacity:1; transform:translateY(0); } .toast.error { background:#9e2722; } [hidden] { display:none !important; }
@media (max-width:1100px) { .workspace { grid-template-columns:1fr; } .inspector { position:static; grid-template-columns:1fr 1fr; } .saved-note { grid-column:1/-1; } }
@media (max-width:760px) { .app-shell { display:block; } .sidebar { position:fixed; left:0; transform:translateX(-100%); width:245px; transition:.22s; } .sidebar.open { transform:translateX(0); } .menu-button { display:block; } .topbar { padding:17px 19px; min-height:82px; justify-content:flex-start; } .topbar h1 { font-size:20px; } .top-status { margin-left:auto; } .workspace { padding:18px 14px 35px; } .content { padding:21px 17px; } .section-heading { align-items:flex-start; } .section-number { display:none; } .two-columns,.inspector { grid-template-columns:1fr; } .span-2 { grid-column:auto; } .form-actions { flex-wrap:wrap; } .button.secondary { width:100%; margin:0; } .button.primary,.button.dark { flex:1; } .saved-note { grid-column:auto; } }
@media (max-width:480px) { .mode-selector { grid-template-columns:1fr; } .section-heading { display:grid; } .top-status { display:none; } .form-section-title { display:grid; gap:3px; } .form-actions .button { width:100%; flex:auto; } }
@media (prefers-reduced-motion:reduce) { *,*::before,*::after { scroll-behavior:auto !important; transition-duration:.01ms !important; } }
</style>
</head>
<body>
<div class="app-shell">
  <aside class="sidebar">
    <a class="brand" href="#settings" aria-label="ESP NODE 設定中心首頁">
      <span class="brand-mark" aria-hidden="true"><i></i><i></i><i></i></span>
      <span><strong>ESP NODE</strong><small>CONTROL PANEL</small></span>
    </a>
    <nav aria-label="設定導覽">
      <a class="nav-link active" href="#settings"><span>01</span>連線設定</a>
      <a class="nav-link" href="#vps-status"><span>02</span>連線狀態</a>
      <a class="nav-link" href="#config-preview"><span>03</span>設定預覽</a>
    </nav>
    <div class="device-card">
      <span class="status-light idle" id="sidebarLight"></span>
      <div><small>本機節點</small><strong id="sidebarStatus">讀取中…</strong></div>
    </div>
    <p class="version">ESP NODE Firmware</p>
  </aside>

  <main>
    <header class="topbar">
      <button class="menu-button" id="menuButton" type="button" aria-label="開啟選單">☰</button>
      <div><p>DEVICE CONFIGURATION</p><h1>ESP NODE 設定中心</h1></div>
      <div class="top-status"><span class="status-light" id="topLight"></span>
        <div><small>本機 IP</small><strong id="topIp">--</strong></div></div>
    </header>

    <div class="workspace">
      <section class="content" id="settings">
        <div class="section-heading">
          <div><span class="section-number">01</span><div><p>CONNECTION SETUP</p><h2>裝置與 Host 連線設定</h2></div></div>
        </div>

        <form id="settingsForm" novalidate>
          <fieldset class="mode-fieldset">
            <legend>節點模式</legend>
            <div class="mode-selector">
              <label><input type="radio" name="nodeMode" value="master" checked /><span><b>主機</b><small>建立網路並上傳資料到 Host</small></span></label>
              <label><input type="radio" name="nodeMode" value="slave" /><span><b>從機</b><small>配對主機並轉送感測資料</small></span></label>
            </div>
          </fieldset>

          <div class="form-section">
            <div class="form-section-title"><span>裝置</span><small>辨識這台 ESP NODE</small></div>
            <div class="fields two-columns">
              <label class="field"><span>裝置 ID (Node ID)</span>
                <input id="deviceId" name="deviceId" type="text" maxlength="8" required pattern="[A-Za-z0-9_-]+" />
                <small>限 8 字元內英數與 - _</small></label>
              <label class="field slave-only" hidden><span>主機 Node ID</span>
                <input id="targetId" name="targetId" type="text" maxlength="8" placeholder="主機的 Node ID" /></label>
              <label class="field slave-only" hidden><span>盆栽 / 節點名稱</span>
                <input id="nodeLabel" name="nodeLabel" type="text" maxlength="48" placeholder="Pot A" /></label>
              <label class="field slave-only" hidden><span>Transfer token（綁定用）</span>
                <div class="password-field"><input id="slaveTok" type="password" autocomplete="off" /><button class="reveal-button" type="button" data-target="slaveTok">顯示</button></div>
                <small>連上主機時自動交給主機代送綁定</small></label>
              <label class="field"><span>群組密碼 (連 NODE 網路)</span>
                <div class="password-field"><input id="apPsk" name="apPsk" type="password" maxlength="64" /><button class="reveal-button" type="button" data-target="apPsk">顯示</button></div>
                <small>主機與從機共用，也是 Wi-Fi 密碼</small></label>
            </div>
          </div>

          <div class="form-section host-only">
            <div class="form-section-title"><span>Wi-Fi 上行</span><small>主機要連上的外部網路（選填）</small></div>
            <div class="fields two-columns">
              <label class="field"><span>Wi-Fi SSID</span><input id="wifiSsid" name="wifiSsid" type="text" placeholder="外部網路 SSID" /></label>
              <label class="field"><span>Wi-Fi 密碼</span>
                <div class="password-field"><input id="wifiPassword" name="wifiPassword" type="password" placeholder="密碼" /><button class="reveal-button" type="button" data-target="wifiPassword">顯示</button></div></label>
              <label class="field"><span>位址配置</span><select id="ipMode" name="ipMode"><option value="dhcp">DHCP</option><option value="static">固定 IP</option></select></label>
              <label class="field static-ip-field" hidden><span>固定 IP / 閘道 / 遮罩</span>
                <div style="display:grid;gap:8px">
                  <input id="sIp" type="text" placeholder="IP，例 192.168.1.80" />
                  <input id="sGw" type="text" placeholder="Gateway，例 192.168.1.1" />
                  <input id="sMask" type="text" placeholder="Subnet，例 255.255.255.0" />
                </div></label>
            </div>
          </div>

          <div class="form-section host-only">
            <div class="form-section-title"><span>資料收集 Host</span><small>接收並寫入 SQL 的伺服器</small></div>
            <div class="fields">
              <label class="field" style="display:flex;align-items:center;gap:8px;flex-direction:row">
                <input type="checkbox" id="hostEnabled" style="width:auto"> 啟用上傳到 Host</label>
              <label class="field"><span>Broker URL（WSS）</span>
                <input id="hostUrl" type="text" placeholder="wss://mqtt.rabbitsayhello.me/" /></label>
              <label class="field"><span>MQTT 密碼 <i>enrollment 取得</i></span>
                <div class="password-field"><input id="hostToken" type="password" autocomplete="off" /><button class="reveal-button" type="button" data-target="hostToken">顯示</button></div></label>
              <label class="field"><span>Master ID</span>
                <input id="masterId" type="text" maxlength="32" placeholder="master-001" /></label>
              <label class="field"><span>Enrollment token（一次性）</span>
                <div class="password-field"><input id="enrollToken" type="password" autocomplete="off" /><button class="reveal-button" type="button" data-target="enrollToken">顯示</button></div></label>
            </div>
          </div>

          <div class="form-section host-only">
            <div class="form-section-title"><span>中繼 / 節點</span><small>（從機模式下才有意義）</small></div>
            <div class="fields two-columns">
              <label class="field"><span>Relay</span><select id="relay">
                <option value="0">關閉</option><option value="1">開啟（手動）</option><option value="2">自動</option></select></label>
              <label class="field"><span>自動提升門檻</span><input id="relayThr" type="number" min="1" max="8" /></label>
            </div>
          </div>

          <div class="form-actions">
            <button class="button secondary" id="testButton" type="button"><span>◌</span>讀取 / 測試</button>
            <button class="button primary" type="submit">儲存設定</button>
            <button class="button dark" id="rebootBtn" type="button">套用並重新啟動</button>
          </div>
        </form>
      </section>

      <aside class="inspector">
        <section class="status-panel" id="vps-status">
          <div class="panel-heading"><div><p>LIVE STATUS</p><h2>連線狀態</h2></div><span class="status-badge idle" id="overallBadge">讀取中</span></div>
          <ol class="connection-steps">
            <li data-step="config"><span class="step-icon">1</span><div><strong>設定檢查</strong><small id="stConfig">等待讀取</small></div></li>
            <li data-step="wifi"><span class="step-icon">2</span><div><strong>Wi-Fi</strong><small id="stWifi">尚未連線</small></div></li>
            <li data-step="host"><span class="step-icon">3</span><div><strong>Host 上傳</strong><small id="stHost">未啟用</small></div></li>
            <li data-step="ready"><span class="step-icon">4</span><div><strong>資料通道</strong><small id="stChannel">等待節點</small></div></li>
          </ol>
          <div class="latency"><span>最近延遲</span><strong id="latencyValue">-- ms</strong></div>
        </section>

        <section class="preview-panel" id="config-preview">
          <div class="panel-heading"><div><p>CONFIG PREVIEW</p><h2>設定預覽</h2></div><button id="copyButton" class="icon-text-button" type="button">複製</button></div>
          <pre><code id="configJson">（尚未讀取）</code></pre>
          <div class="saved-note"><span>✓</span><p><strong id="saveState">尚未寫入裝置</strong><small id="saveTime">從 /api/config 讀取</small></p></div>
        </section>
      </aside>
    </div>
  </main>
</div>
<div class="toast" id="toast" role="status" aria-live="polite"></div>
<script>
"use strict";
const $ = (id) => document.getElementById(id);
const $q = (sel) => document.querySelector(sel);
const $qa = (sel) => Array.prototype.slice.call(document.querySelectorAll(sel));
let lastStatus = null;
let lastConfig = null;

async function jget(u){ const r = await fetch(u); if(!r.ok) throw new Error("HTTP "+r.status); return r.json(); }
async function jpost(u, o){ const r = await fetch(u, { method:"POST", headers:{ "Content-Type":"application/json" }, body: JSON.stringify(o||{}) }); return r.json(); }

function toast(msg, type){ const t=$("toast"); t.textContent=msg; t.className="toast show"+(type==="error"?" error":""); clearTimeout(toast._t); toast._t=setTimeout(function(){ t.className="toast"; }, 3200); }
function mode(){ const el=$q('input[name=nodeMode]:checked'); return el ? el.value : "master"; }
function stepState(li, cls){ li.className = cls; }
function setStep(name, cls, text){ const li=$q('[data-step="'+name+'"]'); if(li) stepState(li, cls); const id = { config:"stConfig", wifi:"stWifi", host:"stHost", ready:"stChannel" }[name]; if(id && text!=null) $(id).textContent = text; }

function refreshModeUI(){
  const host = mode() === "master";
  $qa(".host-only").forEach(function(el){ el.hidden = !host; });
  $qa(".slave-only").forEach(function(el){ el.hidden = host; });
  $q(".static-ip-field").hidden = $("ipMode").value !== "static";
}
function fillForm(c){
  const m = c.role === "slave" ? "slave" : "master";
  const radio = $q('input[name=nodeMode][value="'+m+'"]'); if(radio) radio.checked = true;
  $("deviceId").value = c.node_id || "";
  $("targetId").value = c.target_id || "";
  $("apPsk").value = c.ap_psk || "";
  $("wifiSsid").value = c.upstream_ssid || "";
  $("wifiPassword").value = c.upstream_psk || "";
  $("ipMode").value = c.ip_mode === "static" ? "static" : "dhcp";
  $("sIp").value = c.ip || "";
  $("sGw").value = c.gateway || "";
  $("sMask").value = c.subnet || "";
  $("relay").value = String(c.relay || 0);
  $("relayThr").value = c.relay_threshold != null ? c.relay_threshold : "";
  $("hostEnabled").checked = !!c.host_enabled;
  $("hostUrl").value = c.host_url || "wss://mqtt.rabbitsayhello.me/";
  $("hostToken").value = c.host_token || "";
  $("masterId").value = c.master_id || "master-001";
  $("enrollToken").value = "";   // one-time token: never prefill from device
  $("nodeLabel").value = c.node_label || "";
  $("slaveTok").value = "";      // transfer token: never prefill from device
  refreshModeUI();
}
function collect(){
  return {
    role: mode(),
    node_id: $("deviceId").value.trim(),
    target_id: $("targetId").value.trim(),
    upstream_ssid: $("wifiSsid").value.trim(),
    upstream_psk: $("wifiPassword").value,
    ip_mode: $("ipMode").value,
    ip: $("sIp").value.trim(),
    gateway: $("sGw").value.trim(),
    subnet: $("sMask").value.trim(),
    relay: parseInt($("relay").value || "0", 10),
    relay_threshold: parseInt($("relayThr").value || "3", 10),
    ap_psk: $("apPsk").value.trim(),
    host_enabled: $("hostEnabled").checked,
    host_url: $("hostUrl").value.trim(),
    host_token: $("hostToken").value.trim(),
    master_id: $("masterId").value.trim() || "master-001",
    enroll_token: $("enrollToken").value.trim(),
    node_label: $("nodeLabel").value.trim(),
    slave_token: $("slaveTok").value.trim()
  };
}
function showPreview(c){ $("configJson").textContent = JSON.stringify(c, null, 2); }

function updateSteps(st, cfg){
  const c = cfg || lastConfig || {};
  if(st) {
    setStep("config", "done", "Node " + (st.node_id || "-") + " · " + (st.role === "slave" ? "從機" : "主機"));
    setStep("wifi", st.sta_connected ? "done" : (st.ap_running ? "active" : ""),
            st.sta_connected ? "STA 已連線 " + (st.sta_rssi || "?") + " dBm" : (st.ap_running ? "僅 AP 模式" : "未連線"));
  }
  if (c) {
    setStep("host", (c.host_enabled && c.host_url) ? "done" : (c.host_enabled ? "error" : ""),
            (c.host_enabled && c.host_url) ? "已啟用 → " + c.host_url : (c.host_enabled ? "缺少 URL" : "未啟用"));
  }
  if (st) {
    setStep("ready", (st.children > 0) ? "done" : "",
            (st.children > 0) ? "已連子節點 " + st.children + " 台" : "等待節點 / 資料");
  }
}

async function refreshStatus(){
  try {
    const st = await jget("/api/status");
    lastStatus = st;
    const ok = st.ap_running || st.sta_connected;
    $("sidebarStatus").textContent = (st.role === "slave" ? "從機" : "主機") + " " + (st.node_id || "");
    $("sidebarLight").className = "status-light" + (ok ? "" : " idle");
    $("topLight").className = "status-light" + (ok ? "" : " idle");
    $("topIp").textContent = st.ip || "--";
    $("overallBadge").className = "status-badge " + (ok ? "success" : "idle");
    $("overallBadge").textContent = ok ? "正常" : "等待連線";
    updateSteps(st);
  } catch (e) {
    $("sidebarStatus").textContent = "無法連線";
    $("sidebarLight").className = "status-light error";
    $("topIp").textContent = "--";
    $("overallBadge").className = "status-badge error";
    $("overallBadge").textContent = "離線";
  }
}

async function doTest(){
  const btn = $("testButton"); btn.disabled = true;
  $("overallBadge").className = "status-badge testing"; $("overallBadge").textContent = "測試中";
  const t0 = performance.now();
  try {
    const st = await jget("/api/status");
    const c  = await jget("/api/config");
    lastStatus = st; lastConfig = c;
    fillForm(c); showPreview(c);
    const ms = Math.max(1, Math.round(performance.now() - t0));
    $("latencyValue").textContent = ms + " ms";
    updateSteps(st, c);
    $("overallBadge").className = "status-badge success"; $("overallBadge").textContent = "正常";
    $("saveState").textContent = "已與裝置同步";
    toast("已從裝置讀取設定與狀態");
  } catch (e) {
    $("overallBadge").className = "status-badge error"; $("overallBadge").textContent = "無法連線";
    toast("無法連線裝置：" + e.message, "error");
  } finally { btn.disabled = false; }
}

async function saveConfig(reboot){
  const btn = $qa("#rebootBtn")[0]; if(reboot && btn) btn.disabled = true;
  const body = collect();
  try {
    const r = await jpost("/api/config", body);
    if (!r.ok) { toast("儲存失敗" + (r.err ? "：" + r.err : ""), "error"); return; }
    const c = await jget("/api/config");
    lastConfig = c; showPreview(c); updateSteps(lastStatus, c);
    $("saveState").textContent = "已寫入裝置";
    $("saveTime").textContent = new Date().toLocaleTimeString() + "（NVS）";
    toast(reboot ? "已儲存，正在重新啟動…" : "設定已寫入（Wi-Fi/角色變更需重啟）");
    if (reboot) { await jpost("/api/reboot", {}); }
  } catch (e) {
    toast("寫入失敗：" + e.message, "error");
  } finally { if(reboot && btn) btn.disabled = false; }
}

$q("#settingsForm").addEventListener("submit", function(e){ e.preventDefault(); saveConfig(false); });
$("#testButton").addEventListener("click", doTest);
$("#rebootBtn").addEventListener("click", function(){ saveConfig(true); });
$("#copyButton").addEventListener("click", function(){
  const t = $("configJson").textContent;
  if (navigator.clipboard && navigator.clipboard.writeText) { navigator.clipboard.writeText(t).then(function(){ toast("設定 JSON 已複製"); }); }
  else { toast("瀏覽器未允許剪貼簿", "error"); }
});
$qa(".reveal-button").forEach(function(b){ b.addEventListener("click", function(){
  const inp = $(b.dataset.target); if(!inp) return;
  const show = inp.type === "password";
  inp.type = show ? "text" : "password";
  b.textContent = show ? "隱藏" : "顯示";
}); });
$qa('input[name=nodeMode]').forEach(function(r){ r.addEventListener("change", refreshModeUI); });
$("ipMode").addEventListener("change", refreshModeUI);
$("#menuButton").addEventListener("click", function(){ $q(".sidebar").classList.toggle("open"); });
$qa(".nav-link").forEach(function(l){ l.addEventListener("click", function(){
  $qa(".nav-link").forEach(function(x){ x.classList.remove("active"); }); l.classList.add("active");
  $q(".sidebar").classList.remove("open");
}); });

(function init(){
  refreshModeUI();
  doTest();
  setInterval(refreshStatus, 5000);
})();
</script>
</body>
</html>
)htmlstr";

#endif // HTML_PAGES_H
