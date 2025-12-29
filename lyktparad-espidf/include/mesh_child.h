/* Mesh Child Node Module
 *
 * This module contains code specific to child/non-root nodes in the mesh network.
 * Child nodes receive heartbeats, process RGB commands, and update their LED state.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_CHILD_H__
#define __MESH_CHILD_H__

#include "esp_err.h"

/*******************************************************
 *                Child Node Functions
 *******************************************************/

/* Initialize child node module */
esp_err_t mesh_child_init(void);

#endif /* __MESH_CHILD_H__ */
