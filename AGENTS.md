# Build & Check Commands

- `cargo +esp build --target xtensa-esp32-espidf` — build firmware
- `cargo test --lib stepper::ramp::tests` — host-based ramp unit tests
- `espflash flash --monitor --port COM5 "target/xtensa-esp32-espidf/debug/ecotiter"` — flash + monitor
- WDT must be disabled: `unsafe { esp_idf_sys::esp_task_wdt_deinit(); }`

# RMT Stepper API (esp-idf-hal v0.46, IDF v6)

- `TxChannelDriver::new(pin: impl OutputPin, config: &TxChannelConfig) -> Result<Self, EspError>` — create TX channel
- `CopyEncoder::new() -> Result<CopyEncoder, EspError>` — encoder for &[Symbol]
- `channel.send_and_wait(encoder, signal, &TransmitConfig{..})` — blocking transmit
- `RmtChannel` trait must be in scope for `disable()`
- `PinDriver<'d, Output>` has **1** generic arg (MODE), not 2
- Use `pin.degrade_output()` to convert concrete GPIO to `AnyOutputPin`
- `EspError` from `esp_idf_sys`, not `esp_idf_hal`
- `PinDriver::set_low()` / `set_high()` — control EN/DIR
- EN pin **active LOW**: call `set_low()` in constructor

# GPIO Pinout

| Component | Pin | Driver |
|---|---|---|
| TMC2209 STEP | GPIO25 | TxChannelDriver (RMT) |
| TMC2209 DIR | GPIO26 | PinDriver::output |
| TMC2209 EN | GPIO27 | PinDriver::output (active LOW) |
| TMC2209 UART TX/RX | GPIO17/16 | (deferred) |
| Valve open/close | GPIO12/13 | PinDriver::output |
| Limit FULL | GPIO32 | PinDriver::input + ISR |
| Limit EMPTY | GPIO35 | PinDriver::input + ISR |
| DS18B20 | GPIO4 | OneWire bitbang |
| ADC (pH) | GPIO34 | ADC1 |
| LED | GPIO2 | PinDriver::output |
| USB-Serial RX | **GPIO3 — НЕ ТРОГАТЬ** | |

# Common Issues

- `rst:0x8 (TG1WDT_SYS_RESET)` — WDT timeout from blocking RMT. Add `esp_task_wdt_deinit()`.
- GPIO pin constructors have private fields — use `peripherals.pins.gpioXX.degrade_output()`
- `esp32-nimble` needs local patch for IDF v6: add `all(esp_idf_version_major = "6")` to two `cfg_if!` blocks in `ble_characteristic.rs`

# Project Conventions

- Never break existing functionality or business logic.
- Never invent methods/hooks/APIs — verify against official docs or source.
- Never fix symptoms; always find root cause.
- Answer user questions immediately, clearly, concisely.
- Show only changed fragments with file paths.
- Never assert physical-world events — ask the user what they observe.
- Core principles: DRY, KISS, YAGNI.
- Prefer cohesive units over fragmented micro-modules.
- Extract non-obvious constants to config module or top of file.
- All pipelines must pass with 0 errors, 0 warnings — no "pre-existing" excuses.
- Use LF (\n) line endings.
- Two-strike rule: 2 attempts per task, then stop and consult user.

# COM Port Safety

- **ABSOLUTELY FORBIDDEN to launch background processes that hold COM ports.**
  Any blocking/serial tool (pio device monitor, serial terminal, etc.) MUST be run with an explicit timeout via the bash tool's `timeout` parameter or via `timeout <seconds>` prefix. Never leave a process occupying COM after exit.

# Python Script Rules

- NEVER use `python -c "..."` inline scripts — bash on Windows mangles quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
  Use `C:\Users\vlbes\AppData\Local\Temp\opencode` for temp scripts.
- Example:
  ```bash
  # Write temp script
  cat > "$TMPDIR/test_serial.py" << 'PYEOF'
  ... python code ...
  PYEOF
  python "$TMPDIR/test_serial.py"
  ```
