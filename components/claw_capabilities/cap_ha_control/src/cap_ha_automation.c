/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "cap_ha_auto";

static const char *VALID_AUTO_ACTIONS[] = {
    "create", "update", "remove", "list", "trigger_now", "enable", "disable"
};
#define VALID_AUTO_ACTIONS_COUNT (sizeof(VALID_AUTO_ACTIONS) / sizeof(VALID_AUTO_ACTIONS[0]))

static bool auto_action_is_valid(const char *a)
{
    if (!a || !*a) return false;
    for (size_t i = 0; i < VALID_AUTO_ACTIONS_COUNT; i++) {
        if (strcmp(a, VALID_AUTO_ACTIONS[i]) == 0) return true;
    }
    return false;
}

static void emit_auto_failure(char *output, size_t output_size, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddNullToObject(root, "automation_id");
    char *s = cJSON_PrintUnformatted(root);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    else {
        snprintf(output, output_size,
                 "{\"success\":false,\"message\":\"내부 오류\",\"automation_id\":null}");
    }
    cJSON_Delete(root);
}

/* Translate a resolved entity + device_action + optional data params into the
 * HA-native action[] array. Returns a freshly-allocated cJSON array; caller
 * owns the lifecycle. Returns NULL on error and logs WARN. */
static cJSON *build_ha_action_array(const cap_ha_entity_t *entity,
                                    const char *device_action,
                                    int brightness_pct, int kelvin,
                                    const char *color)
{
    const char *svc = cap_ha_action_to_service(entity->domain, device_action);
    if (!svc) {
        ESP_LOGW(TAG, "unsupported (domain=%s, action=%s)", entity->domain, device_action);
        return NULL;
    }
    cJSON *arr = cJSON_CreateArray();
    cJSON *step = cJSON_CreateObject();
    char service_full[48];
    snprintf(service_full, sizeof(service_full), "%s.%s", entity->domain, svc);
    cJSON_AddStringToObject(step, "service", service_full);

    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "entity_id", entity->id);
    cJSON_AddItemToObject(step, "target", target);

    cJSON *data = cJSON_CreateObject();
    bool data_used = false;
    if (brightness_pct >= 0 && entity->supports_brightness) {
        cJSON_AddNumberToObject(data, "brightness_pct", brightness_pct);
        data_used = true;
    }
    if (kelvin >= 0 && (entity->supports_color || entity->supports_brightness)) {
        cJSON_AddNumberToObject(data, "kelvin", kelvin);
        data_used = true;
    }
    if (color && *color && entity->supports_color) {
        int rgb[3];
        if (cap_ha_color_to_rgb(color, rgb) == ESP_OK) {
            cJSON_AddItemToObject(data, "rgb_color", cJSON_CreateIntArray(rgb, 3));
            data_used = true;
        }
    }
    if (data_used) cJSON_AddItemToObject(step, "data", data);
    else cJSON_Delete(data);

    cJSON_AddItemToArray(arr, step);
    return arr;
}

/* Translate user trigger spec into HA-native trigger[] (and optional condition[]).
 * Out params: trigger_out / condition_out (caller owns; condition_out may be NULL).
 * Returns ESP_OK + sets *trigger_out, or error with both NULL. */
