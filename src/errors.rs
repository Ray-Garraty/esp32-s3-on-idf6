use core::fmt;

#[derive(Debug)]
pub enum StepperError {
    InitFailed,
    Rmt(i32),
    InvalidConfig(&'static str),
    LimitSwitchReached,
}

impl fmt::Display for StepperError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            StepperError::InitFailed => write!(f, "GPIO init failed"),
            StepperError::Rmt(code) => write!(f, "RMT error: {}", code),
            StepperError::InvalidConfig(msg) => write!(f, "Invalid config: {}", msg),
            StepperError::LimitSwitchReached => write!(f, "Limit switch reached"),
        }
    }
}

#[cfg(target_arch = "xtensa")]
impl From<esp_idf_sys::EspError> for StepperError {
    fn from(e: esp_idf_sys::EspError) -> Self {
        StepperError::Rmt(e.code())
    }
}
