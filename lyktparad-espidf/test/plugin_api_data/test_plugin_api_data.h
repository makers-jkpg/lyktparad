/* Plugin API Data Endpoint Test Header
 *
 * Unit tests for plugin_forward_data_to_mesh function.
 * Tests parameter validation, command construction, and error handling.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 */

#ifndef TEST_PLUGIN_API_DATA_H
#define TEST_PLUGIN_API_DATA_H

/* Test function declarations */
void test_plugin_forward_data_parameter_validation(void);
void test_plugin_forward_data_size_limits(void);
void test_plugin_forward_data_zero_length(void);
void test_plugin_forward_data_command_construction(void);
void test_plugin_forward_data_plugin_not_found(void);
void test_plugin_forward_data_no_child_nodes(void);

#endif // TEST_PLUGIN_API_DATA_H
