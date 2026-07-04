//! Trapezoidal acceleration ramp computation.
//!
//! Pure integer arithmetic — no floating-point, no ESP-IDF dependencies.
//! The ramp is computed once per motion config; `Vec` is allowed at config-change time
//! per `docs/refs/coding_style.md §5`.

#![forbid(unsafe_code)]
/// Configuration for a trapezoidal acceleration ramp.
///
/// # Fields
/// - `accel_steps`:   Number of steps during acceleration phase.
/// - `decel_steps`:   Number of steps during deceleration phase.
/// - `min_interval_us`: Interval between steps at full speed (shortest).
/// - `max_interval_us`: Interval between steps at start/stop (longest).
#[derive(Debug, Clone, Copy)]
pub struct RampConfig {
    pub accel_steps: u32,
    pub decel_steps: u32,
    pub min_interval_us: u32,
    pub max_interval_us: u32,
}

impl RampConfig {
    /// Create a new `RampConfig` from acceleration profile parameters.
    ///
    /// `max_hz` is the maximum step frequency (cruise speed).
    /// `min_hz` is the minimum step frequency (start/stop speed).
    /// Intervals are derived as `1_000_000 / hz`.
    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        clippy::manual_checked_ops
    )]
    pub const fn new(accel_steps: u32, decel_steps: u32, max_hz: u32, min_hz: u32) -> Self {
        Self {
            accel_steps,
            decel_steps,
            // Division by zero is guarded by the `> 0` check above.
            min_interval_us: if max_hz > 0 { 1_000_000 / max_hz } else { 333 },
            max_interval_us: if min_hz > 0 {
                1_000_000 / min_hz
            } else {
                33_333
            },
        }
    }
}

/// Iterator that lazily computes per-step intervals for a trapezoidal acceleration ramp.
///
/// Produces the same values as [`compute_ramp`] but without heap allocation.
/// Each call to `next()` computes the next interval in O(1).
///
/// The profile is:
/// - **Acceleration**: intervals linearly decrease from `max_interval_us` to `min_interval_us`.
/// - **Cruise**: constant interval at `min_interval_us`.
/// - **Deceleration**: intervals linearly increase back to `max_interval_us`.
///
/// If `total_steps` is less than `accel_steps + decel_steps`, the profile becomes
/// triangular (accel immediately followed by decel, no cruise phase).
///
/// # Zero steps
///
/// `RampIter::new(0, _)` yields no items.
#[derive(Debug, Clone)]
pub struct RampIter {
    total_steps: u32,
    pos: u32,
    /// Number of acceleration steps (computed from total vs config).
    accel_steps: u32,
    /// Number of deceleration steps (computed from total vs config).
    decel_steps: u32,
    /// Number of cruise steps (computed from total vs config).
    cruise_steps: u32,
    /// `max_interval_us - min_interval_us` as i64 for signed arithmetic.
    range: i64,
    min_interval_us: u32,
    max_interval_us: u32,
}

impl RampIter {
    /// Create a new lazy ramp iterator.
    ///
    /// Pre-computes the accel/decel/cruise split from `total_steps` and `config`.
    /// Construction is O(1) — no heap allocation.
    pub fn new(total_steps: u32, config: &RampConfig) -> Self {
        if total_steps == 0 {
            return Self {
                total_steps: 0,
                pos: 0,
                accel_steps: 0,
                decel_steps: 0,
                cruise_steps: 0,
                range: 0,
                min_interval_us: 0,
                max_interval_us: 0,
            };
        }

        let range = i64::from(config.max_interval_us) - i64::from(config.min_interval_us);

        let (accel_steps, decel_steps, cruise_steps) =
            if total_steps >= config.accel_steps + config.decel_steps {
                (
                    config.accel_steps,
                    config.decel_steps,
                    total_steps - config.accel_steps - config.decel_steps,
                )
            } else {
                let half = total_steps / 2;
                (half, total_steps - half, 0)
            };

        Self {
            total_steps,
            pos: 0,
            accel_steps,
            decel_steps,
            cruise_steps,
            range,
            min_interval_us: config.min_interval_us,
            max_interval_us: config.max_interval_us,
        }
    }
}

