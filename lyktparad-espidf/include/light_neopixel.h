/* Mesh Internal Communication Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __LIGHT_NEOPIXEL_H__
#define __LIGHT_NEOPIXEL_H__

#include "esp_err.h"
#include "esp_mesh.h"
#include "mesh_config.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Constants
 *******************************************************/
/* Color format configuration
 * Define USE_GRB to enable GRB format (swap R and G when setting pixels)
 * Leave undefined to use standard RGB format
 */
#define USE_GRB  /* Enable GRB format conversion for WS2812 LEDs */

#define MESH_LIGHT_RED       (0xff)
#define MESH_LIGHT_GREEN     (0xfe)
#define MESH_LIGHT_BLUE      (0xfd)
#define MESH_LIGHT_YELLOW    (0xfc)
#define MESH_LIGHT_PINK      (0xfb)
#define MESH_LIGHT_INIT      (0xfa)
#define MESH_LIGHT_WARNING   (0xf9)
#define MESH_LIGHT_WHITE     (0xf8)
#define MESH_LIGHT_ORANGE    (0xf7)


/* Mesh command prefixes - used to identify message types */
#define  MESH_CMD_HEARTBEAT      (0x01)
#define  MESH_CMD_LIGHT_ON_OFF   (0x02)
#define  MESH_CMD_SET_RGB        (0x03)
#define  MESH_CMD_SEQUENCE       (0x04)  /* Sequence command: 386 bytes total (1 byte command + 1 byte rhythm + 384 bytes color data) */

/*******************************************************
 *                Type Definitions
 *******************************************************/

/*******************************************************
 *                Structures
 *******************************************************/
typedef struct {
    uint8_t cmd;
    bool on;
    uint8_t token_id;
    uint16_t token_value;
} mesh_light_ctl_t;

/*******************************************************
 *                Variables Declarations
 *******************************************************/

/*******************************************************
 *                Function Definitions
 *******************************************************/
esp_err_t mesh_light_init(void);
esp_err_t mesh_light_set_colour(int color);
esp_err_t mesh_light_set_rgb(uint8_t r, uint8_t g, uint8_t b);
esp_err_t mesh_light_process(mesh_addr_t *from, uint8_t *buf, uint16_t len);

/* State access functions for web interface (root node only) */
/* These functions are implemented in mesh_root.c */
uint32_t mesh_get_heartbeat_count(void);
void mesh_get_current_rgb(uint8_t *r, uint8_t *g, uint8_t *b, bool *is_set);
int mesh_get_node_count(void);

/* Send RGB color to all mesh nodes (root node only) */
/* This function is implemented in mesh_root.c */
esp_err_t mesh_send_rgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* __LIGHT_NEOPIXEL_H__ */
