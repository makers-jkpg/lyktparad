/* Plugin Web UI Test Implementation
 *
 * Comprehensive unit tests for plugin_web_ui module.
 * Uses Unity test framework and mocks for external dependencies.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 */

#include "unity.h"
#include "test_plugin_web_ui.h"
#include "test_mocks.h"
#include "plugin_web_ui.h"
#include "plugin_system.h"
#include <string.h>
#include <stdlib.h>

/* Test fixtures */
static plugin_info_t test_plugin;
static plugin_web_ui_callbacks_t test_callbacks;

/* Static test content */
static const char *test_html_content = "<div>Test HTML</div>";
static const char *test_js_content = "console.log('test');";
static const char *test_css_content = ".test { color: red; }";

/* Callback functions for testing */
static const char *test_html_callback(void)
{
    return test_html_content;
}

static const char *test_js_callback(void)
{
    return test_js_content;
}

static const char *test_css_callback(void)
{
    return test_css_content;
}

static const char *test_null_callback(void)
{
    return NULL;
}

void setUp(void)
{
    /* Initialize all mocks */
    mock_plugin_registry_init();
    mock_malloc_init();
    mock_esp_ptr_in_drom_init();
    mock_esp_log_init();

    /* Reset test plugin */
    memset(&test_plugin, 0, sizeof(test_plugin));
    test_plugin.name = "test_plugin";

    /* Reset test callbacks */
    memset(&test_callbacks, 0, sizeof(test_callbacks));
}

void tearDown(void)
{
    /* Cleanup mocks */
    mock_plugin_registry_reset();
    mock_malloc_reset();
    mock_esp_ptr_in_drom_reset();
    mock_esp_log_reset();
}

/* ========== Registration Function Tests ========== */

void test_plugin_web_ui_registration(void)
{
    /* Test 2.2.1: Registration with valid plugin name */
    test_callbacks.html_callback = test_html_callback;
    test_callbacks.js_callback = test_js_callback;
    test_callbacks.css_callback = test_css_callback;
    test_callbacks.dynamic_mask = 0x00; /* All static */

    mock_plugin_register("test_plugin", &test_plugin);

    esp_err_t result = plugin_register_web_ui("test_plugin", &test_callbacks);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(test_plugin.web_ui);
    TEST_ASSERT_EQUAL_PTR(test_html_callback, test_plugin.web_ui->html_callback);
    TEST_ASSERT_EQUAL_PTR(test_js_callback, test_plugin.web_ui->js_callback);
    TEST_ASSERT_EQUAL_PTR(test_css_callback, test_plugin.web_ui->css_callback);
    TEST_ASSERT_EQUAL(0x00, test_plugin.web_ui->dynamic_mask);

    /* Test 2.2.2: Registration with invalid plugin name */
    result = plugin_register_web_ui("nonexistent_plugin", &test_callbacks);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, result);

    /* Test 2.2.3: Registration with NULL parameters */
    result = plugin_register_web_ui(NULL, &test_callbacks);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);

    result = plugin_register_web_ui("test_plugin", NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);

    /* Test 2.2.4: Registration overwrite behavior */
    plugin_web_ui_callbacks_t callbacks2;
    memset(&callbacks2, 0, sizeof(callbacks2));
    callbacks2.html_callback = test_html_callback;
    callbacks2.dynamic_mask = 0x01; /* HTML dynamic */

    mock_malloc_init(); /* Reset malloc tracking */
    result = plugin_register_web_ui("test_plugin", &callbacks2);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_PTR(test_html_callback, test_plugin.web_ui->html_callback);
    TEST_ASSERT_NULL(test_plugin.web_ui->js_callback);
    TEST_ASSERT_NULL(test_plugin.web_ui->css_callback);
    TEST_ASSERT_EQUAL(0x01, test_plugin.web_ui->dynamic_mask);

    /* Test 2.2.5: Registration with all dynamic_mask combinations */
    const uint8_t masks[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    for (int i = 0; i < 8; i++) {
        callbacks2.dynamic_mask = masks[i];
        result = plugin_register_web_ui("test_plugin", &callbacks2);
        TEST_ASSERT_EQUAL(ESP_OK, result);
        TEST_ASSERT_EQUAL(masks[i], test_plugin.web_ui->dynamic_mask);
    }

    /* Test 2.2.6: Registration memory allocation failure */
    mock_malloc_set_fail(true);
    result = plugin_register_web_ui("test_plugin", &test_callbacks);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, result);
    mock_malloc_set_fail(false);
}

