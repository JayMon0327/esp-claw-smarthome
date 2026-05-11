/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "cap_ha_http";

esp_err_t cap_ha_http_post_service(const char *domain, const char *service,
                                   const char *body_json, int *http_status_out,
                                   char *response_buf, size_t response_buf_size)
{
    (void)domain; (void)service; (void)body_json;
    (void)response_buf; (void)response_buf_size;
    if (http_status_out) *http_status_out = 0;
    ESP_LOGW(TAG, "stub: post_service");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cap_ha_http_get_states(char *response_buf, size_t response_buf_size)
{
    (void)response_buf; (void)response_buf_size;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size)
{ if (url_out && url_size) url_out[0] = '\0'; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size)
{ if (token_out && token_size) token_out[0] = '\0'; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t cap_ha_http_set_url(const char *url) { (void)url; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cap_ha_http_set_token(const char *token) { (void)token; return ESP_ERR_NOT_SUPPORTED; }
