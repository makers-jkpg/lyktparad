/* Effects Mode Module Header
 *
 * This module provides effects mode functionality for the mesh network.
 * Effects mode allows synchronized playback of visual effects across all mesh nodes.
 */

#ifndef __NODE_EFFECTS_H__
#define __NODE_EFFECTS_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// params: RGB brightness (0-255), duration_on (ms), duration_off (ms), repeat_count (number of on/off cycles)
#define EFFECT_STROBE 1

// fade is a smooth transition between on and off states, start and end rgb values are configurable
#define EFFECT_FADE   2

// ... add more effect IDs as needed

struct effect_params_t {
    uint8_t command;           // command is the effect command identifier (i.e., always MESH_CMD_EFFECT)
    uint8_t effect_id;         // effect_id is the id of the effect to play (see play_effect function for details)
    uint16_t start_delay_ms;   // start_delay_ms is the delay before starting the effect
};

struct effect_params_strobe_t {
    struct effect_params_t base;
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t duration_on;
    uint16_t duration_off;
    uint8_t repeat_count;
};

// It's a coincidence that the fade params are similar to strobe, but they are separate effects
struct effect_params_fade_t {
    struct effect_params_t base;
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t fade_in_ms, fade_out_ms;
    uint16_t duration_ms;
    uint8_t repeat_count;
};

esp_err_t effect_timer_start();
esp_err_t effect_timer_stop();
void play_effect(struct effect_params_t* params);

#endif /* __NODE_EFFECTS_H__ */
