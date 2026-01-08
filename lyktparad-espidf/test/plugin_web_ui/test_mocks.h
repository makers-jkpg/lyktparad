/* Test Mocks Header
 *
 * Mock framework for plugin_web_ui unit tests.
 * Provides mocked versions of external dependencies.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 */

#ifndef TEST_MOCKS_H
#define TEST_MOCKS_H

#include "plugin_system.h"
#include <stdbool.h>
#include <stdint.h>

/* Mock plugin registry */
typedef struct {
    const char *name;
    plugin_info_t info;
    bool is_registered;
} mock_plugin_entry_t;

/* Mock plugin registry control */
void mock_plugin_registry_init(void);
void mock_plugin_registry_reset(void);
void mock_plugin_register(const char *name, plugin_info_t *info);
void mock_plugin_unregister(const char *name);
void mock_plugin_set_get_by_name_result(const plugin_info_t *result);

/* Mock malloc/free tracking */
typedef struct {
    void *ptr;
    size_t size;
    bool freed;
} mock_malloc_entry_t;

void mock_malloc_init(void);
void mock_malloc_reset(void);
void mock_malloc_set_fail(bool should_fail);
uint32_t mock_malloc_get_allocation_count(void);
uint32_t mock_malloc_get_free_count(void);
bool mock_malloc_has_leaks(void);
void *mock_malloc(size_t size);
void mock_free(void *ptr);

/* Mock esp_ptr_in_drom */
void mock_esp_ptr_in_drom_init(void);
void mock_esp_ptr_in_drom_reset(void);
void mock_esp_ptr_in_drom_set_result(const void *ptr, bool result);
bool mock_esp_ptr_in_drom(const void *ptr);

/* Mock ESP_LOG */
void mock_esp_log_init(void);
void mock_esp_log_reset(void);
uint32_t mock_esp_log_get_count(const char *level);
bool mock_esp_log_contains(const char *level, const char *message);
const char *mock_esp_log_get_last(const char *level);

#endif // TEST_MOCKS_H
