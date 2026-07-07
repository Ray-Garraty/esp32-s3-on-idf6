idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        domain
        application
        infrastructure
        interface
        diag
        esp_system
        nvs_flash
        freertos
)
