//! ESP-IDF HTTP server with REST API, captive portal, WebSocket, and WebUI.
//!
//! # Architecture
//!
//! The HTTP server runs in a dedicated ESP-IDF task (stack size configured via
//! `config::HTTP_SERVER_STACK`). Routes are registered at construction time.
//!
//! - REST API handlers call `rest_api.rs` functions for JSON building.
//! - POST /api/command uses the stub path (Phase 5 adds full dispatch).
//! - WebSocket at `/ws/stream` broadcasts JSON events to all connected clients.
//! - Captive portal routes redirect common probe URLs to /wifi.
//! - WebUI routes serve the embedded HTML dashboard.

use std::collections::BTreeMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// Global restart flag set by captive portal when WiFi credentials are saved.
///
/// Main loop reads this instead of http_server.restart_pending().
/// Thread-safe: written by HTTP handler (httpd task), read by main loop.
pub static G_RESTART_PENDING: AtomicBool = AtomicBool::new(false);

use embedded_svc::http::Method;
use embedded_svc::ws::FrameType;
use esp_idf_svc::http::server::ws::EspHttpWsDetachedSender;
use esp_idf_svc::http::server::{Configuration, EspHttpServer};

use crate::config;
use crate::domain::memory::HTTP_POST_BUF_SIZE;
use crate::domain::memory::MAX_RESPONSE_SIZE;
use crate::errors::NetworkError;
use crate::infrastructure::network::wifi::WifiManager;
use crate::interface::rest_api;
use crate::interface::webui;

// `WifiManager` used with `'static` lifetime for `fn_handler` `Send` bound.
type WifiMgr = Arc<Mutex<WifiManager<'static>>>;

/// WebSocket sessions: session_id -> detached sender.
/// Wrapped in `Mutex` for interior mutability (broadcast from main loop).
static WS_SESSIONS: Mutex<BTreeMap<i32, EspHttpWsDetachedSender>> = Mutex::new(BTreeMap::new());

