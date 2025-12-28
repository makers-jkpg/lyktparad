#ifndef __MESH_WEB_H__
#define __MESH_WEB_H__

#include "esp_err.h"

/*******************************************************
 *                Function Definitions
 *******************************************************/

/* Start HTTP web server (only on root node) */
esp_err_t mesh_web_server_start(void);

/* Stop HTTP web server */
esp_err_t mesh_web_server_stop(void);

#endif /* __MESH_WEB_H__ */

