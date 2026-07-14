idf_component_register(
    SRCS "main.cpp" "version.cpp" "gpio_config.cpp" "log_capture.cpp" "net_owner.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        diag
        domain
        infrastructure
        interface
        application
        nvs_flash
        freertos
        esp_system
        esp_driver_gpio
        esp_driver_rmt
        esp_timer
)

# Build metadata — injected by scripts/idf.sh via env vars, or auto-generated
# Written to a generated header so only version.cpp recompiles on timestamp changes.
if(DEFINED ENV{BUILD_DATE})
    set(BUILD_DATE "$ENV{BUILD_DATE}")
else()
    string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")
endif()
if(DEFINED ENV{GIT_HASH})
    set(GIT_HASH "$ENV{GIT_HASH}")
else()
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT GIT_HASH)
        set(GIT_HASH "unknown")
    endif()
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version_build.h"
    "#pragma once\n"
    "#define BUILD_DATE \"${BUILD_DATE}\"\n"
    "#define GIT_HASH \"${GIT_HASH}\"\n"
    "#define APP_VERSION \"${PROJECT_VER}\"\n"
    )
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
