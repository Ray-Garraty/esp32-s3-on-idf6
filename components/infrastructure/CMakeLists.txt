idf_component_register(
    SRCS
        "src/drivers/stepper.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        domain
        driver
        esp_timer
)
