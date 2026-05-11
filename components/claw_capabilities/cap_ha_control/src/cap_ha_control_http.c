/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_http";

static esp_err_t nvs_get_str_alloc(nvs_handle_t h, const char *key, char *out, size_t out_size)
{
    size_t need = 0;
    esp_err_t err = nvs_get_str(h, key, NULL, &need);
    if (err != ESP_OK) return err;
    if (need > out_size) return ESP_ERR_INVALID_SIZE;
    return nvs_get_str(h, key, out, &need);
}

esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size)
{
    if (!url_out || url_size == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) { url_out[0] = '\0'; return err; }
    err = nvs_get_str_alloc(h, CAP_HA_NVS_KEY_URL, url_out, url_size);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size)
{
    if (!token_out || token_size == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) { token_out[0] = '\0'; return err; }
    err = nvs_get_str_alloc(h, CAP_HA_NVS_KEY_TOKEN, token_out, token_size);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_set_url(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, CAP_HA_NVS_KEY_URL, url);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_set_token(const char *token)
{
    if (!token) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, CAP_HA_NVS_KEY_TOKEN, token);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} cap_ha_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    cap_ha_buf_t *buf = (cap_ha_buf_t *)evt->user_data;
    if (!buf || evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    size_t needed = buf->len + (size_t)evt->data_len;
    if (needed >= buf->cap) {
        ESP_LOGW(TAG, "response truncated: have=%zu need=%zu cap=%zu",
                 buf->len, needed, buf->cap);
        size_t fits = (buf->cap > buf->len + 1) ? (buf->cap - buf->len - 1) : 0;
        if (fits) {
            memcpy(buf->data + buf->len, evt->data, fits);
            buf->len += fits;
            buf->data[buf->len] = '\0';
        }
        return ESP_OK;
    }
    memcpy(buf->data + buf->len, evt->data, evt->data_len);
    buf->len += evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

esp_err_t cap_ha_http_post_service(const char *domain, const char *service,
                                   const char *body_json, int *http_status_out,
                                   char *response_buf, size_t response_buf_size)
{
    if (!domain || !service || !body_json || !response_buf || response_buf_size == 0)
        return ESP_ERR_INVALID_ARG;
    response_buf[0] = '\0';
    if (http_status_out) *http_status_out = 0;

    char base_url[160] = {0};
    char *token = NULL;
    size_t token_cap = 4096;
    esp_err_t err = cap_ha_http_get_url(base_url, sizeof(base_url));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ha_url not set in NVS");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    token = malloc(token_cap);
    if (!token) return ESP_ERR_NO_MEM;
    err = cap_ha_http_get_token(token, token_cap);
    if (err != ESP_OK) { free(token); ESP_LOGW(TAG, "ha_token not set"); return err; }

    /* strip trailing slash from base_url */
    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') { base_url[--blen] = '\0'; }

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s/api/services/%s/%s",
             base_url, domain, service);

    /* Auth header on heap — token can be 4KB; stack-resident would blow
     * the boot-fetch task's 6KB stack (Backtrace: |<-CORRUPTED 확인됨). */
    size_t auth_len = strlen(token) + 16;
    char *auth_header = malloc(auth_len);
    if (!auth_header) { free(token); return ESP_ERR_NO_MEM; }
    snprintf(auth_header, auth_len, "Bearer %s", token);
    free(token);

    cap_ha_buf_t resp = {
        .data = response_buf, .len = 0, .cap = response_buf_size,
    };
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { free(auth_header); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Authorization", auth_header);
    esp_http_client_set_post_field(cli, body_json, (int)strlen(body_json));

    ESP_LOGI(TAG, "POST %s body=%s", full_url, body_json);
    err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    if (http_status_out) *http_status_out = status;
    esp_http_client_cleanup(cli);
    free(auth_header);
    ESP_LOGI(TAG, "POST result err=%s status=%d resp_len=%zu",
             esp_err_to_name(err), status, resp.len);
    /* Contract: ESP_OK == HTTP transport completed (caller checks
     * http_status_out for 2xx vs 4xx/5xx). Returning a hard error here
     * for non-2xx would shadow the actual status (401 in particular)
     * from the failure-message composer, which gives users a misleading
     * "network err" instead of "인증 실패". esp_http_client convention. */
    return err;
}

esp_err_t cap_ha_http_get_states(char *response_buf, size_t response_buf_size)
{
    if (!response_buf || response_buf_size == 0) return ESP_ERR_INVALID_ARG;
    response_buf[0] = '\0';

    char base_url[160] = {0};
    char *token = NULL;
    size_t token_cap = 4096;
    esp_err_t err = cap_ha_http_get_url(base_url, sizeof(base_url));
    if (err != ESP_OK) return err;
    token = malloc(token_cap);
    if (!token) return ESP_ERR_NO_MEM;
    err = cap_ha_http_get_token(token, token_cap);
    if (err != ESP_OK) { free(token); return err; }

    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') base_url[--blen] = '\0';

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s/api/states", base_url);

    size_t auth_len = strlen(token) + 16;
    char *auth_header = malloc(auth_len);
    if (!auth_header) { free(token); return ESP_ERR_NO_MEM; }
    snprintf(auth_header, auth_len, "Bearer %s", token);
    free(token);

    cap_ha_buf_t resp = {
        .data = response_buf, .len = 0, .cap = response_buf_size,
    };
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { free(auth_header); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Authorization", auth_header);

    ESP_LOGI(TAG, "GET %s", full_url);
    err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(auth_header);
    ESP_LOGI(TAG, "GET result err=%s status=%d resp_len=%zu",
             esp_err_to_name(err), status, resp.len);
    if (err != ESP_OK) return err;
    if (status / 100 != 2) {
        ESP_LOGW(TAG, "GET /api/states non-2xx (%d) — caller should treat as failure", status);
        return ESP_ERR_HTTP_CONNECT;
    }
    return ESP_OK;
}
