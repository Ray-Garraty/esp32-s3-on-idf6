---
type: Architecture Reference
title: Communications Protocol — EcoTiter ESP32
description: JSON command/response protocol over Serial, BLE NUS, and WebSocket
tags: [api, serial, ble, websocket, protocol]
timestamp: 2026-07-12
---

# Communications Protocol — EcoTiter ESP32

Protocol: ASCII JSON lines separated by `\n` (`Serial.println`).
Baud rate: 115200 8N1.

Last updated: 2026-07-12.

## Command Format

```json
{"id": <uint64>, "cmd": "<command>", ...params}
```

- `id` — correlation identifier, echoed in response (except broadcast)
- `cmd` — command name (see table below)
- remaining fields — command parameters

## Response Format

### Phase 1 — ACK (command accepted, motor started)

```json
{"id": 42, "status": "ok", "data": {"status": "accepted"}}
```

### Phase 2 — Result (motor completed movement)

```json
{"id": 42, "status": "ok", "data": {"volume_dispensed_ml": 5.0}}
{"id": 42, "status": "error", "message": "limit_empty_reached", "data": {"volume_dispensed_ml": 3.2}}
```

### Single-phase commands (no motor — no phases)

```json
{"id": 42, "status": "ok", "data": {"status": "idle", "volume_ml": 8.14, "speed_ml_min": 0}}
{"id": 42, "status": "error", "message": "invalid_params"}
{"id": 42, "status": "error", "message": "burette_busy"}
```

### Broadcast (no `id`, distinct from responses)

Sent every 300 ms over Serial and WebSocket. Compact format (~97 bytes):

```json
{
  "ts": 58473,
  "temp": 24.5,
  "mv": 41.8,
  "vlv": "in",
  "brt": { "sts": "idle", "vl": 5.2, "spd": 0 }
}
```

**Key mapping table (legacy path → current key):**

| Legacy path | Current key | Type |
|---|---|---|
| `ts` | `ts` | uint32 |
| `sensors.temperature.celsius_val` | `temp` | float \| null |
| `sensors.temperature.is_connected` | inferred from `temp != null` | — |
| `sensors.electrode.mv` | `mv` | float |
| `valve.position` | `vlv` | `"in"` \| `"out"` \| `"unk"` |
| `burette.status` | `brt.sts` | `"idle"` \| `"working"` \| `"error"` |
| `burette.volume_ml` | `brt.vl` | float |
| `burette.speed_ml_min` | `brt.spd` | float |
| `meta.ip` | removed | — |

> **Note:** Temperature sensor disconnected → `"temp": null` (explicit null). The `is_connected` field is removed — the connection status is inferred from the value type (`null` = disconnected). The `meta.ip` field is removed — IP is accessible via the REST API.

## Commands

### `burette.doseVolume`

Dispense a volume. Valve→output, move steps.

**Parameters:**
| Field | Type | Required |
|---|---|---|
| `volume_ml` | float | yes, > 0 |
| `speed_ml_min` | float | yes, > 0 |

**Phases:** ACK → Result (`volume_dispensed_ml`)

**Errors:** `burette_busy`, `start_failed`, `limit_empty_reached`, `watchdog_timeout`, `stopped`

### `burette.fill`

Fill the burette. Valve→input, move to FULL limit.

**Parameters:**
| Field | Type | Required |
|---|---|---|
| `speed_ml_min` | float | yes, > 0 |

**Phases:** ACK → Result

**Errors:** `limit_full_reached` (already full), `burette_busy`, `start_failed`

### `burette.empty`

Empty the burette. Valve→output, move to EMPTY limit.

**Parameters:**
| Field | Type | Required |
|---|---|---|
| `speed_ml_min` | float | yes, > 0 |

**Phases:** ACK → Result

### `burette.rinse`

Rinse: fill → empty × N cycles. State machine, non-blocking.

**Parameters:**
| Field | Type | Required |
|---|---|---|
| `cycles` | uint8 | yes, > 0 |
| `speed_ml_min` | float | yes, > 0 |

**Phases:** ACK → Result (`cycles_completed`)

### `burette.stop`

Soft stop of the current operation. Sets `stop_requested` + `stepper_stop()`.
This is the only stop command used by the client application (Tauri).

**Parameters:** none

**Phases:** Single (ACK only). The interrupted command's Result is delivered on its own ID, not the stop ID.

### `serial.ping`