impl Iterator for RampIter {
    type Item = u32;

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        clippy::cast_lossless
    )]
    fn next(&mut self) -> Option<u32> {
        if self.pos >= self.total_steps {
            return None;
        }
        let i = self.pos;
        self.pos += 1;

        let interval = if i < self.accel_steps {
            // Acceleration phase: decreasing intervals
            let progress = if self.accel_steps <= 1 {
                1_i64
            } else {
                i64::from(i)
            };
            let denom = if self.accel_steps <= 1 {
                1_i64
            } else {
                i64::from(self.accel_steps - 1)
            };
            i64::from(self.max_interval_us) - self.range * progress / denom
        } else if i < self.accel_steps + self.cruise_steps {
            // Cruise phase: constant min interval
            return Some(self.min_interval_us);
        } else {
            // Deceleration phase: increasing intervals
            let decel_i = i - self.accel_steps - self.cruise_steps;
            let progress = if self.decel_steps <= 1 {
                1_i64
            } else {
                i64::from(decel_i)
            };
            let denom = if self.decel_steps <= 1 {
                1_i64
            } else {
                i64::from(self.decel_steps - 1)
            };
            i64::from(self.min_interval_us) + self.range * progress / denom
        };

        Some(
            interval
                .max(i64::from(self.min_interval_us))
                .min(i64::from(self.max_interval_us)) as u32,
        )
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = (self.total_steps - self.pos) as usize;
        (remaining, Some(remaining))
    }
}

