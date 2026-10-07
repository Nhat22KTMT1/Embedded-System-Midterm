idf_component_register(
    SRCS "main.c" "dht22.c" "ssd1306.c" "font5x7.c" "hcsr04.c" "rgb_led.c" "buzzer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c nvs_flash esp_timer
)