USB connection health check (from Tauri). No-op — ESP32 responds `{"status":"ok"}`.
If Tauri does not receive a response for N polling cycles, USB is considered
disconnected and ESP32 switches to BLE/standby mode.

**Parameters:** none

**Response:**
```json
{"id": 42, "status": "ok", "data": {"status": "ok"}}
```

### `burette.emergencyStop`

Hard stop: `stepper_emergency_stop()` + reset `g_pending`.

**Parameters:** none

**Phases:** Single (synchronous)

> **Important:** `emergencyStop` is for internal ESP32 use only (watchdog, homing timeout). The client application (Tauri) must use `burette.stop`. Do not call `emergencyStop` from the client.

### `burette.getStatus`

Current burette state.

**Parameters:** none

**Response (normal):**
```json
{"id": 42, "status": "ok", "data": {"status": "idle", "volume_ml": 5.2, "speed_ml_min": 0}}
```

**Response (during boot homing):**
```json
{"id": 42, "status": "ok", "data": {"status": "moving", "volume_ml": null, "speed_ml_min": 0.0}}
```

> `volume_ml: null` — residual volume is unknown until homing completes (FULL limit switch reached).

### `burette.cal.get`

Current calibration coefficients.

**Parameters:** none

**Response:**
```json
{"id": 42, "status": "ok", "data": {"steps_per_ml": 7730.0, "nominal_vol": 8.14, "speed_coeff": 0.03052, "min_freq": 30, "max_freq": 3000, "is_default": true}}
```

### `burette.cal.calcVolume`

Gravimetric calculation (preview, does not persist).

**Parameters:**
| Field | Type | Required |
|---|---|---|
| `mass_g` | float | yes |
| `temp_c` | float | no (default: 25) |
| `pressure_kpa` | float | no (default: 101.325) |
| `target_vol_ml` | float | yes |

**Response:**
```json
{"id": 42, "status": "ok", "data": {"z_factor": 1.0018, "actual_volume_ml": 4.9912, "new_steps_per_ml": 7746.3, "relative_error_pct": -0.18}}
```

### `burette.cal.calcSpeed`

OLS speed calculation (preview, does not persist).

**Parameters:**
```json
{"measurements": [{"freq_hz": 500, "speed_ml_min": 15.2}, {"freq_hz": 1000, "speed_ml_min": 30.5}]}
```

**Response:**
```json
{"id": 42, "status": "ok", "data": {"k": 0.03052, "r_squared": 0.9991, "min_freq": 30, "max_freq": 3000}}
```

### `burette.cal.save`

Persist current `g_burette_cal` to NVS.

**Parameters:** none

### `burette.cal.reset`

Reset calibration to defaults, delete NVS keys.

**Parameters:** none

## Error Codes

| Code | Description |
|---|---|
| `burette_busy` | Another operation is in progress |
| `start_failed` | Motor did not start |
| `limit_full_reached` | Burette is full (FULL limit switch) |
| `limit_empty_reached` | Burette is empty (EMPTY limit switch) |
| `stall_detected` | StallGuard triggered |
| `stopped` | Interrupted by `burette.stop` |
| `watchdog_timeout` | Operation hung > 60 s |
| `invalid_params` | Parameters out of range |

---

## WebSocket Protocol (`/ws/stream`)

Replaces SSE (`GET /api/events`) from the legacy Arduino firmware. All
real-time data (broadcasts, logs, motor results) is pushed via a single
WebSocket endpoint at `ws://<device-ip>/ws/stream`.

### Connection

```
ws://192.168.4.1/ws/stream     (AP mode)
ws://ecotiter.local/ws/stream   (mDNS, STA mode)
```

### Server → Client messages

**Broadcast (300 ms interval, extended format):**
```json
{
  "event": "broadcast",
  "data": {
    "ts": 58473, "temp": 24.5, "mv": 41.8,
    "vlv": "in",
    "brt": { "sts": "idle", "vl": 5.2, "spd": 0 },
    "stepperDrv": {"connected": true, "otpw": false, "ot": false},
    "limitSwitch": {"full": false, "empty": false},
    "stallGuard": {"value": 0, "threshold": 0, "isStalled": false}
  }
}
```

**Log push (ESP_LOG messages):**
```json
{"event": "log", "data": {"level": "INFO", "msg": "motor task started"}}
```

### Client → Server messages

Not used. The WebSocket is unidirectional (server push). Commands use
Serial or BLE (or HTTP REST).
