/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cap_ha_board";

esp_err_t cap_ha_board_dispatch(const char *target, const char *action,
                                int brightness_pct, const char *color,
                                char *message_out, size_t message_size)
{
    (void)target; (void)action; (void)brightness_pct; (void)color;
    (void)TAG;
    snprintf(message_out, message_size, "보드 RGB 분기 미구현 (stub).");
    return ESP_ERR_NOT_SUPPORTED;
}
