/* Plugin System Central Include File
 *
 * This file includes all plugin header files. Plugins are automatically
 * discovered by the build system and their headers are included here.
 *
 * Each plugin should register itself in its implementation file.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __PLUGINS_H__
#define __PLUGINS_H__

/* Include plugin headers */
#include "plugins/effects/effects_plugin.h"
#include "plugins/sequence/sequence_plugin.h"

/**
 * @brief Initialize all plugins
 *
 * This function registers all plugins with the plugin system.
 * Should be called during system initialization.
 */
static inline void plugins_init(void)
{
    /* Register effects plugin */
    effects_plugin_register();

    /* Register sequence plugin */
    sequence_plugin_register();
}

#endif /* __PLUGINS_H__ */
