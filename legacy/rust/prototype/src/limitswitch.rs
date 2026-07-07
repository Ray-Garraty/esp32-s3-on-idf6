use core::sync::atomic::{AtomicBool, Ordering};

use esp_idf_hal::gpio::{InputPin, PinDriver, Pull};
use esp_idf_hal::gpio::{InterruptType, Input};

use crate::errors::StepperError;

pub struct LimitSwitch {
    pin: PinDriver<'static, Input>,
    triggered: &'static AtomicBool,
}

impl LimitSwitch {
    pub fn new(
        pin: impl InputPin + 'static,
        pull: Pull,
        triggered: &'static AtomicBool,
    ) -> Result<Self, StepperError> {
        let mut pin = PinDriver::input(pin, pull)?;

        let name = pin.pin();
        log::info!("LimitSwitch GPIO{} init: pull={:?}", name, pull);
        log::info!("  initial level = {}", if pin.is_high() { "HIGH" } else { "LOW" });

        pin.set_interrupt_type(InterruptType::PosEdge)?;
        unsafe {
            pin.subscribe(move || {
                triggered.store(true, Ordering::Relaxed);
            })?
        };
        pin.enable_interrupt()?;

        Ok(Self { pin, triggered })
    }

    pub fn is_triggered(&self) -> bool {
        self.triggered.load(Ordering::Acquire)
    }

    pub fn clear(&self) {
        self.triggered.store(false, Ordering::Release);
    }

    pub fn rearm(&mut self) -> Result<(), StepperError> {
        self.pin.enable_interrupt()?;
        Ok(())
    }

    pub fn level(&self) -> bool {
        self.pin.is_high()
    }
}
