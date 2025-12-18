idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS ""
                       PRIV_REQUIRES driver
                       PRIV_REQUIRES esp_timer
                       PRIV_REQUIRES nvs_flash
)
