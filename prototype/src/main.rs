use core::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use log::*;

use esp_idf_hal::gpio::Pull;
use esp_idf_hal::peripherals::Peripherals;

use ecotiter_fw::errors::StepperError;
use ecotiter_fw::limitswitch::LimitSwitch;
use ecotiter_fw::stepper::rmt_stepper::RmtStepper;
use ecotiter_fw::types::Direction;
use ecotiter_fw::webserver::WebServer;
use ecotiter_fw::wifi::WifiManager;

static LIMIT_FULL_TRIGGERED: AtomicBool = AtomicBool::new(false);
static LIMIT_EMPTY_TRIGGERED: AtomicBool = AtomicBool::new(false);

fn main() {
    esp_idf_sys::link_patches();
    ecotiter_fw::logger::init();

    unsafe {
        esp_idf_sys::esp_task_wdt_deinit();

        // Suppress noise from HTTP client disconnects
        esp_idf_sys::esp_log_level_set(
            b"httpd_txrx\0".as_ptr() as *const core::ffi::c_char,
            esp_idf_sys::esp_log_level_t_ESP_LOG_ERROR,
        );
    }

    info!("=== EcoTiter firmware ===");

    let peripherals = Peripherals::take().expect("Peripherals::take()");

    let mut stepper = RmtStepper::new(
        peripherals.pins.gpio25.degrade_output(),
        peripherals.pins.gpio26.degrade_output(),
        peripherals.pins.gpio27.degrade_output(),
    )
    .expect("RmtStepper::new()");

    stepper.set_stop_flag(&LIMIT_FULL_TRIGGERED);

    let mut limit_full = LimitSwitch::new(
        peripherals.pins.gpio32,
        Pull::Down,
        &LIMIT_FULL_TRIGGERED,
    )
    .expect("LimitSwitch FULL (GPIO32)");

    let mut limit_empty = LimitSwitch::new(
        peripherals.pins.gpio35,
        Pull::Floating,
        &LIMIT_EMPTY_TRIGGERED,
    )
    .expect("LimitSwitch EMPTY (GPIO35)");

    let mut led = esp_idf_hal::gpio::PinDriver::output(
        peripherals.pins.gpio2.degrade_output(),
    )
    .expect("LED GPIO2");

    led.set_low().ok();

    // === ADC (pH electrode on GPIO34, ADC1_CH6) ===
    use esp_idf_hal::adc::attenuation::DB_12;
    use esp_idf_hal::adc::oneshot::config::AdcChannelConfig;
    use esp_idf_hal::adc::oneshot::{AdcChannelDriver, AdcDriver};

    let adc = AdcDriver::new(peripherals.adc1).expect("AdcDriver::new()");
    let mut adc_channel = AdcChannelDriver::new(
        &adc,
        peripherals.pins.gpio34,
        &AdcChannelConfig {
            attenuation: DB_12,
            ..Default::default()
        },
    )
    .expect("AdcChannelDriver::new()");

    let mut adc_buf = [0u16; 64];
    let mut adc_idx = 0usize;

    // === Temperature (DS18B20 on GPIO4 via software bitbang) ===
    {
        let gpio33 = peripherals.pins.gpio33;
        info!("DS18B20: software bitbang on GPIO33");
        let _ = std::thread::Builder::new()
            .stack_size(16384)
            .name("temp".into())
            .spawn(move || {
                info!("Temperature thread started");
                let mut bus = match ecotiter_fw::temperature::OneWireBus::new(gpio33) {
                    Ok(b) => b,
                    Err(e) => {
                        error!("OneWireBus::new() failed: {:?}", e);
                        return;
                    }
                };
                loop {
                    if let Some(temp) = ecotiter_fw::temperature::read_sensor(&mut bus) {
                        info!("Temperature: {:.1}°C", temp);
                    }
                    std::thread::sleep(std::time::Duration::from_millis(1000));
                }
            })
            .expect("temp thread spawn");
    }

    {
        use esp_idf_svc::eventloop::EspSystemEventLoop;
        use esp_idf_svc::nvs::EspDefaultNvsPartition;

        let sys_loop = EspSystemEventLoop::take().expect("EspSystemEventLoop::take()");
        let nvs = EspDefaultNvsPartition::take().expect("EspDefaultNvsPartition::take()");

        let ble_active = Arc::new(AtomicBool::new(false));

        let wifi_mgr: Arc<Mutex<WifiManager>> = Arc::new(Mutex::new(
            WifiManager::new(
                peripherals.modem,
                sys_loop,
                Some(nvs),
                ble_active.clone(),
            )
            .expect("WifiManager::new()"),
        ));

        wifi_mgr.lock().unwrap().init();

        let webserver = WebServer::new(wifi_mgr.clone());

        std::thread::sleep(std::time::Duration::from_millis(200));

        stepper.enable().expect("enable");
        stepper
            .set_direction(Direction::Cw)
            .expect("set_direction");

        info!("Stepper 500 Hz CW");
        info!("ADC sampling on GPIO34 (pH)");
        info!("DS18B20 temperature on GPIO33");

        let chunk: Vec<u32> = vec![2000; 64];
        let mut motor_stopped = false;

        loop {
            // WiFi processing (low priority)
            if let Ok(mut wifi) = wifi_mgr.lock() {
                let ble = ble_active.load(std::sync::atomic::Ordering::Relaxed);
                wifi.set_ble_active(ble);
                wifi.process();
            }

            if webserver.restart_pending() {
                info!("Restart pending after WiFi config — restarting");
                std::thread::sleep(std::time::Duration::from_millis(500));
                unsafe {
                    esp_idf_sys::esp_restart();
                }
            }

            // ADC sample (single read, non-blocking — ~30 µs)
            match adc_channel.read() {
                Ok(mv) => {
                    adc_buf[adc_idx] = mv;
                    adc_idx = (adc_idx + 1) % 64;
                    if adc_idx == 0 {
                        let sum: u32 = adc_buf.iter().map(|&v| v as u32).sum();
                        let avg = (sum / 64) as u16;
                        ecotiter_fw::adc::set_raw_mv(avg);
                        info!("ADC: raw_mv={}, calibrated_mv={}", avg, ecotiter_fw::adc::calibrated_mv());
                    }
                }
                Err(e) => warn!("ADC read error: {:?}", e),
            }

            // Stepper + limit switches (existing logic)
            let full_active = limit_full.level();

            if full_active {
                led.set_high().ok();
            } else {
                led.set_low().ok();
            }

            if !motor_stopped {
                if full_active {
                    stepper.emergency_stop().ok();
                    info!("FULL switch active — motor stopped, LED ON");
                    motor_stopped = true;
                } else {
                    match stepper.move_steps(&chunk) {
                        Ok(()) => {}
                        Err(StepperError::LimitSwitchReached) => {
                            stepper.emergency_stop().ok();
                            info!("FULL switch hit during chunk — motor stopped");
                            motor_stopped = true;
                        }
                        Err(e) => {
                            info!("Stepper error: {:?}", e);
                            motor_stopped = true;
                        }
                    }
                }
            }

            if limit_empty.is_triggered() {
                info!("EMPTY switch GPIO35 HIGH (latched)");
                limit_empty.clear();
                limit_empty.rearm().ok();
            }

            if motor_stopped && limit_full.is_triggered() {
                limit_full.clear();
                limit_full.rearm().ok();
            }

            std::thread::sleep(std::time::Duration::from_millis(10));
        }
    }
}