/// Compute a trapezoidal acceleration ramp as a vector of per-step intervals (microseconds).
///
/// This is an allocating version — prefer [`RampIter`] for new code.
///
/// The profile is:
/// - **Acceleration**: intervals linearly decrease from `max_interval_us` to `min_interval_us`.
/// - **Cruise**: constant interval at `min_interval_us`.
/// - **Deceleration**: intervals linearly increase back to `max_interval_us`.
///
/// If `total_steps` is less than `accel_steps + decel_steps`, the profile becomes
/// triangular (accel immediately followed by decel, no cruise phase).
///
/// Returns an empty `Vec` when `total_steps` is zero.
///
/// # Allocation note
///
/// The returned `Vec` allocates `total_steps * 4` bytes on the heap. For large
/// step counts (> 2000) in constrained heap environments, use [`RampIter`] instead.
///
/// Kept for backward compatibility and tests.
#[allow(
    clippy::disallowed_types,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    clippy::cast_lossless
)]
pub fn compute_ramp(total_steps: u32, config: &RampConfig) -> Vec<u32> {
    RampIter::new(total_steps, config).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg_1000_to_100() -> RampConfig {
        RampConfig::new(10, 10, 1000, 100)
    }

    #[test]
    fn zero_steps() {
        let r = compute_ramp(0, &cfg_1000_to_100());
        assert!(r.is_empty());
    }

    #[test]
    fn single_step() {
        let r = compute_ramp(1, &cfg_1000_to_100());
        assert_eq!(r.len(), 1);
    }

    #[test]
    fn two_steps() {
        let r = compute_ramp(2, &cfg_1000_to_100());
        assert_eq!(r.len(), 2);
    }

    #[test]
    fn cruise_at_full_speed() {
        let cfg = RampConfig::new(5, 5, 1000, 100);
        let r = compute_ramp(50, &cfg);
        assert_eq!(r.len(), 50);
        for i in 5..45 {
            assert_eq!(
                r[i], cfg.min_interval_us,
                "step {} should be at min_interval",
                i
            );
        }
    }

    #[test]
    fn triangular_no_cruise() {
        let cfg = RampConfig::new(50, 50, 1000, 100);
        let r = compute_ramp(20, &cfg);
        assert_eq!(r.len(), 20);
        // first half should be decreasing (accelerating)
        for i in 1..10 {
            assert!(r[i] <= r[i - 1], "accel failed at step {}", i);
        }
        // second half should be increasing (decelerating)
        for i in 11..20 {
            assert!(r[i] >= r[i - 1], "decel failed at step {}", i);
        }
    }

    #[test]
    fn interval_bounds() {
        let cfg = cfg_1000_to_100();
        let r = compute_ramp(100, &cfg);
        for &interval in &r {
            assert!(interval >= cfg.min_interval_us);
            assert!(interval <= cfg.max_interval_us);
        }
    }

    #[test]
    fn deterministic() {
        let cfg = cfg_1000_to_100();
        assert_eq!(compute_ramp(50, &cfg), compute_ramp(50, &cfg));
    }

    #[test]
    fn accel_decel_monotonic() {
        let cfg = RampConfig::new(10, 10, 1000, 100);
        let r = compute_ramp(50, &cfg);

        for i in 1..10 {
            assert!(
                r[i] <= r[i - 1],
                "accel step {}: {} > {}",
                i,
                r[i],
                r[i - 1]
            );
        }
        for i in 10..40 {
            assert_eq!(r[i], cfg.min_interval_us, "cruise step {}", i);
        }
        for i in 41..50 {
            assert!(
                r[i] >= r[i - 1],
                "decel step {}: {} < {}",
                i,
                r[i],
                r[i - 1]
            );
        }
    }

    #[test]
    fn accel_first_is_max_interval() {
        let cfg = cfg_1000_to_100();
        let r = compute_ramp(50, &cfg);
        assert_eq!(r[0], cfg.max_interval_us);
    }

    #[test]
    fn decel_last_is_max_interval() {
        let cfg = cfg_1000_to_100();
        let r = compute_ramp(50, &cfg);
        assert_eq!(r[49], cfg.max_interval_us);
    }

    /// Property: all intervals are within [min_interval_us, max_interval_us].
    #[test]
    fn property_interval_bounds() {
        let cfg = RampConfig::new(10, 10, 1000, 100);
        for total in 0..200 {
            let r = compute_ramp(total, &cfg);
            for &iv in &r {
                assert!(iv >= cfg.min_interval_us);
                assert!(iv <= cfg.max_interval_us);
            }
        }
    }

    /// Property: ramp output length always equals input `total_steps`.
    #[test]
    fn property_length_matches_input() {
        let cfg = RampConfig::new(10, 10, 1000, 100);
        for total in 0..200 {
            let r = compute_ramp(total, &cfg);
            assert_eq!(r.len(), total as usize);
        }
    }

    /// Property: accel phase is non-increasing, decel phase is non-decreasing.
    #[test]
    fn property_monotonic_accel_decel() {
        let cfg = RampConfig::new(10, 10, 1000, 100);
        for total in 1..200 {
            let r = compute_ramp(total, &cfg);

            // Determine the actual (accel, decel, cruise) split using the same
            // logic as compute_ramp, so bounds match exactly.
            let (a_steps, d_steps) = if total >= cfg.accel_steps + cfg.decel_steps {
                (cfg.accel_steps, cfg.decel_steps)
            } else {
                (total / 2, total - total / 2)
            };

            // Accel: need at least 2 steps for a meaningful monotonicity check
            if a_steps >= 2 {
                for i in 1..a_steps {
                    assert!(
                        r[i as usize] <= r[(i - 1) as usize],
                        "accel failed at total={}, step={}",
                        total,
                        i
                    );
                }
            }

            // Decel: need at least 2 steps
            if d_steps >= 2 {
                let d_start = total - d_steps;
                for i in (d_start + 1)..total {
                    assert!(
                        r[i as usize] >= r[(i - 1) as usize],
                        "decel failed at total={}, step={}",
                        total,
                        i
                    );
                }
            }
        }
    }
}
