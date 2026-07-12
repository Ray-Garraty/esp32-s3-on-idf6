#pragma once

#include <cstdint>
#include "esp_err.h"

typedef struct {} wdt_hal_context_t;
typedef int wdt_inst_t;
#define WDT_RWDT ((wdt_inst_t)0)

typedef enum {
    WDT_STAGE0,
    WDT_STAGE1,
} wdt_stage_t;

typedef enum {
    WDT_STAGE_ACTION_OFF,
    WDT_STAGE_ACTION_RESET_SYSTEM,
} wdt_stage_action_t;

inline void wdt_hal_init(wdt_hal_context_t*, wdt_inst_t, uint32_t, bool) {}
inline void wdt_hal_deinit(wdt_hal_context_t*) {}
inline void wdt_hal_write_protect_disable(wdt_hal_context_t*) {}
inline void wdt_hal_write_protect_enable(wdt_hal_context_t*) {}
inline void wdt_hal_set_flashboot_en(wdt_hal_context_t*, bool) {}
inline void wdt_hal_config_stage(wdt_hal_context_t*, wdt_stage_t, uint32_t, wdt_stage_action_t) {}
inline void wdt_hal_enable(wdt_hal_context_t*) {}
inline void wdt_hal_feed(wdt_hal_context_t*) {}
