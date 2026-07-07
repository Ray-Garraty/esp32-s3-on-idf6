use core::sync::atomic::{AtomicI16, AtomicU16, Ordering};

pub const PIN: u8 = 34;
pub const SAMPLES: u32 = 64;

const DEFAULT_A_X1000: u16 = 1000; // a = 1.0
const DEFAULT_B: i16 = 0;

static RAW_MV: AtomicU16 = AtomicU16::new(0);
static COEFF_A_X1000: AtomicU16 = AtomicU16::new(DEFAULT_A_X1000);
static COEFF_B: AtomicI16 = AtomicI16::new(DEFAULT_B);

pub fn raw_mv() -> u16 {
    RAW_MV.load(Ordering::Relaxed)
}

pub fn calibrated_mv() -> i16 {
    let raw = RAW_MV.load(Ordering::Relaxed) as i32;
    let a = COEFF_A_X1000.load(Ordering::Relaxed) as i32;
    let b = COEFF_B.load(Ordering::Relaxed) as i32;
    let result = (a * raw) / 1000 + b;
    result.clamp(i16::MIN as i32, i16::MAX as i32) as i16
}

pub fn set_raw_mv(mv: u16) {
    RAW_MV.store(mv, Ordering::Relaxed);
}

pub fn set_calibration(a: f32, b: f32) {
    COEFF_A_X1000.store((a * 1000.0) as u16, Ordering::Relaxed);
    COEFF_B.store(b as i16, Ordering::Relaxed);
}

pub fn get_calibration() -> (f32, f32) {
    let a = COEFF_A_X1000.load(Ordering::Relaxed) as f32 / 1000.0;
    let b = COEFF_B.load(Ordering::Relaxed) as f32;
    (a, b)
}

pub fn reset_calibration() {
    COEFF_A_X1000.store(DEFAULT_A_X1000, Ordering::Relaxed);
    COEFF_B.store(DEFAULT_B, Ordering::Relaxed);
}
