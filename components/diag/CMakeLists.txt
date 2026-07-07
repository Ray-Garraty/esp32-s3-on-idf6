idf_component_register(
    SRCS
        "src/black_box.cpp"
        "src/crash_handler.cpp"
        "src/stack_monitor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_system
        esp_timer
        freertos
        soc
        domain
)

# Panic handler chain MUST be in IRAM (LL-010)
target_compile_options(${COMPONENT_LIB} PRIVATE -ffunction-sections)
