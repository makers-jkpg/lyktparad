#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "led_strip.h"          // from espressif/led_strip component
#include "driver/rmt_tx.h"      // RMT config types
#include "driver/gpio.h"

#include "esp_mesh.h"

static const char *TAG = "ws2812_blink";

#define LED_GPIO                10          // Waveshare ESP32-C3-Zero WS2812 DI pin
#define LED_NUM_PIXELS          1
#define LED_STRIP_RMT_RES_HZ    10000000    // 10 MHz for RMT resolution
#define DIAG_GPIO               2

void app_main(void)
{
    esp_err_t ret;

    // --- Configure RMT channel for WS2812 strip backend ---
    // Note: led_strip_new_rmt_device will create and manage its own RMT
    // channel. Manually creating a channel here can cause conflicts.

    // --- Configure LED strip object (1 WS2812 pixel on GPIO10) ---
    led_strip_handle_t led_strip = NULL;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_NUM_PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = 0,          // 0 = use default
        .flags = {
            .with_dma = false,
        },
    };

    ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "WS2812 blink example started");

    // Configure a diagnostic GPIO to confirm MCU is toggling pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DIAG_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool on = false;

    while (1) {
        if (on) {
            // Set pixel 0 to a dim green (R,G,B)
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 64, 0, 0));
        } else {
            // Turn all pixels off
            ESP_ERROR_CHECK(led_strip_clear(led_strip));
        }

        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        // Toggle diagnostic GPIO so you can verify the MCU is running and
        // that the pin toggles (check with multimeter or scope)
        gpio_set_level(DIAG_GPIO, on ? 1 : 0);

        on = !on;
        vTaskDelay(pdMS_TO_TICKS(500));  // 500 ms blink
    }
}
