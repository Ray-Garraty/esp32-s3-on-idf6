idf_component_register(
    SRCS
        "src/drivers/stepper.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        domain
        esp_driver_gpio
        esp_driver_rmt
        esp_timer
        esp_common
)
