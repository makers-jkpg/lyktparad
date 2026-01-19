/* Test Mocks Implementation
 *
 * Mock framework implementation for plugin_web_ui unit tests.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 */

#include "test_mocks.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#define MAX_MOCK_PLUGINS 10
#define MAX_MOCK_MALLOC_ENTRIES 100
#define MAX_LOG_ENTRIES 100

/* Mock plugin registry */
static mock_plugin_entry_t mock_plugins[MAX_MOCK_PLUGINS];
static uint8_t mock_plugin_count = 0;
static const plugin_info_t *mock_get_by_name_result = NULL;

/* Mock malloc/free tracking */
static mock_malloc_entry_t mock_malloc_entries[MAX_MOCK_MALLOC_ENTRIES];
static uint32_t mock_malloc_count = 0;
static uint32_t mock_free_count = 0;
static bool mock_malloc_should_fail = false;

/* Mock esp_ptr_in_drom */
typedef struct {
    const void *ptr;
    bool result;
} mock_ptr_result_t;
static mock_ptr_result_t mock_ptr_results[100];
static uint8_t mock_ptr_result_count = 0;
static bool mock_ptr_default_result = false;

/* Mock ESP_LOG */
typedef struct {
    char level[16];
    char message[256];
} mock_log_entry_t;
static mock_log_entry_t mock_log_entries[MAX_LOG_ENTRIES];
static uint32_t mock_log_count = 0;

void mock_plugin_registry_init(void)
{
    memset(mock_plugins, 0, sizeof(mock_plugins));
    mock_plugin_count = 0;
    mock_get_by_name_result = NULL;
}

void mock_plugin_registry_reset(void)
{
    for (uint8_t i = 0; i < mock_plugin_count; i++) {
        if (mock_plugins[i].info.web_ui != NULL) {
            free(mock_plugins[i].info.web_ui);
            mock_plugins[i].info.web_ui = NULL;
        }
    }
    mock_plugin_registry_init();
}

void mock_plugin_register(const char *name, plugin_info_t *info)
{
    if (mock_plugin_count >= MAX_MOCK_PLUGINS) {
        return;
    }

    mock_plugins[mock_plugin_count].name = name;
    if (info != NULL) {
        memcpy(&mock_plugins[mock_plugin_count].info, info, sizeof(plugin_info_t));
    } else {
        memset(&mock_plugins[mock_plugin_count].info, 0, sizeof(plugin_info_t));
        mock_plugins[mock_plugin_count].info.name = name;
    }
    mock_plugins[mock_plugin_count].is_registered = true;
    mock_plugin_count++;
}

void mock_plugin_unregister(const char *name)
{
    for (uint8_t i = 0; i < mock_plugin_count; i++) {
        if (strcmp(mock_plugins[i].name, name) == 0) {
            if (mock_plugins[i].info.web_ui != NULL) {
                free(mock_plugins[i].info.web_ui);
            }
            mock_plugins[i].is_registered = false;
            return;
        }
    }
}

void mock_plugin_set_get_by_name_result(const plugin_info_t *result)
{
    mock_get_by_name_result = result;
}

/* Mock plugin_get_by_name implementation */
const plugin_info_t *plugin_get_by_name(const char *name)
{
    if (mock_get_by_name_result != NULL) {
        return mock_get_by_name_result;
    }

    if (name == NULL) {
        return NULL;
    }

    for (uint8_t i = 0; i < mock_plugin_count; i++) {
        if (mock_plugins[i].is_registered &&
            mock_plugins[i].name != NULL &&
            strcmp(mock_plugins[i].name, name) == 0) {
            return &mock_plugins[i].info;
        }
    }

    return NULL;
}

void mock_malloc_init(void)
{
    memset(mock_malloc_entries, 0, sizeof(mock_malloc_entries));
    mock_malloc_count = 0;
    mock_free_count = 0;
    mock_malloc_should_fail = false;
}

void mock_malloc_reset(void)
{
    /* Free all remaining allocations */
    for (uint32_t i = 0; i < mock_malloc_count; i++) {
        if (mock_malloc_entries[i].ptr != NULL && !mock_malloc_entries[i].freed) {
            free(mock_malloc_entries[i].ptr);
            mock_malloc_entries[i].freed = true;
        }
    }
    mock_malloc_init();
}

void mock_malloc_set_fail(bool should_fail)
{
    mock_malloc_should_fail = should_fail;
}

uint32_t mock_malloc_get_allocation_count(void)
{
    return mock_malloc_count;
}

