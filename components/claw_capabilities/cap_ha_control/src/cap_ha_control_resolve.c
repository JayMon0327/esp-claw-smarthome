/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_resolve";

extern const char entities_default_json_start[] asm("_binary_entities_default_json_start");
extern const char entities_default_json_end[]   asm("_binary_entities_default_json_end");

static cap_ha_registry_t s_static_registry = {0};

static bool supports_array_has(const cJSON *arr, const char *needle)
{
    if (!cJSON_IsArray(arr)) return false;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsString(it) && strcmp(it->valuestring, needle) == 0) return true;
    }
    return false;
}

static esp_err_t parse_registry(const char *json_str, cap_ha_registry_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "parse failed");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *entities = cJSON_GetObjectItemCaseSensitive(root, "entities");
    if (!cJSON_IsArray(entities)) {
        ESP_LOGE(TAG, "entities array missing");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    int count = cJSON_GetArraySize(entities);
    if (count <= 0) {
        cJSON_Delete(root);
        out->items = NULL;
        out->count = 0;
        return ESP_OK;
    }
    cap_ha_entity_t *items = calloc((size_t)count, sizeof(*items));
    if (!items) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }

    int written = 0;
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, entities) {
        const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(e, "id");
        const cJSON *name_j = cJSON_GetObjectItemCaseSensitive(e, "friendly_name");
        const cJSON *domain_j = cJSON_GetObjectItemCaseSensitive(e, "domain");
        const cJSON *supports_j = cJSON_GetObjectItemCaseSensitive(e, "supports");
        if (!cJSON_IsString(id_j) || !cJSON_IsString(name_j) || !cJSON_IsString(domain_j)) {
            ESP_LOGW(TAG, "entry %d missing fields, skipping", written);
            continue;
        }
        cap_ha_entity_t *t = &items[written];
        strlcpy(t->id, id_j->valuestring, sizeof(t->id));
        strlcpy(t->friendly_name, name_j->valuestring, sizeof(t->friendly_name));
        strlcpy(t->domain, domain_j->valuestring, sizeof(t->domain));
        t->supports_brightness = supports_array_has(supports_j, "brightness");
        t->supports_color = supports_array_has(supports_j, "color");
        written++;
    }
    out->items = items;
    out->count = (size_t)written;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t cap_ha_resolve_init(void)
{
    size_t len = (size_t)(entities_default_json_end - entities_default_json_start);
    char *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    memcpy(buf, entities_default_json_start, len);
    buf[len] = '\0';
    esp_err_t err = parse_registry(buf, &s_static_registry);
    free(buf);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loaded %zu static entities", s_static_registry.count);
    } else {
        ESP_LOGE(TAG, "static registry parse failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    (void)target; (void)out;
    /* Real cascade lands in Task 5. */
    return ESP_ERR_NOT_FOUND;
}

esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    out_csv[0] = '\0';
    size_t emitted = 0;
    for (size_t i = 0; i < s_static_registry.count && emitted < max; i++) {
        const char *sep = (emitted == 0) ? "" : ", ";
        size_t cur = strlen(out_csv);
        if (cur + strlen(sep) + strlen(s_static_registry.items[i].friendly_name) + 1 > out_size) break;
        snprintf(out_csv + cur, out_size - cur, "%s%s",
                 sep, s_static_registry.items[i].friendly_name);
        emitted++;
    }
    if (emitted == 0) snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}

esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv, size_t out_size)
{
    return cap_ha_resolve_top_candidates(out_csv, out_size, s_static_registry.count);
}

esp_err_t cap_ha_resolve_refresh_from_ha(void) { return ESP_ERR_NOT_SUPPORTED; }
