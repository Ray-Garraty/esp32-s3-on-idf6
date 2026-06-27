use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct DeviceStatus {
    pub wifi_mode: String,
    pub wifi_connected: bool,
    pub wifi_ssid: Option<String>,
    pub wifi_rssi: Option<i32>,
    pub ap_mode: bool,
}
