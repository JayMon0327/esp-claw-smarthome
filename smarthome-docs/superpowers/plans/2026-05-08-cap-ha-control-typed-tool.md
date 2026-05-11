# cap_ha_control Typed Tool — Implementation Plan (v3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP-Claw가 Telegram 자연어를 받아 HA 기기(light/cover/switch) + onboard RGB를 단일 typed tool `ha_control`로 제어하고, 거짓 성공 / multi-round arg drift / 토큰 노출 / raw MCP 우회를 architecture 차원에서 차단한다.

**Architecture:** 신규 component `cap_ha_control`가 LLM-visible tool 1개를 노출. LLM은 typed payload(target/action/brightness_pct/color/kelvin)만 채우고, schema validate → entity resolve → HA REST 또는 board driver 분기 → 한국어 message 작성을 모두 firmware가 담당. LLM은 결과 message를 verbatim echo. cap_mcp_client / cap_lua_* 는 enabled로 두되 LLM-visible cap_groups에서 제외.

**Tech Stack:** ESP-IDF v5.5.4 / esp_http_client (HA REST POST + GET) / cJSON / NVS (token, ha_url, entity_cache) / claw_cap descriptor / espressif/led_strip ^3.0.3 managed component (board branch, direct C API; in-repo lua_module_led_strip과 동일 버전 핀). 기존 setup_wizard 웹 UI는 v3 흐름에서 변경되지 않음 — ha_url/ha_token은 v3에서 console (`ha_control --set-url/--set-token`)로만 입력.

**Spec:** `smarthome-docs/superpowers/specs/2026-05-08-cap-ha-control-typed-tool-design.md` (commit `75944e7`).

**Review revision pass 2 (2026-05-08):** 첫 8개 + 다음 5개 항목 반영됨.

Pass 2 (5 items):
- Task 2 step 14: `s_capability_group_infos[]`에도 cap_ha_control entry 추가 (UI/listing 일관성).
- Task 6: `cap_ha_http_post_service`가 non-2xx에서 ESP_ERR_HTTP_CONNECT를 돌려주면 401 인증 실패 메시지가 가려짐 → 전송 성공 시 항상 ESP_OK 반환, status 판정은 caller가 함.
- Task 1 step 2/3: 함수 시그니처가 `cap_mcp_call_remote_tool(const char *, cJSON **)`이므로 string rewrite가 아니라 cJSON 객체 조작으로 재작성. 빈 arguments + content text 안의 Error/FAIL 둘 다 root.isError로 박음. VLA 회피.
- Task 9 step 5: `boot_fetch_task` forward declaration을 함수 본문 안이 아닌 파일 상단으로 이동 (nested extern 회피).
- Task 9 buffer: `/api/states`용으로 별도 64KB `CAP_HA_STATES_BUF_BYTES` 도입 — service-call 16KB와 분리. 그래도 truncate 가능성은 best-effort로 명시.

Pass 1 (8 items):
1. Task 2: app_claw build wiring (`CMakeLists.txt`, `idf_component.yml`)을 같은 commit에 포함하도록 step 추가 (이게 빠지면 `#include "cap_ha_control.h"` 빌드 실패).
2. Task 2/11: `cap_ha_group_init`을 `claw_cap_lifecycle_fn` (= `esp_err_t (*)(void)`)에 맞춰 인자 없는 시그니처로 수정.
3. Task 8: `led_strip_set_pixel(r,g,b)` + RGB에 brightness 스케일 (HSV 우회). `idf_component.yml` deps를 `espressif/led_strip: ^3.0.3` (in-repo lua_module과 동일 핀)으로.
4. Task 12: ha_url/ha_token 입력은 console-only로 결정. wizard NVS namespace(`app_config`)와 cap_ha_http NVS namespace(`ha_ctl`) split을 v3에서 미해결 — 통합은 v4.
5. Task 6: "256자 제한 제거" 표현을 "URL 160B + token 4096B caller-provided"로 정정. 진짜 제한 제거는 v4의 `_alloc` helper.
6. Task 1 step 3: 응답 본문에 Error/FAIL 신호 시 root JSON에 `isError:true` 명시 주입 (로깅만으론 LLM 차단 보장 안 됨).
7. Task 12: `vis_cap_groups` 기본값을 sdkconfig가 아닌 `application/edge_agent/components/app_config/app_config.c:43` `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS`에 박음.
8. Task 13: `skills_list.json`을 전체 교체가 아니라 4개 entry만 surgical 제거 (Python in-place patch).

---

## File Plan

