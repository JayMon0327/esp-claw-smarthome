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
#include <stdio.h>

static const char *TAG = "cap_ha_control";

static char s_ha_description[1024];
static char s_ha_friendly_names[256];
static char s_ha_automation_description[1536];

static esp_err_t cap_ha_execute(const char *input_json,
                                const claw_cap_call_context_t *ctx,
                                char *output,
                                size_t output_size)
{
    (void)ctx;
    return cap_ha_core_execute(input_json, output, output_size);
}

static esp_err_t cap_ha_automation_execute_wrapper(const char *input_json,
                                                   const claw_cap_call_context_t *ctx,
                                                   char *output,
                                                   size_t output_size)
{
    (void)ctx;
    return cap_ha_automation_execute(input_json, output, output_size);
}

static claw_cap_descriptor_t s_ha_descriptors[] = {
    {
        .id = "ha_control",
        .name = "ha_control",
        .family = "ha",
        .description = NULL, /* set in group_init via compose_description() */
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
    {
        .id = "ha_automation",
        .name = "ha_automation",
        .family = "ha",
        .description = NULL, /* set in cap_ha_compose_description */
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
              "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"update\",\"remove\",\"list\",\"trigger_now\",\"enable\",\"disable\"]},"
              "\"automation_id\":{\"type\":\"string\",\"description\":\"HA entity local id (without 'automation.' prefix). create assigns automatically (esp_claw_<ts>).\"},"
              "\"alias\":{\"type\":\"string\",\"description\":\"Human-readable name visible in HA UI.\"},"
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"state\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id of the entity whose state change fires the automation (e.g., '현관 도어센서' or 'binary_sensor.front_door').\"},"
                "\"to\":{\"type\":\"string\",\"description\":\"state: required target state ('on'/'off'/'open'/'closed' etc).\"},"
                "\"from\":{\"type\":\"string\",\"description\":\"state: optional previous state. If omitted, firmware auto-fills the domain-pair opposite (binary_sensor/light/switch on<->off, cover open<->closed, lock locked<->unlocked) to force a HA transition. Specify explicitly to override.\"}"
              "}},"
              "\"condition\":{\"type\":\"object\",\"description\":\"Optional gate that must be true at trigger time. Single object — for AND, compose at the calling layer (v6 will add OR/NOT).\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"time_range\",\"weekday\",\"state\"]},"
                "\"after\":{\"type\":\"string\",\"description\":\"time_range: 'HH:MM' lower bound (inclusive). Omit for 'before only'.\"},"
                "\"before\":{\"type\":\"string\",\"description\":\"time_range: 'HH:MM' upper bound (inclusive). Omit for 'after only'.\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekday: 0=Sunday\"},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id whose current state is gated.\"},"
                "\"state\":{\"type\":\"string\",\"description\":\"state: required current state value (e.g., 'off' to fire only when the entity is off).\"}"
              "}},"
              "\"target\":{\"type\":\"string\",\"description\":\"HA entity friendly name or entity_id. board:* targets are not supported in v4 (HA-side automation only).\"},"
              "\"device_action\":{\"type\":\"string\",\"enum\":[\"turn_on\",\"turn_off\",\"toggle\",\"open\",\"close\"]},"
              "\"brightness_pct\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},"
              "\"color\":{\"type\":\"string\"},"
              "\"kelvin\":{\"type\":\"integer\",\"minimum\":2000,\"maximum\":6500}"
            "},"
            "\"required\":[\"action\"]}",
        .execute = cap_ha_automation_execute_wrapper,
    },
};

void cap_ha_compose_description(void)
{
    cap_ha_resolve_active_friendly_names(s_ha_friendly_names, sizeof(s_ha_friendly_names));
    snprintf(s_ha_description, sizeof(s_ha_description),
             "Control smart-home devices via Home Assistant or onboard hardware. "
             "Single entry point for lights, curtains, switches, and the onboard RGB LED. "
             "Active devices (use these names verbatim in 'target'): %s. "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM. "
             "Do not invent confirmation messages.",
             s_ha_friendly_names);
    s_ha_descriptors[0].description = s_ha_description;

    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target' or in trigger.entity / condition.entity): %s. "
             "board:* targets (onboard RGB) are NOT supported here — those require on-device automation (deferred). "
             "Trigger kinds: 'daily_time' (HH:MM), 'weekly' (HH:MM + weekdays[]), 'interval' (interval_ms ≥ 2000), "
             "'state' (entity + to; firmware auto-fills 'from' as the domain-pair opposite to force HA transition semantics — pass 'from' explicitly to override). "
             "Optional 'condition' object gates the trigger: 'time_range' (after/before HH:MM), 'weekday' (weekdays[]), 'state' (entity + state). "
             "Example: door sensor opens between 10:00–18:00 → light on. "
             "Use 'create' (assigns automation_id), 'update' (needs automation_id), 'remove' (needs automation_id), "
             "'list' (returns existing automations), 'trigger_now' (force-fire by id), 'enable'/'disable' (toggle by id). "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
    s_ha_descriptors[1].description = s_ha_automation_description;
}

/* claw_cap_lifecycle_fn = esp_err_t (*)(void) — see claw_cap.h. No params. */
static esp_err_t cap_ha_group_init(void)
{
    esp_err_t err = cap_ha_resolve_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resolve_init returned %s — using static-only registry",
                 esp_err_to_name(err));
    }
    cap_ha_compose_description();
    err = cmd_cap_ha_control_register();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "cmd register failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

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
