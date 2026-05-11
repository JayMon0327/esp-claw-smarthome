/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
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
        if (s) { snprintf(output_json, output_size, "%s", s); free(s); }
        cJSON_Delete(out);
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* HA branch placeholder until Tasks 6/7. */
    emit_failure(output_json, output_size,
                 "HA 분기 미구현 (다음 task에서 구현).");
    cJSON_Delete(root);
    return ESP_OK;
}
