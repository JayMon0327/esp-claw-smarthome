/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"

#define CAP_HA_NVS_NAMESPACE       "ha_ctl"
#define CAP_HA_NVS_KEY_URL         "ha_url"
#define CAP_HA_NVS_KEY_TOKEN       "ha_token"
#define CAP_HA_NVS_KEY_CACHE       "entity_cache"
#define CAP_HA_NVS_KEY_INSECURE    "ha_insecure"  /* bool: skip TLS cert verify */

#define CAP_HA_HTTP_TIMEOUT_MS     8000

/* Service-call response: HA가 service result만 돌려주므로 16KB 충분. */
#define CAP_HA_RESPONSE_BUF_BYTES  (16 * 1024)
/* /api/states full snapshot: HA 본체는 light/cover/switch/sensor/etc 통째라
 * 16KB로는 흔히 잘린다. 64KB로 best-effort. 그래도 잘리면 truncate + WARN
 * (정적 registry fallback이 있어 데모 blocker는 아니지만 enrichment는
 * 부분적이다). v4에서 streaming parser로 교체 검토. */
#define CAP_HA_STATES_BUF_BYTES    (64 * 1024)
#define CAP_HA_MAX_REGISTRY_ENTRIES 64

typedef struct {
    char id[64];              /* entity_id, e.g. "light.smart_bulb" or "board:onboard_rgb" */
    char friendly_name[64];   /* Korean or English display name */
    char domain[16];          /* "light" / "cover" / "switch" / "board" */
    bool supports_brightness;
    bool supports_color;
} cap_ha_entity_t;

typedef struct {
    cap_ha_entity_t *items;
    size_t count;
} cap_ha_registry_t;

/* core */
esp_err_t cap_ha_core_execute(const char *input_json,
                              char *output_json,
                              size_t output_size);

/* Returns the HA service name (e.g. "turn_on", "open_cover") for a (domain, action)
 * pair, or NULL if the combination is unsupported. Used by both ha_control dispatch
 * and ha_automation translation. */
const char *cap_ha_action_to_service(const char *domain, const char *action);

esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size);

/* resolve */
esp_err_t cap_ha_resolve_init(void);
esp_err_t cap_ha_resolve_target(const char *target,
                                cap_ha_entity_t *out);
esp_err_t cap_ha_resolve_top_candidates(char *out_csv,
                                        size_t out_size,
                                        size_t max);
esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv,
                                               size_t out_size);
esp_err_t cap_ha_resolve_refresh_from_ha(void);

/* http */
esp_err_t cap_ha_http_post_service(const char *domain,
                                   const char *service,
                                   const char *body_json,
                                   int *http_status_out,
                                   char *response_buf,
                                   size_t response_buf_size);
esp_err_t cap_ha_http_get_states(char *response_buf,
                                 size_t response_buf_size);
esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size);
esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size);
esp_err_t cap_ha_http_set_url(const char *url);
esp_err_t cap_ha_http_set_token(const char *token);
esp_err_t cap_ha_http_set_insecure(bool insecure);
bool cap_ha_http_get_insecure(void);

/* HA REST automation CRUD (used by cap_ha_automation). */
esp_err_t cap_ha_http_put_automation_config(const char *id,
                                            const char *config_json,
                                            int *http_status_out,
                                            char *response_buf,
                                            size_t response_buf_size);
esp_err_t cap_ha_http_get_automation_config(const char *id,
                                            int *http_status_out,
                                            char *response_buf,
                                            size_t response_buf_size);
esp_err_t cap_ha_http_delete_automation_config(const char *id,
                                               int *http_status_out,
                                               char *response_buf,
                                               size_t response_buf_size);
esp_err_t cap_ha_http_reload_automations(int *http_status_out,
                                         char *response_buf,
                                         size_t response_buf_size);
esp_err_t cap_ha_http_call_automation_service(const char *service,
                                              const char *entity_id,
                                              int *http_status_out,
                                              char *response_buf,
                                              size_t response_buf_size);

/* board */
esp_err_t cap_ha_board_dispatch(const char *target,
                                const char *action,
                                int brightness_pct,
                                const char *color,
                                char *message_out,
                                size_t message_size);

/* color + message helpers (defined in cap_ha_control_core.c) */
esp_err_t cap_ha_color_to_rgb(const char *color, int rgb_out[3]);
void cap_ha_compose_success_message(const cap_ha_entity_t *e,
                                    const char *action, int brightness_pct,
                                    const char *color,
                                    char *out, size_t out_size);
void cap_ha_compose_failure_message(int http_status, esp_err_t http_err,
                                    char *out, size_t out_size);

/* description (defined in cap_ha_control.c) */
void cap_ha_compose_description(void);
