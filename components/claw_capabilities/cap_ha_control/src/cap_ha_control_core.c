/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_core";

static const char *VALID_ACTIONS[] = { "turn_on", "turn_off", "toggle", "open", "close" };
#define VALID_ACTIONS_COUNT (sizeof(VALID_ACTIONS) / sizeof(VALID_ACTIONS[0]))

static bool action_is_valid(const char *a)
{
    if (!a || !*a) return false;
    for (size_t i = 0; i < VALID_ACTIONS_COUNT; i++) {
        if (strcmp(a, VALID_ACTIONS[i]) == 0) return true;
    }
    return false;
}

typedef struct { const char *name; int r; int g; int b; } cap_ha_color_t;

static const cap_ha_color_t COLOR_TABLE[] = {
    { "yellow", 255, 255, 0 },
    { "red",    255, 0,   0 },
    { "green",  0,   255, 0 },
    { "blue",   0,   0,   255 },
    { "purple", 128, 0,   255 },
    { "white",  255, 255, 255 },
    { "orange", 255, 165, 0 },
    { "pink",   255, 105, 180 },
};

esp_err_t cap_ha_color_to_rgb(const char *color, int rgb_out[3])
{
    if (!color || !*color) return ESP_ERR_INVALID_ARG;

    if (color[0] == '#' && strlen(color) == 7) {
        char r[3] = { color[1], color[2], 0 };
        char g[3] = { color[3], color[4], 0 };
        char b[3] = { color[5], color[6], 0 };
        rgb_out[0] = (int)strtol(r, NULL, 16);
        rgb_out[1] = (int)strtol(g, NULL, 16);
        rgb_out[2] = (int)strtol(b, NULL, 16);
        return ESP_OK;
    }
    for (size_t i = 0; i < sizeof(COLOR_TABLE)/sizeof(COLOR_TABLE[0]); i++) {
        if (strcmp(COLOR_TABLE[i].name, color) == 0) {
            rgb_out[0] = COLOR_TABLE[i].r;
            rgb_out[1] = COLOR_TABLE[i].g;
            rgb_out[2] = COLOR_TABLE[i].b;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static const char *action_kor(const char *action)
{
    if (strcmp(action, "turn_on") == 0)  return "켰";
    if (strcmp(action, "turn_off") == 0) return "껐";
    if (strcmp(action, "toggle") == 0)   return "전환했";
    if (strcmp(action, "open") == 0)     return "열었";
    if (strcmp(action, "close") == 0)    return "닫았";
    return "처리했";
}

void cap_ha_compose_success_message(const cap_ha_entity_t *e,
                                    const char *action, int brightness_pct,
                                    const char *color,
                                    char *out, size_t out_size)
{
    char extras[64] = {0};
    size_t off = 0;
    if (color && *color) {
        off += snprintf(extras + off, sizeof(extras) - off, " %s", color);
    }
    if (brightness_pct > 0) {
        snprintf(extras + off, sizeof(extras) - off, " %d%%", brightness_pct);
    }
    snprintf(out, out_size, "%s%s을(를) %s습니다.",
             e->friendly_name, extras, action_kor(action));
}

void cap_ha_compose_failure_message(int http_status, esp_err_t http_err,
                                    char *out, size_t out_size)
{
    /* 401 takes precedence over http_err: esp_http_client returns
     * ESP_ERR_NOT_SUPPORTED for auth failures even though the HTTP
     * exchange completed, and "network err" hides the real cause. */
    if (http_status == 401 || http_status == 403) {
        snprintf(out, out_size, "HA 인증에 실패했습니다 (토큰 확인 필요).");
    } else if (http_status >= 400) {
        snprintf(out, out_size,
                 "HA 호출이 실패했습니다 (status=%d).", http_status);
    } else if (http_err != ESP_OK) {
        snprintf(out, out_size,
                 "HA 호출이 실패했습니다 (network err=%s).",
                 esp_err_to_name(http_err));
    } else {
        snprintf(out, out_size, "HA가 동작을 거부했습니다.");
    }
}

static void emit_failure(char *output, size_t output_size, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddNullToObject(root, "entity_id");
    cJSON_AddNullToObject(root, "raw_status");
    char *s = cJSON_PrintUnformatted(root);
    if (s) {
        snprintf(output, output_size, "%s", s);
        free(s);
    } else {
        snprintf(output, output_size,
                 "{\"success\":false,\"message\":\"internal error\",\"entity_id\":null,\"raw_status\":null}");
    }
    cJSON_Delete(root);
}

esp_err_t cap_ha_core_execute(const char *input_json,
                              char *output_json,
                              size_t output_size)
{
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        emit_failure(output_json, output_size,
                     "요청을 해석할 수 없습니다 (JSON parse 실패).");
        return ESP_OK;
    }

    const cJSON *target_j = cJSON_GetObjectItemCaseSensitive(root, "target");
    const cJSON *action_j = cJSON_GetObjectItemCaseSensitive(root, "action");
    const cJSON *brightness_j = cJSON_GetObjectItemCaseSensitive(root, "brightness_pct");
    const cJSON *color_j = cJSON_GetObjectItemCaseSensitive(root, "color");
    const cJSON *kelvin_j = cJSON_GetObjectItemCaseSensitive(root, "kelvin");

    const char *target = cJSON_IsString(target_j) ? target_j->valuestring : NULL;
    const char *action = cJSON_IsString(action_j) ? action_j->valuestring : NULL;

    if (!target || !*target || !action || !*action) {
        emit_failure(output_json, output_size,
                     "요청 정보가 부족합니다 (target/action 누락).");
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (!action_is_valid(action)) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "지원하지 않는 동작입니다 (action=%s).", action);
        emit_failure(output_json, output_size, msg);
        cJSON_Delete(root);
        return ESP_OK;
    }
    int brightness_pct = -1;
    if (cJSON_IsNumber(brightness_j)) {
        brightness_pct = brightness_j->valueint;
        if (brightness_pct < 1 || brightness_pct > 100) {
            emit_failure(output_json, output_size,
                         "밝기는 1–100 사이여야 합니다.");
            cJSON_Delete(root);
            return ESP_OK;
        }
    }
    int kelvin = -1;
    if (cJSON_IsNumber(kelvin_j)) {
        kelvin = kelvin_j->valueint;
        if (kelvin < 2000 || kelvin > 6500) {
            emit_failure(output_json, output_size,
                         "색온도는 2000–6500K 사이여야 합니다.");
            cJSON_Delete(root);
            return ESP_OK;
        }
    }
    (void)kelvin;
    const char *color = cJSON_IsString(color_j) ? color_j->valuestring : NULL;

    /* Resolve. Until Task 5 lands, resolve_target returns NOT_FOUND for
     * everything; emit a placeholder reject message for now. */
    cap_ha_entity_t entity = {0};
    esp_err_t err = cap_ha_resolve_target(target, &entity);
    if (err != ESP_OK) {
        char candidates[192];
        cap_ha_resolve_top_candidates(candidates, sizeof(candidates), 5);
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "\"%s\"에 해당하는 기기를 찾지 못했습니다. 사용 가능한 기기: %s.",
                 target, candidates);
        emit_failure(output_json, output_size, msg);
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* Dispatch — board branch in Task 8, HA branch in Tasks 6/7. */
    if (strncmp(entity.domain, "board", 5) == 0) {
        char msg[160];
        err = cap_ha_board_dispatch(entity.id, action, brightness_pct, color,
                                    msg, sizeof(msg));
        cJSON *out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "success", err == ESP_OK);
        cJSON_AddStringToObject(out, "message", msg);
        cJSON_AddStringToObject(out, "entity_id", entity.id);
        cJSON_AddNullToObject(out, "raw_status");
        char *s = cJSON_PrintUnformatted(out);
        if (s) {
            snprintf(output_json, output_size, "%s", s);
            free(s);
        } else {
            snprintf(output_json, output_size,
                     "{\"success\":false,\"message\":\"internal error\",\"entity_id\":null,\"raw_status\":null}");
        }
        cJSON_Delete(out);
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* HA branch — service mapping per spec section 5. */
    const char *svc = NULL;
    if (strcmp(entity.domain, "light") == 0) {
        if (strcmp(action, "turn_on") == 0)        svc = "turn_on";
        else if (strcmp(action, "turn_off") == 0)  svc = "turn_off";
        else if (strcmp(action, "toggle") == 0)    svc = "toggle";
    } else if (strcmp(entity.domain, "cover") == 0) {
        if (strcmp(action, "open") == 0)           svc = "open_cover";
        else if (strcmp(action, "close") == 0)     svc = "close_cover";
        else if (strcmp(action, "toggle") == 0)    svc = "toggle";
    } else if (strcmp(entity.domain, "switch") == 0) {
        if (strcmp(action, "turn_on") == 0)        svc = "turn_on";
        else if (strcmp(action, "turn_off") == 0)  svc = "turn_off";
        else if (strcmp(action, "toggle") == 0)    svc = "toggle";
    }
    if (!svc) {
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "%s은(는) 해당 동작을 지원하지 않습니다 (action=%s).",
                 entity.friendly_name, action);
        emit_failure(output_json, output_size, msg);
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* Build service_data — silent drop unsupported fields with WARN log. */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "entity_id", entity.id);

    if (brightness_pct >= 0) {
        if (entity.supports_brightness) {
            cJSON_AddNumberToObject(body, "brightness_pct", brightness_pct);
        } else {
            ESP_LOGW(TAG, "%s does not support brightness; dropping", entity.id);
        }
    }
    if (kelvin >= 0) {
        if (entity.supports_color || entity.supports_brightness) {
            cJSON_AddNumberToObject(body, "kelvin", kelvin);
        } else {
            ESP_LOGW(TAG, "%s does not support kelvin; dropping", entity.id);
        }
    }
    if (color && *color) {
        if (entity.supports_color) {
            int rgb[3];
            if (cap_ha_color_to_rgb(color, rgb) == ESP_OK) {
                cJSON *arr = cJSON_CreateIntArray(rgb, 3);
                cJSON_AddItemToObject(body, "rgb_color", arr);
            } else {
                ESP_LOGW(TAG, "color '%s' not recognized; dropping", color);
            }
        } else {
            ESP_LOGW(TAG, "%s does not support color; dropping", entity.id);
        }
    }

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) {
        emit_failure(output_json, output_size, "내부 오류 (JSON 직렬화 실패).");
        cJSON_Delete(root);
        return ESP_OK;
    }

    char ha_resp[CAP_HA_RESPONSE_BUF_BYTES];
    int http_status = 0;
    err = cap_ha_http_post_service(entity.domain, svc, body_str, &http_status,
                                   ha_resp, sizeof(ha_resp));
    free(body_str);

    bool body_signals_failure = (strstr(ha_resp, "\"isError\":true") != NULL);
    bool ok = (err == ESP_OK) && (http_status / 100 == 2) && !body_signals_failure;

    char msg[256];
    if (ok) {
        cap_ha_compose_success_message(&entity, action, brightness_pct, color, msg, sizeof(msg));
    } else {
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "success", ok);
    cJSON_AddStringToObject(out, "message", msg);
    cJSON_AddStringToObject(out, "entity_id", entity.id);
    cJSON_AddNumberToObject(out, "raw_status", http_status);
    char *out_str = cJSON_PrintUnformatted(out);
    if (out_str) {
        snprintf(output_json, output_size, "%s", out_str);
        free(out_str);
    } else {
        snprintf(output_json, output_size,
                 "{\"success\":false,\"message\":\"internal error\",\"entity_id\":null,\"raw_status\":null}");
    }
    cJSON_Delete(out);
    cJSON_Delete(root);
    return ESP_OK;
}