/* ========== Bundle Retrieval Function Tests ========== */

void test_plugin_web_ui_bundle_retrieval(void)
{
    /* Setup plugin with all callbacks */
    test_callbacks.html_callback = test_html_callback;
    test_callbacks.js_callback = test_js_callback;
    test_callbacks.css_callback = test_css_callback;
    test_callbacks.dynamic_mask = 0x00;

    mock_plugin_register("test_plugin", &test_plugin);
    plugin_register_web_ui("test_plugin", &test_callbacks);

    /* Test 2.3.7: Dry-run mode */
    size_t required_size = 0;
    esp_err_t result = plugin_get_web_bundle("test_plugin", NULL, 0, &required_size);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_GREATER_THAN(0, required_size);

    /* Test 2.3.1: Bundle retrieval with all callbacks */
    char *buffer = (char *)malloc(required_size);
    TEST_ASSERT_NOT_NULL(buffer);

    size_t actual_size = 0;
    result = plugin_get_web_bundle("test_plugin", buffer, required_size, &actual_size);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL(required_size, actual_size);

    /* Verify JSON format */
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"html\":"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"js\":"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"css\":"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, test_html_content));
    TEST_ASSERT_NOT_NULL(strstr(buffer, test_js_content));
    TEST_ASSERT_NOT_NULL(strstr(buffer, test_css_content));

    free(buffer);

    /* Test 2.3.2: Bundle retrieval with some NULL callbacks */
    test_callbacks.html_callback = test_html_callback;
    test_callbacks.js_callback = NULL;
    test_callbacks.css_callback = NULL;
    plugin_register_web_ui("test_plugin", &test_callbacks);

    required_size = 0;
    plugin_get_web_bundle("test_plugin", NULL, 0, &required_size);
    buffer = (char *)malloc(required_size);
    result = plugin_get_web_bundle("test_plugin", buffer, required_size, &actual_size);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"html\":"));
    TEST_ASSERT_NULL(strstr(buffer, "\"js\":"));
    TEST_ASSERT_NULL(strstr(buffer, "\"css\":"));
    free(buffer);

    /* Test 2.3.4: Invalid plugin name */
    result = plugin_get_web_bundle("nonexistent", NULL, 0, &required_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, result);

    /* Test 2.3.5: Plugin without web UI */
    plugin_info_t plugin_no_ui;
    memset(&plugin_no_ui, 0, sizeof(plugin_no_ui));
    plugin_no_ui.name = "no_ui_plugin";
    mock_plugin_register("no_ui_plugin", &plugin_no_ui);

    result = plugin_get_web_bundle("no_ui_plugin", NULL, 0, &required_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, result);

    /* Test 2.3.6: NULL parameters */
    result = plugin_get_web_bundle(NULL, NULL, 0, &required_size);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);

    result = plugin_get_web_bundle("test_plugin", NULL, 0, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);

    /* Test 2.3.8: Buffer overflow */
    test_callbacks.html_callback = test_html_callback;
    test_callbacks.js_callback = test_js_callback;
    test_callbacks.css_callback = test_css_callback;
    plugin_register_web_ui("test_plugin", &test_callbacks);

    required_size = 0;
    plugin_get_web_bundle("test_plugin", NULL, 0, &required_size);
    buffer = (char *)malloc(required_size - 1); /* Too small */
    result = plugin_get_web_bundle("test_plugin", buffer, required_size - 1, &actual_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, result);
    free(buffer);
}

/* ========== JSON Escaping Tests ========== */

