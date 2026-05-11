/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cmd_cap_ha_control.h"
#include "cap_ha_control_internal.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cap_ha_cli";

static struct {
    struct arg_str *call;
    struct arg_str *resolve;
    struct arg_lit *refresh;
    struct arg_str *set_url;
    struct arg_str *set_token;
    struct arg_str *set_insecure;
    struct arg_str *automation;
    struct arg_end *end;
} ha_args;

static int cmd_ha_control(int argc, char **argv)
{
    (void)TAG;
    int nerrors = arg_parse(argc, argv, (void **)&ha_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ha_args.end, argv[0]);
        return 1;
    }

    if (ha_args.set_url->count > 0) {
        esp_err_t err = cap_ha_http_set_url(ha_args.set_url->sval[0]);
        printf("set_url: %s\n", esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    if (ha_args.set_token->count > 0) {
        esp_err_t err = cap_ha_http_set_token(ha_args.set_token->sval[0]);
        printf("set_token: %s\n", esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    if (ha_args.set_insecure->count > 0) {
        const char *v = ha_args.set_insecure->sval[0];
        bool ins = (strcmp(v, "on") == 0 || strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
        esp_err_t err = cap_ha_http_set_insecure(ins);
        printf("set_insecure: %s (value=%s)\n", esp_err_to_name(err), ins ? "on" : "off");
        return (err == ESP_OK) ? 0 : 1;
    }
    if (ha_args.refresh->count > 0) {
        esp_err_t err = cap_ha_resolve_refresh_from_ha();
        printf("refresh_from_ha: %s\n", esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    if (ha_args.resolve->count > 0) {
        cap_ha_entity_t e = {0};
        esp_err_t err = cap_ha_resolve_target(ha_args.resolve->sval[0], &e);
        if (err == ESP_OK) {
            printf("resolve: id=%s friendly=%s domain=%s brightness=%d color=%d\n",
                   e.id, e.friendly_name, e.domain, e.supports_brightness, e.supports_color);
        } else {
            char cands[192];
            cap_ha_resolve_top_candidates(cands, sizeof(cands), 5);
            printf("resolve: NOT_FOUND. candidates: %s\n", cands);
        }
        return (err == ESP_OK) ? 0 : 1;
    }
    if (ha_args.call->count > 0) {
        char output[768];
        cap_ha_core_execute(ha_args.call->sval[0], output, sizeof(output));
        printf("%s\n", output);
        return 0;
    }
    if (ha_args.automation->count > 0) {
        char output[1024];
        esp_err_t err = cap_ha_automation_execute(ha_args.automation->sval[0],
                                                  output, sizeof(output));
        printf("%s\n", output);
        return (err == ESP_OK) ? 0 : 1;
    }

    printf("ha_control: at least one of --call/--resolve/--refresh-registry/--set-url/--set-token/--set-insecure/--automation required\n");
    return 1;
}

esp_err_t cmd_cap_ha_control_register(void)
{
    ha_args.call      = arg_str0(NULL, "call", "<json>", "ha_control payload as JSON");
    ha_args.resolve   = arg_str0(NULL, "resolve", "<target>", "lookup target in registry");
    ha_args.refresh   = arg_lit0(NULL, "refresh-registry", "fetch /api/states and update NVS cache");
    ha_args.set_url   = arg_str0(NULL, "set-url", "<url>", "store HA URL in NVS");
    ha_args.set_token = arg_str0(NULL, "set-token", "<token>", "store HA bearer token in NVS");
    ha_args.set_insecure = arg_str0(NULL, "set-insecure", "<on|off>",
                                    "Skip TLS cert verify for https:// HA URLs (demo only)");
    ha_args.automation = arg_str0(NULL, "automation", "<json>",
                                  "Run cap_ha_automation_execute directly with a typed JSON payload");
    ha_args.end       = arg_end(4);

    static const esp_console_cmd_t cmd = {
        .command = "ha_control",
        .help = "ha_control --call '<json>' | --resolve <target> | --refresh-registry | --set-url <url> | --set-token <token> | --set-insecure <on|off> | --automation '<json>'",
        .hint = NULL,
        .func = &cmd_ha_control,
        .argtable = &ha_args,
    };
    return esp_console_cmd_register(&cmd);
}
