/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_resolve";

extern const char entities_default_json_start[] asm("_binary_entities_default_json_start");
extern const char entities_default_json_end[]   asm("_binary_entities_default_json_end");

static cap_ha_registry_t s_static_registry = {0};
static cap_ha_registry_t s_cache_registry = {0};
/* s_cache_mutex guards s_cache_registry. s_static_registry is write-once
 * at init and read-only afterwards; no mutex needed. */
static SemaphoreHandle_t s_cache_mutex = NULL;

#if CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED
static void boot_fetch_task(void *arg);
#endif

static bool supports_array_has(const cJSON *arr, const char *needle)
{
    if (!cJSON_IsArray(arr)) return false;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsString(it) && strcmp(it->valuestring, needle) == 0) return true;
    }
    return false;
}

static bool registry_has_id(const cap_ha_registry_t *reg, const char *id)
{
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].id, id) == 0) return true;
    }
    return false;
}

static void normalize_korean(const char *in, char *out, size_t out_size)
{
    if (!in || !out || out_size == 0) { if (out && out_size) out[0] = '\0'; return; }
    size_t len = strlen(in);
    /* Strip ASCII whitespace anywhere. */
    size_t w = 0;
    for (size_t i = 0; i < len && w + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ' || c == '\t') continue;
        out[w++] = (char)c;
    }
    out[w] = '\0';

    /* Drop a trailing 1-syllable Korean particle (등/의/은/는/이/가/을/를/도) if
     * the string ends with one. UTF-8 of these particles is 3 bytes each. */
    static const char *trailing_particles[] = {
        "\xeb\x93\xb1", /* 등 */
        "\xec\x9d\x98", /* 의 */
        "\xec\x9d\x80", /* 은 */
        "\xeb\x8a\x94", /* 는 */
        "\xec\x9d\xb4", /* 이 */
        "\xea\xb0\x80", /* 가 */
        "\xec\x9d\x84", /* 을 */
        "\xeb\xa5\xbc", /* 를 */
        "\xeb\x8f\x84", /* 도 */
    };
    if (w >= 3) {
        for (size_t k = 0; k < sizeof(trailing_particles) / sizeof(trailing_particles[0]); k++) {
            if (memcmp(out + w - 3, trailing_particles[k], 3) == 0) {
                out[w - 3] = '\0';
                return;
            }
        }
    }
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
    if (count > CAP_HA_MAX_REGISTRY_ENTRIES) {
        ESP_LOGW(TAG, "registry entry count %d exceeds cap %d; truncating",
                 count, CAP_HA_MAX_REGISTRY_ENTRIES);
        count = CAP_HA_MAX_REGISTRY_ENTRIES;
    }
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
        if (written >= count) break;
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

static esp_err_t load_cache_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t need = 0;
    err = nvs_get_blob(h, CAP_HA_NVS_KEY_CACHE, NULL, &need);
    if (err != ESP_OK) { nvs_close(h); return err; }
    char *buf = malloc(need + 1);
    if (!buf) { nvs_close(h); return ESP_ERR_NO_MEM; }
    err = nvs_get_blob(h, CAP_HA_NVS_KEY_CACHE, buf, &need);
    nvs_close(h);
    if (err != ESP_OK) { free(buf); return err; }
    buf[need] = '\0';
    err = parse_registry(buf, &s_cache_registry);
    free(buf);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loaded %zu cached entities from NVS", s_cache_registry.count);
    }
    return err;
}

static esp_err_t store_cache_to_nvs(const char *json_blob)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, CAP_HA_NVS_KEY_CACHE, json_blob, strlen(json_blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_resolve_init(void)
{
    if (!s_cache_mutex) {
        s_cache_mutex = xSemaphoreCreateMutex();
        if (!s_cache_mutex) return ESP_ERR_NO_MEM;
    }
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
    if (err == ESP_OK) {
        (void)load_cache_from_nvs();  /* best-effort; absence is fine */
    }
#if CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED
    xTaskCreate(boot_fetch_task, "ha_ctl_boot", 6 * 1024, NULL, 4, NULL);
#endif
    return err;
}

static bool lookup_in(const cap_ha_registry_t *reg, const char *target,
                     bool by_id, bool exact_friendly, bool norm_friendly,
                     cap_ha_entity_t *out)
{
    char target_norm[64] = {0};
    if (norm_friendly) normalize_korean(target, target_norm, sizeof(target_norm));
    for (size_t i = 0; i < reg->count; i++) {
        if (by_id && strcmp(reg->items[i].id, target) == 0) { *out = reg->items[i]; return true; }
        if (exact_friendly && strcmp(reg->items[i].friendly_name, target) == 0) { *out = reg->items[i]; return true; }
        if (norm_friendly) {
            char fn_norm[64];
            normalize_korean(reg->items[i].friendly_name, fn_norm, sizeof(fn_norm));
            if (fn_norm[0] && strcmp(fn_norm, target_norm) == 0) { *out = reg->items[i]; return true; }
        }
    }
    return false;
}

esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    if (!target || !*target || !out) return ESP_ERR_INVALID_ARG;
    /* Stage 1: exact entity_id (static first, then cache). */
    if (lookup_in(&s_static_registry, target, true, false, false, out)) return ESP_OK;
    {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        bool found = lookup_in(&s_cache_registry, target, true, false, false, out);
        xSemaphoreGive(s_cache_mutex);
        if (found) return ESP_OK;
    }
    /* Stage 2: exact friendly_name. */
    if (lookup_in(&s_static_registry, target, false, true, false, out)) return ESP_OK;
    {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        bool found = lookup_in(&s_cache_registry, target, false, true, false, out);
        xSemaphoreGive(s_cache_mutex);
        if (found) return ESP_OK;
    }
    /* Stage 3: normalized friendly_name. */
    if (lookup_in(&s_static_registry, target, false, false, true, out)) return ESP_OK;
    {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        bool found = lookup_in(&s_cache_registry, target, false, false, true, out);
        xSemaphoreGive(s_cache_mutex);
        if (found) return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    out_csv[0] = '\0';
    size_t emitted = 0;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cap_ha_registry_t *regs[2] = { &s_static_registry, &s_cache_registry };
    for (int r = 0; r < 2 && emitted < max; r++) {
        for (size_t i = 0; i < regs[r]->count && emitted < max; i++) {
            if (r == 1 && registry_has_id(&s_static_registry, regs[r]->items[i].id)) continue;
            const char *sep = (emitted == 0) ? "" : ", ";
            size_t cur = strlen(out_csv);
            const char *fn = regs[r]->items[i].friendly_name;
            if (cur + strlen(sep) + strlen(fn) + 1 >= out_size) goto done;
            snprintf(out_csv + cur, out_size - cur, "%s%s", sep, fn);
            emitted++;
        }
    }
done:
    xSemaphoreGive(s_cache_mutex);
    if (emitted == 0) snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}

esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv, size_t out_size)
{
    return cap_ha_resolve_top_candidates(out_csv, out_size,
                                         s_static_registry.count + s_cache_registry.count);
}