신규 (모두 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/` 기준):

| 파일 | 작업 | 책임 |
|---|---|---|
| `components/claw_capabilities/cap_ha_control/CMakeLists.txt` | create | component register + EMBED_TXTFILES (entities.default.json) |
| `components/claw_capabilities/cap_ha_control/idf_component.yml` | create | deps 명세 (없음, std components) |
| `components/claw_capabilities/cap_ha_control/Kconfig` | create | `CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED` |
| `components/claw_capabilities/cap_ha_control/data/entities.default.json` | create | 정적 registry — 사용자 수동 5개 |
| `components/claw_capabilities/cap_ha_control/include/cap_ha_control.h` | create | `cap_ha_control_register_group()` |
| `components/claw_capabilities/cap_ha_control/include/cmd_cap_ha_control.h` | create | `cmd_cap_ha_control_register()` |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` | create | descriptor + group_init + 정적 registry 로드 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` | create | 내부 타입 / 함수 선언 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c` | create | execute() 진입점, schema validate, dispatch, 메시지 합성 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` | create | target resolve cascade, registry I/O, NVS cache, boot-fetch task |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` | create | HA REST POST/GET, Bearer, response parse |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_board.c` | create | `board:onboard_rgb` led_strip 분기 |
| `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c` | create | console 명령 (--call/--resolve/--refresh-registry/--set-url/--set-token) |

수정:

| 파일 | 작업 |
|---|---|
| `components/common/app_claw/Kconfig` | `APP_CLAW_CAP_HA_CONTROL` 토글 추가 |
| `components/common/app_claw/CMakeLists.txt` | 조건부 `cap_ha_control` REQUIRES 추가 |
| `components/common/app_claw/idf_component.yml` | 조건부 `cap_ha_control` dependency + path 추가 |
| `components/common/app_claw/app_capabilities.c` | `app_cap_register_ha_control()` 추가 + 등록 dispatch |
| `components/common/app_claw/app_claw.c:44` (`APP_SYSTEM_PROMPT_COMMON`) | verbatim echo + lua_*/mcp_call_tool 금지 라인 추가 |
| `components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c` | (단기 patch) `arguments` 빈/누락 reject + Error/FAIL 본문 detect |
| `application/edge_agent/sdkconfig.defaults` | `CONFIG_APP_CLAW_CAP_HA_CONTROL=y`, `CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y` |
| `application/edge_agent/components/app_config/app_config.c` | `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS` 기본값 갱신 |

삭제 (v2 cleanup):

| 파일 | 작업 |
|---|---|
| `application/edge_agent/main/lua_scripts/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.lua` | delete |
| `application/edge_agent/main/skills/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.md` | delete |
| `application/edge_agent/main/lua_scripts/builtin/demo_secrets.lua` (또는 fatfs 경로) | delete |
| `application/edge_agent/main/skills/skills_list.json` | 4개 entry 제거 |
| `.gitignore` | demo_secrets.lua 라인 제거 |

learn log:

| 파일 | 작업 |
|---|---|
| `docs/learn/20260508-cap-ha-control-v3.md` | create (Task 18) |

---

## Pre-flight

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
git status -s
```
Expected: clean (또는 v2 후속 작업 외 무관 항목 없음). 각 task는 독립 commit. `~/.gstack/projects/esp-claw/secrets.env`의 `HA_PI_IP`, `HA_LONG_LIVED_TOKEN`, `ESP_PORT`가 채워져 있어야 함. 토큰은 v2 시연 노출 후 폐기·재발급된 새 값.

---

## Task 1: 단기 안전 패치 — cap_mcp_client 거짓 성공 차단 + system prompt 강화

**Files:**
- Modify: `components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c`
- Modify: `components/common/app_claw/app_claw.c:44` (`APP_SYSTEM_PROMPT_COMMON`)

문맥: spec § 12 단기 patch 통합. cap_ha_control이 ship되기 전에도 시연 안전성 보강. `arguments` 빈 객체로 흘러드는 path를 막고, LLM이 raw `mcp_call_tool`/lua 도구로 우회하는 자유도를 system prompt에서 제거.

- [ ] **Step 1: 실제 함수 시그니처 / arguments 흐름 식별**

```bash
grep -n "cap_mcp_call_remote_tool\|cap_mcp_parse_common_input\|cJSON \*\*result_out" components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c | head -10
```

확인할 사실 (현재 코드):
- `cap_mcp_call_remote_tool(const char *input_json, cJSON **result_out)` — caller-provided 문자열 buffer가 아니라 cJSON tree를 out-param으로 돌려준다.
- `cap_mcp_parse_common_input(..., cJSON **arguments_out)` — input JSON을 파싱해 arguments cJSON object를 채워준다.
- 응답 root는 line ~402의 `root = cJSON_CreateObject()` 이후 line ~430 부근에서 `content` / `isError` 필드를 채우는 흐름.

따라서 패치는 **string rewrite가 아니라 cJSON 객체 조작**으로 한다.

- [ ] **Step 2: 빈 arguments / 누락 시 cJSON-기반 reject**

`cap_mcp_call_remote_tool` 안에서 `cap_mcp_parse_common_input(...)`이 ESP_OK로 돌아온 직후 (현재 line ~369 근처, `cap_mcp_build_full_url` 호출 전), 다음을 추가:

```c
/* Reject calls with missing/empty arguments object. v2 demos showed
 * gpt-5-mini drifting to '{}' on the second round, after which HA
 * silently failed while the LLM narrated success. */
if (!arguments || !cJSON_IsObject(arguments) || cJSON_GetArraySize(arguments) == 0) {
    cJSON *err_root = cJSON_CreateObject();
    cJSON *content_arr = cJSON_AddArrayToObject(err_root, "content");
    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text",
        "mcp_call_tool requires a non-empty 'arguments' object. "
        "For smart-home control use ha_control instead.");
    cJSON_AddItemToArray(content_arr, text_obj);
    cJSON_AddBoolToObject(err_root, "isError", true);
    cJSON_Delete(arguments);
    *result_out = err_root;
    ESP_LOGW(TAG, "rejecting mcp_call_tool: arguments missing/empty");
    return ESP_OK;  /* result_out carries isError:true — LLM-blocked at schema level */
}
```

(`return ESP_OK` 인 이유: out-param으로 isError 결과를 넘겨야 LLM에 도달한다. ESP_ERR_INVALID_ARG로 끝내면 caller가 result_out=NULL을 보고 generic error path로 빠진다.)

- [ ] **Step 3: 응답이 실패 신호를 담고 있으면 root cJSON에 isError:true 명시**

현재 코드는 line ~408–417에서 RPC-level error를 root.error_message로만 박고 isError flag는 빼먹는다. line ~427–432에서 result.content / result.isError를 root에 복사하지만, MCP 서버가 isError를 빼먹고 content text에 "Error:" / "FAIL"만 박는 경우 root.isError가 없는 채 반환된다. 둘 다 LLM이 거짓 성공을 합성할 source가 된다.

패치는 두 분기 모두에 isError 명시 보강:

(1) RPC-level error 분기 (line ~408 근처, `if (cJSON_IsObject(error_obj)) { ... cJSON_AddStringToObject(root, "error_message", ...); ... }` 블록 안에서 `*result_out = root` 직전):

```c
    cJSON_AddBoolToObject(root, "isError", true);
```

(2) tools/result 분기 (line ~430의 `if (cJSON_IsBool(is_error)) { cJSON_AddBoolToObject(root, "isError", cJSON_IsTrue(is_error)); }` 블록을 다음으로 교체):

```c
bool flagged_error = cJSON_IsBool(is_error) && cJSON_IsTrue(is_error);
if (!flagged_error && cJSON_IsArray(content)) {
    /* Content array의 type=text 항목들을 훑어 실패 marker가 있으면 강제 flag. */
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, content) {
        const cJSON *type_j = cJSON_GetObjectItem(item, "type");
        const cJSON *text_j = cJSON_GetObjectItem(item, "text");
        if (cJSON_IsString(type_j) && cJSON_IsString(text_j) &&
            strcmp(type_j->valuestring, "text") == 0) {
            const char *t = text_j->valuestring;
            if (strstr(t, "Error:") || strstr(t, "\"isError\":true") ||
                strstr(t, "FAIL") || strstr(t, "fail:") || strstr(t, "ERROR")) {
                flagged_error = true;
                ESP_LOGW(TAG, "MCP content signalled failure; forcing isError=true");
                break;
            }
        }
    }
}
cJSON_AddBoolToObject(root, "isError", flagged_error);
```

순수 cJSON 조작이므로 VLA / output_size escaping 문제 없음. 실패 source가 root.isError에 박혀 LLM이 schema 레벨에서 차단된다.

- [ ] **Step 4: APP_SYSTEM_PROMPT_COMMON에 두 줄 추가**

`components/common/app_claw/app_claw.c:44`의 `APP_SYSTEM_PROMPT_COMMON` 매크로를 다음으로 치환:

```c
#define APP_SYSTEM_PROMPT_COMMON \
    "You are the ESP-Claw. " \
    "Answer briefly and plainly. " \
    "Treat Skills List as a catalog of optional skills." \
    "Use 'activate_skill' to load a skill, and you will gain more callable capabilities\n" \
    "Skills are user-facing functions, while Capabilities are internal functions used by the model.\n" \
    "After completing the task, call 'deactivate_skill' to keep the context streamlined and efficient." \
    "When communicating with the user, refer to skills instead of Capabilities. " \
    "When a tool returns a 'message' field, respond to the user with that message verbatim — do not rephrase, do not add commentary, do not invent confirmation. " \
    "Do not claim a tool succeeded unless its 'success' field is true. " \
    "For smart-home control (lights, curtains, switches, the onboard RGB LED) use the 'ha_control' tool only. Never call 'mcp_call_tool' or any 'lua_*' tool to control devices."
```

- [ ] **Step 5: 빌드 (다음 task에서 일괄 flash)**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. `cap_mcp_client_core.c` 변경 + `app_claw.c` 매크로 변경이 컴파일 통과.

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c \
        components/common/app_claw/app_claw.c
git commit -m "$(cat <<'EOF'
fix(cap_mcp_client): reject empty arguments + strengthen system prompt

Two short-term patches preceding the cap_ha_control v3 architecture:

- cap_mcp_client.mcp_call_tool now rejects calls with missing or empty
  'arguments' objects. v2 demos surfaced LLM rounds where arguments
  drifted to {} and HA failed silently while the LLM narrated success.
- APP_SYSTEM_PROMPT_COMMON gains two enforcement lines: respond using
  the tool 'message' field verbatim and never claim success unless
  'success' is true; for device control use ha_control only — never
  mcp_call_tool or lua_*.

These run before cap_ha_control ships and remain useful safety nets
afterwards (the prompt rules also bind ha_control's verbatim contract).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: cap_ha_control 컴포넌트 스켈레톤 (빌드만 통과, 동작 없음)

**Files:**
- Create: `components/claw_capabilities/cap_ha_control/CMakeLists.txt`
- Create: `components/claw_capabilities/cap_ha_control/idf_component.yml`
- Create: `components/claw_capabilities/cap_ha_control/Kconfig`
- Create: `components/claw_capabilities/cap_ha_control/include/cap_ha_control.h`
- Create: `components/claw_capabilities/cap_ha_control/include/cmd_cap_ha_control.h`
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c` (stub)
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` (stub)
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` (stub)
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_board.c` (stub)
- Create: `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c` (stub)
- Create: `components/claw_capabilities/cap_ha_control/data/entities.default.json` (placeholder 1개 entry)
- Modify: `components/common/app_claw/Kconfig`
- Modify: `components/common/app_claw/CMakeLists.txt`
- Modify: `components/common/app_claw/idf_component.yml`
- Modify: `components/common/app_claw/app_capabilities.c`

문맥: cap_mcp_client (`components/claw_capabilities/cap_mcp_client/`)를 reference로 동일한 패턴. 이 task 끝에 빌드는 통과하지만 LLM에 도구가 등록만 되어 있고 호출 시 stub 응답만 돌려줌. **중요**: app_claw는 sub-component를 두 곳에서 wiring한다 — `CMakeLists.txt`의 조건부 REQUIRES 블록 + `idf_component.yml`의 조건부 path. 이 둘에 cap_ha_control을 추가하지 않으면 다음 task에서 `#include "cap_ha_control.h"`가 빌드 실패한다.

- [ ] **Step 1: 디렉터리 생성 + 엔트리 stub 파일들**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
mkdir -p components/claw_capabilities/cap_ha_control/{include,src,data}
```

- [ ] **Step 2: `CMakeLists.txt` 작성**

```cmake
idf_component_register(
    SRCS
        "src/cap_ha_control.c"
        "src/cap_ha_control_core.c"
        "src/cap_ha_control_resolve.c"
        "src/cap_ha_control_http.c"
        "src/cap_ha_control_board.c"
        "src/cmd_cap_ha_control.c"
    INCLUDE_DIRS
        "include"
        "src"
    EMBED_TXTFILES
        "data/entities.default.json"
    REQUIRES
        claw_cap
        esp_http_client
        json
        nvs_flash
        console
        led_strip
)
```

(`led_strip`은 managed component `espressif/led_strip`이 노출하는 component 이름.)

- [ ] **Step 3: `idf_component.yml` 작성**

```yaml
## IDF Component Manager Manifest File
dependencies:
  espressif/led_strip: ^3.0.3
```

(in-repo `lua_module_led_strip`과 동일 버전 핀.)

- [ ] **Step 4: `Kconfig` 작성**

```
menu "cap_ha_control"

    config CAP_HA_CONTROL_BOOT_FETCH_ENABLED
        bool "Enrich entity registry from HA /api/states at boot"
        default y
        help
            When enabled, cap_ha_control will fetch /api/states from HA
            once after Wi-Fi connects and cache filtered entities (light,
            cover, switch) into NVS as supplemental registry entries.
            If HA is unreachable, the static entities.default.json is
            used alone.

endmenu
```

- [ ] **Step 5: `include/cap_ha_control.h` 작성**

```c
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cap_ha_control_register_group(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 6: `include/cmd_cap_ha_control.h` 작성**

```c
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cmd_cap_ha_control_register(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 7: `src/cap_ha_control_internal.h` 작성**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"

#define CAP_HA_NVS_NAMESPACE       "ha_ctl"
#define CAP_HA_NVS_KEY_URL         "ha_url"
#define CAP_HA_NVS_KEY_TOKEN       "ha_token"
#define CAP_HA_NVS_KEY_CACHE       "entity_cache"

#define CAP_HA_HTTP_TIMEOUT_MS     8000

/* Service-call response: HA가 service result만 돌려주므로 16KB 충분. */
#define CAP_HA_RESPONSE_BUF_BYTES  (16 * 1024)
/* /api/states full snapshot: HA 본체는 light/cover/switch/sensor/etc 통째라
 * 16KB로는 흔히 잘린다. 64KB로 best-effort. 그래도 잘리면 truncate + WARN
 * (정적 registry fallback이 있어 데모 blocker는 아니지만 enrichment는
 * 부분적이다). v4에서 streaming parser로 교체 검토. */
#define CAP_HA_STATES_BUF_BYTES    (64 * 1024)

typedef struct {
    char id[64];              /* entity_id, e.g. "light.smart_bulb" or "board:onboard_rgb" */
    char friendly_name[64];   /* Korean or English display name */
    char domain[16];          /* "light" / "cover" / "switch" / "board" */
    bool supports_brightness;
    bool supports_color;
} cap_ha_entity_t;

typedef struct {
    cap_ha_entity_t *items;
    size_t count;
} cap_ha_registry_t;

/* core */
esp_err_t cap_ha_core_execute(const char *input_json,
                              char *output_json,
                              size_t output_size);

/* resolve */
esp_err_t cap_ha_resolve_init(void);
esp_err_t cap_ha_resolve_target(const char *target,
                                cap_ha_entity_t *out);
esp_err_t cap_ha_resolve_top_candidates(char *out_csv,
                                        size_t out_size,
                                        size_t max);
esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv,
                                               size_t out_size);
esp_err_t cap_ha_resolve_refresh_from_ha(void);

/* http */
esp_err_t cap_ha_http_post_service(const char *domain,
                                   const char *service,
                                   const char *body_json,
                                   int *http_status_out,
                                   char *response_buf,
                                   size_t response_buf_size);
esp_err_t cap_ha_http_get_states(char *response_buf,
                                 size_t response_buf_size);
esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size);
esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size);
esp_err_t cap_ha_http_set_url(const char *url);
esp_err_t cap_ha_http_set_token(const char *token);

/* board */
esp_err_t cap_ha_board_dispatch(const char *target,
                                const char *action,
                                int brightness_pct,
                                const char *color,
                                char *message_out,
                                size_t message_size);
```

- [ ] **Step 8: `src/cap_ha_control.c` 작성 (descriptor + register)**

```c
/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control.h"
#include "cap_ha_control_internal.h"
#include "cmd_cap_ha_control.h"

#include "claw_cap.h"
#include "esp_log.h"

static const char *TAG = "cap_ha_control";

static esp_err_t cap_ha_execute(const claw_cap_descriptor_t *descriptor,
                                const char *input_json,
                                char *output_json,
                                size_t output_size,
                                void *user_ctx)
{
    (void)descriptor;
    (void)user_ctx;
    return cap_ha_core_execute(input_json, output_json, output_size);
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
```

- [ ] **Step 9: 5개 src 파일 stub 작성 (이후 task에서 본구현)**

`src/cap_ha_control_core.c`:
```c
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cap_ha_core";

esp_err_t cap_ha_core_execute(const char *input_json,
                              char *output_json,
                              size_t output_size)
{
    (void)input_json;
    ESP_LOGW(TAG, "stub: ha_control not yet implemented");
    snprintf(output_json, output_size,
             "{\"success\":false,\"message\":\"ha_control not yet implemented (stub).\"}");
    return ESP_OK;
}
```

`src/cap_ha_control_resolve.c`:
```c
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cap_ha_resolve";

esp_err_t cap_ha_resolve_init(void) { ESP_LOGI(TAG, "stub init"); return ESP_OK; }
esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    (void)target; (void)out;
    return ESP_ERR_NOT_FOUND;
}
esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    (void)max;
    snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}
esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv, size_t out_size)
{
    snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}
esp_err_t cap_ha_resolve_refresh_from_ha(void) { return ESP_ERR_NOT_SUPPORTED; }
```

`src/cap_ha_control_http.c`:
```c
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cap_ha_http";

esp_err_t cap_ha_http_post_service(const char *domain, const char *service,
                                   const char *body_json, int *http_status_out,
                                   char *response_buf, size_t response_buf_size)
{
    (void)domain; (void)service; (void)body_json;
    (void)response_buf; (void)response_buf_size;
    if (http_status_out) *http_status_out = 0;
    ESP_LOGW(TAG, "stub: post_service");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cap_ha_http_get_states(char *response_buf, size_t response_buf_size)
{
    (void)response_buf; (void)response_buf_size;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size)
{ if (url_out && url_size) url_out[0] = '\0'; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size)
{ if (token_out && token_size) token_out[0] = '\0'; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t cap_ha_http_set_url(const char *url) { (void)url; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cap_ha_http_set_token(const char *token) { (void)token; return ESP_ERR_NOT_SUPPORTED; }
```

`src/cap_ha_control_board.c`:
```c
#include "cap_ha_control_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cap_ha_board";

esp_err_t cap_ha_board_dispatch(const char *target, const char *action,
                                int brightness_pct, const char *color,
                                char *message_out, size_t message_size)
{
    (void)target; (void)action; (void)brightness_pct; (void)color;
    snprintf(message_out, message_size, "보드 RGB 분기 미구현 (stub).");
    return ESP_ERR_NOT_SUPPORTED;
}
```

`src/cmd_cap_ha_control.c`:
```c
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
```

- [ ] **Step 10: `data/entities.default.json` placeholder 작성**

```json
{
  "entities": [
    {
      "id": "board:onboard_rgb",
      "friendly_name": "보드 RGB",
      "domain": "board",
      "supports": ["color", "brightness"]
    }
  ]
}
```

- [ ] **Step 11: `components/common/app_claw/Kconfig` — `APP_CLAW_CAP_HA_CONTROL` 추가**

`APP_CLAW_CAP_TIME` 블록 (line ~124) 바로 뒤에 추가:

```
        config APP_CLAW_CAP_HA_CONTROL
            bool "Enable HA control capability"
            default y
            help
                Enable the cap_ha_control capability and keep app_claw's direct
                dependency on cap_ha_control. Provides a single typed tool
                'ha_control' that owns smart-home dispatch (HA REST + onboard
                board RGB) and exposes structured success/message contracts to
                the LLM for verbatim echo.
```

- [ ] **Step 12: `components/common/app_claw/CMakeLists.txt` — 조건부 REQUIRES 블록 추가**

기존 `if(CONFIG_APP_CLAW_CAP_TIME) ... endif()` 블록 다음(또는 `CONFIG_APP_CLAW_CAP_*` 블록들이 모인 영역의 알파벳 순서에 맞는 자리)에 추가:

```cmake
if(CONFIG_APP_CLAW_CAP_HA_CONTROL)
    list(APPEND app_claw_requires cap_ha_control)
endif()
```

- [ ] **Step 13: `components/common/app_claw/idf_component.yml` — 조건부 dependency 추가**

기존 `cap_files` / `cap_time` 등 항목과 같은 형태로 (`dependencies:` 매핑 안에) 추가:

```yaml
  cap_ha_control:
    rules:
      - if: $CONFIG{APP_CLAW_CAP_HA_CONTROL} == True
    path: ../../claw_capabilities/cap_ha_control
```

(이 두 wiring이 빠지면 Step 14의 `app_cap_register_ha_control()`이 `#include "cap_ha_control.h"` 단계에서 빌드 실패한다.)

- [ ] **Step 14: `components/common/app_claw/app_capabilities.c` — register dispatch 추가**

`#if CONFIG_APP_CLAW_CAP_TIME` 블록 (line ~468) 다음 자리에 헤더 include 추가 (파일 상단의 cap_time include 옆):

```c
#if CONFIG_APP_CLAW_CAP_HA_CONTROL
#include "cap_ha_control.h"
#endif
```

`app_cap_register_time()` 함수 (line ~469) 다음에 새 함수 추가:

```c
#if CONFIG_APP_CLAW_CAP_HA_CONTROL
static esp_err_t app_cap_register_ha_control(const app_claw_config_t *config,
                                             const app_claw_storage_paths_t *paths)
{
    (void)config;
    (void)paths;
    return cap_ha_control_register_group();
}
#endif
```

그리고 같은 파일의 dispatch 테이블 `s_capability_group_entries[]` 배열 (cap_time entry 다음 자리)에 다음을 끼워 넣는다:

```c
#if CONFIG_APP_CLAW_CAP_HA_CONTROL
    { "cap_ha_control", "HA Control", "Register HA control cap", false, NULL, app_cap_register_ha_control },
#endif
```

엔트리 시그니처는 기존 패턴과 동일: `{ id, label, log_msg, requires_paths, prepare_fn, register_fn }`. cap_ha_control은 prepare 단계가 따로 없으므로 `prepare_fn = NULL`.

**같은 파일의 두 번째 테이블 `s_capability_group_infos[]`에도 동일한 cap을 추가해야 한다** (UI / capability list 일관성 — 안 넣으면 wizard listing이나 cap inspection에서 안 보임). cap_time entry (`{ "cap_time", "Time", false }`) 다음 자리에:

```c
#if CONFIG_APP_CLAW_CAP_HA_CONTROL
    { "cap_ha_control", "HA Control", false },
#endif
```

infos 테이블 시그니처는 `{ id, label, requires_paths }` 3-tuple로 entries보다 짧다. 두 테이블 모두 같은 순서/같은 #if 가드를 유지.

- [ ] **Step 15: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. fatal error 없음. `cap_ha_control` component가 빌드 그래프에 들어와 `libcap_ha_control.a`가 생성됨.

```bash
ls build/esp-idf/cap_ha_control/ 2>/dev/null
```
Expected: `libcap_ha_control.a`, `*.o` 파일들.

- [ ] **Step 16: commit**

```bash
git add components/claw_capabilities/cap_ha_control/ \
        components/common/app_claw/Kconfig \
        components/common/app_claw/CMakeLists.txt \
        components/common/app_claw/idf_component.yml \
        components/common/app_claw/app_capabilities.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_control): scaffold component (stub execute, descriptor only)

New capability cap_ha_control. This commit only scaffolds: directory,
Kconfig toggle, descriptor with the typed 'ha_control' input schema,
embedded entities.default.json placeholder, stub implementations for
core/resolve/http/board/cli. Builds clean and registers the capability
group; calling ha_control returns a stub failure message until later
tasks fill in real behavior.

Following tasks:
- Task 3: schema validate + execute() dispatch
- Task 4: static registry load
- Task 5: target resolve cascade
- Task 6: HA REST POST/GET
- Task 7: service mapping (light/cover/switch + brightness/color data)
- Task 8: onboard board RGB branch
- Task 9: Korean message composition
- Task 10: boot-fetch + NVS cache
- Task 11: console commands
- Task 12: active registry → tool description injection

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Schema validate + execute() dispatch entry point

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c`

문맥: spec § 3 — input parse + 누락/enum 외 reject. Resolve / HA / board는 다음 task. 이 task 끝에 console에서 `ha_control --call '{"action":"turn_on"}'` 호출 시 reject 메시지가 정확히 나옴 (단, --call 명령 자체는 Task 11에서 본구현).

- [ ] **Step 1: `cap_ha_control_core.c` 본구현 작성**

```c
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
```

- [ ] **Step 2: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공.

- [ ] **Step 3: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c
git commit -m "feat(cap_ha_control): schema validation + dispatch entry point

Parse target/action/brightness_pct/color/kelvin, reject empty/invalid
inputs with Korean messages, then dispatch to board branch (stub) or
HA branch (stub). Resolve cascade returns NOT_FOUND for now — this
becomes a real lookup once the static registry lands in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 정적 registry 로드 (entities.default.json embed)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/data/entities.default.json`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

문맥: spec § 6 — embed된 JSON을 boot 시 1회 파싱해 in-RAM `cap_ha_registry_t s_static_registry`로 들고 있음. Task 5에서 resolve cascade가 이 데이터를 사용. Task 10에서 NVS cache 합집합 추가.

- [ ] **Step 1: `data/entities.default.json` 본 entry로 교체**

```json
{
  "entities": [
    {
      "id": "light.smart_bulb",
      "friendly_name": "화장실 조명",
      "domain": "light",
      "supports": ["brightness", "color"]
    },
    {
      "id": "cover.zemismart_smart_curtain",
      "friendly_name": "거실 커튼",
      "domain": "cover",
      "supports": []
    },
    {
      "id": "switch.living_room_outlet",
      "friendly_name": "거실 콘센트",
      "domain": "switch",
      "supports": []
    },
    {
      "id": "board:onboard_rgb",
      "friendly_name": "보드 RGB",
      "domain": "board",
      "supports": ["color", "brightness"]
    }
  ]
}
```

- [ ] **Step 2: `cap_ha_control_resolve.c` — embed + parse + 정적 registry 메모리 보유**

전체 교체 (다음 task에서 추가 함수만 본구현 — 이 task는 init/active_friendly_names/top_candidates만 진짜 구현, target lookup은 Task 5):

```c
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
```

- [ ] **Step 3: 빌드 (embed symbol 검증)**

```bash
cd application/edge_agent
idf.py build 2>&1 | tail -40
```
Expected: 성공. embed symbol 인식. `nm build/esp-idf/cap_ha_control/libcap_ha_control.a 2>/dev/null | grep entities_default` 결과 `_binary_entities_default_json_start/end` 보임.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/data/entities.default.json \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "feat(cap_ha_control): static entity registry loader (embed JSON)

Embed entities.default.json into the cap_ha_control binary via
EMBED_TXTFILES, parse it once at group_init into an in-RAM registry,
and expose top_candidates / active_friendly_names. resolve_target is
still a stub — the real cascade arrives next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Target resolve cascade (정확 일치 → 정규화 일치)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

문맥: spec § 4 — 3-stage cascade. NVS cache 병합은 Task 10. 한국어 정규화는 단순(공백 제거 + trailing 조사 trim).

- [ ] **Step 1: 한국어 정규화 helper + cascade 함수**

`cap_ha_control_resolve.c`에 다음 함수들을 추가 (파일 상단 helpers 섹션):

```c
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
```

- [ ] **Step 2: `cap_ha_resolve_target` 본구현으로 치환**

```c
esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    if (!target || !*target || !out) return ESP_ERR_INVALID_ARG;
    if (s_static_registry.count == 0) return ESP_ERR_NOT_FOUND;

    /* Stage 1: exact entity_id match. */
    for (size_t i = 0; i < s_static_registry.count; i++) {
        if (strcmp(s_static_registry.items[i].id, target) == 0) {
            *out = s_static_registry.items[i];
            return ESP_OK;
        }
    }
    /* Stage 2: exact friendly_name match. */
    for (size_t i = 0; i < s_static_registry.count; i++) {
        if (strcmp(s_static_registry.items[i].friendly_name, target) == 0) {
            *out = s_static_registry.items[i];
            return ESP_OK;
        }
    }
    /* Stage 3: normalized friendly_name match. */
    char target_norm[64];
    normalize_korean(target, target_norm, sizeof(target_norm));
    if (target_norm[0] == '\0') return ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < s_static_registry.count; i++) {
        char fn_norm[64];
        normalize_korean(s_static_registry.items[i].friendly_name,
                         fn_norm, sizeof(fn_norm));
        if (fn_norm[0] && strcmp(fn_norm, target_norm) == 0) {
            *out = s_static_registry.items[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
```

- [ ] **Step 3: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "feat(cap_ha_control): target resolve cascade (3 stages)

Match target against entity_id, friendly_name, and a normalized form
(whitespace stripped, common Korean trailing particles dropped). LLM
remains responsible for fuzzy semantic matching from the active
registry list — firmware only does exact + simple normalize.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: HA REST HTTP layer (POST /api/services + GET /api/states + NVS url/token)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c`

문맥: spec § 7 — Bearer 헤더, esp_http_client, response buffer 16 KB cap. NVS namespace `ha_ctl`에서 `ha_url`/`ha_token` 읽고 쓰기. **버퍼 한계**: caller-provided buffer 패턴 유지 — URL은 160B(http(s)://host:port + 약간), token은 4096B(HA long-lived JWT 안전 마진). v2의 256B 제한은 token에 한해 4096B로 확장하여 해소; "제한 제거"가 아니다. 진짜 제거가 필요해지면 `cap_ha_http_get_url_alloc(char **out)` 시그니처로 별도 helper를 v4에서 추가.

- [ ] **Step 1: NVS getter/setter 본구현**

```c
#include "cap_ha_control_internal.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_http";

static esp_err_t nvs_get_str_alloc(nvs_handle_t h, const char *key, char *out, size_t out_size)
{
    size_t need = 0;
    esp_err_t err = nvs_get_str(h, key, NULL, &need);
    if (err != ESP_OK) return err;
    if (need > out_size) return ESP_ERR_INVALID_SIZE;
    return nvs_get_str(h, key, out, &need);
}

esp_err_t cap_ha_http_get_url(char *url_out, size_t url_size)
{
    if (!url_out || url_size == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) { url_out[0] = '\0'; return err; }
    err = nvs_get_str_alloc(h, CAP_HA_NVS_KEY_URL, url_out, url_size);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_get_token(char *token_out, size_t token_size)
{
    if (!token_out || token_size == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) { token_out[0] = '\0'; return err; }
    err = nvs_get_str_alloc(h, CAP_HA_NVS_KEY_TOKEN, token_out, token_size);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_set_url(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, CAP_HA_NVS_KEY_URL, url);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t cap_ha_http_set_token(const char *token)
{
    if (!token) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, CAP_HA_NVS_KEY_TOKEN, token);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
```

- [ ] **Step 2: HTTP event handler (response buffer accumulation)**

같은 파일에 추가:

```c
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} cap_ha_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    cap_ha_buf_t *buf = (cap_ha_buf_t *)evt->user_data;
    if (!buf || evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    size_t needed = buf->len + (size_t)evt->data_len;
    if (needed >= buf->cap) {
        ESP_LOGW(TAG, "response truncated: have=%zu need=%zu cap=%zu",
                 buf->len, needed, buf->cap);
        size_t fits = (buf->cap > buf->len + 1) ? (buf->cap - buf->len - 1) : 0;
        if (fits) {
            memcpy(buf->data + buf->len, evt->data, fits);
            buf->len += fits;
            buf->data[buf->len] = '\0';
        }
        return ESP_OK;
    }
    memcpy(buf->data + buf->len, evt->data, evt->data_len);
    buf->len += evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}
```

- [ ] **Step 3: POST /api/services 구현**

```c
esp_err_t cap_ha_http_post_service(const char *domain, const char *service,
                                   const char *body_json, int *http_status_out,
                                   char *response_buf, size_t response_buf_size)
{
    if (!domain || !service || !body_json || !response_buf || response_buf_size == 0)
        return ESP_ERR_INVALID_ARG;
    response_buf[0] = '\0';
    if (http_status_out) *http_status_out = 0;

    char base_url[160] = {0};
    char *token = NULL;
    size_t token_cap = 4096;
    esp_err_t err = cap_ha_http_get_url(base_url, sizeof(base_url));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ha_url not set in NVS");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    token = malloc(token_cap);
    if (!token) return ESP_ERR_NO_MEM;
    err = cap_ha_http_get_token(token, token_cap);
    if (err != ESP_OK) { free(token); ESP_LOGW(TAG, "ha_token not set"); return err; }

    /* strip trailing slash from base_url */
    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') { base_url[--blen] = '\0'; }

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s/api/services/%s/%s",
             base_url, domain, service);

    char auth_header[4128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    free(token);

    cap_ha_buf_t resp = {
        .data = response_buf, .len = 0, .cap = response_buf_size,
    };
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Authorization", auth_header);
    esp_http_client_set_post_field(cli, body_json, (int)strlen(body_json));

    ESP_LOGI(TAG, "POST %s body=%s", full_url, body_json);
    err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    if (http_status_out) *http_status_out = status;
    esp_http_client_cleanup(cli);
    ESP_LOGI(TAG, "POST result err=%s status=%d resp_len=%zu",
             esp_err_to_name(err), status, resp.len);
    /* Contract: ESP_OK == HTTP transport completed (caller checks
     * http_status_out for 2xx vs 4xx/5xx). Returning a hard error here
     * for non-2xx would shadow the actual status (401 in particular)
     * from the failure-message composer, which gives users a misleading
     * "network err" instead of "인증 실패". esp_http_client convention. */
    return err;
}
```

- [ ] **Step 4: GET /api/states 구현 (boot-fetch에서 사용)**

```c
esp_err_t cap_ha_http_get_states(char *response_buf, size_t response_buf_size)
{
    if (!response_buf || response_buf_size == 0) return ESP_ERR_INVALID_ARG;
    response_buf[0] = '\0';

    char base_url[160] = {0};
    char *token = NULL;
    size_t token_cap = 4096;
    esp_err_t err = cap_ha_http_get_url(base_url, sizeof(base_url));
    if (err != ESP_OK) return err;
    token = malloc(token_cap);
    if (!token) return ESP_ERR_NO_MEM;
    err = cap_ha_http_get_token(token, token_cap);
    if (err != ESP_OK) { free(token); return err; }

    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') base_url[--blen] = '\0';

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s/api/states", base_url);
    char auth_header[4128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    free(token);

    cap_ha_buf_t resp = {
        .data = response_buf, .len = 0, .cap = response_buf_size,
    };
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Authorization", auth_header);

    ESP_LOGI(TAG, "GET %s", full_url);
    err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    ESP_LOGI(TAG, "GET result err=%s status=%d resp_len=%zu",
             esp_err_to_name(err), status, resp.len);
    if (err != ESP_OK) return err;
    if (status / 100 != 2) {
        ESP_LOGW(TAG, "GET /api/states non-2xx (%d) — caller should treat as failure", status);
        return ESP_ERR_HTTP_CONNECT;
    }
    return ESP_OK;
}
```

- [ ] **Step 5: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. esp_http_client, nvs_flash 의존성 해결.

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c
git commit -m "feat(cap_ha_control): HA REST HTTP layer + NVS url/token

Implements cap_ha_http_post_service / cap_ha_http_get_states using
esp_http_client + Bearer header. URL and token live in NVS namespace
'ha_ctl' (keys: ha_url, ha_token); URL fits in 160B and token in 4096B
(extended from v2's 256B; not 'unlimited' — caller-provided buffer
pattern retained). Response buffer is bounded at 16 KB with a soft
truncate + warn log.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Service mapping + light/cover/switch dispatch + color/brightness data

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c`

문맥: spec § 5 — domain↔action 부조합은 reject, data field ↔ entity.supports 부조합은 silent drop + WARN. light는 brightness_pct/color/kelvin attach. cover/switch는 entity_id만.

- [ ] **Step 1: HA branch dispatch helper 추가 (`cap_ha_control_core.c`)**

기존 `cap_ha_core_execute`의 "HA 분기 placeholder" 자리를 다음으로 교체:

```c
/* HA branch — service mapping per spec section 5. */
const char *svc = NULL;
if (strcmp(entity.domain, "light") == 0) {
    if (strcmp(action, "turn_on") == 0)        svc = "turn_on";
    else if (strcmp(action, "turn_off") == 0)  svc = "turn_off";
    else if (strcmp(action, "toggle") == 0)    svc = "toggle";
} else if (strcmp(entity.domain, "cover") == 0) {
    if (strcmp(action, "open") == 0)           svc = "open_cover";
    else if (strcmp(action, "close") == 0)     svc = "close_cover";
    else if (strcmp(action, "toggle") == 0)    svc = "toggle";
} else if (strcmp(entity.domain, "switch") == 0) {
    if (strcmp(action, "turn_on") == 0)        svc = "turn_on";
    else if (strcmp(action, "turn_off") == 0)  svc = "turn_off";
    else if (strcmp(action, "toggle") == 0)    svc = "toggle";
}
if (!svc) {
    char msg[200];
    snprintf(msg, sizeof(msg),
             "%s은(는) 해당 동작을 지원하지 않습니다 (action=%s).",
             entity.friendly_name, action);
    emit_failure(output_json, output_size, msg);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Build service_data — silent drop unsupported fields with WARN log. */
cJSON *body = cJSON_CreateObject();
cJSON_AddStringToObject(body, "entity_id", entity.id);

if (brightness_pct >= 0) {
    if (entity.supports_brightness) {
        cJSON_AddNumberToObject(body, "brightness_pct", brightness_pct);
    } else {
        ESP_LOGW(TAG, "%s does not support brightness; dropping", entity.id);
    }
}
if (kelvin >= 0) {
    if (entity.supports_color || entity.supports_brightness) {
        cJSON_AddNumberToObject(body, "kelvin", kelvin);
    } else {
        ESP_LOGW(TAG, "%s does not support kelvin; dropping", entity.id);
    }
}
if (color && *color) {
    if (entity.supports_color) {
        int rgb[3];
        if (cap_ha_color_to_rgb(color, rgb) == ESP_OK) {
            cJSON *arr = cJSON_CreateIntArray(rgb, 3);
            cJSON_AddItemToObject(body, "rgb_color", arr);
        } else {
            ESP_LOGW(TAG, "color '%s' not recognized; dropping", color);
        }
    } else {
        ESP_LOGW(TAG, "%s does not support color; dropping", entity.id);
    }
}

char *body_str = cJSON_PrintUnformatted(body);
cJSON_Delete(body);
if (!body_str) {
    emit_failure(output_json, output_size, "내부 오류 (JSON 직렬화 실패).");
    cJSON_Delete(root);
    return ESP_OK;
}

char ha_resp[CAP_HA_RESPONSE_BUF_BYTES];
int http_status = 0;
err = cap_ha_http_post_service(entity.domain, svc, body_str, &http_status,
                               ha_resp, sizeof(ha_resp));
free(body_str);

bool body_signals_failure = (strstr(ha_resp, "\"isError\":true") != NULL);
bool ok = (err == ESP_OK) && (http_status / 100 == 2) && !body_signals_failure;

char msg[256];
if (ok) {
    cap_ha_compose_success_message(&entity, action, brightness_pct, color, msg, sizeof(msg));
} else {
    cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
}

cJSON *out = cJSON_CreateObject();
cJSON_AddBoolToObject(out, "success", ok);
cJSON_AddStringToObject(out, "message", msg);
cJSON_AddStringToObject(out, "entity_id", entity.id);
cJSON_AddNumberToObject(out, "raw_status", http_status);
char *out_str = cJSON_PrintUnformatted(out);
if (out_str) { snprintf(output_json, output_size, "%s", out_str); free(out_str); }
cJSON_Delete(out);
cJSON_Delete(root);
return ESP_OK;
```

- [ ] **Step 2: `cap_ha_color_to_rgb` + message composer 함수들 추가**

`cap_ha_control_core.c` 상단 helper 영역에 추가:

```c
typedef struct { const char *name; int r; int g; int b; } cap_ha_color_t;

static const cap_ha_color_t COLOR_TABLE[] = {
    { "yellow", 255, 255, 0 },
    { "red",    255, 0,   0 },
    { "green",  0,   255, 0 },
    { "blue",   0,   0,   255 },
    { "purple", 128, 0,   255 },
    { "white",  255, 255, 255 },
    { "orange", 255, 165, 0 },
    { "pink",   255, 105, 180 },
};

esp_err_t cap_ha_color_to_rgb(const char *color, int rgb_out[3])
{
    if (!color || !*color) return ESP_ERR_INVALID_ARG;

    if (color[0] == '#' && strlen(color) == 7) {
        char r[3] = { color[1], color[2], 0 };
        char g[3] = { color[3], color[4], 0 };
        char b[3] = { color[5], color[6], 0 };
        rgb_out[0] = (int)strtol(r, NULL, 16);
        rgb_out[1] = (int)strtol(g, NULL, 16);
        rgb_out[2] = (int)strtol(b, NULL, 16);
        return ESP_OK;
    }
    for (size_t i = 0; i < sizeof(COLOR_TABLE)/sizeof(COLOR_TABLE[0]); i++) {
        if (strcmp(COLOR_TABLE[i].name, color) == 0) {
            rgb_out[0] = COLOR_TABLE[i].r;
            rgb_out[1] = COLOR_TABLE[i].g;
            rgb_out[2] = COLOR_TABLE[i].b;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static const char *action_kor(const char *action)
{
    if (strcmp(action, "turn_on") == 0)  return "켰";
    if (strcmp(action, "turn_off") == 0) return "껐";
    if (strcmp(action, "toggle") == 0)   return "전환했";
    if (strcmp(action, "open") == 0)     return "열었";
    if (strcmp(action, "close") == 0)    return "닫았";
    return "처리했";
}

void cap_ha_compose_success_message(const cap_ha_entity_t *e,
                                    const char *action, int brightness_pct,
                                    const char *color,
                                    char *out, size_t out_size)
{
    char extras[64] = {0};
    size_t off = 0;
    if (color && *color) {
        off += snprintf(extras + off, sizeof(extras) - off, " %s", color);
    }
    if (brightness_pct > 0) {
        snprintf(extras + off, sizeof(extras) - off, " %d%%", brightness_pct);
    }
    snprintf(out, out_size, "%s%s을(를) %s습니다.",
             e->friendly_name, extras, action_kor(action));
}

void cap_ha_compose_failure_message(int http_status, esp_err_t http_err,
                                    char *out, size_t out_size)
{
    if (http_err != ESP_OK) {
        snprintf(out, out_size,
                 "HA 호출이 실패했습니다 (network err=%s).",
                 esp_err_to_name(http_err));
    } else if (http_status == 401) {
        snprintf(out, out_size, "HA 인증에 실패했습니다 (토큰 확인 필요).");
    } else if (http_status >= 400) {
        snprintf(out, out_size,
                 "HA 호출이 실패했습니다 (status=%d).", http_status);
    } else {
        snprintf(out, out_size, "HA가 동작을 거부했습니다.");
    }
}
```

`cap_ha_control_internal.h`에 위 두 메시지 합성 함수 + 컬러 변환 prototype을 forward 선언으로 추가:

```c
esp_err_t cap_ha_color_to_rgb(const char *color, int rgb_out[3]);
void cap_ha_compose_success_message(const cap_ha_entity_t *e,
                                    const char *action, int brightness_pct,
                                    const char *color,
                                    char *out, size_t out_size);
void cap_ha_compose_failure_message(int http_status, esp_err_t http_err,
                                    char *out, size_t out_size);
```

- [ ] **Step 3: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h
git commit -m "feat(cap_ha_control): light/cover/switch dispatch + color table + Korean message

Maps action enum to HA service per domain (light/cover/switch),
attaches brightness_pct/color/kelvin only when entity supports the
field (silent drop + WARN otherwise). Color names map to rgb_color via
an internal table; '#rrggbb' parsed inline. Success and failure
messages are firmware-composed Korean strings — the LLM will echo
these verbatim under the new system-prompt rule from Task 1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Onboard board branch (board:onboard_rgb → led_strip)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_board.c`

문맥: spec § 8 — v1.3 N16R8 clone (CH343), GPIO 48, COUNT 1. RGB 직접 + brightness_pct 스케일 → `led_strip_set_pixel(handle, 0, r, g, b)`. v2의 rgb_purple_*.lua가 한 일을 cap 안으로 흡수. led_strip C API를 직접 호출 (lua wrapper 없이). HSV 변환 단계는 생략 — managed component v3에 `set_pixel_hsv`도 존재하지만 이미 `cap_ha_color_to_rgb`가 RGB를 만들고, brightness 스케일을 RGB에 곱하는 게 lua_module_led_strip의 기존 패턴과 일치(불필요한 색공간 round-trip 회피).

managed component 버전은 in-repo lua_module과 일치시킨다 — `lua_module_led_strip`은 `^3.0.3`을 쓰므로 동일하게 고정.

- [ ] **Step 1: led_strip dependency를 cap_ha_control에 직접 추가**

cap_ha_control은 lua를 우회해 led_strip C API를 직접 쓴다. `components/claw_capabilities/cap_ha_control/idf_component.yml`을 다음으로 교체 (Task 2의 빈 deps 자리):

```yaml
## IDF Component Manager Manifest File
dependencies:
  espressif/led_strip: ^3.0.3
```

`CMakeLists.txt`의 `REQUIRES`에는 `led_strip`만 있으면 됨 (managed component가 자동 노출하는 component name과 동일). Task 2의 REQUIRES 목록에 `lua_module_led_strip`이 들어가 있으면 제거하고 `led_strip`만 둔다 — cap_ha_control이 lua wrapper에 의존할 필요는 없다.

- [ ] **Step 2: `cap_ha_control_board.c` 본구현 (RGB direct, brightness_pct는 RGB에 스케일)**

```c
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
```

- [ ] **Step 3: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. led_strip 헤더가 안 잡히면 Step 1로 돌아가 의존성 추가.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/idf_component.yml \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_board.c
git commit -m "feat(cap_ha_control): onboard board:onboard_rgb branch (GPIO 48)

Implements the board branch of the typed tool: 'board:onboard_rgb'
target is dispatched to a one-shot WS2812 driver call on GPIO 48
(v1.3 N16R8 clone, verified in docs/learn/20260508-bathroom-rgb-demo-result-and-rgb-gpio.md).
Color names resolve via cap_ha_color_to_rgb; brightness_pct scales the
RGB triple directly and the result is written with led_strip_set_pixel
(matches the lua_module_led_strip pattern, no HSV round-trip). espressif/led_strip
is pinned to ^3.0.3 to match the in-repo lua module. Driver handle is
opened, used, and deleted within the call so RMT channels are released
between requests.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Boot-fetch background task + NVS entity_cache enrichment

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

문맥: spec § 6 v1 tier — 부팅 후 Wi-Fi up되면 background task가 GET /api/states 1회 호출, light/cover/switch 필터링해 NVS `ha_ctl/entity_cache` blob에 저장. 다음 부팅부터는 NVS cache + 정적 파일 union이 active. 정적 파일 우선 (충돌 시 한국어 friendly_name 보존).

- [ ] **Step 1: NVS cache load (init 시점) + active union 갱신**

`cap_ha_control_resolve.c`에 NVS cache 로드 함수와 합집합 동작 추가. Top of file:

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
```

`s_static_registry` 다음에 cache 보관 변수 추가:

```c
static cap_ha_registry_t s_cache_registry = {0};

static bool registry_has_id(const cap_ha_registry_t *reg, const char *id)
{
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].id, id) == 0) return true;
    }
    return false;
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
```

- [ ] **Step 2: `cap_ha_resolve_init` 끝에 cache load 추가**

기존 `cap_ha_resolve_init` 함수 끝의 `return err` 직전에 다음 두 줄:

```c
    if (err == ESP_OK) {
        (void)load_cache_from_nvs();  /* best-effort; absence is fine */
    }
```

- [ ] **Step 3: resolve cascade가 cache도 보도록 확장**

`cap_ha_resolve_target` 본문 안에서 매 stage가 `s_static_registry` 외에 `s_cache_registry`도 검사하도록 변경. 가장 깔끔한 방법은 lookup helper 함수:

```c
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
```

`cap_ha_resolve_target`을 다음으로 치환:

```c
esp_err_t cap_ha_resolve_target(const char *target, cap_ha_entity_t *out)
{
    if (!target || !*target || !out) return ESP_ERR_INVALID_ARG;
    /* Stage 1: exact entity_id (static first, then cache). */
    if (lookup_in(&s_static_registry, target, true, false, false, out)) return ESP_OK;
    if (lookup_in(&s_cache_registry,  target, true, false, false, out)) return ESP_OK;
    /* Stage 2: exact friendly_name. */
    if (lookup_in(&s_static_registry, target, false, true, false, out)) return ESP_OK;
    if (lookup_in(&s_cache_registry,  target, false, true, false, out)) return ESP_OK;
    /* Stage 3: normalized friendly_name. */
    if (lookup_in(&s_static_registry, target, false, false, true, out)) return ESP_OK;
    if (lookup_in(&s_cache_registry,  target, false, false, true, out)) return ESP_OK;
    return ESP_ERR_NOT_FOUND;
}
```

`cap_ha_resolve_top_candidates` / `cap_ha_resolve_active_friendly_names`도 cache까지 포함하도록 수정 (정적 우선, 중복 id는 skip):

```c
esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    out_csv[0] = '\0';
    size_t emitted = 0;

    cap_ha_registry_t *regs[2] = { &s_static_registry, &s_cache_registry };
    for (int r = 0; r < 2 && emitted < max; r++) {
        for (size_t i = 0; i < regs[r]->count && emitted < max; i++) {
            /* dedupe by id against already-included static entries */
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
    if (emitted == 0) snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}

esp_err_t cap_ha_resolve_active_friendly_names(char *out_csv, size_t out_size)
{
    return cap_ha_resolve_top_candidates(out_csv, out_size,
                                         s_static_registry.count + s_cache_registry.count);
}
```

- [ ] **Step 4: HA `/api/states` 응답을 entities JSON 으로 변환 + NVS 저장**

`cap_ha_resolve_refresh_from_ha` 본구현:

```c
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
        if (s_cache_registry.items) free(s_cache_registry.items);
        s_cache_registry = (cap_ha_registry_t){0};
        parse_registry(blob, &s_cache_registry);
    }
    ESP_LOGI(TAG, "boot-fetch: kept %d entities, NVS store=%s",
             kept, esp_err_to_name(store_err));
    free(blob);
    return store_err;
}
```

- [ ] **Step 5: 부팅 시 background task 등록 (Wi-Fi up 후 1회)**

**Forward declaration은 함수 안에 두면 안 된다** — C 표준상 nested function declaration이 일부 toolchain에서 깨질 수 있고, ESP-IDF가 `-Wnested-externs` 등 경고를 켤 가능성도 있다. 파일 상단(다른 static 함수들 forward decl 모이는 영역)에 #if 가드와 함께 둔다.

파일 상단 (top-level static 선언 영역):

```c
#if CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED
static void boot_fetch_task(void *arg);
#endif
```

`cap_ha_resolve_init` 본문 끝(다른 init 작업 후)에 task 시작:

```c
#if CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED
    xTaskCreate(boot_fetch_task, "ha_ctl_boot", 6 * 1024, NULL, 4, NULL);
#endif
```

task 본문 정의는 같은 파일 어디든 (init 함수 위 또는 아래):

```c
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
```

- [ ] **Step 6: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공.

- [ ] **Step 7: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "feat(cap_ha_control): boot-fetch + NVS entity_cache enrichment

Adds the v1 registry tier from the spec: at init, load any cached
entities from NVS namespace 'ha_ctl' blob 'entity_cache'; once Wi-Fi
is up a one-shot background task GETs /api/states, filters
light/cover/switch entries, and persists them as the cache. Resolve
cascade now consults static∪cache (static wins on id collision); the
candidates list reflects the same union.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Console commands (--call, --resolve, --refresh-registry, --set-url, --set-token)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`

문맥: spec § 11 — 콘솔 단독 검증 + 토큰/URL 입력 fallback. argtable3 또는 단순 argv 파싱.

- [ ] **Step 1: argtable3 기반 console 명령 본구현**

```c
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
    struct arg_end *end;
} ha_args;

static int cmd_ha_control(int argc, char **argv)
{
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

    printf("ha_control: at least one of --call/--resolve/--refresh-registry/--set-url/--set-token required\n");
    return 1;
}

esp_err_t cmd_cap_ha_control_register(void)
{
    ha_args.call      = arg_str0(NULL, "call", "<json>", "ha_control payload as JSON");
    ha_args.resolve   = arg_str0(NULL, "resolve", "<target>", "lookup target in registry");
    ha_args.refresh   = arg_lit0(NULL, "refresh-registry", "fetch /api/states and update NVS cache");
    ha_args.set_url   = arg_str0(NULL, "set-url", "<url>", "store HA URL in NVS");
    ha_args.set_token = arg_str0(NULL, "set-token", "<token>", "store HA bearer token in NVS");
    ha_args.end       = arg_end(2);

    static const esp_console_cmd_t cmd = {
        .command = "ha_control",
        .help = "ha_control --call '<json>' | --resolve <target> | --refresh-registry | --set-url <url> | --set-token <token>",
        .hint = NULL,
        .func = &cmd_ha_control,
        .argtable = &ha_args,
    };
    return esp_console_cmd_register(&cmd);
}
```

- [ ] **Step 2: argtable3 의존성 확인**

```bash
grep -rn "argtable3" components/claw_capabilities/cap_mcp_client/CMakeLists.txt 2>/dev/null
```
Expected: `argtable3` 또는 standard `console` deps에 포함. cap_ha_control의 `CMakeLists.txt` REQUIRES에 `argtable3` 추가 (필요 시):

```cmake
REQUIRES
    claw_cap
    esp_http_client
    json
    nvs_flash
    console
    argtable3
    led_strip
```

(`espressif/led_strip`이 managed component면 `idf_component.yml`로 처리.)

- [ ] **Step 3: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. argtable3 link 통과.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c \
        components/claw_capabilities/cap_ha_control/CMakeLists.txt
git commit -m "feat(cap_ha_control): console commands (call/resolve/refresh/set-url/set-token)

argtable3-based console driver for cap_ha_control. --call dispatches a
JSON payload through the same execute() path the LLM hits; --resolve
exercises the cascade for debugging; --refresh-registry pokes the
boot-fetch path manually; --set-url and --set-token persist NVS values
as a wizard-less fallback for demo prep.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Active registry → ha_control tool description 갱신

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`

문맥: spec § 4 — LLM이 fuzzy 책임을 지려면 실시간 friendly_name 리스트가 tool description에 들어와야 함. claw_cap의 dynamic description 또는 group_init 시 1회 inject. 가장 간단한 path는 정적 description 뒤에 friendly_name 리스트를 1회 합성한 문자열을 두는 것 (boot-fetch 후 변경은 다음 부팅에 반영 — 시연용 충분).

- [ ] **Step 1: dynamic description 합성 + descriptor 사본 보유**

기존 `s_ha_descriptors` static 정의를 mutable 영역으로 옮기고, `cap_ha_group_init`에서 description을 동적 합성한 문자열로 교체:

`cap_ha_control.c`를 다음과 같이 수정 (descriptor 배열을 static const → static로 변경, description을 buffer로 보관):

```c
static char s_ha_description[1024];
static char s_ha_friendly_names[256];

static claw_cap_descriptor_t s_ha_descriptors[] = {
    {
        .id = "ha_control",
        .name = "ha_control",
        .family = "ha",
        .description = NULL, /* set in group_init */
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

static void compose_description(void)
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
}

static esp_err_t cap_ha_group_init(void)
{
    esp_err_t err = cap_ha_resolve_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resolve_init returned %s — using static-only registry",
                 esp_err_to_name(err));
    }
    compose_description();
    err = cmd_cap_ha_control_register();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "cmd register failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}
```

`s_ha_group`의 `descriptors`도 const 제거가 필요하면 조정 (아직 const라도 description은 pointer라 OK).

- [ ] **Step 2: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공.

- [ ] **Step 3: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control.c
git commit -m "feat(cap_ha_control): inject active friendly_names into tool description

cap_ha_group_init composes the LLM-visible tool description from the
static∪cache registry's friendly_names so the model has a fresh device
list to fuzzy-match against. Boot-fetch updates take effect on the next
reboot, which is the operating cadence a demo needs.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: sdkconfig + vis_cap_groups 기본값 (console-only token path)

**Files:**
- Modify: `application/edge_agent/sdkconfig.defaults`
- Modify: `application/edge_agent/components/app_config/app_config.c`

문맥: spec § 10 — `CONFIG_APP_CLAW_CAP_HA_CONTROL=y`, boot-fetch 활성화. `vis_cap_groups` 기본값을 `cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`로 갱신.

**중요한 결정 (review feedback #4 반영)**: v3에서 ha_url/ha_token 입력 경로는 **console-only** (`ha_control --set-url <url>` / `--set-token <token>`)로 한정한다.
- 이유: cap_ha_http_*는 NVS namespace `ha_ctl`을 직접 read/write하지만, 현재 setup wizard는 `app_config_t` (다른 NVS namespace/table)에 값을 박는 흐름이다. wizard에 ha_url/ha_token 필드만 추가하면 wizard에 입력해도 cap_ha_control이 못 읽는 split-brain이 생긴다.
- v4에서 둘 중 하나로 통합: (a) wizard 핸들러가 `app_config_t`에 박지 않고 `cap_ha_http_set_url/set_token()`을 직접 호출, 또는 (b) cap_ha_http_*가 `app_config` API를 경유하도록 어댑터화. v3에서는 거기까지 안 간다.
- v3 시연 흐름: 보드 erase-flash → wizard로 Wi-Fi/LLM/Telegram만 입력 → 부팅 후 USB 콘솔에서 `ha_control --set-url <ip>` + `--set-token <token>` 실행 → reboot.

- [ ] **Step 1: sdkconfig.defaults 갱신**

`application/edge_agent/sdkconfig.defaults` 끝에 추가:

```
# cap_ha_control v3
CONFIG_APP_CLAW_CAP_HA_CONTROL=y
CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y
```

기존 `sdkconfig` 파일이 충돌 값(예: `CONFIG_APP_CLAW_CAP_HA_CONTROL=n`)을 들고 있으면 다음으로 갱신:

```bash
cd application/edge_agent
idf.py reconfigure
grep -E "APP_CLAW_CAP_HA_CONTROL|CAP_HA_CONTROL_BOOT_FETCH" sdkconfig
```
Expected:
```
CONFIG_APP_CLAW_CAP_HA_CONTROL=y
CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y
```

- [ ] **Step 2: `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS` 기본값 갱신**

`application/edge_agent/components/app_config/app_config.c:43` 의 매크로를 다음으로 교체:

```c
#define APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS "cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system"
```

(원래 빈 문자열 `""`였음. 이 default가 NVS에 빈 값일 때 fallback으로 들어간다 — `app_config.c:70`의 `APP_CONFIG_FIELD(llm_visible_cap_groups, "vis_cap_groups", APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS)` 를 통해.)

기존 보드는 NVS에 빈 값 또는 v2 default를 들고 있을 수 있으므로 erase-flash 후 새 default가 적용된다는 점을 Task 14 step 1에서 확인.

`sdkconfig`/`sdkconfig.defaults`에 vis_cap_groups 관련 키가 들어있으면 그쪽도 같이 정리(이전 시도의 잔재 방지):

```bash
grep -nE "vis_cap_groups|VISIBLE_CAP_GROUPS" application/edge_agent/sdkconfig*
```
Expected: 결과 없음. 있으면 라인 삭제.

- [ ] **Step 3: 빌드**

```bash
cd application/edge_agent
idf.py build
```
Expected: 성공. `app_config.o`가 새 매크로 값을 박은 채 link 됨.

- [ ] **Step 4: commit**

```bash
git add application/edge_agent/sdkconfig.defaults \
        application/edge_agent/components/app_config/app_config.c
git commit -m "$(cat <<'EOF'
config(cap_ha_control): enable cap_ha_control + boot-fetch + vis_cap_groups default

sdkconfig.defaults pins APP_CLAW_CAP_HA_CONTROL=y and
CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y so a fresh build/flash brings the
new tool online. APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS becomes
'cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system' so the LLM
sees only the typed tool surface, not raw cap_lua / cap_mcp_client.

ha_url/ha_token input is intentionally console-only in v3
(`ha_control --set-url` / `--set-token`); setup wizard integration is
deferred to v4 because the wizard's app_config namespace and
cap_ha_http_*'s 'ha_ctl' namespace need a deliberate adapter, not a
field-add that would silently desync.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: v2 cleanup — 4 lua + 4 skill md + skills_list 4 entry + demo_secrets 삭제

**Files:**
- Delete: `application/edge_agent/main/lua_scripts/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.lua`
- Delete: `application/edge_agent/main/skills/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.md`
- Delete: `application/edge_agent/main/lua_scripts/builtin/demo_secrets.lua` (if present)
- Modify: `application/edge_agent/main/skills/skills_list.json`
- Modify: `.gitignore`

문맥: spec § 14 — v3 ship 시점에 v2 path 완전 제거. cap_ha_control이 같은 동작을 cover.

- [ ] **Step 1: 파일 삭제**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
rm -f application/edge_agent/main/lua_scripts/bathroom_light_on.lua \
      application/edge_agent/main/lua_scripts/bathroom_light_off.lua \
      application/edge_agent/main/lua_scripts/rgb_purple_on.lua \
      application/edge_agent/main/lua_scripts/rgb_purple_off.lua \
      application/edge_agent/main/skills/bathroom_light_on.md \
      application/edge_agent/main/skills/bathroom_light_off.md \
      application/edge_agent/main/skills/rgb_purple_on.md \
      application/edge_agent/main/skills/rgb_purple_off.md
# demo_secrets.lua 추적 안 됐어도 working tree에 있으면 제거 (gitignored)
rm -f application/edge_agent/main/lua_scripts/builtin/demo_secrets.lua
rm -f application/edge_agent/fatfs_image/scripts/builtin/demo_secrets.lua
```

- [ ] **Step 2: `skills_list.json`에서 4개 entry 제거 (surgical patch)**

전체 교체는 위험하다 — 다른 사용자/branch가 추가한 skill entry를 날릴 수 있다. 다음 4개 id만 제거하는 JSON 패치 형태로 처리:
- `bathroom_light_on`
- `bathroom_light_off`
- `rgb_purple_on`
- `rgb_purple_off`

Python으로 in-place patch (다른 entry는 보존):

```bash
python3 - <<'PY'
import json, pathlib
p = pathlib.Path("application/edge_agent/main/skills/skills_list.json")
data = json.loads(p.read_text(encoding="utf-8"))
remove = {"bathroom_light_on", "bathroom_light_off", "rgb_purple_on", "rgb_purple_off"}
before = len(data.get("skills", []))
data["skills"] = [s for s in data.get("skills", []) if s.get("id") not in remove]
removed = before - len(data["skills"])
p.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"removed {removed} entries (expected 4); remaining {len(data['skills'])}")
PY
```
Expected: `removed 4 entries (expected 4); remaining N` (N >= 1 — 최소 `weather_search`는 남음). 결과가 `removed 4`가 아니면 working tree 상태가 예상과 다르다 — 중단 후 git diff 확인.

- [ ] **Step 3: `.gitignore`에서 demo_secrets.lua 라인 제거**

```bash
grep -n "demo_secrets" .gitignore
```

해당 라인(들)을 직접 편집해 삭제. 보통 다음 두 줄:
```
# Demo secrets — never committed
application/edge_agent/fatfs_image/scripts/builtin/demo_secrets.lua
```
(또는 v2가 main 경로로 옮긴 라인.) 두 줄 모두 제거.

- [ ] **Step 4: 빌드 + manifest 검증**

```bash
cd application/edge_agent
idf.py build
cat build/component_skills_manifest.json | python3 -m json.tool 2>/dev/null | grep -E "bathroom|rgb_purple" | head
cat build/component_builtin_lua_manifest.json | python3 -m json.tool 2>/dev/null | grep -E "bathroom|rgb_purple|demo_secrets" | head
ls fatfs_image/scripts/builtin/ 2>/dev/null | grep -E "(bathroom|rgb_purple|demo_secrets)"
ls fatfs_image/skills/ 2>/dev/null | grep -E "(bathroom|rgb_purple)"
```
Expected: 모든 grep 결과 비어있음. v2 path 완전 제거.

- [ ] **Step 5: commit**

```bash
git add -A application/edge_agent/main/lua_scripts/ \
        application/edge_agent/main/skills/ \
        .gitignore
git commit -m "$(cat <<'EOF'
chore(v2-cleanup): remove bathroom_light + rgb_purple lua skills + demo_secrets

cap_ha_control v3 owns the bathroom light and onboard RGB paths now;
the v2 lua skills (bathroom_light_on/off, rgb_purple_on/off) and their
matching skill markdown / skills_list entries are gone. demo_secrets.lua
is also removed (token now lives in NVS); the .gitignore entry that
hid it is dropped to avoid an orphaned rule.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Build + flash + boot 로그 검증 + erase-flash + NVS 설정

**Files:** (none — 보드 작업)

문맥: 단일 build/flash로 인프라가 보드에 도달. erase-flash로 v2 잔재 NVS 정리 → wizard로 Wi-Fi/LLM/Telegram → 콘솔로 `ha_control --set-url/--set-token` (Task 12 결정: ha_url/ha_token은 v3 wizard에 없음). vis_cap_groups는 Task 12에서 박은 default가 자동 적용.

- [ ] **Step 1: 풀 erase + flash**

```bash
cd application/edge_agent
source ~/.gstack/projects/esp-claw/secrets.env
idf.py -p "$ESP_PORT" erase-flash
idf.py -p "$ESP_PORT" flash monitor
```

- [ ] **Step 2: 부팅 로그 검증**

monitor에서 다음 로그 라인을 확인:
- `Registered capability group: cap_ha_control`
- `cap_ha_resolve: loaded N static entities` (N >= 4)
- `cap_ha_resolve: loaded ... cached entities from NVS` 또는 `boot-fetch failed: ESP_ERR_NVS_NOT_FOUND` (정상 — 첫 부팅)
- `Registered capability group: cap_skill`
- `Registered capability group: cap_im_tg`

기대치 외:
- `cap_lua` / `cap_mcp_client` 등록은 보이되 vis 출력에는 빠져 있어야 함 (Task 12 vis_cap_groups 확인).

- [ ] **Step 3: SetupWizard 진입 + 콘솔 ha_url/ha_token 입력 (분리)**

웹 SetupWizard에서 (Wi-Fi/LLM/Telegram만):
- Wi-Fi: `hyodol_practice` (또는 사용자)
- LLM provider/profile: `openai`, model `gpt-5-mini`, key `OPENAI_API_KEY`
- Telegram bot token: `TELEGRAM_BOT_TOKEN`
- vis_cap_groups: 빈 값으로 두면 Task 12에서 박은 default(`cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`)가 자동 적용. (또는 명시적으로 같은 값 입력해 NVS에 박아도 됨.)

Wizard에는 ha_url/ha_token 필드가 v3에서 의도적으로 없다 (Task 12 결정). 모니터 콘솔(USB)에서 직접:

```
ha_control --set-url http://192.168.1.94:8123
ha_control --set-token <new_token>
```

각각 `set_url: ESP_OK` / `set_token: ESP_OK` 출력 확인. 두 값은 NVS namespace `ha_ctl` 의 `ha_url` / `ha_token`에 저장된다 (cap_ha_http_*가 직접 read하는 경로).

- [ ] **Step 4: 재부팅 + boot-fetch 동작 확인**

```bash
# monitor 안에서 보드 reset 또는 EN 버튼.
```

monitor 로그에 `boot-fetch: kept N entities, NVS store=ESP_OK` 보이면 v1 tier 동작 OK. HA 미접속이면 `boot-fetch failed: ...` 라인 + 정적 registry로 계속 fallback.

---

## Task 15: 콘솔 단독 검증 (spec § 11 unit-ish 1–7)

**Files:** (none — 행동 검증)

문맥: cap_ha_control이 LLM 없이도 정확히 동작하는지 확인. 실패 패턴은 코드 문제 → 직전 task로 회귀 디버그.

- [ ] **Step 1: 정상 light turn_on**

```
ha_control --call '{"target":"화장실 조명","action":"turn_on","brightness_pct":60,"color":"yellow"}'
```
Expected: `{"success":true,"message":"화장실 조명 yellow 60%을(를) 켰습니다.","entity_id":"light.smart_bulb","raw_status":200}` (정확한 메시지는 compose 함수 출력에 따름) + 실제 램프 ON.

- [ ] **Step 2: target 누락 reject**

```
ha_control --call '{"action":"turn_on"}'
```
Expected: `success:false`, message에 `"target/action 누락"`.

- [ ] **Step 3: 존재하지 않는 target reject + 후보**

```
ha_control --call '{"target":"존재하지 않는 기기","action":"turn_on"}'
```
Expected: `success:false`, message에 후보 (`화장실 조명, 거실 커튼, 거실 콘센트, 보드 RGB`).

- [ ] **Step 4: cover ↔ turn_on 부조합**

```
ha_control --call '{"target":"거실 커튼","action":"turn_on"}'
```
Expected: `success:false`, message에 `"해당 동작을 지원하지 않습니다"`.

- [ ] **Step 5: cover.close + brightness silent drop**

```
ha_control --call '{"target":"거실 커튼","action":"close","brightness_pct":50}'
```
Expected: `success:true`, message `"거실 커튼을(를) 닫았습니다."`. monitor 로그에 `does not support brightness; dropping` 보임. (HA에 실제 cover entity 없으면 success:false — 시연 환경에 거실 커튼이 없으면 step 5는 환경 의존.)

- [ ] **Step 6: 보드 RGB 보라색**

```
ha_control --call '{"target":"보드 RGB","action":"turn_on","color":"purple"}'
```
Expected: `success:true`, message `"보드 RGB를 purple 켰습니다."` + 실제 LED 보라.

```
ha_control --call '{"target":"보드 RGB","action":"turn_off"}'
```
Expected: `success:true` + LED OFF.

- [ ] **Step 7: resolve 디버그**

```
ha_control --resolve "화장실 등"
```
Expected: cascade stage 3 (정규화 일치)로 `light.smart_bulb` 매칭.

```
ha_control --refresh-registry
```
Expected: `refresh_from_ha: ESP_OK` (HA 가용 시).

---

## Task 16: Telegram 자연어 E2E (single + multi-round + false-success block)

**Files:** (none — 행동 검증)

문맥: spec § 11 — LLM이 typed payload를 정확히 만들고, 멀티라운드에서 target 누락 0회, message verbatim echo 100%.

- [ ] **Step 1: 단일 라운드 시나리오 (`/start` 새 세션)**

Telegram에서 `/start` 후:
1. "화장실 조명 켜줘" → cap_ha_control 호출 + 램프 ON + 정확한 한국어 응답
2. "화장실 60%로 좀 노란빛으로" → brightness/color attached + ON
3. "화장실 꺼" → OFF
4. "보드 LED 보라색" → 보드 LED 보라
5. "보드 LED 꺼" → OFF
6. "거실 커튼 닫아줘" → curtain close (HA에 entity 있으면)

각 응답이 cap의 message 텍스트와 일치하는지 확인 (LLM이 자체 합성 한 줄 추가하지 않음).

- [ ] **Step 2: 멀티라운드 안정성 — 1세션 20회 mixed**

같은 세션에서 1–6 시나리오 무작위 반복 + 새 표현 ("화장실 등 좀", "보라색으로 켜줘", "조명 좀 어둡게 30%로"). Expected: 18/20 이상 (90%) 모든 axis 통과.

성공 axis (3개 모두 만족 시 1회 success):
- (a) ha_control 호출 (다른 tool 우회 없음)
- (b) 실제 동작 (램프/LED/커튼)
- (c) LLM 응답 == cap message (자체 합성 0회)

- [ ] **Step 3: 거짓 성공 차단 검증**

monitor에서 강제 fail 유도:

```
ha_control --set-token invalid_token
```

다시 Telegram에서 "화장실 조명 켜줘". Expected: cap이 401을 받아 `success:false, message:"HA 인증에 실패했습니다 (토큰 확인 필요)."` 반환. **LLM 응답이 그 message를 verbatim — "켰습니다" 자체 합성 0회.** 이게 v3의 핵심 차이점 검증.

복구:
```
ha_control --set-token <real_token>
```

- [ ] **Step 4: 실패 시 디버그 가이드**

- LLM이 다른 tool 호출 → vis_cap_groups 확인 (`cap_lua` / `cap_mcp_client`이 visible 아닌지 보드 console로 점검). system prompt에 "ha_control only" 라인 보존됐는지 cap_llm_inspect로 마지막 요청 dump.
- LLM이 message를 paraphrase → APP_SYSTEM_PROMPT_COMMON의 verbatim 라인 확인. 강화 필요 시 한국어 강제 (`"...message 그대로 답하라."`)로 추가.
- target 누락 회귀 → registry friendly_name이 description에 inject 됐는지 확인. tool description에서 active devices 리스트가 보이는지 cap_llm_inspect.
- 90% 미만 + 위 모두 OK → 모델 격상 (gpt-5-mini → gpt-5.4) 마지막 수단.

---

## Task 17: 최종 보안 audit + commit + learn log

**Files:**
- Create: `docs/learn/20260508-cap-ha-control-v3.md`

문맥: 토큰 누출 검사, v2 잔재 grep, learn log 기록.

- [ ] **Step 1: 토큰 누출 grep**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
git log --all -- "**/demo_secrets*"
git grep -E "Bearer [A-Za-z0-9]{20,}|eyJ[A-Za-z0-9_-]{20,}"
```
Expected:
- 첫 번째: 기존 commit 히스토리 일부에 demo_secrets.lua 변경이 보일 수 있으나 (예: v2 ship 시점), `git log --all -- "**/demo_secrets*" --since "2026-05-08"`로 새 commit엔 없는지 확인. 필요 시 `git secrets` 또는 `git filter-repo`로 history scrubbing은 별도 작업 (이 plan 범위 밖).
- 두 번째: 현재 working tree + index에서 토큰 패턴 0건. 0건이 아니면 commit 중단 + 즉시 정리.

- [ ] **Step 2: v2 path 잔재 grep**

```bash
git grep -lE "bathroom_light_on|bathroom_light_off|rgb_purple_on|rgb_purple_off" -- ':(exclude)docs/learn/' ':(exclude)smarthome-docs/'
```
Expected: 결과 없음 (또는 history 문서 외에 없음). 결과 있으면 해당 파일에서 추가 정리.

- [ ] **Step 3: learn log 작성**

`docs/learn/20260508-cap-ha-control-v3.md`:

```markdown
# cap_ha_control v3 — 작업 기록 (2026-05-08)

## 목표
v2의 거짓 성공 / multi-round arg drift / token 노출 / raw MCP 우회를 architecture 차원에서 차단하는 단일 typed tool 도입.

## 핵심 발상
LLM은 자연어 → typed payload 변환만, firmware가 validate / resolve / dispatch / 한국어 message 작성. LLM은 message verbatim echo. 거짓 성공을 합성할 표면 자체 없음.

## 적용된 변경
- 신규 `components/claw_capabilities/cap_ha_control/` (1개 capability `ha_control`).
- 정적 registry: `data/entities.default.json` (4 entries: 화장실 조명, 거실 커튼, 거실 콘센트, 보드 RGB).
- HA REST direct (POST /api/services + GET /api/states), Bearer NVS (`ha_ctl/ha_url`, `ha_ctl/ha_token`); URL 160B / token 4096B caller-provided buffer (v2 256B 제한을 token 한해 4096B로 확장; full removal은 v4 `_alloc` helper로 후속).
- Boot-fetch background task → /api/states → light/cover/switch 필터 → NVS `ha_ctl/entity_cache` blob.
- Onboard `board:onboard_rgb` 분기 (GPIO 48, color→HSV).
- Active registry friendly_names가 tool description에 inject — LLM fuzzy 매칭 책임.
- 단기 patch: cap_mcp_client에서 빈 arguments reject + APP_SYSTEM_PROMPT_COMMON에 verbatim echo + lua_*/mcp_call_tool 금지.
- v2 4 lua skill + skill md + demo_secrets.lua 삭제, .gitignore 라인 제거.
- vis_cap_groups 기본을 `cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`로.

## E2E 결과
- 단일 라운드 6/6 axis pass: <측정값>
- 멀티라운드 20회 success rate: <측정값>
- 거짓 성공 차단 검증 (token 무효화): <확인>
- 모델: gpt-5-mini 1차, system prompt 보강 필요 여부 / gpt-5.4 격상 여부 <기록>

## v2 architecture와 비교
- v2: argument-free 4 lua skill, LLM이 path만 선택 — 인자 표면 0이지만 entity 추가 = lua 4개 추가, 거짓 성공은 LLM 자체 합성에 의존.
- v3: typed payload 1개 tool, LLM이 인자 채움 + firmware schema reject + message verbatim — entity는 registry에 추가만, 거짓 성공은 architecture 차원 차단.

## 잔여 / 다음
- climate / fan / media_player / scene 도메인 → v4
- HA secure NVS storage (현재 평문) → v4
- 다중 entity 복합 명령의 부분 실패 처리 → v4
- Setup wizard ha_url/ha_token 필드 추가 + NVS namespace 통합 (`app_config` ↔ `ha_ctl`) → v4
- `cap_ha_http_get_url_alloc(char **out)` / `_token_alloc` 으로 caller-buffer 한계 완전 제거 → v4
```

`<측정값>`/`<확인>`/`<기록>`은 시연 후 직접 채움.

- [ ] **Step 4: learn log commit**

```bash
git add docs/learn/20260508-cap-ha-control-v3.md
git commit -m "docs(learn): cap_ha_control v3 작업 기록

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 5: 최종 git status 확인**

```bash
git status -s
git log --oneline -20
```
Expected: clean. Task 1–13 + 17 commit이 보임. demo_secrets / token 누출 없음.

---

## Self-Review

**1. Spec coverage:**
- § 1 Goal & 핵심 발상 → Task 1 (system prompt verbatim) + Task 3 (schema validate) + Task 11 (description inject). ✅
- § 2 Architecture → Task 2 (component scaffold) + Task 12 (vis_cap_groups). ✅
- § 3 Tool schema → Task 2 (descriptor) + Task 3 (validation). ✅
- § 4 Resolve cascade → Task 5 (3 stages) + Task 9 (static∪cache). ✅
- § 5 Service mapping + color table + supports drop → Task 7. ✅
- § 6 Registry v0+v1 → Task 4 (static embed) + Task 9 (boot-fetch + NVS cache). ✅
- § 7 HA endpoints + token storage → Task 6. ✅
- § 8 Onboard board branch → Task 8. ✅
- § 9 Result contract + verbatim echo → Task 1 (prompt) + Task 7 (compose) + Task 11 (description). ✅
- § 10 File plan → Task 2 + 12. ✅
- § 11 Test plan → Task 15 (console) + Task 16 (Telegram E2E). ✅
- § 12 Risk register → Task 6 (16KB cap) + Task 9 (NVS blob) + Task 14 (boot 로그 가드) + Task 16 (모델 격상 fallback). ✅
- § 13 Out of scope → Task 17 learn log 잔여. ✅
- § 14 Migration → Task 13. ✅
- § 15 Demo guardrails → Task 14 + Task 17. ✅

**2. Placeholder scan:**
- Task 12는 review revision 이후 console-only로 명확히 결정됨. wizard frontend 변경은 v3 범위 밖이며 placeholder도 없음.
- 그 외 모든 step에 실제 코드 / 명령 / expected output 포함.

**3. Type consistency:**
- `cap_ha_entity_t` 필드 (id/friendly_name/domain/supports_brightness/supports_color)는 internal.h에 정의 → resolve.c (parse_registry, lookup_in), core.c (dispatch), board.c (target check)에서 일관 사용.
- `cap_ha_color_to_rgb(color, int rgb_out[3])` 시그니처는 internal.h에 forward 선언 → core.c (table) + board.c (color_to_hsv)가 동일 시그니처로 호출. ✅
- NVS 키 매크로 (`CAP_HA_NVS_KEY_URL/TOKEN/CACHE`) — http.c와 resolve.c가 동일 macro 사용. ✅
- `cap_ha_resolve_top_candidates(out, size, max)` / `_active_friendly_names(out, size)` — cli.c와 cap_ha_control.c (description 합성)가 동일 시그니처. ✅
- `cap_ha_compose_success_message` / `_failure_message` 시그니처 internal.h에 박음 → core.c에서만 정의 + 사용. ✅

**4. Frequent commits:**
- Task 1–13에 각 1 commit, Task 14는 보드 작업 (no commit), Task 15–16은 행동 검증 (no commit), Task 17이 learn log + audit. 총 14개 commit. ✅

**5. TDD 적용도:**
- Firmware 특성상 unit test framework 부재 → 검증은 console + Telegram 행동 테스트. spec § 11 7개 console case가 Task 15에 1:1 매핑됨. 코드 작성 시점이 아닌 build/flash 시점에 검증되는 한계 — v2 plan과 동일한 trade-off.

큰 누락 없음. Task 12는 review revision 이후 console-only로 lock-in 됨; wizard ha_url/ha_token 필드 추가는 v4 (NVS namespace 통합과 함께).

---

## Execution Handoff

Plan v3 saved to `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task + 두 단계 리뷰. 빠른 반복.

**2. Inline Execution** — 이 세션에서 직접 진행, 체크포인트마다 리뷰.

어느 방식으로 갈지 알려줘.
