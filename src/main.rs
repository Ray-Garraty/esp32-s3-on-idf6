#[cfg(target_arch = "xtensa")]
use ecotiter_fw::stepper::rmt_stepper::RmtStepper;
#[cfg(target_arch = "xtensa")]
use ecotiter_fw::types::Direction;
#[cfg(target_arch = "xtensa")]
use esp_idf_hal::peripherals::Peripherals;

#[cfg(target_arch = "xtensa")]
fn run_stepper_continuous() {
    use log::info;

    let peripherals = Peripherals::take().expect("Peripherals::take()");

    let mut stepper = RmtStepper::new(
        peripherals.pins.gpio25.degrade_output(),
        peripherals.pins.gpio26.degrade_output(),
        peripherals.pins.gpio27.degrade_output(),
    )
    .expect("RmtStepper::new()");

    stepper.enable().expect("enable");
    stepper
        .set_direction(Direction::Cw)
        .expect("set_direction");

    info!("500 Hz CW: STEP=25 DIR=26 EN=27");

    let chunk: Vec<u32> = vec![2000; 128];

    loop {
        stepper.move_steps(&chunk).expect("send");
    }
}

fn main() {
    esp_idf_sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();
    log::set_max_level(log::LevelFilter::Info);

    // Disable TWDT — RMT send_and_wait blocks > 250ms, idle task can't feed WDT
    unsafe { esp_idf_sys::esp_task_wdt_deinit(); }

    log::info!("=== 500 Hz RMT stepper test ===");

    #[cfg(target_arch = "xtensa")]
    run_stepper_continuous();

    #[cfg(not(target_arch = "xtensa"))]
    loop {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}
