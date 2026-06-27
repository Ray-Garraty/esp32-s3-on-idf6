use core::sync::atomic::{AtomicBool, Ordering};

use esp_idf_hal::gpio::{AnyOutputPin, Output, PinDriver};
use esp_idf_hal::rmt::config::{Loop, TransmitConfig, TxChannelConfig};
use esp_idf_hal::rmt::encoder::CopyEncoder;
use esp_idf_hal::rmt::{PinState, Pulse, PulseTicks, RmtChannel, Symbol, TxChannelDriver};
use esp_idf_hal::units::FromValueType;

use crate::errors::StepperError;
use crate::types::Direction;

const PULSE_WIDTH_TICKS: u16 = 1;
const RMT_RESOLUTION: u32 = 1_000_000;
const CHUNK_MAX: usize = 16;

pub struct RmtStepper<'d> {
    channel: TxChannelDriver<'d>,
    dir: PinDriver<'d, Output>,
    en: PinDriver<'d, Output>,
    stop_flag: Option<&'static AtomicBool>,
}

impl<'d> RmtStepper<'d> {
    pub fn new(
        step_pin: AnyOutputPin<'d>,
        dir_pin: AnyOutputPin<'d>,
        en_pin: AnyOutputPin<'d>,
    ) -> Result<Self, StepperError> {
        let dir = PinDriver::output(dir_pin)?;
        let mut en = PinDriver::output(en_pin)?;
        en.set_low()?;

        let channel = TxChannelDriver::new(
            step_pin,
            &TxChannelConfig {
                resolution: RMT_RESOLUTION.Hz(),
                transaction_queue_depth: 2,
                ..Default::default()
            },
        )?;

        Ok(Self {
            channel,
            dir,
            en,
            stop_flag: None,
        })
    }

    pub fn enable(&mut self) -> Result<(), StepperError> {
        self.en.set_low()?;
        Ok(())
    }

    pub fn emergency_stop(&mut self) -> Result<(), StepperError> {
        self.channel.disable()?;
        Ok(())
    }

    pub fn disable(&mut self) -> Result<(), StepperError> {
        self.channel.disable()?;
        self.en.set_high()?;
        Ok(())
    }

    pub fn set_direction(&mut self, dir: Direction) -> Result<(), StepperError> {
        match dir {
            Direction::Cw => self.dir.set_high()?,
            Direction::Ccw => self.dir.set_low()?,
        }
        Ok(())
    }

    pub fn set_stop_flag(&mut self, flag: &'static AtomicBool) {
        self.stop_flag = Some(flag);
    }

    pub fn clear_stop_flag(&mut self) {
        self.stop_flag = None;
    }

    pub fn move_steps(&mut self, intervals_us: &[u32]) -> Result<(), StepperError> {
        if intervals_us.is_empty() {
            return Ok(());
        }

        if let Some(flag) = self.stop_flag {
            if flag.load(Ordering::Acquire) {
                return Err(StepperError::LimitSwitchReached);
            }
        }

        let mut chunk = Vec::with_capacity(CHUNK_MAX);

        for &interval in intervals_us {
            if chunk.len() >= CHUNK_MAX {
                self.transmit(&chunk)?;
                chunk.clear();

                if let Some(flag) = self.stop_flag {
                    if flag.load(Ordering::Acquire) {
                        return Err(StepperError::LimitSwitchReached);
                    }
                }
            }

            let low_ticks = (interval.saturating_sub(PULSE_WIDTH_TICKS as u32))
                .min(32767) as u16;

            let symbol = Symbol::new(
                Pulse::new(PinState::High, PulseTicks::new(PULSE_WIDTH_TICKS)?),
                Pulse::new(PinState::Low, PulseTicks::new(low_ticks)?),
            );
            chunk.push(symbol);
        }

        if !chunk.is_empty() {
            self.transmit(&chunk)?;
        }

        Ok(())
    }

    fn transmit(&mut self, symbols: &[Symbol]) -> Result<(), StepperError> {
        let encoder = CopyEncoder::new()?;
        self.channel.send_and_wait(
            encoder,
            symbols,
            &TransmitConfig {
                loop_count: Loop::None,
                eot_level: false,
                ..Default::default()
            },
        )?;
        Ok(())
    }
}

impl<'d> Drop for RmtStepper<'d> {
    fn drop(&mut self) {
        let _ = self.channel.disable();
        let _ = self.en.set_high();
    }
}
