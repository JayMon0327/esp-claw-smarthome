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
    } else if (strcmp(kind, "state") == 0) {
        /* HA state platform 분기 — door sensor → light 같은 entity-state-change.
         * 입력: trigger.entity (friendly name or entity_id), trigger.to (필수),
         *       trigger.from (optional). 'for' (duration), template, attribute
         *       change 같은 advanced 옵션은 v4 에서 제외 (v5 후속). */
        const cJSON *entity_j = cJSON_GetObjectItem(trigger_in, "entity");
        const cJSON *to_j     = cJSON_GetObjectItem(trigger_in, "to");
        const cJSON *from_j   = cJSON_GetObjectItem(trigger_in, "from");
        if (!cJSON_IsString(entity_j) || !entity_j->valuestring[0] ||
            !cJSON_IsString(to_j) || !to_j->valuestring[0]) {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "state trigger 에는 entity 와 to (둘 다 비어있지 않은 문자열) 가 필요합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        /* friendly_name 으로 들어오면 registry 로 entity_id 해석, 이미 'domain.<x>'
         * 형식이면 그대로 사용 (registry 미적재 entity 도 사용자가 직접 지정 가능). */
        const char *resolved_eid = NULL;
        cap_ha_entity_t e = {0};
        if (strchr(entity_j->valuestring, '.') != NULL &&
            strncmp(entity_j->valuestring, "board:", 6) != 0) {
            /* entity_id 같이 보이면 verbatim */
            resolved_eid = entity_j->valuestring;
        } else if (cap_ha_resolve_target(entity_j->valuestring, &e) == ESP_OK) {
            if (strcmp(e.domain, "board") == 0) {
                cJSON_Delete(arr);
                snprintf(err_msg, err_msg_size,
                         "보드 entity (%s) 는 state trigger 의 entity 로 사용할 수 없습니다.",
                         entity_j->valuestring);
                return ESP_ERR_INVALID_ARG;
            }
            resolved_eid = e.id;
        } else {
            cJSON_Delete(arr);
            char cand[192];
            cap_ha_resolve_top_candidates(cand, sizeof(cand), 5);
            snprintf(err_msg, err_msg_size,
                     "trigger.entity \"%s\" 를 해석하지 못했습니다. 후보: %s.",
                     entity_j->valuestring, cand);
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "state");
        cJSON_AddStringToObject(step, "entity_id", resolved_eid);
        cJSON_AddStringToObject(step, "to", to_j->valuestring);
        if (cJSON_IsString(from_j) && from_j->valuestring[0]) {
            cJSON_AddStringToObject(step, "from", from_j->valuestring);
        }
        cJSON_AddItemToArray(arr, step);
    } else {
        cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 trigger.kind입니다 (%s). "
                 "daily_time/weekly/interval/state 만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }

    *trigger_out = arr;
    return ESP_OK;
}

/* Resolve an automation's CONFIG id (our esp_claw_<ts>) to its actual HA
 * runtime entity_id (e.g., "automation.v4_update_debug"). HA derives the
 * entity_id from the alias slug, not from the config id, so subsequent
 * service calls (trigger/turn_on/turn_off) must use the runtime entity_id.
 *
 * Returns ESP_OK with *out_entity_id populated (including "automation."
 * prefix) on match, ESP_ERR_NOT_FOUND if no entity has attributes.id ==
 * config_id, or upstream error.
 */
static esp_err_t resolve_entity_id_by_config_id(const char *config_id,
                                                char *out_entity_id,
                                                size_t out_size)
{
    if (!config_id || !*config_id || !out_entity_id || out_size == 0)
        return ESP_ERR_INVALID_ARG;
    out_entity_id[0] = '\0';

    char *states = malloc(CAP_HA_STATES_BUF_BYTES);
    if (!states) return ESP_ERR_NO_MEM;
    esp_err_t err = cap_ha_http_get_states(states, CAP_HA_STATES_BUF_BYTES);
    if (err != ESP_OK) { free(states); return err; }

    cJSON *arr = cJSON_Parse(states);
    free(states);
    if (!cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, arr) {
        const cJSON *eid = cJSON_GetObjectItemCaseSensitive(e, "entity_id");
        if (!cJSON_IsString(eid) ||
            strncmp(eid->valuestring, "automation.", 11) != 0) continue;
        const cJSON *attr = cJSON_GetObjectItemCaseSensitive(e, "attributes");
        const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(attr, "id");
        if (cJSON_IsString(id_j) && strcmp(id_j->valuestring, config_id) == 0) {
            snprintf(out_entity_id, out_size, "%s", eid->valuestring);
            result = ESP_OK;
            break;
        }
    }
    cJSON_Delete(arr);
    return result;
}

static esp_err_t do_create(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *target_j = cJSON_GetObjectItem(root, "target");
    const cJSON *dev_action_j = cJSON_GetObjectItem(root, "device_action");
    const cJSON *trigger_in = cJSON_GetObjectItem(root, "trigger");
    const cJSON *alias_j = cJSON_GetObjectItem(root, "alias");
    if (!cJSON_IsString(target_j) || !cJSON_IsString(dev_action_j) ||
        !cJSON_IsObject(trigger_in)) {
        emit_auto_failure(output, output_size,
                          "자동화 등록에는 trigger / target / device_action이 모두 필요합니다.");
        return ESP_OK;
    }

    /* Reject board:* targets — HA can't automate on-board entities. */
    if (strncmp(target_j->valuestring, "board:", 6) == 0) {
        emit_auto_failure(output, output_size,
                          "보드 자체 자동화는 v5에서 지원될 예정입니다. v4에서는 HA 기기만 자동화 가능합니다.");
        return ESP_OK;
    }

    cap_ha_entity_t entity = {0};
    if (cap_ha_resolve_target(target_j->valuestring, &entity) != ESP_OK) {
        char candidates[192];
        cap_ha_resolve_top_candidates(candidates, sizeof(candidates), 5);
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "\"%s\"에 해당하는 기기를 찾지 못했습니다. 사용 가능한 기기: %s.",
                 target_j->valuestring, candidates);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    if (strncmp(entity.domain, "board", 5) == 0) {
        emit_auto_failure(output, output_size,
                          "보드 자체 자동화는 v5에서 지원될 예정입니다.");
        return ESP_OK;
    }

    int brightness_pct = -1, kelvin = -1;
    const cJSON *bj = cJSON_GetObjectItem(root, "brightness_pct");
    if (cJSON_IsNumber(bj)) brightness_pct = bj->valueint;
    const cJSON *kj = cJSON_GetObjectItem(root, "kelvin");
    if (cJSON_IsNumber(kj)) kelvin = kj->valueint;
    const cJSON *cj = cJSON_GetObjectItem(root, "color");
    const char *color = cJSON_IsString(cj) ? cj->valuestring : NULL;

    cJSON *action_arr = build_ha_action_array(&entity, dev_action_j->valuestring,
                                              brightness_pct, kelvin, color);
    if (!action_arr) {
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "%s은(는) 해당 동작을 지원하지 않습니다 (action=%s).",
                 entity.friendly_name, dev_action_j->valuestring);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    cJSON *trigger_arr = NULL, *condition_arr = NULL;
    char err_msg[160];
    if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                               err_msg, sizeof(err_msg)) != ESP_OK) {
        emit_auto_failure(output, output_size, err_msg);
        cJSON_Delete(action_arr);
        return ESP_OK;
    }

    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "alias",
                            (cJSON_IsString(alias_j) && alias_j->valuestring[0])
                            ? alias_j->valuestring
                            : entity.friendly_name);
    cJSON_AddItemToObject(config, "triggers", trigger_arr);
    if (condition_arr) cJSON_AddItemToObject(config, "conditions", condition_arr);
    cJSON_AddItemToObject(config, "actions", action_arr);
    cJSON_AddStringToObject(config, "mode", "single");

    char auto_id[64];
    snprintf(auto_id, sizeof(auto_id), "esp_claw_%lld",
             esp_timer_get_time() / 1000000);

    char *config_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (!config_str) {
        emit_auto_failure(output, output_size, "내부 오류 (config 직렬화 실패).");
        return ESP_OK;
    }

    char http_resp[1024];
    int http_status = 0;
    esp_err_t err = cap_ha_http_put_automation_config(auto_id, config_str,
                                                     &http_status, http_resp,
                                                     sizeof(http_resp));
    free(config_str);
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    int reload_status = 0;
    char reload_resp[256];
    cap_ha_http_reload_automations(&reload_status, reload_resp, sizeof(reload_resp));
    if (reload_status / 100 != 2) {
        ESP_LOGW(TAG, "automation create succeeded but reload returned %d", reload_status);
    }

    char resolved_eid[96] = {0};
    if (resolve_entity_id_by_config_id(auto_id, resolved_eid, sizeof(resolved_eid)) != ESP_OK) {
        snprintf(resolved_eid, sizeof(resolved_eid), "automation.%s", auto_id);
        ESP_LOGW(TAG, "post-create entity_id lookup miss; using fallback %s", resolved_eid);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[384];
    snprintf(msg, sizeof(msg),
             "'%s' %s 자동화를 등록했습니다 (ID: %s, config_id: %s). HA UI에서 확인 가능합니다.",
             entity.friendly_name, dev_action_j->valuestring, resolved_eid, auto_id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", auto_id);
    cJSON_AddStringToObject(resp, "entity_id", resolved_eid);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_remove(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    /* Accept both 'esp_claw_<ts>' and 'automation.esp_claw_<ts>' forms. */
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;

    char http_resp[256];
    int http_status = 0;
    esp_err_t err = cap_ha_http_delete_automation_config(id, &http_status, http_resp, sizeof(http_resp));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    int reload_status = 0;
    cap_ha_http_reload_automations(&reload_status, http_resp, sizeof(http_resp));

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[160];
    snprintf(msg, sizeof(msg), "자동화 'automation.%s'를 삭제했습니다.", id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_list(char *output, size_t output_size)
{
    char *states = malloc(CAP_HA_STATES_BUF_BYTES);
    if (!states) {
        emit_auto_failure(output, output_size, "내부 오류 (메모리 부족).");
        return ESP_OK;
    }
    esp_err_t err = cap_ha_http_get_states(states, CAP_HA_STATES_BUF_BYTES);
    if (err != ESP_OK) {
        free(states);
        emit_auto_failure(output, output_size, "HA에서 자동화 목록을 가져오지 못했습니다.");
        return ESP_OK;
    }

    cJSON *arr = cJSON_Parse(states);
    free(states);
    cJSON *out_arr = cJSON_CreateArray();
    int count_total = 0, count_esp = 0;
    if (cJSON_IsArray(arr)) {
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, arr) {
            const cJSON *eid = cJSON_GetObjectItemCaseSensitive(e, "entity_id");
            if (!cJSON_IsString(eid) ||
                strncmp(eid->valuestring, "automation.", 11) != 0) continue;
            count_total++;
            const cJSON *attr = cJSON_GetObjectItemCaseSensitive(e, "attributes");
            const cJSON *fn = cJSON_GetObjectItemCaseSensitive(attr, "friendly_name");
            const cJSON *st = cJSON_GetObjectItemCaseSensitive(e, "state");
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "entity_id", eid->valuestring);
            cJSON_AddStringToObject(item, "friendly_name",
                                   cJSON_IsString(fn) ? fn->valuestring : "");
            cJSON_AddStringToObject(item, "state",
                                   cJSON_IsString(st) ? st->valuestring : "");
            const cJSON *cid = cJSON_GetObjectItemCaseSensitive(attr, "id");
            bool esp_managed = cJSON_IsString(cid) &&
                               strncmp(cid->valuestring, "esp_claw_", 9) == 0;
            cJSON_AddBoolToObject(item, "esp_claw_managed", esp_managed);
            if (cJSON_IsString(cid)) cJSON_AddStringToObject(item, "config_id", cid->valuestring);
            cJSON_AddItemToArray(out_arr, item);
            if (esp_managed) count_esp++;
        }
    }
    if (arr) cJSON_Delete(arr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[128];
    snprintf(msg, sizeof(msg),
             "자동화 %d건 (ESP-Claw 등록: %d건). HA UI에서 모두 확인 가능합니다.",
             count_total, count_esp);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddItemToObject(resp, "automations", out_arr);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_service(const cJSON *root, const char *service,
                            const char *success_msg_fmt,
                            char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;

    char entity_id[96];
    /* Try resolving via attributes.id first (covers our esp_claw_<ts> form).
     * If not found, fall back to "automation.<id>" — works when caller passed
     * the already-slugified entity_id local part. */
    if (resolve_entity_id_by_config_id(id, entity_id, sizeof(entity_id)) != ESP_OK) {
        snprintf(entity_id, sizeof(entity_id), "automation.%s", id);
    }

    char http_resp[256];
    int http_status = 0;
    esp_err_t err = cap_ha_http_call_automation_service(service, entity_id,
                                                       &http_status, http_resp, sizeof(http_resp));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[200];
    snprintf(msg, sizeof(msg), success_msg_fmt, entity_id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_trigger_now(const cJSON *root, char *out, size_t size) {
    return do_service(root, "trigger", "'%s' 자동화를 즉시 실행했습니다.", out, size);
}
static esp_err_t do_enable(const cJSON *root, char *out, size_t size) {
    return do_service(root, "turn_on", "'%s' 자동화를 활성화했습니다.", out, size);
}
static esp_err_t do_disable(const cJSON *root, char *out, size_t size) {
    return do_service(root, "turn_off", "'%s' 자동화를 비활성화했습니다.", out, size);
}

static esp_err_t do_update(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;

    char existing[2048];
    int http_status = 0;
    esp_err_t err = cap_ha_http_get_automation_config(id, &http_status,
                                                     existing, sizeof(existing));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        if (http_status == 404) {
            snprintf(msg, sizeof(msg),
                     "자동화 '%s'를 찾을 수 없습니다. 먼저 create로 등록하세요.", id);
        } else {
            cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        }
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    cJSON *cfg = cJSON_Parse(existing);
    if (!cJSON_IsObject(cfg)) {
        if (cfg) cJSON_Delete(cfg);
        emit_auto_failure(output, output_size, "기존 자동화 config 파싱 실패.");
        return ESP_OK;
    }

    /* Merge: any caller-provided field overrides existing. */
    const cJSON *trigger_in = cJSON_GetObjectItem(root, "trigger");
    if (cJSON_IsObject(trigger_in)) {
        cJSON *trigger_arr = NULL, *condition_arr = NULL;
        char err_msg[160];
        if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                                   err_msg, sizeof(err_msg)) != ESP_OK) {
            cJSON_Delete(cfg);
            emit_auto_failure(output, output_size, err_msg);
            return ESP_OK;
        }
        cJSON_DeleteItemFromObject(cfg, "trigger");
        cJSON_DeleteItemFromObject(cfg, "triggers");
        cJSON_AddItemToObject(cfg, "triggers", trigger_arr);
        cJSON_DeleteItemFromObject(cfg, "condition");
        cJSON_DeleteItemFromObject(cfg, "conditions");
        if (condition_arr) cJSON_AddItemToObject(cfg, "conditions", condition_arr);
    }

    /* target/device_action change → rebuild action[].
     * Caller may provide either or both; if missing, fall back to the existing
     * action[0]'s target.entity_id / service name. */
    const cJSON *target_j = cJSON_GetObjectItem(root, "target");
    const cJSON *dev_action_j = cJSON_GetObjectItem(root, "device_action");
    if (cJSON_IsString(target_j) || cJSON_IsString(dev_action_j)) {
        const char *target_name = NULL;
        if (cJSON_IsString(target_j)) {
            if (strncmp(target_j->valuestring, "board:", 6) == 0) {
                cJSON_Delete(cfg);
                emit_auto_failure(output, output_size,
                                  "보드 자체 자동화는 v5에서 지원될 예정입니다.");
                return ESP_OK;
            }
            target_name = target_j->valuestring;
        } else {
            /* Pull entity_id from existing action[0].target.entity_id */
            const cJSON *existing_act = cJSON_GetObjectItem(cfg, "actions");
            if (!cJSON_IsArray(existing_act)) existing_act = cJSON_GetObjectItem(cfg, "action");
            if (cJSON_IsArray(existing_act) && cJSON_GetArraySize(existing_act) > 0) {
                const cJSON *step0 = cJSON_GetArrayItem(existing_act, 0);
                const cJSON *tgt = cJSON_GetObjectItem(step0, "target");
                const cJSON *eid = cJSON_GetObjectItem(tgt, "entity_id");
                if (cJSON_IsString(eid)) target_name = eid->valuestring;
            }
        }

        const char *dev_action = NULL;
        if (cJSON_IsString(dev_action_j)) {
            dev_action = dev_action_j->valuestring;
        } else {
            /* Pull from existing action[0].service ("light.turn_on" → "turn_on"). */
            const cJSON *existing_act = cJSON_GetObjectItem(cfg, "actions");
            if (!cJSON_IsArray(existing_act)) existing_act = cJSON_GetObjectItem(cfg, "action");
            if (cJSON_IsArray(existing_act) && cJSON_GetArraySize(existing_act) > 0) {
                const cJSON *step0 = cJSON_GetArrayItem(existing_act, 0);
                const cJSON *svc = cJSON_GetObjectItem(step0, "service");
                if (!cJSON_IsString(svc)) svc = cJSON_GetObjectItem(step0, "action");
                if (cJSON_IsString(svc)) {
                    const char *dot = strchr(svc->valuestring, '.');
                    if (dot) dev_action = dot + 1;
                }
            }
        }

        if (!target_name || !dev_action) {
            cJSON_Delete(cfg);
            emit_auto_failure(output, output_size,
                              "기존 action 정보가 부족해 target/device_action 추론 실패.");
            return ESP_OK;
        }

        cap_ha_entity_t entity = {0};
        if (cap_ha_resolve_target(target_name, &entity) != ESP_OK) {
            cJSON_Delete(cfg);
            char msg[200];
            snprintf(msg, sizeof(msg), "\"%s\"에 해당하는 기기를 찾지 못했습니다.", target_name);
            emit_auto_failure(output, output_size, msg);
            return ESP_OK;
        }

        int brightness_pct = -1, kelvin = -1;
        const cJSON *bj = cJSON_GetObjectItem(root, "brightness_pct");
        if (cJSON_IsNumber(bj)) brightness_pct = bj->valueint;
        const cJSON *kj = cJSON_GetObjectItem(root, "kelvin");
        if (cJSON_IsNumber(kj)) kelvin = kj->valueint;
        const cJSON *cj = cJSON_GetObjectItem(root, "color");
        const char *color = cJSON_IsString(cj) ? cj->valuestring : NULL;

        cJSON *new_action = build_ha_action_array(&entity, dev_action,
                                                  brightness_pct, kelvin, color);
        if (!new_action) {
            cJSON_Delete(cfg);
            char msg[200];
            snprintf(msg, sizeof(msg),
                     "%s은(는) 해당 동작을 지원하지 않습니다 (action=%s).",
                     entity.friendly_name, dev_action);
            emit_auto_failure(output, output_size, msg);
            return ESP_OK;
        }
        cJSON_DeleteItemFromObject(cfg, "action");
        cJSON_DeleteItemFromObject(cfg, "actions");
        cJSON_AddItemToObject(cfg, "actions", new_action);
    }

    const cJSON *alias_j = cJSON_GetObjectItem(root, "alias");
    if (cJSON_IsString(alias_j)) {
        cJSON_DeleteItemFromObject(cfg, "alias");
        cJSON_AddStringToObject(cfg, "alias", alias_j->valuestring);
    }

    char *config_str = cJSON_PrintUnformatted(cfg);
    cJSON_Delete(cfg);
    if (!config_str) {
        emit_auto_failure(output, output_size, "내부 오류 (config 직렬화 실패).");
        return ESP_OK;
    }

    char http_resp[256];
    err = cap_ha_http_put_automation_config(id, config_str, &http_status,
                                            http_resp, sizeof(http_resp));
    free(config_str);
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    int rs = 0; cap_ha_http_reload_automations(&rs, http_resp, sizeof(http_resp));

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[200];
    snprintf(msg, sizeof(msg), "자동화 'automation.%s'를 업데이트했습니다.", id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
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

    esp_err_t r = ESP_OK;
    if (strcmp(action, "create") == 0)            r = do_create(root, output_json, output_size);
    else if (strcmp(action, "remove") == 0)       r = do_remove(root, output_json, output_size);
    else if (strcmp(action, "list") == 0)         r = do_list(output_json, output_size);
    else if (strcmp(action, "trigger_now") == 0)  r = do_trigger_now(root, output_json, output_size);
    else if (strcmp(action, "enable") == 0)       r = do_enable(root, output_json, output_size);
    else if (strcmp(action, "disable") == 0)      r = do_disable(root, output_json, output_size);
    else if (strcmp(action, "update") == 0)       r = do_update(root, output_json, output_size); else {
        emit_auto_failure(output_json, output_size,
                          "내부 오류 (action validation은 통과했으나 dispatch가 누락됨).");
    }
    cJSON_Delete(root);
    return r;
}
