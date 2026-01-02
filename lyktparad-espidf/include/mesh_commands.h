/* Mesh Commands Header
 *
 * This header defines all mesh protocol command constants used for communication
 * between nodes in the mesh network. Command definitions are protocol-level and
 * independent of hardware implementation.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_COMMANDS_H__
#define __MESH_COMMANDS_H__

#include <stdint.h>

/*******************************************************
 *                Mesh Command Definitions
 *******************************************************/

/* Mesh Command Definitions - Light and Sequence Commands */
#define  MESH_CMD_HEARTBEAT      (0x01)
#define  MESH_CMD_LIGHT_ON_OFF   (0x02)
#define  MESH_CMD_SET_RGB        (0x03)
#define  MESH_CMD_SEQUENCE       (0x04)  /* Sequence command: 386 bytes total (1 byte command + 1 byte rhythm + 384 bytes color data) */
#define  MESH_CMD_SEQUENCE_START (0x05)  /* Start sequence playback */
#define  MESH_CMD_SEQUENCE_STOP  (0x06)  /* Stop sequence playback */
#define  MESH_CMD_SEQUENCE_RESET (0x07)  /* Reset sequence pointer to 0 */
#define  MESH_CMD_SEQUENCE_BEAT  (0x08)  /* Tempo synchronization beat (2 bytes: command + 1-byte pointer) */

/* Mesh Command Definitions - OTA/MUPDATE Commands */
/* Commands with prefix 0xF are reserved for OTA/update functionality */
#define  MESH_CMD_OTA_REQUEST    (0xF0)  /* Leaf node requests firmware update */
#define  MESH_CMD_OTA_START      (0xF1)  /* Root starts OTA distribution */
#define  MESH_CMD_OTA_BLOCK      (0xF2)  /* Firmware block data */
#define  MESH_CMD_OTA_ACK        (0xF3)  /* Block acknowledgment */
#define  MESH_CMD_OTA_STATUS     (0xF4)  /* Update status query */
#define  MESH_CMD_OTA_PREPARE_REBOOT (0xF5)  /* Prepare for coordinated reboot */
#define  MESH_CMD_OTA_REBOOT     (0xF6)  /* Execute coordinated reboot */

#endif /* __MESH_COMMANDS_H__ */
