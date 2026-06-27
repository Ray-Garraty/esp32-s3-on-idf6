use log::info;

fn heap_free() -> u32 {
    unsafe { esp_idf_sys::heap_caps_get_free_size(esp_idf_sys::MALLOC_CAP_DEFAULT as u32) as u32 }
}

fn heap_largest_free() -> u32 {
    unsafe {
        esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DEFAULT as u32)
            as u32
    }
}

fn log_heap(stage: &str) {
    info!(
        "[HEAP] {}: free={} ({} KB), largest_block={} ({} KB)",
        stage,
        heap_free(),
        heap_free() / 1024,
        heap_largest_free(),
        heap_largest_free() / 1024
    );
}

fn main() {
    esp_idf_sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();
    log::set_max_level(log::LevelFilter::Info);

    info!("EcoTiter firmware v5 (Rust/ESP-IDF v6) - Phase 0");
    log_heap("after_link_patches");

    let _peripherals = esp_idf_hal::peripherals::Peripherals::take();
    log_heap("after_peripherals_take");

    info!("Initializing NimBLE...");
    init_ble();
    log_heap("after_ble_init");

    let heap_free = heap_free();
    let heap_largest = heap_largest_free();
    info!("=== PHASE 0 COMPLETE ===");
    info!("Free heap: {} bytes ({} KB)", heap_free, heap_free / 1024);
    info!(
        "Largest block: {} bytes ({} KB)",
        heap_largest,
        heap_largest / 1024
    );

    if heap_free < 30 * 1024 {
        info!("*** RED FLAG: Free heap < 30 KB - migration not feasible on WROOM-32 ***");
    } else if heap_free < 50 * 1024 {
        info!("*** YELLOW FLAG: Free heap < 50 KB - proceed with mitigations ***");
    } else {
        info!("*** GREEN FLAG: Free heap >= 50 KB - feasible ***");
    }

    let mut tick: u32 = 0;
    loop {
        std::thread::sleep(std::time::Duration::from_millis(5000));
        tick += 1;
        if tick % 6 == 0 {
            log_heap("heartbeat");
        }
        info!("Alive (tick={})", tick);
    }
}

fn init_ble() {
    use esp32_nimble::utilities::BleUuid;
    use esp32_nimble::{BLEAdvertisementData, BLEDevice, NimbleProperties};

    let ble_device = BLEDevice::take();
    let server = ble_device.get_server();
    let advertising = ble_device.get_advertising();

    let nus_service = server.create_service(BleUuid::Uuid16(0xFFE0));

    let _rx_char = nus_service.lock().create_characteristic(
        BleUuid::Uuid16(0xFFE1),
        NimbleProperties::WRITE | NimbleProperties::WRITE_NO_RSP,
    );

    let _tx_char = nus_service.lock().create_characteristic(
        BleUuid::Uuid16(0xFFE2),
        NimbleProperties::NOTIFY | NimbleProperties::READ,
    );

    advertising
        .lock()
        .set_data(
            BLEAdvertisementData::new()
                .name("EcoTiter-XXXX")
                .add_service_uuid(BleUuid::Uuid16(0xFFE0)),
        )
        .unwrap();

    match advertising.lock().start() {
        Ok(_) => info!("NimBLE advertising started"),
        Err(e) => info!("NimBLE advertising start failed: {:?}", e),
    }

    info!("NimBLE BLE server initialized");
}
