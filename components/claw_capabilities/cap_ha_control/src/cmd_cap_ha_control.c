/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cmd_cap_ha_control.h"
#include "cap_ha_control_internal.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cap_ha_cli";

static int cmd_ha_control(int argc, char **argv)
{
    (void)argc; (void)argv;
    ESP_LOGW(TAG, "stub ha_control console command");
    return 0;
}

esp_err_t cmd_cap_ha_control_register(void)
{
    static const esp_console_cmd_t cmd = {
        .command = "ha_control",
        .help = "ha_control --call '<json>' | --resolve <target> | --refresh-registry | --set-url <url> | --set-token <token>",
        .hint = NULL,
        .func = &cmd_ha_control,
    };
    return esp_console_cmd_register(&cmd);
}
