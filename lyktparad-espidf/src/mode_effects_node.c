#include <inttypes.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "light_common_cathode.h"
#include "node_effects.h"

#define MODE_EFFECTS_NODE_TAG "mode_effects_node"

struct effect_params_strobe_t *current_strobe_params;
struct effect_params_fade_t *current_fade_params;

/* Runtime state */
static esp_timer_handle_t effect_timer = NULL;
static uint8_t current_effect_id = 0;
static bool effect_running = false;
static bool strobe_is_on = false;
static uint8_t strobe_repeat_remaining = 0;
/* Fade runtime state */
static uint8_t fade_phase = 0; /* 0=idle,1=fade_in,2=hold,3=fade_out */
static uint32_t fade_elapsed_ms = 0;
static const uint32_t fade_step_ms = 20; /* step resolution for fades */
static uint32_t fade_repeat_remaining = 0;

void effect_timer_callback(void* arg);

static inline uint8_t interp_u8(uint8_t start, uint8_t end, uint32_t elapsed, uint32_t total)
{
    if (total == 0) return end;
    if (elapsed >= total) return end;
    uint32_t s = start;
    uint32_t e = end;
    return (uint8_t)((s * (total - elapsed) + e * elapsed) / total);
}

esp_err_t effect_timer_start()
{
    if (effect_timer != NULL) {
        ESP_LOGD(MODE_EFFECTS_NODE_TAG, "Timer already created");
        return ESP_OK;
    }

    esp_timer_create_args_t args = {
        .callback = &effect_timer_callback,
        .arg = NULL,
        .name = "mode_effects_timer",
    };

    esp_err_t err = esp_timer_create(&args, &effect_timer);
    if (err != ESP_OK) {
        ESP_LOGE(MODE_EFFECTS_NODE_TAG, "Failed to create timer: %s", esp_err_to_name(err));
        effect_timer = NULL;
        return err;
    }

    ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Effect timer created");
    return ESP_OK;
}

esp_err_t effect_timer_stop()
{
    if (effect_timer != NULL) {
        esp_timer_stop(effect_timer);
        esp_timer_delete(effect_timer);
        effect_timer = NULL;
    }

    if (current_strobe_params != NULL) {
        free(current_strobe_params);
        current_strobe_params = NULL;
    }

    if (current_fade_params != NULL) {
        free(current_fade_params);
        current_fade_params = NULL;
    }

    current_effect_id = 0;
    effect_running = false;
    strobe_is_on = false;
    strobe_repeat_remaining = 0;
    fade_phase = 0;
    fade_elapsed_ms = 0;
    fade_repeat_remaining = 0;

    ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Effect timer stopped and state cleared");
    return ESP_OK;
}

