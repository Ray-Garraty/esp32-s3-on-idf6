use core::ops::{Add, Sub};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Steps(pub i32);

impl Steps {
    pub fn abs(self) -> u32 {
        self.0.unsigned_abs()
    }
}

impl Add for Steps {
    type Output = Self;
    fn add(self, rhs: Self) -> Self {
        Steps(self.0 + rhs.0)
    }
}

impl Sub for Steps {
    type Output = Self;
    fn sub(self, rhs: Self) -> Self {
        Steps(self.0 - rhs.0)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Hz(pub u32);

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Direction {
    Cw,
    Ccw,
}

impl Direction {
    pub fn is_cw(&self) -> bool {
        matches!(self, Direction::Cw)
    }
}

impl From<Direction> for bool {
    fn from(d: Direction) -> bool {
        d.is_cw()
    }
}
