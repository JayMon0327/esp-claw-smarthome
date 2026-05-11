/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cap_ha_resolve";

esp_err_t cap_ha_resolve_init(void) { ESP_LOGI(TAG, "stub init"); return ESP_OK; }
esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    (void)target; (void)out;
    return ESP_ERR_NOT_FOUND;
}
esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    (void)max;
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}
esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv, size_t out_size)
{
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}
esp_err_t cap_ha_resolve_refresh_from_ha(void) { return ESP_ERR_NOT_SUPPORTED; }