void effect_timer_callback(void* arg)
{
    (void)arg;
    if (!effect_running) {
        return;
    }

    if (current_effect_id == EFFECT_STROBE && current_strobe_params != NULL) {
        struct effect_params_strobe_t *p = current_strobe_params;

        if (!strobe_is_on) {
            /* Turn ON */
            set_rgb_led(p->r_on, p->g_on, p->b_on);
            strobe_is_on = true;
            /* Schedule next toggle after duration_on */
            if (effect_timer != NULL) {
                esp_timer_start_once(effect_timer, (uint64_t)p->duration_on * 1000ULL);
            }
            return;
        } else {
            /* Turn OFF */
            set_rgb_led(p->r_off, p->g_off, p->b_off);
            strobe_is_on = false;

            if (p->repeat_count > 0) {
                if (strobe_repeat_remaining > 0) {
                    strobe_repeat_remaining--;
                }
                if (strobe_repeat_remaining == 0) {
                    /* Finished requested cycles */
                    ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Strobe effect finished (repeat_count reached)");
                    effect_timer_stop();
                    return;
                }
            }

            /* Schedule next toggle after duration_off */
            if (effect_timer != NULL) {
                esp_timer_start_once(effect_timer, (uint64_t)p->duration_off * 1000ULL);
            }
            return;
        }
    } else if (current_effect_id == EFFECT_FADE && current_fade_params != NULL) {
        struct effect_params_fade_t *p = current_fade_params;

        
        if (fade_phase == 1) { /* fade_in: from on -> off */
            if (p->fade_in_ms == 0) {
                set_rgb_led(p->r_off, p->g_off, p->b_off);
                fade_phase = 2; /* go to hold */
                fade_elapsed_ms = 0;
                if (p->duration_ms > 0) {
                    if (effect_timer != NULL) esp_timer_start_once(effect_timer, (uint64_t)p->duration_ms * 1000ULL);
                    return;
                }
            } else {
                uint32_t elapsed = fade_elapsed_ms;
                uint32_t total = p->fade_in_ms;
                uint8_t r = interp_u8(p->r_on, p->r_off, elapsed, total);
                uint8_t g = interp_u8(p->g_on, p->g_off, elapsed, total);
                uint8_t b = interp_u8(p->b_on, p->b_off, elapsed, total);
                set_rgb_led(r, g, b);

                fade_elapsed_ms += fade_step_ms;
                if (fade_elapsed_ms >= p->fade_in_ms) {
                    set_rgb_led(p->r_off, p->g_off, p->b_off);
                    fade_phase = 2; /* hold */
                    fade_elapsed_ms = 0;
                    if (p->duration_ms > 0) {
                        if (effect_timer != NULL) esp_timer_start_once(effect_timer, (uint64_t)p->duration_ms * 1000ULL);
                        return;
                    }
                } else {
                    if (effect_timer != NULL) esp_timer_start_once(effect_timer, (uint64_t)fade_step_ms * 1000ULL);
                    return;
                }
            }
        }

        if (fade_phase == 2) { /* hold between fades */
            if (p->fade_out_ms > 0) {
                fade_phase = 3; /* start fade_out */
                fade_elapsed_ms = 0;
                if (effect_timer != NULL) esp_timer_start_once(effect_timer, 1);
                return;
            } else {
                if (p->repeat_count > 0) {
                    if (fade_repeat_remaining > 0) fade_repeat_remaining--;
                    if (fade_repeat_remaining == 0) {
                        ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Fade effect finished (repeat_count reached)");
                        effect_timer_stop();
                        return;
                    }
                }
                set_rgb_led(p->r_on, p->g_on, p->b_on);
                fade_phase = 1;
                fade_elapsed_ms = 0;
                if (effect_timer != NULL) esp_timer_start_once(effect_timer, 1);
                return;
            }
        }

        if (fade_phase == 3) { /* fade_out: from off -> on */
            if (p->fade_out_ms == 0) {
                set_rgb_led(p->r_on, p->g_on, p->b_on);
                if (p->repeat_count > 0) {
                    if (fade_repeat_remaining > 0) fade_repeat_remaining--;
                    if (fade_repeat_remaining == 0) {
                        ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Fade effect finished (repeat_count reached)");
                        effect_timer_stop();
                        return;
                    }
                }
                fade_phase = 1;
                fade_elapsed_ms = 0;
                if (effect_timer != NULL) esp_timer_start_once(effect_timer, 1);
                return;
            } else {
                uint32_t elapsed = fade_elapsed_ms;
                uint32_t total = p->fade_out_ms;
                uint8_t r = interp_u8(p->r_off, p->r_on, elapsed, total);
                uint8_t g = interp_u8(p->g_off, p->g_on, elapsed, total);
                uint8_t b = interp_u8(p->b_off, p->b_on, elapsed, total);
                set_rgb_led(r, g, b);

                fade_elapsed_ms += fade_step_ms;
                if (fade_elapsed_ms >= p->fade_out_ms) {
                    set_rgb_led(p->r_on, p->g_on, p->b_on);
                    if (p->repeat_count > 0) {
                        if (fade_repeat_remaining > 0) fade_repeat_remaining--;
                        if (fade_repeat_remaining == 0) {
                            ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Fade effect finished (repeat_count reached)");
                            effect_timer_stop();
                            return;
                        }
                    }
                    fade_phase = 1;
                    fade_elapsed_ms = 0;
                    if (effect_timer != NULL) esp_timer_start_once(effect_timer, 1);
                    return;
                } else {
                    if (effect_timer != NULL) esp_timer_start_once(effect_timer, (uint64_t)fade_step_ms * 1000ULL);
                    return;
                }
            }
        }
    }

    /* Other effects would go here */
}