esp_err_t cap_ha_resolve_refresh_from_ha(void)
{
    /* /api/states는 home의 모든 entity를 한 번에 돌려줘서 service-call 응답
     * (16KB)보다 훨씬 크다. 별도 64KB 버퍼로 best-effort 채취 — truncation은
     * cap_ha_http_get_states 내부에서 WARN 로그만 남기고 partial JSON을 돌려준다. */
    char *raw = malloc(CAP_HA_STATES_BUF_BYTES);
    if (!raw) return ESP_ERR_NO_MEM;
    esp_err_t err = cap_ha_http_get_states(raw, CAP_HA_STATES_BUF_BYTES);
    if (err != ESP_OK) { free(raw); return err; }

    cJSON *arr = cJSON_Parse(raw);
    free(raw);
    if (!cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *out_root = cJSON_CreateObject();
    cJSON *out_entities = cJSON_AddArrayToObject(out_root, "entities");
    cJSON *st = NULL;
    int kept = 0;
    cJSON_ArrayForEach(st, arr) {
        const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(st, "entity_id");
        const cJSON *attr = cJSON_GetObjectItemCaseSensitive(st, "attributes");
        if (!cJSON_IsString(id_j)) continue;
        const char *id = id_j->valuestring;
        const char *dot = strchr(id, '.');
        if (!dot) continue;
        char domain[16] = {0};
        size_t dlen = (size_t)(dot - id);
        if (dlen >= sizeof(domain)) continue;
        memcpy(domain, id, dlen);
        if (strcmp(domain, "light") != 0 && strcmp(domain, "cover") != 0 &&
            strcmp(domain, "switch") != 0) continue;

        const cJSON *fn_j = cJSON_GetObjectItemCaseSensitive(attr, "friendly_name");
        const char *friendly = (cJSON_IsString(fn_j) && fn_j->valuestring[0])
                               ? fn_j->valuestring : id;

        const cJSON *sf_j = cJSON_GetObjectItemCaseSensitive(attr, "supported_features");
        bool has_brightness = (cJSON_IsNumber(sf_j) && (sf_j->valueint & 1)); /* heuristic */
        const cJSON *cm_j = cJSON_GetObjectItemCaseSensitive(attr, "supported_color_modes");
        bool has_color = cJSON_IsArray(cm_j) && cJSON_GetArraySize(cm_j) > 0;

        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", id);
        cJSON_AddStringToObject(e, "friendly_name", friendly);
        cJSON_AddStringToObject(e, "domain", domain);
        cJSON *sup = cJSON_AddArrayToObject(e, "supports");
        if (has_brightness) cJSON_AddItemToArray(sup, cJSON_CreateString("brightness"));
        if (has_color)      cJSON_AddItemToArray(sup, cJSON_CreateString("color"));
        cJSON_AddItemToArray(out_entities, e);
        kept++;
    }
    cJSON_Delete(arr);

    char *blob = cJSON_PrintUnformatted(out_root);
    cJSON_Delete(out_root);
    if (!blob) return ESP_ERR_NO_MEM;
    esp_err_t store_err = store_cache_to_nvs(blob);
    if (store_err == ESP_OK) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        if (s_cache_registry.items) free(s_cache_registry.items);
        s_cache_registry = (cap_ha_registry_t){0};
        parse_registry(blob, &s_cache_registry);
        xSemaphoreGive(s_cache_mutex);
    }
    ESP_LOGI(TAG, "boot-fetch: kept %d entities, NVS store=%s",
             kept, esp_err_to_name(store_err));
    free(blob);
    return store_err;
}

#if CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED
static void boot_fetch_task(void *arg)
{
    (void)arg;
    /* Cheap wait for network: poll url+token and try; retry up to N times. */
    char url_chk[160];
    for (int i = 0; i < 30; i++) {
        if (cap_ha_http_get_url(url_chk, sizeof(url_chk)) == ESP_OK && url_chk[0]) break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    /* Give Wi-Fi/TCP time to settle. */
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_err_t err = cap_ha_resolve_refresh_from_ha();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "boot-fetch failed: %s (will use static-only registry)",
                 esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}
#endif
