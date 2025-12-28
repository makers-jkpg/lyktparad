
/* Mesh Internal Communication Example

   LED-strip based implementation of mesh_light API.
   Uses the led_strip component (WS2812) instead of LEDC PWM.
*/

#include <string.h>
#include "esp_err.h"
#include "esp_mesh.h"
#include "mesh_light.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"

/*******************************************************
 *                LED strip configuration
 *******************************************************/
/* Defaults to match the example in main.c. Change if needed. */
#define MESH_LED_GPIO            10
#define MESH_LED_NUM_PIXELS      1
#define MESH_LED_RMT_RES_HZ      10000000

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static bool s_light_inited = false;
static led_strip_handle_t s_led_strip = NULL;

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

	/* set default initial color */
	mesh_light_set(MESH_LIGHT_INIT);
	return ESP_OK;
}

esp_err_t mesh_light_set(int color)
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
	default:
		r = 0; g = 0; b = 0; break;
	}

	esp_err_t err = led_strip_set_pixel(s_led_strip, 0, r, g, b);
	if (err != ESP_OK) return err;
	return led_strip_refresh(s_led_strip);
}

void mesh_connected_indicator(int layer)
{
	switch (layer) {
	case 1: mesh_light_set(MESH_LIGHT_PINK); break;
	case 2: mesh_light_set(MESH_LIGHT_YELLOW); break;
	case 3: mesh_light_set(MESH_LIGHT_RED); break;
	case 4: mesh_light_set(MESH_LIGHT_BLUE); break;
	case 5: mesh_light_set(MESH_LIGHT_GREEN); break;
	case 6: mesh_light_set(MESH_LIGHT_WARNING); break;
	default: mesh_light_set(0); break;
	}
}

void mesh_disconnected_indicator(void)
{
	mesh_light_set(MESH_LIGHT_WARNING);
}

esp_err_t mesh_light_process(mesh_addr_t *from, uint8_t *buf, uint16_t len)
{
	mesh_light_ctl_t *in = (mesh_light_ctl_t *) buf;
	if (!from || !buf || len < sizeof(mesh_light_ctl_t)) {
		return ESP_FAIL;
	}
	if (in->token_id != MESH_TOKEN_ID || in->token_value != MESH_TOKEN_VALUE) {
		return ESP_FAIL;
	}
	if (in->cmd == MESH_CMD_LIGHT_ON_OFF) {
		if (in->on) {
			mesh_connected_indicator(esp_mesh_get_layer());
		} else {
			mesh_light_set(0);
		}
	}
	return ESP_OK;
}