void play_effect(struct effect_params_t* params)
{
    if (params == NULL) {
        ESP_LOGE(MODE_EFFECTS_NODE_TAG, "play_effect called with NULL params");
        return;
    }

    ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Playing effect ID: %d", params->effect_id);

    switch (params->effect_id) {
        case EFFECT_STROBE: {
            struct effect_params_strobe_t *p = (struct effect_params_strobe_t *)params;

            /* Free previous params if any */
            if (current_strobe_params != NULL) {
                free(current_strobe_params);
                current_strobe_params = NULL;
            }

            current_strobe_params = malloc(sizeof(*p));
            if (current_strobe_params == NULL) {
                ESP_LOGE(MODE_EFFECTS_NODE_TAG, "Failed to allocate strobe params");
                return;
            }
            memcpy(current_strobe_params, p, sizeof(*p));

            /* initialize state */
            strobe_is_on = false;
            strobe_repeat_remaining = p->repeat_count;
            current_effect_id = EFFECT_STROBE;
            effect_running = true;

            /* Ensure timer exists */
            if (effect_timer_start() != ESP_OK) {
                ESP_LOGE(MODE_EFFECTS_NODE_TAG, "Failed to create/start effect timer");
                free(current_strobe_params);
                current_strobe_params = NULL;
                effect_running = false;
                current_effect_id = 0;
                return;
            }

            /* Start after optional start_delay_ms, otherwise start immediately */
            if (p->base.start_delay_ms > 0) {
                esp_timer_start_once(effect_timer, (uint64_t)p->base.start_delay_ms * 1000ULL);
            } else {
                /* schedule immediate callback (very short timeout) */
                esp_timer_start_once(effect_timer, 1);
            }

            ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Strobe effect started: on(%d,%d,%d) off(%d,%d,%d) on_ms=%u off_ms=%u repeats=%u",
                     p->r_on, p->g_on, p->b_on,
                     p->r_off, p->g_off, p->b_off,
                     p->duration_on, p->duration_off, p->repeat_count);
            break;
        }
        case EFFECT_FADE: {
            struct effect_params_fade_t *p = (struct effect_params_fade_t *)params;

            /* Free previous params if any */
            if (current_fade_params != NULL) {
                free(current_fade_params);
                current_fade_params = NULL;
            }

            current_fade_params = malloc(sizeof(*p));
            if (current_fade_params == NULL) {
                ESP_LOGE(MODE_EFFECTS_NODE_TAG, "Failed to allocate fade params");
                return;
            }
            memcpy(current_fade_params, p, sizeof(*p));

            /* initialize state */
            fade_phase = 1; /* start with fade_in (on -> off) */
            fade_elapsed_ms = 0;
            fade_repeat_remaining = p->repeat_count;
            current_effect_id = EFFECT_FADE;
            effect_running = true;

            /* Ensure timer exists */
            if (effect_timer_start() != ESP_OK) {
                ESP_LOGE(MODE_EFFECTS_NODE_TAG, "Failed to create/start effect timer");
                free(current_fade_params);
                current_fade_params = NULL;
                effect_running = false;
                current_effect_id = 0;
                return;
            }

            /* Set initial color to 'on' values, then start after optional delay */
            set_rgb_led(p->r_on, p->g_on, p->b_on);
            if (p->base.start_delay_ms > 0) {
                esp_timer_start_once(effect_timer, (uint64_t)p->base.start_delay_ms * 1000ULL);
            } else {
                esp_timer_start_once(effect_timer, 1);
            }

            ESP_LOGI(MODE_EFFECTS_NODE_TAG, "Fade effect started: on(%d,%d,%d) off(%d,%d,%d) in_ms=%u out_ms=%u hold_ms=%u repeats=%u",
                     p->r_on, p->g_on, p->b_on,
                     p->r_off, p->g_off, p->b_off,
                     p->fade_in_ms, p->fade_out_ms, p->duration_ms, p->repeat_count);
            break;
        }
        default:
            ESP_LOGW(MODE_EFFECTS_NODE_TAG, "Unknown effect_id: %d", params->effect_id);
            break;
    }
}