/// Broadcast a JSON event to all connected WebSocket clients.
///
/// Called from main loop (non-blocking — uses `try_lock()` per AGENTS.md
/// Golden Rule). The event is wrapped in a JSON envelope:
/// `{"event":"<type>","data":<payload>}`
pub fn broadcast_websocket_event(event_type: &str, data: &str) {
    let mut msg: heapless::String<{ MAX_RESPONSE_SIZE + 64 }> = heapless::String::new();
    let _ = core::fmt::write(
        &mut msg,
        format_args!(r#"{{"event":"{event_type}","data":{data}}}"#),
    );

    if let Ok(mut sessions) = WS_SESSIONS.try_lock() {
        // Remove closed sessions as we iterate
        sessions.retain(|session_id, sender| {
            if sender.is_closed() {
                log::info!("WS: session {session_id} stale, removing");
                false
            } else if let Err(e) = sender.send(FrameType::Text(false), msg.as_bytes()) {
                log::warn!("WS: session {session_id} send failed: {e:?}");
                true // keep — might recover
            } else {
                true
            }
        });
    }
}

/// HTTP server wrapper.
pub struct HttpServer {
    /// The underlying ESP-IDF HTTP server.
    server: EspHttpServer<'static>,
}

/// Minimum stack bytes required before safely calling EspHttpServer::new().
/// Below this threshold, C-struct init could overflow the stack.
const MIN_STACK_FOR_HTTP_INIT: u32 = 4096;

impl HttpServer {
    /// Create a new HTTP server and register all routes.
    ///
    /// # Arguments
    ///
    /// * `wifi_mgr` - Shared WiFi manager (`Arc<Mutex<>>`) for status/connect routes.
    ///
    /// # Errors
    ///
    /// Returns `NetworkError::HttpServerInitFailed` if the server cannot be created.
    // Values are moved into closures; taking references would require extra Arcs.
    #[allow(clippy::needless_pass_by_value)]
    pub fn new(wifi_mgr: WifiMgr) -> Result<Self, NetworkError> {
        // Runtime stack guard: warn if remaining stack is critically low
        if crate::esp_safe::stack_watermark() < MIN_STACK_FOR_HTTP_INIT {
            log::warn!(
                "Low stack ({} bytes) before HTTP init — risk of overflow",
                crate::esp_safe::stack_watermark()
            );
        }

        let config = Configuration {
            stack_size: config::HTTP_SERVER_STACK,
            ..Default::default()
        };

        let server = EspHttpServer::new(&config).map_err(|_| NetworkError::HttpServerInitFailed)?;

        let mut http = Self { server };

        http.register_captive_routes(Arc::clone(&wifi_mgr))?;
        http.register_api_routes(Arc::clone(&wifi_mgr))?;
        http.register_ws_routes()?;
        http.register_webui_routes()?;

        log::info!("HTTP: server started on port {}", config::HTTP_PORT);

        Ok(http)
    }

    // ── Captive portal routes ───────────────────────────────────

    // Values are moved into closures; taking references would require extra Arcs.
    #[allow(
        clippy::needless_pass_by_value,
        clippy::option_if_let_else,
        clippy::manual_string_new
    )]
    fn register_captive_routes(&mut self, wifi_mgr: WifiMgr) -> Result<(), NetworkError> {
        // GET /wifi — captive portal WiFi configuration form
        self.server
            .fn_handler(
                "/wifi",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut resp = request.into_ok_response()?;
                    resp.write(webui::WIFI_HTML.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /wifi/connect — save credentials and trigger restart
        let wifi_mgr_clone = Arc::clone(&wifi_mgr);
        self.server
            .fn_handler(
                "/wifi/connect",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    // Read body
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let body_str = core::str::from_utf8(&body[..n]).unwrap_or("{}");

                    if let Ok(val) = serde_json::from_str::<serde_json::Value>(body_str) {
                        let ssid = val["ssid"].as_str().unwrap_or("");
                        let password = val["password"].as_str().unwrap_or("");

                        if !ssid.is_empty() {
                            // Lock WiFi and save credentials
                            if let Ok(mut wifi) = wifi_mgr_clone.lock() {
                                // Try connecting (blocking, but this is in HTTP task context)
                                if wifi.try_connect_sta(ssid, password) {
                                    WifiManager::save_credentials_to_nvs(ssid, password);
                                    G_RESTART_PENDING.store(true, Ordering::Release);
                                }
                            }
                        }
                    }

                    let mut resp = request.into_ok_response()?;
                    let json = r#"{"status":"ok","success":true,"message":"Credentials saved. Restarting..."}"#;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /wifi/status — return current WiFi status JSON
        let wifi_status_mgr = Arc::clone(&wifi_mgr);
        self.server
            .fn_handler("/wifi/status", Method::Get, move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                let json = if let Ok(wifi) = wifi_status_mgr.lock() {
                    let ap_mode = wifi.is_ap_mode();
                    let connected = wifi.is_connected();
                    let ssid = wifi.wifi_ssid().unwrap_or("").to_string();
                    let rssi = wifi.wifi_rssi().unwrap_or(0);
                    let ip = wifi
                        .wifi_ip()
                        .map_or_else(|| "".to_string(), |a| a.to_string());
                    format!(
                        r#"{{"ap_mode":{ap_mode},"connected":{connected},"ssid":"{ssid}","rssi":{rssi},"ip":"{ip}"}}"#
                    )
                } else {
                    r#"{"error":"wifi_locked"}"#.to_string()
                };

                let mut resp = request.into_ok_response()?;
                resp.write(json.as_bytes())?;
                resp.flush()?;
                Ok(())
            })
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // Captive portal probe redirects (all → /wifi)
        let probes = [
            "/generate_204",
            "/hotspot-detect.html",
            "/ncsi.txt",
            "/connecttest.txt",
            "/gen_204",
        ];

        for path in &probes {
            self.server
                .fn_handler(
                    path,
                    Method::Get,
                    |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                        let mut resp =
                            request.into_response(302, None, &[("Location", "/wifi")])?;
                        resp.flush()?;
                        Ok(())
                    },
                )
                .map_err(|_| NetworkError::HttpServerInitFailed)?;
        }

        Ok(())
    }

    // ── API routes ──────────────────────────────────────────────

    // Values are moved into closures; route registration is inherently verbose.
    #[allow(
        clippy::needless_pass_by_value,
        clippy::too_many_lines,
        clippy::option_if_let_else
    )]
    fn register_api_routes(&mut self, wifi_mgr: WifiMgr) -> Result<(), NetworkError> {
        // GET /api/ping — health check
        self.server
            .fn_handler(
                "/api/ping",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = rest_api::handle_api_ping();
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/status — full device status
        let status_wifi = Arc::clone(&wifi_mgr);
        self.server
            .fn_handler(
                "/api/status",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = if let Ok(wifi) = status_wifi.lock() {
                        let wifi_connected = wifi.is_connected();
                        let wifi_ssid = wifi.wifi_ssid().map(ToString::to_string);
                        let wifi_rssi = wifi.wifi_rssi();
                        let wifi_ip = wifi.wifi_ip().map(|a| a.to_string());
                        let is_ap_mode = wifi.is_ap_mode();

                        rest_api::handle_api_status(
                            wifi_connected,
                            wifi_ssid.as_deref(),
                            wifi_rssi,
                            wifi_ip.as_deref(),
                            is_ap_mode,
                            None,   // temp — read from shared state in Phase 5
                            0,      // mv — read from ADC in Phase 5
                            "in",   // vlv — read from valve in Phase 5
                            "idle", // brt status
                            0.0,    // brt volume
                            0.0,    // brt speed
                            0,      // ts
                        )
                    } else {
                        rest_api::handle_api_status(
                            false, None, None, None, false, None, 0, "in", "idle", 0.0, 0.0, 0,
                        )
                    };

                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /api/command — execute a JSON command (stub path, Phase 5 adds dispatch)
        self.server
            .fn_handler(
                "/api/command",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let json = rest_api::handle_api_command(&body[..n]);
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /api/valve/set — set valve position
        self.server
            .fn_handler(
                "/api/valve/set",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let json = rest_api::handle_valve_set(&body[..n]);
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/valve/state — get current valve position
        self.server
            .fn_handler(
                "/api/valve/state",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = rest_api::handle_valve_state();
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/logs — return log entries as JSON
        self.server
            .fn_handler(
                "/api/logs",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = rest_api::handle_api_logs(config::LOG_DEFAULT_LIMIT);
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/logs/download — plain-text log file
        self.server
            .fn_handler(
                "/api/logs/download",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let text = rest_api::handle_api_logs_download();
                    let mut resp = request.into_response(
                        200,
                        Some("OK"),
                        &[("Content-Type", "text/plain")],
                    )?;
                    resp.write(text.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // DELETE /api/logs — clear log ring buffer
        self.server
            .fn_handler(
                "/api/logs",
                Method::Delete,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = r#"{"status":"ok"}"#;
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        Ok(())
    }

    // ── WebSocket routes ──────────────────────────────────────────

    fn register_ws_routes(&mut self) -> Result<(), NetworkError> {
        self.server
            .ws_handler("/ws/stream", None, move |ws| {
                if ws.is_new() {
                    let session_id = ws.session();
                    match ws.create_detached_sender() {
                        Ok(sender) => {
                            if let Ok(mut sessions) = WS_SESSIONS.lock() {
                                sessions.insert(session_id, sender);
                                log::info!(
                                    "WS: session {session_id} connected ({} total)",
                                    sessions.len()
                                );
                            }
                            let _ = ws.send(
                                FrameType::Text(false),
                                br#"{"event":"connected","data":{}}"#,
                            );
                        }
                        Err(e) => log::warn!("WS: create_detached_sender failed: {e:?}"),
                    }
                    return Ok(());
                }

                if ws.is_closed() {
                    let session_id = ws.session();
                    if let Ok(mut sessions) = WS_SESSIONS.lock() {
                        sessions.remove(&session_id);
                        log::info!(
                            "WS: session {session_id} closed ({} remaining)",
                            sessions.len()
                        );
                    }
                    return Ok(());
                }

                Ok::<(), esp_idf_sys::EspError>(())
            })
            .map_err(|_| NetworkError::HttpServerInitFailed)?;
        Ok(())
    }

    // ── WebUI routes ────────────────────────────────────────────

    fn register_webui_routes(&mut self) -> Result<(), NetworkError> {
        macro_rules! static_route {
            ($path:expr, $content:expr) => {
                self.server
                    .fn_handler(
                        $path,
                        Method::Get,
                        |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                            let mut resp = request.into_ok_response()?;
                            resp.write($content.as_bytes())?;
                            resp.flush()?;
                            Ok(())
                        },
                    )
                    .map_err(|_| NetworkError::HttpServerInitFailed)?;
            };
        }

        static_route!("/", webui::INDEX_HTML);
        static_route!("/style.css", webui::STYLE_CSS);
        static_route!("/js/state.js", webui::STATE_JS);
        static_route!("/js/ws.js", webui::WS_JS);
        static_route!("/js/ui-update.js", webui::UI_UPDATE_JS);
        static_route!("/js/logs.js", webui::LOGS_JS);
        static_route!("/js/stepper.js", webui::STEPPER_JS);
        static_route!("/js/calibration.js", webui::CALIBRATION_JS);
        static_route!("/js/init.js", webui::INIT_JS);

        Ok(())
    }
}
