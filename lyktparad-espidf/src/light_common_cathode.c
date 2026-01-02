/* Common-Cathode RGB LED Driver
 *
 * This file implements LEDC PWM control for common-cathode RGB LEDs.
 * The LED cathodes are connected to GND, and the anodes are connected
 * to GPIO pins. Higher duty cycle values result in brighter LEDs.
 */

#include "light_common_cathode.h"
#include "config/mesh_device_config.h"

#ifdef RGB_ENABLE
#include "driver/ledc.h"

void init_rgb_led(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = RGB_LEDC_RESOLUTION,
        .freq_hz = RGB_LEDC_FREQUENCY_HZ,
        .speed_mode = RGB_LEDC_MODE,
        .timer_num = RGB_LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ch = {
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
        .speed_mode = RGB_LEDC_MODE,
        .timer_sel = RGB_LEDC_TIMER,
    };

    ch.channel = RGB_CHANNEL_R;
    ch.gpio_num = RGB_GPIO_R;
    ledc_channel_config(&ch);
    ch.channel = RGB_CHANNEL_G;
    ch.gpio_num = RGB_GPIO_G;
    ledc_channel_config(&ch);
    ch.channel = RGB_CHANNEL_B;
    ch.gpio_num = RGB_GPIO_B;
    ledc_channel_config(&ch);

    /* apply initial zero duty */
    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_R, 0);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_R);
    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_G, 0);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_G);
    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_B, 0);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_B);
}

void set_rgb_led(int r, int g, int b)
{
    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }

    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_R, (uint32_t)r);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_R);
    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_G, (uint32_t)g);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_G);
    ledc_set_duty(RGB_LEDC_MODE, RGB_CHANNEL_B, (uint32_t)b);
    ledc_update_duty(RGB_LEDC_MODE, RGB_CHANNEL_B);
}
#else
/* Stub functions when RGB LED is not enabled */
void init_rgb_led(void)
{
    /* No-op: RGB LED not enabled */
}

void set_rgb_led(int r, int g, int b)
{
    /* No-op: RGB LED not enabled */
    (void)r;
    (void)g;
    (void)b;
}
#endif /* RGB_ENABLE */
