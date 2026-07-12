idf_component_register(
    SRCS "main.cpp"
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
if(DEFINED ENV{BUILD_DATE})
    set(BUILD_DATE $ENV{BUILD_DATE})
else()
    string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")
endif()
if(DEFINED ENV{GIT_HASH})
    set(GIT_HASH $ENV{GIT_HASH})
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

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BUILD_DATE="${BUILD_DATE}"
    GIT_HASH="${GIT_HASH}"
    APP_VERSION="${PROJECT_VER}"
)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
