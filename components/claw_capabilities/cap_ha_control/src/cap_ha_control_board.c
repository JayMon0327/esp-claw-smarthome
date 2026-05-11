/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include "led_strip.h"
#include <string.h>

static const char *TAG = "cap_ha_board";

#define BOARD_RGB_GPIO    48
#define BOARD_RGB_COUNT   1

static void scale_rgb_by_pct(int rgb[3], int brightness_pct)
{
    if (brightness_pct <= 0) return;          /* unchanged: full color */
    if (brightness_pct > 100) brightness_pct = 100;
    for (int i = 0; i < 3; i++) {
        int scaled = (rgb[i] * brightness_pct) / 100;
        if (scaled < 0) scaled = 0;
        if (scaled > 255) scaled = 255;
        rgb[i] = scaled;
    }
}

esp_err_t cap_ha_board_dispatch(const char *target, const char *action,
                                int brightness_pct, const char *color,
                                char *message_out, size_t message_size)
{
    (void)TAG;
    if (!message_out || message_size == 0) return ESP_ERR_INVALID_ARG;
    if (strcmp(target, "board:onboard_rgb") != 0) {
        snprintf(message_out, message_size,
                 "지원하지 않는 board target입니다 (%s).", target);
        return ESP_ERR_NOT_SUPPORTED;
    }

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = BOARD_RGB_GPIO,
        .max_leds = BOARD_RGB_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    led_strip_handle_t handle = NULL;
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &handle);
    if (err != ESP_OK) {
        snprintf(message_out, message_size,
                 "보드 RGB 초기화에 실패했습니다 (RMT 자원 부족).");
        return err;
    }

    if (strcmp(action, "turn_off") == 0 || strcmp(action, "close") == 0) {
        err = led_strip_clear(handle);
        led_strip_refresh(handle);
        led_strip_del(handle);
        if (err == ESP_OK) {
            snprintf(message_out, message_size, "보드 RGB를 껐습니다.");
        } else {
            snprintf(message_out, message_size,
                     "보드 RGB 끄기 실패 (%s).", esp_err_to_name(err));
        }
        return err;
    }

    if (strcmp(action, "turn_on") != 0 && strcmp(action, "toggle") != 0 &&
        strcmp(action, "open") != 0) {
        led_strip_del(handle);
        snprintf(message_out, message_size,
                 "보드 RGB는 해당 동작을 지원하지 않습니다 (action=%s).", action);
        return ESP_ERR_INVALID_ARG;
    }

    /* Color → RGB. Default = white. brightness_pct scales RGB directly
     * (matches lua_module_led_strip's pattern: no HSV round-trip). */
    int rgb[3];
    if (color && *color) {
        if (cap_ha_color_to_rgb(color, rgb) != ESP_OK) {
            led_strip_del(handle);
            snprintf(message_out, message_size,
                     "지원하지 않는 색상입니다 (color=%s).", color);
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        rgb[0] = 255; rgb[1] = 255; rgb[2] = 255;
    }
    scale_rgb_by_pct(rgb, brightness_pct);
    /* Ensure the LED is visibly on if any rounding produced all-zero. */
    if (rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) rgb[0] = rgb[1] = rgb[2] = 1;

    err = led_strip_set_pixel(handle, 0,
                              (uint32_t)rgb[0], (uint32_t)rgb[1], (uint32_t)rgb[2]);
    if (err == ESP_OK) err = led_strip_refresh(handle);
    led_strip_del(handle);
    if (err != ESP_OK) {
        snprintf(message_out, message_size,
                 "보드 RGB 설정 실패 (%s).", esp_err_to_name(err));
        return err;
    }

    if (color && *color && brightness_pct > 0) {
        snprintf(message_out, message_size,
                 "보드 RGB를 %s %d%%로 켰습니다.", color, brightness_pct);
    } else if (color && *color) {
        snprintf(message_out, message_size,
                 "보드 RGB를 %s 켰습니다.", color);
    } else if (brightness_pct > 0) {
        snprintf(message_out, message_size,
                 "보드 RGB를 %d%% 밝기로 켰습니다.", brightness_pct);
    } else {
        snprintf(message_out, message_size, "보드 RGB를 켰습니다.");
    }
    return ESP_OK;
}
