use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use log::*;

use esp_idf_hal::io::EspIOError;
use esp_idf_svc::http::server::{EspHttpServer, Method};

use crate::config;
use crate::wifi::WifiManager;

pub type SharedWifi = Arc<Mutex<WifiManager>>;

#[allow(dead_code)]
pub struct WebServer {
    server: EspHttpServer<'static>,
    restart_pending: Arc<AtomicBool>,
}

impl WebServer {
    pub fn new(wifi_mgr: SharedWifi) -> Self {
        let mut server = EspHttpServer::new(&Default::default())
            .expect("EspHttpServer::new()");
        let restart_pending = Arc::new(AtomicBool::new(false));

        Self::register_wifi_routes(&mut server, wifi_mgr.clone(), restart_pending.clone());
        Self::register_api_routes(&mut server, wifi_mgr.clone());
        Self::register_catch_all(&mut server, wifi_mgr.clone());

        info!("HTTP server started on port {}", config::HTTP_PORT);
        Self {
            server,
            restart_pending,
        }
    }

    pub fn restart_pending(&self) -> bool {
        self.restart_pending.load(Ordering::Relaxed)
    }

    fn register_wifi_routes(
        server: &mut EspHttpServer<'static>,
        wifi_mgr: SharedWifi,
        restart_pending: Arc<AtomicBool>,
    ) {
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/wifi", Method::Get, move |request| -> Result<(), EspIOError> {
                let _ = &mgr;
                let mut resp = request.into_ok_response()?;
                resp.write(CAPTIVE_PORTAL_HTML.as_bytes())?;
                Ok(())
            })
            .ok();

        let mgr = wifi_mgr.clone();
        let rp = restart_pending.clone();
        server
            .fn_handler("/wifi/connect", Method::Post, move |mut request| -> Result<(), EspIOError> {
                let body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let body_str = String::from_utf8_lossy(&body);
                let parsed: serde_json::Value = match serde_json::from_str(&body_str) {
                    Ok(v) => v,
                    Err(_) => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Invalid JSON\"}")?;
                        return Ok(());
                    }
                };

                let ssid = match parsed["ssid"].as_str() {
                    Some(s) => s,
                    None => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Missing ssid\"}")?;
                        return Ok(());
                    }
                };
                let password = match parsed["password"].as_str() {
                    Some(p) => p,
                    None => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Missing password\"}")?;
                        return Ok(());
                    }
                };

                info!("WiFi credentials received: {}", ssid);

                {
                    let mut wifi = mgr.lock().unwrap();
                    wifi.stop();
                    WifiManager::save_credentials_to_nvs(ssid, password);
                }

                rp.store(true, Ordering::Relaxed);

                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"success\":true,\"message\":\"Saved, restarting...\"}")?;
                Ok(())
            })
            .ok();

        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/wifi/status", Method::Get, move |request| -> Result<(), EspIOError> {
                let wifi = mgr.lock().unwrap();
                let status_json = serde_json::json!({
                    "ap_mode": wifi.is_ap_mode(),
                    "connected": wifi.is_connected(),
                    "ssid": wifi.wifi_ssid(),
                    "rssi": wifi.wifi_rssi(),
                });
                drop(wifi);

                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&status_json).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();
    }

    fn register_api_routes(server: &mut EspHttpServer<'static>, wifi_mgr: SharedWifi) {
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/api/status", Method::Get, move |request| -> Result<(), EspIOError> {
                let wifi = mgr.lock().unwrap();
                let status_obj = serde_json::json!({
                    "wifi_mode": format!("{:?}", wifi.mode()),
                    "wifi_connected": wifi.is_connected(),
                    "wifi_ssid": wifi.wifi_ssid(),
                    "wifi_rssi": wifi.wifi_rssi(),
                    "ap_mode": wifi.is_ap_mode(),
                });
                drop(wifi);

                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&status_obj).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/api/ping", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"status\":\"ok\"}")?;
                Ok(())
            })
            .ok();

        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/api/events", Method::Get, move |mut request| -> Result<(), EspIOError> {
                let _body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let mut resp = request.into_ok_response()?;
                for _ in 0..5 {
                    let wifi = mgr.lock().unwrap();
                    let status_obj = serde_json::json!({
                        "wifi_mode": format!("{:?}", wifi.mode()),
                        "wifi_connected": wifi.is_connected(),
                        "wifi_ssid": wifi.wifi_ssid(),
                        "ap_mode": wifi.is_ap_mode(),
                    });
                    drop(wifi);

                    let json = serde_json::to_string(&status_obj).unwrap_or_default();
                    let msg = format!("event: status\ndata: {}\n\n", json);
                    if resp.write(msg.as_bytes()).is_err() {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(1000));
                }
                Ok(())
            })
            .ok();
    }

    fn register_catch_all(server: &mut EspHttpServer<'static>, wifi_mgr: SharedWifi) {
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/", Method::Get, move |request| -> Result<(), EspIOError> {
                let wifi = mgr.lock().unwrap();
                let in_ap = wifi.is_ap_mode();
                drop(wifi);

                if in_ap {
                    let mut resp = request.into_ok_response()?;
                    resp.write(CAPTIVE_PORTAL_HTML.as_bytes())?;
                } else {
                    let mut resp = request.into_ok_response()?;
                    resp.write(b"{\"status\":\"EcoTiter online\"}")?;
                }
                Ok(())
            })
            .ok();
    }
}

