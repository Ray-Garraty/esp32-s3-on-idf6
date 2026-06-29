use core::sync::atomic::{AtomicI32, Ordering};

use log::*;

use esp_idf_hal::delay::Ets;
use esp_idf_hal::gpio::{PinDriver, Pull};

pub const PIN: u8 = 33;
pub const READ_INTERVAL_MS: u64 = 1000;
pub const CONVERSION_WAIT_MS: u64 = 800;

const DISCONNECTED: i32 = i32::MIN;

static TEMP_C_X100: AtomicI32 = AtomicI32::new(DISCONNECTED);

pub fn temp_celsius() -> Option<f32> {
    let v = TEMP_C_X100.load(Ordering::Relaxed);
    if v == DISCONNECTED {
        None
    } else {
        Some(v as f32 / 100.0)
    }
}

pub struct OneWireBus {
    pin: PinDriver<'static, esp_idf_hal::gpio::InputOutput>,
}

impl OneWireBus {
    pub fn new(pin: impl esp_idf_hal::gpio::InputPin + esp_idf_hal::gpio::OutputPin + 'static) -> Result<Self, esp_idf_sys::EspError> {
        let mut pin = PinDriver::input_output_od(pin, Pull::Up)?;
        pin.set_high().ok();
        Ok(Self { pin })
    }

    pub fn reset(&mut self) -> bool {
        self.pin.set_low().ok();
        Ets::delay_us(480);
        self.pin.set_high().ok();
        Ets::delay_us(75);
        let present = self.pin.is_low();
        Ets::delay_us(405);
        present
    }

    pub fn write_byte(&mut self, byte: u8) {
        for i in 0..8 {
            if (byte >> i) & 1 != 0 {
                self.write_bit_1();
            } else {
                self.write_bit_0();
            }
        }
    }

    fn write_bit_1(&mut self) {
        self.pin.set_low().ok();
        Ets::delay_us(6);
        self.pin.set_high().ok();
        Ets::delay_us(64);
    }

    fn write_bit_0(&mut self) {
        self.pin.set_low().ok();
        Ets::delay_us(60);
        self.pin.set_high().ok();
        Ets::delay_us(10);
    }

    pub fn read_byte(&mut self) -> u8 {
        let mut byte = 0u8;
        for i in 0..8 {
            if self.read_bit() {
                byte |= 1 << i;
            }
        }
        byte
    }

    fn read_bit(&mut self) -> bool {
        self.pin.set_low().ok();
        Ets::delay_us(3);
        self.pin.set_high().ok();
        Ets::delay_us(10);
        let bit = self.pin.is_high();
        Ets::delay_us(53);
        bit
    }

    pub fn skip_rom(&mut self) {
        self.write_byte(0xCC);
    }

    pub fn convert_t(&mut self) {
        self.write_byte(0x44);
    }

    pub fn read_scratchpad(&mut self) -> [u8; 9] {
        self.write_byte(0xBE);
        let mut buf = [0u8; 9];
        for byte in &mut buf {
            *byte = self.read_byte();
        }
        buf
    }
}

unsafe impl Send for OneWireBus {}

pub fn read_sensor(bus: &mut OneWireBus) -> Option<f32> {
    if !bus.reset() {
        warn!("DS18B20 not detected (no presence pulse)");
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    bus.skip_rom();
    bus.convert_t();

    std::thread::sleep(std::time::Duration::from_millis(800));

    if !bus.reset() {
        warn!("DS18B20 lost during conversion");
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    bus.skip_rom();
    let buf = bus.read_scratchpad();

    let lsb = buf[0];
    let msb = buf[1];
    let temp_raw = (msb as u16) << 8 | lsb as u16;
    let temp = f32::from(temp_raw) / 16.0;

    if temp < -55.0 || temp > 125.0 {
        warn!("DS18B20 out of range: {}°C", temp);
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    TEMP_C_X100.store((temp * 100.0) as i32, Ordering::Relaxed);
    Some(temp)
}

pub fn clear() {
    TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
}