uint32_t mock_malloc_get_free_count(void)
{
    return mock_free_count;
}

bool mock_malloc_has_leaks(void)
{
    for (uint32_t i = 0; i < mock_malloc_count; i++) {
        if (mock_malloc_entries[i].ptr != NULL && !mock_malloc_entries[i].freed) {
            return true;
        }
    }
    return false;
}

void *mock_malloc(size_t size)
{
    if (mock_malloc_should_fail) {
        return NULL;
    }

    if (mock_malloc_count >= MAX_MOCK_MALLOC_ENTRIES) {
        return NULL;
    }

    void *ptr = malloc(size);
    if (ptr != NULL) {
        mock_malloc_entries[mock_malloc_count].ptr = ptr;
        mock_malloc_entries[mock_malloc_count].size = size;
        mock_malloc_entries[mock_malloc_count].freed = false;
        mock_malloc_count++;
    }

    return ptr;
}

void mock_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    for (uint32_t i = 0; i < mock_malloc_count; i++) {
        if (mock_malloc_entries[i].ptr == ptr && !mock_malloc_entries[i].freed) {
            free(ptr);
            mock_malloc_entries[i].freed = true;
            mock_free_count++;
            return;
        }
    }

    /* Not found in tracking, free anyway */
    free(ptr);
}

void mock_esp_ptr_in_drom_init(void)
{
    memset(mock_ptr_results, 0, sizeof(mock_ptr_results));
    mock_ptr_result_count = 0;
    mock_ptr_default_result = false;
}

void mock_esp_ptr_in_drom_reset(void)
{
    mock_esp_ptr_in_drom_init();
}

void mock_esp_ptr_in_drom_set_result(const void *ptr, bool result)
{
    if (mock_ptr_result_count >= 100) {
        return;
    }

    mock_ptr_results[mock_ptr_result_count].ptr = ptr;
    mock_ptr_results[mock_ptr_result_count].result = result;
    mock_ptr_result_count++;
}

/* Mock esp_ptr_in_drom implementation */
bool mock_esp_ptr_in_drom(const void *ptr)
{
    if (ptr == NULL) {
        return false;
    }

    /* Check if we have a specific result for this pointer */
    for (uint8_t i = 0; i < mock_ptr_result_count; i++) {
        if (mock_ptr_results[i].ptr == ptr) {
            return mock_ptr_results[i].result;
        }
    }

    /* Use default result */
    return mock_ptr_default_result;
}

/* Override esp_ptr_in_drom macro from esp_memory_utils.h */
bool esp_ptr_in_drom(const void *ptr)
{
    return mock_esp_ptr_in_drom(ptr);
}

void mock_esp_log_init(void)
{
    memset(mock_log_entries, 0, sizeof(mock_log_entries));
    mock_log_count = 0;
}

void mock_esp_log_reset(void)
{
    mock_esp_log_init();
}

uint32_t mock_esp_log_get_count(const char *level)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < mock_log_count; i++) {
        if (strcmp(mock_log_entries[i].level, level) == 0) {
            count++;
        }
    }
    return count;
}

bool mock_esp_log_contains(const char *level, const char *message)
{
    for (uint32_t i = 0; i < mock_log_count; i++) {
        if (strcmp(mock_log_entries[i].level, level) == 0 &&
            strstr(mock_log_entries[i].message, message) != NULL) {
            return true;
        }
    }
    return false;
}

const char *mock_esp_log_get_last(const char *level)
{
    for (int32_t i = (int32_t)mock_log_count - 1; i >= 0; i--) {
        if (strcmp(mock_log_entries[i].level, level) == 0) {
            return mock_log_entries[i].message;
        }
    }
    return NULL;
}

/* Mock ESP_LOG macros - these will be defined in test files that include this */
/* We provide functions that can be called by mock macros */
void mock_esp_log_record(const char *level, const char *tag, const char *format, ...)
{
    (void)tag;
    if (mock_log_count < MAX_LOG_ENTRIES) {
        va_list args;
        va_start(args, format);
        strncpy(mock_log_entries[mock_log_count].level, level, sizeof(mock_log_entries[mock_log_count].level) - 1);
        mock_log_entries[mock_log_count].level[sizeof(mock_log_entries[mock_log_count].level) - 1] = '\0';
        vsnprintf(mock_log_entries[mock_log_count].message, sizeof(mock_log_entries[mock_log_count].message), format, args);
        va_end(args);
        mock_log_count++;
    }
}

#include <stdarg.h>
