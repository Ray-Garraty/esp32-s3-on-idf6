#[derive(Debug, Clone, Copy)]
pub struct RampConfig {
    pub accel_steps: u32,
    pub decel_steps: u32,
    pub min_interval_us: u32,
    pub max_interval_us: u32,
}

impl RampConfig {
    pub const fn new(
        accel_steps: u32,
        decel_steps: u32,
        max_hz: u32,
        min_hz: u32,
    ) -> Self {
        Self {
            accel_steps,
            decel_steps,
            min_interval_us: if max_hz > 0 { 1_000_000 / max_hz } else { 333 },
            max_interval_us: if min_hz > 0 { 1_000_000 / min_hz } else { 33333 },
        }
    }
}

pub fn compute_ramp(total_steps: u32, config: &RampConfig) -> Vec<u32> {
    if total_steps == 0 {
        return Vec::new();
    }

    let range = config.max_interval_us as i64 - config.min_interval_us as i64;

    let (accel_steps, decel_steps, cruise_steps) = if total_steps >= config.accel_steps + config.decel_steps {
        (config.accel_steps, config.decel_steps, total_steps - config.accel_steps - config.decel_steps)
    } else {
        let half = total_steps / 2;
        (half, total_steps - half, 0)
    };

    let mut intervals = Vec::with_capacity(total_steps as usize);

    for i in 0..accel_steps {
        let progress = if accel_steps <= 1 { 1 } else { i as i64 };
        let denom = if accel_steps <= 1 { 1 } else { (accel_steps - 1) as i64 };
        let interval = config.max_interval_us as i64 - range * progress / denom;
        intervals.push(interval.max(config.min_interval_us as i64) as u32);
    }

    for _ in 0..cruise_steps {
        intervals.push(config.min_interval_us);
    }

    for i in 0..decel_steps {
        let progress = if decel_steps <= 1 { 1 } else { i as i64 };
        let denom = if decel_steps <= 1 { 1 } else { (decel_steps - 1) as i64 };
        let interval = config.min_interval_us as i64 + range * progress / denom;
        intervals.push(interval.min(config.max_interval_us as i64) as u32);
    }

    intervals
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
            assert_eq!(r[i], cfg.min_interval_us, "step {} should be at min_interval", i);
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
            assert!(r[i] <= r[i - 1], "accel step {}: {} > {}", i, r[i], r[i - 1]);
        }
        for i in 10..40 {
            assert_eq!(r[i], cfg.min_interval_us, "cruise step {}", i);
        }
        for i in 41..50 {
            assert!(r[i] >= r[i - 1], "decel step {}: {} < {}", i, r[i], r[i - 1]);
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
}
