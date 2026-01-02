/* Neopixel/WS2812 LED Strip Driver
 *
 * LED-strip based implementation of mesh_light API.
 * Uses the led_strip component (WS2812) instead of LEDC PWM.
 */

#include <string.h>
#include "esp_err.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "mesh_device_config.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static bool s_light_inited = false;
static led_strip_handle_t s_led_strip = NULL;

/*******************************************************
 *                Helper Functions
 *******************************************************/
/* Wrapper function for led_strip_set_pixel that handles RGB/GRB conversion
 * This function always accepts RGB values and converts to the appropriate format
 * based on the USE_GRB define before calling the underlying led_strip_set_pixel function.
 */
static esp_err_t led_strip_set_pixel_rgb(led_strip_handle_t strip, uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
#ifdef USE_GRB
    /* Swap R and G for GRB format - hardware expects GRB order */
    return led_strip_set_pixel(strip, index, g, r, b);
#else
    /* Standard RGB format - no conversion needed */
    return led_strip_set_pixel(strip, index, r, g, b);
#endif
}

/*******************************************************
 *                Function Definitions
 *******************************************************/
esp_err_t mesh_light_init(void)
{
    if (s_light_inited == true) {
        return ESP_OK;
    }
    s_light_inited = true;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = MESH_LED_GPIO,
        .max_leds = MESH_LED_NUM_PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = MESH_LED_RMT_RES_HZ,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led_strip);
    if (err != ESP_OK) {
        s_light_inited = false;
        return err;
    }

    /* set default initial color - RED for unconnected state */
    mesh_light_set_colour(MESH_LIGHT_RED);
    return ESP_OK;
}

/* Set LED color using predefined color constants */
esp_err_t mesh_light_set_colour(int color)
{
    if (!s_light_inited || s_led_strip == NULL) {
        return ESP_FAIL;
    }

    uint8_t r = 0, g = 0, b = 0;
    switch (color) {
    case MESH_LIGHT_RED:
        r = 155; g = 0; b = 0; break;
    case MESH_LIGHT_GREEN:
        r = 0; g = 155; b = 0; break;
    case MESH_LIGHT_BLUE:
        r = 0; g = 0; b = 155; break;
    case MESH_LIGHT_YELLOW:
        r = 155; g = 155; b = 0; break;
    case MESH_LIGHT_PINK:
        r = 155; g = 0; b = 155; break;
    case MESH_LIGHT_INIT:
        r = 0; g = 155; b = 155; break;
    case MESH_LIGHT_WARNING:
        r = 155; g = 155; b = 155; break;
    case MESH_LIGHT_WHITE:
        r = 255; g = 255; b = 255; break;
    case MESH_LIGHT_ORANGE:
        r = 255; g = 165; b = 0; break;
    default:
        r = 0; g = 0; b = 0; break;
    }

    /* Use wrapper function to handle RGB/GRB conversion based on USE_GRB define */
    esp_err_t err = led_strip_set_pixel_rgb(s_led_strip, 0, r, g, b);
    if (err != ESP_OK) return err;
    return led_strip_refresh(s_led_strip);
}

/* Set LED color using direct RGB values (0-255) */
esp_err_t mesh_light_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_light_inited || s_led_strip == NULL) {
        return ESP_FAIL;
    }

    /* Use wrapper function to handle RGB/GRB conversion based on USE_GRB define */
    esp_err_t err = led_strip_set_pixel_rgb(s_led_strip, 0, r, g, b);
    if (err != ESP_OK) return err;
    return led_strip_refresh(s_led_strip);
}


esp_err_t mesh_light_process(mesh_addr_t *from, uint8_t *buf, uint16_t len)
{
    if (!from || !buf || len < 1) {
        return ESP_FAIL;
    }

    uint8_t cmd = buf[0];

    /* Handle RGB command (4 bytes: cmd + R + G + B) */
    if (cmd == MESH_CMD_SET_RGB) {
        if (len < 4) {
            return ESP_FAIL;
        }
        uint8_t r = buf[1];
        uint8_t g = buf[2];
        uint8_t b = buf[3];
        return mesh_light_set_rgb(r, g, b);
    }

    /* Handle light on/off command (requires token authentication) */
    if (len < sizeof(mesh_light_ctl_t)) {
        return ESP_FAIL;
    }
    mesh_light_ctl_t *in = (mesh_light_ctl_t *) buf;
    if (in->token_id != MESH_CONFIG_TOKEN_ID || in->token_value != MESH_CONFIG_TOKEN_VALUE) {
        return ESP_FAIL;
    }
    if (in->cmd == MESH_CMD_LIGHT_ON_OFF) {
        if (in->on) {
            mesh_light_set_colour(0); /* Turn on - use default/current color */
        } else {
            mesh_light_set_colour(0); /* Turn off */
        }
        return ESP_OK;
    }
    /* Unknown command type */
    return ESP_FAIL;
}