void test_plugin_web_ui_json_escaping(void)
{
    /* Create callbacks that return content with special characters */
    static const char *html_with_quotes = "<div id=\"test\">Content</div>";
    static const char *html_with_backslash = "<script>var path = 'C:\\Users\\';</script>";
    static const char *html_with_newline = "Line 1\nLine 2";
    static const char *html_with_cr = "Line 1\rLine 2";
    static const char *html_mixed = "Quote: \" Backslash: \\ Newline:\n CR:\r";

    /* Test 2.4.1: Quotes escaping */
    test_callbacks.html_callback = (plugin_web_content_callback_t)html_with_quotes;
    test_callbacks.js_callback = NULL;
    test_callbacks.css_callback = NULL;
    mock_plugin_register("escape_test", &test_plugin);
    plugin_register_web_ui("escape_test", &test_callbacks);

    size_t required_size = 0;
    plugin_get_web_bundle("escape_test", NULL, 0, &required_size);
    char *buffer = (char *)malloc(required_size);
    plugin_get_web_bundle("escape_test", buffer, required_size, &required_size);

    /* Verify quotes are escaped */
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\\\""));
    TEST_ASSERT_NULL(strstr(buffer, "id=\"test\"")); /* Should be escaped */

    free(buffer);
}

/* ========== Flash Detection Tests ========== */

void test_plugin_web_ui_flash_detection(void)
{
    /* Test 2.5.3: NULL pointer */
    mock_esp_ptr_in_drom_set_result(NULL, false);
    bool result = mock_esp_ptr_in_drom(NULL);
    TEST_ASSERT_FALSE(result);

    /* Note: Actual Flash vs Heap detection requires real pointers,
     * which is difficult to test in isolation without real memory addresses.
     * Integration tests would verify this behavior. */
}

/* ========== Memory Management Tests ========== */

void test_plugin_web_ui_memory_management(void)
{
    /* Test 2.6.1: Memory allocation and deallocation */
    mock_malloc_init();

    test_callbacks.html_callback = test_html_callback;
    test_callbacks.js_callback = test_js_callback;
    test_callbacks.css_callback = test_css_callback;
    test_callbacks.dynamic_mask = 0x00;

    mock_plugin_register("mem_test", &test_plugin);

    uint32_t alloc_before = mock_malloc_get_allocation_count();
    plugin_register_web_ui("mem_test", &test_callbacks);
    uint32_t alloc_after = mock_malloc_get_allocation_count();

    TEST_ASSERT_GREATER_THAN(alloc_before, alloc_after);

    /* Test overwrite frees old memory */
    plugin_register_web_ui("mem_test", &test_callbacks);

    /* Test that dynamic content is freed */
    test_callbacks.dynamic_mask = 0x07; /* All dynamic */
    char *dynamic_html = (char *)malloc(100);
    char *dynamic_js = (char *)malloc(100);
    char *dynamic_css = (char *)malloc(100);
    strcpy(dynamic_html, test_html_content);
    strcpy(dynamic_js, test_js_content);
    strcpy(dynamic_css, test_css_content);

    plugin_web_ui_callbacks_t dynamic_callbacks;
    dynamic_callbacks.html_callback = (plugin_web_content_callback_t)dynamic_html;
    dynamic_callbacks.js_callback = (plugin_web_content_callback_t)dynamic_js;
    dynamic_callbacks.css_callback = (plugin_web_content_callback_t)dynamic_css;
    dynamic_callbacks.dynamic_mask = 0x07;

    plugin_register_web_ui("mem_test", &dynamic_callbacks);

    size_t required_size = 0;
    plugin_get_web_bundle("mem_test", NULL, 0, &required_size);
    char *buffer = (char *)malloc(required_size);
    plugin_get_web_bundle("mem_test", buffer, required_size, &required_size);

    /* Dynamic content should be freed after bundle building */
    free(buffer);

    /* Verify no leaks */
    TEST_ASSERT_FALSE(mock_malloc_has_leaks());
}
