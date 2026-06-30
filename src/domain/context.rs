//! Execution context marker types.
//!
//! `MotorContext` — must only be instantiated inside a dedicated motor/task thread.
//! Blocking RMT operations require `&MotorContext` and are rejected at
//! compile time when called without one.
//!
//! `MainContext` — symmetric marker for the main loop (future use).
//!
//! A motor thread creates its own `MotorContext` and passes `&MotorContext`
//! to every blocking call. If the main loop ever needs to create a
//! `MotorContext` to call a blocking function, that is a **code smell**
//! caught during review.

#![forbid(unsafe_code)]
/// Marker: execution inside a dedicated thread.
pub struct MotorContext;

/// Marker: execution inside the main loop.
pub struct MainContext;