static esp_err_t build_ha_trigger_array(const cJSON *trigger_in,
                                        cJSON **trigger_out, cJSON **condition_out,
                                        char *err_msg, size_t err_msg_size)
{
    *trigger_out = NULL;
    *condition_out = NULL;
    if (!cJSON_IsObject(trigger_in)) {
        snprintf(err_msg, err_msg_size, "trigger object가 필요합니다.");
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *kind_j = cJSON_GetObjectItem(trigger_in, "kind");
    const char *kind = cJSON_IsString(kind_j) ? kind_j->valuestring : NULL;
    if (!kind) {
        snprintf(err_msg, err_msg_size, "trigger.kind가 필요합니다.");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *arr = cJSON_CreateArray();

    if (strcmp(kind, "daily_time") == 0 || strcmp(kind, "weekly") == 0) {
        const cJSON *time_j = cJSON_GetObjectItem(trigger_in, "time");
        if (!cJSON_IsString(time_j) || strlen(time_j->valuestring) != 5 ||
            time_j->valuestring[2] != ':') {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size, "trigger.time은 'HH:MM' 형식이어야 합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        char at[12];
        snprintf(at, sizeof(at), "%s:00", time_j->valuestring);
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "time");
        cJSON_AddStringToObject(step, "at", at);
        cJSON_AddItemToArray(arr, step);

        if (strcmp(kind, "weekly") == 0) {
            const cJSON *days = cJSON_GetObjectItem(trigger_in, "weekdays");
            if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) == 0) {
                cJSON_Delete(arr);
                snprintf(err_msg, err_msg_size, "weekly trigger에는 weekdays 배열이 필요합니다.");
                return ESP_ERR_INVALID_ARG;
            }
            static const char *DAY_NAMES[] = {"sun","mon","tue","wed","thu","fri","sat"};
            cJSON *cond_arr = cJSON_CreateArray();
            cJSON *cond_step = cJSON_CreateObject();
            cJSON_AddStringToObject(cond_step, "condition", "time");
            cJSON *weekday_arr = cJSON_CreateArray();
            cJSON *d = NULL;
            cJSON_ArrayForEach(d, days) {
                if (cJSON_IsNumber(d) && d->valueint >= 0 && d->valueint <= 6) {
                    cJSON_AddItemToArray(weekday_arr,
                                         cJSON_CreateString(DAY_NAMES[d->valueint]));
                }
            }
            cJSON_AddItemToObject(cond_step, "weekday", weekday_arr);
            cJSON_AddItemToArray(cond_arr, cond_step);
            *condition_out = cond_arr;
        }
    } else if (strcmp(kind, "interval") == 0) {
        const cJSON *iv_j = cJSON_GetObjectItem(trigger_in, "interval_ms");
        if (!cJSON_IsNumber(iv_j) || iv_j->valueint < 2000) {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "interval_ms는 2000 이상이어야 합니다 (HA time_pattern 한계).");
            return ESP_ERR_INVALID_ARG;
        }
        int iv = iv_j->valueint;
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "time_pattern");
        if (iv < 60000) {
            int sec = iv / 1000;
            if (sec < 2 || sec > 60) sec = sec < 2 ? 2 : 60;
            char val[8];
            snprintf(val, sizeof(val), "/%d", sec);
            cJSON_AddStringToObject(step, "seconds", val);
        } else if (iv < 3600000) {
            int min = iv / 60000;
            if (min < 1 || min > 60) min = min < 1 ? 1 : 60;
            char val[8]; snprintf(val, sizeof(val), "/%d", min);
            cJSON_AddStringToObject(step, "minutes", val);
        } else {
            int hr = iv / 3600000;
            if (hr < 1 || hr > 24) hr = hr < 1 ? 1 : 24;
            char val[8]; snprintf(val, sizeof(val), "/%d", hr);
            cJSON_AddStringToObject(step, "hours", val);
        }
        cJSON_AddItemToArray(arr, step);
    } else {
        cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 trigger.kind입니다 (%s). daily_time/weekly/interval만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }

    *trigger_out = arr;
    return ESP_OK;
}

esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size)
{
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        emit_auto_failure(output_json, output_size,
                          "요청을 해석할 수 없습니다 (JSON parse 실패).");
        return ESP_OK;
    }
    const cJSON *action_j = cJSON_GetObjectItem(root, "action");
    const char *action = cJSON_IsString(action_j) ? action_j->valuestring : NULL;
    if (!auto_action_is_valid(action)) {
        emit_auto_failure(output_json, output_size,
                          "action은 create/update/remove/list/trigger_now/enable/disable 중 하나여야 합니다.");
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* stub — 다음 task에서 분기 채움 */
    emit_auto_failure(output_json, output_size,
                      "ha_automation 미구현 (stub — 다음 task에서 채움).");
    cJSON_Delete(root);
    /* Mark builders as used until Task 6.4 wires them in. */
    (void)build_ha_action_array;
    (void)build_ha_trigger_array;
    return ESP_OK;
}
