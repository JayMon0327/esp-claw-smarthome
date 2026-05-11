/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control.h"
#include "cap_ha_control_internal.h"
#include "cmd_cap_ha_control.h"

#include "claw_cap.h"
#include "esp_log.h"

static const char *TAG = "cap_ha_control";

static esp_err_t cap_ha_execute(const char *input_json,
                                const claw_cap_call_context_t *ctx,
                                char *output,
                                size_t output_size)
{
    (void)ctx;
    return cap_ha_core_execute(input_json, output, output_size);
}

/* claw_cap_lifecycle_fn = esp_err_t (*)(void) — see claw_cap.h. No params. */
static esp_err_t cap_ha_group_init(void)
{
    esp_err_t err = cap_ha_resolve_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resolve_init returned %s — falling back to static-only registry",
                 esp_err_to_name(err));
    }
    err = cmd_cap_ha_control_register();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "cmd register failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static const claw_cap_descriptor_t s_ha_descriptors[] = {
    {
        .id = "ha_control",
        .name = "ha_control",
        .family = "ha",
        .description =
            "Control smart-home devices through Home Assistant or onboard hardware. "
            "Single entry point for lights, curtains, switches, and the onboard RGB LED. "
            "After it returns, respond to the user with the result 'message' field VERBATIM. "
            "Do not invent confirmation messages.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
              "\"target\":{\"type\":\"string\",\"description\":\"Korean friendly name from the active registry, or 'board:<slug>'.\"},"
              "\"action\":{\"type\":\"string\",\"enum\":[\"turn_on\",\"turn_off\",\"toggle\",\"open\",\"close\"]},"
              "\"brightness_pct\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},"
              "\"color\":{\"type\":\"string\",\"description\":\"Color name (yellow/red/purple/...) or '#rrggbb'.\"},"
              "\"kelvin\":{\"type\":\"integer\",\"minimum\":2000,\"maximum\":6500}"
            "},"
            "\"required\":[\"target\",\"action\"]}",
        .execute = cap_ha_execute,
    },
};

static const claw_cap_group_t s_ha_group = {
    .group_id = "cap_ha_control",
    .descriptors = s_ha_descriptors,
    .descriptor_count = sizeof(s_ha_descriptors) / sizeof(s_ha_descriptors[0]),
    .group_init = cap_ha_group_init,
};

esp_err_t cap_ha_control_register_group(void)
{
    if (claw_cap_group_exists(s_ha_group.group_id)) {
        return ESP_OK;
    }
    return claw_cap_register_group(&s_ha_group);
}
