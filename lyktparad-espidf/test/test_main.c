/* Test Main
 *
 * PlatformIO unit test main function using Unity test framework.
 * This file is the entry point for all unit tests.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 */

#include <stdio.h>
#include "unity.h"

void setUp(void)
{
    /* Set up test fixtures before each test */
}

void tearDown(void)
{
    /* Clean up after each test */
}

/* External test function declarations */
extern void test_plugin_web_ui_registration(void);
extern void test_plugin_web_ui_bundle_retrieval(void);
extern void test_plugin_web_ui_json_escaping(void);
extern void test_plugin_web_ui_flash_detection(void);
extern void test_plugin_web_ui_memory_management(void);

/* Plugin API Data Endpoint test declarations */
extern void test_plugin_forward_data_parameter_validation(void);
extern void test_plugin_forward_data_size_limits(void);
extern void test_plugin_forward_data_zero_length(void);
extern void test_plugin_forward_data_command_construction(void);
extern void test_plugin_forward_data_plugin_not_found(void);
extern void test_plugin_forward_data_no_child_nodes(void);

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_plugin_web_ui_registration);
    RUN_TEST(test_plugin_web_ui_bundle_retrieval);
    RUN_TEST(test_plugin_web_ui_json_escaping);
    RUN_TEST(test_plugin_web_ui_flash_detection);
    RUN_TEST(test_plugin_web_ui_memory_management);

    /* Plugin API Data Endpoint tests */
    RUN_TEST(test_plugin_forward_data_parameter_validation);
    RUN_TEST(test_plugin_forward_data_size_limits);
    RUN_TEST(test_plugin_forward_data_zero_length);
    RUN_TEST(test_plugin_forward_data_command_construction);
    RUN_TEST(test_plugin_forward_data_plugin_not_found);
    RUN_TEST(test_plugin_forward_data_no_child_nodes);

    UNITY_END();
}
