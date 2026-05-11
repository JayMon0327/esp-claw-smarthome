/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cap_ha_core";

esp_err_t cap_ha_core_execute(const char *input_json,
                              char *output_json,
                              size_t output_size)
{
    (void)input_json;
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;
    ESP_LOGW(TAG, "stub: ha_control not yet implemented");
    snprintf(output_json, output_size,
             "{\"success\":false,\"message\":\"ha_control not yet implemented (stub).\"}");
    return ESP_OK;
}
