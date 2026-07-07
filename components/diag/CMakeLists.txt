idf_component_register(
    SRCS
        "src/black_box.cpp"
        "src/crash_handler.cpp"
        "src/stack_monitor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_system
        freertos
        domain
)

# Panic handler chain MUST be in IRAM (LL-010)
# Applied via [[gnu::section(".iram1")]] in crash_handler.cpp
target_compile_options(${COMPONENT_LIB} PRIVATE -ffunction-sections)
