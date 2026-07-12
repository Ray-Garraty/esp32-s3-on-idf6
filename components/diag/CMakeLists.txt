idf_component_register(
    SRCS
        "src/black_box.cpp"
        "src/crash_handler.cpp"
        "src/ffi_guard.cpp"
        "src/state_tracer.cpp"
        "src/stack_monitor.cpp"
        "src/rtc_watchdog.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_system
        esp_timer
        esp_hal_wdt
        esp_hal_uart
        freertos
        soc
        domain
)

# Panic handler chain MUST be in IRAM (LL-010)
target_compile_options(${COMPONENT_LIB} PRIVATE -ffunction-sections)