const CAPTIVE_PORTAL_HTML: &str = r#"<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>EcoTiter — WiFi Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.container{background:#fff;border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,.3);max-width:400px;width:100%;padding:40px}
h1{color:#333;margin-bottom:10px;font-size:24px}
.subtitle{color:#666;margin-bottom:30px;font-size:14px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:8px;color:#555;font-weight:500;font-size:14px}
input[type=text],input[type=password]{width:100%;padding:12px 16px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;transition:border-color .3s}
input:focus{outline:0;border-color:#667eea}
button{width:100%;padding:14px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:transform .2s,box-shadow .2s}
button:hover{transform:translateY(-2px);box-shadow:0 10px 20px rgba(102,126,234,.3)}
button:disabled{opacity:.5;cursor:not-allowed;transform:none}
.status{margin-top:20px;padding:12px;border-radius:8px;font-size:14px;display:none}
.status.info{background:#e3f2fd;color:#1976d2;display:block}
.status.success{background:#e8f5e9;color:#388e3c;display:block}
.status.error{background:#ffebee;color:#d32f2f;display:block}
</style>
</head>
<body>
<div class="container">
<h1>EcoTiter</h1>
<p class="subtitle">WiFi setup for initial configuration</p>
<form id="wifi-form">
<div class="form-group">
<label for="ssid">Network name (SSID)</label>
<input type="text" id="ssid" name="ssid" required placeholder="Enter SSID">
</div>
<div class="form-group">
<label for="password">Password</label>
<input type="password" id="password" name="password" required placeholder="Enter password">
</div>
<button type="submit" id="submit-btn">Connect</button>
</form>
<div id="status" class="status"></div>
</div>
<script>
const form=document.getElementById('wifi-form');
const btn=document.getElementById('submit-btn');
const st=document.getElementById('status');
form.addEventListener('submit',async e=>{
e.preventDefault();
const ssid=document.getElementById('ssid').value;
const password=document.getElementById('password').value;
btn.disabled=true;btn.innerHTML='Connecting...';
st.className='status info';st.innerHTML='Connecting...';
try{
const r=await fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password})});
const j=await r.json();
if(j.success){
st.className='status success';st.innerHTML='Connected! Restarting...';
btn.innerHTML='Done!';
setTimeout(()=>{st.innerHTML+='<br>Device restarting. Wait 10-15 seconds.';},2000);
}else{
st.className='status error';st.innerHTML='Error: '+(j.message||'Connection failed');
btn.disabled=false;btn.innerHTML='Connect';
}
}catch(e){
st.className='status error';st.innerHTML='Connection lost. If successful, device is restarting.';
btn.disabled=false;btn.innerHTML='Connect';
}
});
</script>
</body>
</html>"#;
