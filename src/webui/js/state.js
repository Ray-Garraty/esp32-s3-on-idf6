/**
 * @file state.js
 * @brief Centralized application state & config
 */

// ─── Configuration Constants (YAGNI / KISS) ───
const CONFIG = {
  LOG_MAX_ENTRIES: 100,
  SSE_MAX_ENTRIES: 5,
  SSE_CHECK_INTERVAL_MS: 500,
  SSE_PING_INTERVAL_MS: 2000,
  SSE_TIMEOUT_MS: 1500,
  SSE_RECONNECT_DELAY_MS: 3000,
  STEPPER: {
    DEFAULT_FREQ: 300,
    DEFAULT_STEPS: 1000,
    MAX_FREQ: 1000
  },
  BC: {
    MAX_SPEED_ROWS: 10,
    FREQ_PERCENTAGES: [0.25, 0.50, 0.75],
    DEFAULT_TEMP_C: 25,
    DEFAULT_PRESSURE_KPA: 101.3,
    DEFAULT_SPEED_ML_MIN: 20,
    SECONDS_PER_MINUTE: 60
  },
  SG_THRESHOLD_MAX: 255,
  LOG_DEFAULT_LIMIT: 20,
  SSE_PING_THRESHOLD_MS: 3000
};

// ─── Mutable State ───
const APP_STATE = {
  stepper: { direction: 'LIQ_OUT', busy: false, enEnabled: false, mode: 'continuous' },
  valve: { position: 'input' },
  logs: { messages: [], sseEntries: [], sseAutoupdate: true, sseRawJson: false, baseDate: null, baseMillis: 0 },
  ui: { sgEditMode: false, motorStoppedAt: null, logLevelFilter: 'ALL' },
  calibration: { speedRowCount: 0, adcState: null, calibratingIndex: -1 }
};



window.APP_STATE = APP_STATE;
window.CONFIG = CONFIG;
