use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct DeviceStatus {
    pub wifi_mode: String,
    pub wifi_connected: bool,
    pub wifi_ssid: Option<String>,
    pub wifi_rssi: Option<i32>,
    pub wifi_ip: Option<String>,
    pub ap_mode: bool,
}

fn current_temp() -> Option<f32> {
    #[cfg(target_arch = "xtensa")]
    {
        crate::temperature::temp_celsius()
    }
    #[cfg(not(target_arch = "xtensa"))]
    {
        None
    }
}

fn current_mv() -> Option<i16> {
    #[cfg(target_arch = "xtensa")]
    {
        Some(crate::adc::calibrated_mv())
    }
    #[cfg(not(target_arch = "xtensa"))]
    {
        None
    }
}

pub fn format_status_response(
    connected: bool,
    ssid: Option<&str>,
    rssi: Option<i32>,
    ip: Option<String>,
    ap_mode: bool,
) -> String {
    use serde_json::json;

    let mode = if ap_mode {
        "AP"
    } else if connected {
        "STA"
    } else {
        "OFF"
    };

    let obj = json!({
        "wifi_mode": mode,
        "wifi_connected": connected,
        "wifi_ssid": ssid,
        "wifi_rssi": rssi,
        "wifi_ip": ip,
        "ap_mode": ap_mode,
        "temp": current_temp(),
        "mv": current_mv(),
        "vlv": "unk",
        "brt": {
            "sts": "idle",
            "vl": 0.0,
            "spd": 0.0,
        },
        "ts": 0,
    });

    serde_json::to_string(&obj).unwrap_or_default()
}
