/* Common-Cathode RGB LED Driver Header
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __LIGHT_COMMON_CATHODE_H__
#define __LIGHT_COMMON_CATHODE_H__

/*******************************************************
 *                Common-Cathode RGB LED Functions
 *******************************************************/

/**
 * Initialize the common-cathode RGB LED using LEDC PWM.
 * Configures LEDC timer and channels for R, G, B control.
 * Sets initial duty cycle to 0 (LEDs off).
 */
void init_rgb_led(void);

/**
 * Set the RGB LED color values.
 *
 * @param r Red component (0-255, clamped if out of range)
 * @param g Green component (0-255, clamped if out of range)
 * @param b Blue component (0-255, clamped if out of range)
 *
 * For common-cathode LEDs:
 * - Higher values = brighter LED
 * - 0 = LED off
 * - 255 = LED fully on
 */
void set_rgb_led(int r, int g, int b);

#endif /* __LIGHT_COMMON_CATHODE_H__ */
