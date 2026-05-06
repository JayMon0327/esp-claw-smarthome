# Day 0 Discovery Notes

Generated: 2026-05-06
Project: ESP-Claw CEO curiosity demo
Plan: /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/plans/2026-05-06-esp-claw-ceo-demo.md
Source of Truth (Design): /Users/wm-mac-01/.gstack/projects/esp-claw/wm-mac-01-unknown-design-20260506-120130.md
User board: ESP32-S3-WROOM-1 (N16R8) — 16MB Flash + 8MB PSRAM

## Task 1: ESP-Claw repo
- Clone target: /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
- Top-level dirs: application, components, docs, .gitlab (plus files: README.md, README_CN.md, CHANGELOG.md, LICENSE, .gitignore, .gitlab-ci.yml, .pre-commit-config.yaml, .git-blame-ignore-revs, codespell-ignore-list)
- README summary: Espressif's "Chat Coding" AI agent framework for IoT devices — defines device behavior through conversation and runs the full sense/decide/execute loop locally on ESP32-series chips (C reimplementation inspired by OpenClaw).
- Hardware boards mentioned in README: ESP32-S3-based dev boards including breadboards and M5Stack CoreS3. README quote: "ESP-Claw already supports multiple ESP32-S3-based development boards, including breadboards, M5Stack CoreS3, and more." Also mentions ESP32-P4 is supported via local builds. `application/edge_agent/boards/` contains: dfrobot, espressif (esp_box_3, esp_SensairShuttle, esp_vocat_board_v1_2, esp32_p4_eye, esp32_p4_function_ev, esp32_S3_DevKitC_1, esp32_S3_DevKitC_1_breadboard), lilygo, m5stack. ESP32-S3-WROOM-1 (user's module) is the standard module on `esp32_S3_DevKitC_1` — board entry is present.
- LLM providers mentioned: OpenAI (GPT), Anthropic (Claude), Alibaba Cloud Bailian (Qwen), DeepSeek, plus custom OpenAI-/Anthropic-style endpoints. README quote: "ESP-Claw now supports both OpenAI-style APIs and Anthropic-style APIs. It natively supports GPT models from OpenAI, Qwen models from Alibaba Cloud Bailian, Claude models from Anthropic, DeepSeek models from DeepSeek API, and also supports custom endpoints."
- IM/messaging mentioned: Telegram, QQ, Feishu, WeChat (extensible). README quote: "ESP-Claw supports Telegram, QQ, Feishu, and WeChat, and can be extended further."

## Task 2: 보드 지원 (사용자 보유 ESP32-S3-WROOM-1 N16R8)
- boards/ 전체 listing: dfrobot/{dfrobot_k10}, espressif/{esp_box_3, esp_SensairShuttle, esp_vocat_board_v1_2, esp32_p4_eye, esp32_p4_function_ev, esp32_S3_DevKitC_1, esp32_S3_DevKitC_1_breadboard}, lilygo/{lilygo_t_display_s3}, m5stack/{m5stack_cores3, m5stack_sticks3}
- 매칭 후보들: espressif/esp32_S3_DevKitC_1 (bare), espressif/esp32_S3_DevKitC_1_breadboard (full peripherals)
- 결정 보드 디렉토리: application/edge_agent/boards/espressif/esp32_S3_DevKitC_1 (bare WROOM-1 모듈에 가장 근접; breadboard 변형은 ST7789 LCD + USB UVC 카메라 + USB UAC 오디오 + I2S PDM 마이크 등 사용자가 보유하지 않은 하드웨어를 board_devices.yaml에 박제해 둠)
- sdkconfig.defaults 핵심 값 (sdkconfig.defaults.board, verbatim):
  - FLASHSIZE: `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` (8MB — N16R8는 16MB이므로 변경 필요)
  - SPIRAM_MODE: `CONFIG_SPIRAM_MODE_OCT=y` (OCT — N16R8와 일치, 변경 불필요)
  - SPIRAM_TYPE: not set (명시적 SPIRAM_TYPE_* 키 없음. SPIRAM=y + MODE_OCT + SPEED_120M로 N*R8 OCT PSRAM 8MB가 자동 검출됨)
  - 기타: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`, `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`, `CONFIG_ESPTOOLPY_FLASHFREQ_120M=y`, `CONFIG_SPIRAM_SPEED_120M=y`, `CONFIG_APP_CLAW_LUA_MODULE_LCD=y`
- 디스플레이/오디오 의존: no (하드웨어 의존 없음). 단 sdkconfig.defaults.board에 `CONFIG_APP_CLAW_LUA_MODULE_LCD=y`가 들어있어 app_claw가 lua_module_lcd 컴포넌트를 require 하지만 (components/common/app_claw/CMakeLists.txt:145), 이는 Lua 바인딩 모듈일 뿐 실제 LCD 하드웨어를 강제하지 않음. board_devices.yaml에 LCD/오디오/마이크/카메라 device가 없으므로 런타임 초기화도 없음. 빌드 깔끔히 하려면 `=n`으로 두는 것이 맞음.
- 분류: b
- 일정 영향: +0.3
- N16R8 적용 시 필요한 sdkconfig 변경:
  - `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` → `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` (필수)
  - 16MB 활용을 위한 커스텀 파티션 테이블 권장 (breadboard 변형이 partitions_8MB.csv를 쓰는 패턴 참고; N16R8용 partitions_16MB.csv 정의 필요할 수 있음)
  - `CONFIG_APP_CLAW_LUA_MODULE_LCD=y` → `CONFIG_APP_CLAW_LUA_MODULE_LCD=n` (선택, 사용자 보드에 LCD 없음 — 빌드 의존성 정리 차원)
  - SPIRAM_MODE_OCT, SPIRAM_SPEED_120M, FLASHMODE_QIO, FLASHFREQ_120M, CPU_FREQ 240MHz: 변경 없음 (N16R8 OCT와 일치)
- 결정 이유: 보드 엔트리는 존재하고 SPIRAM 설정도 OCT로 N*R8과 일치하지만, Flash 사이즈가 8MB로 박제되어 있어 사용자의 16MB(N16)에 맞춰 한 줄 교체 + 파티션 테이블 검토가 필요함. LCD/오디오 같은 누락 하드웨어 의존은 board_devices.yaml 레벨에 없어 LCD 분류(c)는 해당 안 됨.

## Task 3: Tool Registration + Provisioning/NVS

### Part A: Tool Registration

- 핵심 grep hits (top 5):
  - `components/claw_modules/claw_cap/include/claw_cap.h:120` — `esp_err_t claw_cap_register(const claw_cap_descriptor_t *descriptor);` / 121: `claw_cap_register_group(...)` (실제 tool 등록 진입점)
  - `components/claw_capabilities/cap_time/src/cap_time.c:349` — `static const claw_cap_descriptor_t s_time_descriptors[]` (정상적인 capability 정의 패턴)
  - `components/claw_capabilities/cap_lua/src/cap_lua.c:853` — `static const claw_cap_descriptor_t s_lua_descriptors[]` (`lua_run_script`, `lua_write_script` 등을 LLM-callable tool로 노출)
  - `components/claw_capabilities/cap_mcp_client/src/cap_mcp_client.c:236` — `mcp_list_tools` / `mcp_call_tool` / `mcp_discover` capabilities (외부 MCP 서버 호출 경로)
  - `components/common/app_claw/app_capabilities.c:536` — `s_capability_group_entries[]` (Kconfig `CONFIG_APP_CLAW_CAP_*`로 켜고 끄는 group whitelist; 빌드 시 결정)

- README/docs guidance: README에 tool 등록 자체에 대한 절차 문서는 없음. 단, 핵심 컨셉에 대한 verbatim 문구 두 개:
  - "💬 Chat as Creation: IM chat + dynamic Lua loading — Ordinary users can define device behavior without programming." (`README.md` Key Features 표)
  - "📤 MCP Communication: Supports standard MCP devices — Works as both Server and Client." (`README.md` Key Features 표)
  - 즉 신규 동작 추가는 (1) 펌웨어 빌드된 capability를 호출하는 Lua 스크립트 / (2) MCP 클라이언트로 외부 도구 호출, 두 가지를 1차 권장하는 흐름.

- Example tool 위치 (end-to-end 정독): `components/claw_capabilities/cap_time/src/cap_time.c` (cap_time.c:1-440)
  - 구조: `claw_cap_descriptor_t` 정적 배열 → `claw_cap_group_t` → `cap_time_register_group()` → `app_capabilities.c`의 entry table에서 `CONFIG_APP_CLAW_CAP_TIME` 가드 하에 호출.

- Schema 선언 문법 예시 (cap_time.c:349-360, verbatim):
  ```c
  static const claw_cap_descriptor_t s_time_descriptors[] = {
      {
          .id = "get_current_time",
          .name = "get_current_time",
          .family = "system",
          .description = "Return formatted current local time",
          .kind = CLAW_CAP_KIND_CALLABLE,
          .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
          .input_schema_json = "{\"type\":\"object\",\"properties\":{}}",
          .execute = cap_time_execute,
      },
  };
  ```
  - 즉 JSON Schema는 **C string literal로 박제**되며, descriptor 배열은 컴파일 타임에 결정됨.

- Handler signature (claw_cap.h:62-65, verbatim):
  ```c
  typedef esp_err_t (*claw_cap_execute_fn)(const char *input_json,
                                           const claw_cap_call_context_t *ctx,
                                           char *output,
                                           size_t output_size);
  ```
  - 입력은 JSON 문자열, 출력은 미리 잡힌 버퍼에 채워 반환. ctx는 session_id/channel/chat_id/source_cap을 담음.

- 등록 방식 (3단계, 모두 빌드 단계):
  1. C 파일에 `claw_cap_descriptor_t` 배열 + `cap_xxx_register_group()` 작성
  2. 컴포넌트 `idf_component.yml` + `CMakeLists.txt`에 `REQUIRES claw_cap claw_core` 추가
  3. `components/common/app_claw/app_capabilities.c`의 `s_capability_group_entries[]`에 `CONFIG_APP_CLAW_CAP_XXX` 가드와 함께 entry 추가 + 해당 Kconfig 옵션 정의
  - **자동 디스커버리 / require / manifest 등록 경로 없음**. 모두 명시적 C 등록 + 재빌드.
  - 단, 한 가지 우회로: cap_lua의 `lua_run_script`가 자체적으로 LLM-callable tool이고, `lua_module_call_capability`를 통해 Lua에서 등록된 capability를 호출할 수 있음. 즉 LLM은 새 tool 추가 없이도 "Lua 스크립트를 작성하라 → 그 스크립트가 cap_mcp_client.mcp_call_tool을 호출"이라는 우회 경로로 외부 IO를 할 수 있음.

- Lua bindings 사용 가능 (ha_call_service 관점에서 critical):
  - HTTP client: **NO** — `components/lua_modules/` 28개 모듈 listing(adc/audio/board_manager/button/call_capability/camera/delay/dht/display/environmental_sensor/esp_heap/event_publisher/fuel_gauge/gpio/i2c/imu/ir/knob/lcd/lcd_touch/led_strip/magnetometer/mcpwm/ssd1306/storage/system/touch/uart) 중 HTTP/네트워크 모듈이 **하나도 없음**. `app_lua_modules.c:425`의 entry table에도 http 항목 없음.
  - NVS bindings: **NO** — Lua `storage` 모듈은 존재하지만 FATFS 파일 R/W 바인딩 (`lua_module_storage_register(fatfs_base_path)`)이고 esp_nvs_* API를 노출하지 않음.
  - 우회 경로: `lua_module_call_capability`의 `capability.call(cap_name, payload, opts)` (lua_scripts/basic_capability_call.lua:67)을 통해 Lua에서 `cap_mcp_client.mcp_call_tool`을 호출하면 외부 MCP 서버를 거쳐 HTTP를 내보낼 수 있음. 즉 ha_call_service는 (a) 새 C-level cap_ha_call_service 컴포넌트 추가(rebuild 필요) 또는 (b) HA측에 MCP 서버를 띄우고 ESP-Claw가 `cap_mcp_client`로 그 MCP 도구를 호출 (rebuild 불필요), 두 가지 옵션이 있음.

- 옵션 분류: **B** (C handler + Kconfig + rebuild)
  - 근거: 모든 LLM-callable tool은 C에서 정적 `claw_cap_descriptor_t` 배열로 정의되고, `app_capabilities.c`의 group whitelist에 Kconfig 가드와 함께 추가되어야 함. JSON 매니페스트나 Lua-from-disk-based tool 등록 경로는 코드에 존재하지 않음.
  - 단, **B-lite 우회**: 외부 MCP 서버를 HA 사이드에 띄우면 `cap_mcp_client`(이미 컴파일 가능)만으로 ha_call_service 등가 동작이 가능. 이 경로는 펌웨어 rebuild가 아니라 HA 사이드에 MCP 어댑터(예: `home-assistant-mcp` 커뮤니티 서버 또는 Pi 위의 mcp-server-home-assistant)를 띄우는 작업으로 대체됨.

- 일정 영향:
  - 정통 옵션 B (자체 cap_ha_call_service C 컴포넌트 작성): **+1.5일** (Day 3에 1일 → 2.5일)
  - B-lite (HA에 MCP 서버 띄우고 cap_mcp_client만 사용): **0일** 추가, 단 HA 측 MCP 서버 셋업 0.5일이 별도 발생 (Pi/HA 작업이라 ESP 타임라인엔 영향 없음)
  - 권장: **B-lite를 1차 옵션**으로, 정통 B를 fallback으로 두는 하이브리드. Day 3 시작 시점에 HA에 MCP 어댑터가 있는지 우선 확인.

### Part B: Provisioning / NVS

- 사용 가능한 방법: **(b) 캡티브 포털 + AP 모드 (브라우저 폼)**, 단독 사용. BLE provisioning, UART CLI, menuconfig-only 경로는 모두 부재 또는 제한적.
  - BLE provisioning (a): **없음** — `wifi_prov_mgr` / `wifi_provisioning` API를 main/components에서 사용하지 않음 (grep 결과 0 hits). 
  - 캡티브 포털 (b): **있음** — `application/edge_agent/main/main.c:355`에서 `captive_dns_start()` 호출, AP IP에서 `http_server_init` + `http_server_start` 작동. Captive 404 핸들러로 DNS 모든 도메인을 AP IP로 redirect. (`application/edge_agent/components/http_server/http_server_core.c:24`, `:89`)
  - UART CLI (c): **부분적** — `register_wifi_command()` (main.c:387)와 `cmd_cap_skill.c`, `cmd_cap_lua.c` 등 esp_console 명령이 존재. WiFi cred는 console에서 설정 가능하지만 사용자 노출 절차로는 캡티브 포털이 1차.
  - menuconfig (d): Kconfig `APP_WIFI_SSID`/`APP_WIFI_PASSWORD`로 빌드 시 디폴트 박제 가능 (Kconfig.projbuild:5-15). 보조 수단.

- WiFi 셋업 절차 (사용자가 폰/노트북에서 수행):
  1. 보드 처음 부팅 → STA 연결 실패하면 SoftAP 폴백 (`application/edge_agent/main/main.c:368` STA 30초 대기 후 AP 폴백)
  2. 폰을 AP `esp-claw-XXXXXX`에 연결 (open, 비번 없음 — main.c:374 verbatim quote: `*** Provisioning portal: SSID="%s" (open) IP=%s URL=http://%s/ ***`)
  3. 캡티브 DNS가 모든 도메인을 AP IP로 → 브라우저가 자동으로 captive portal 띄움 (실패 시 수동으로 `http://<ap_ip>/` 접속, 일반적으로 192.168.4.1)
  4. 웹 폼에서 home Wi-Fi SSID/password + LLM API key + Telegram bot token 등 입력 → POST `/api/config` (`application/edge_agent/components/http_server/http_server_config_api.c:343-344`)
  5. POST `/api/restart`로 재부팅 → STA 모드로 home Wi-Fi 접속

- API key/token 입력 방법:
  - 같은 캡티브 포털 웹 페이지에서 모든 secret 한 번에 입력. NVS namespace `app` (settings_store: `app_config_init` → `namespace_name = "app"`)에 string으로 저장.
  - 저장되는 NVS 키 목록 (`app_config.c:47-73`, verbatim): `wifi_ssid`, `wifi_password`, `llm_api_key`, `llm_backend`, `llm_profile`, `llm_model`, `llm_base_url`, `llm_auth_type`, `llm_timeout_ms`, `llm_max_tokens`, `qq_app_id`, `qq_app_secret`, `feishu_app_id`, `feishu_secret`, `tg_bot_token`, `wechat_token`, `wechat_base_url`, `wechat_cdn_url`, `wechat_acct_id`, `brave_key`, `tavily_key`, `en_cap_groups`, `vis_cap_groups`, `en_lua_mods`, `time_timezone`.
  - **이 데모와 직접 관련**: `llm_api_key` (Anthropic key), `tg_bot_token` (Telegram), `en_cap_groups` (cap_im_tg, cap_lua, cap_mcp_client, cap_skill, cap_system, cap_time, cap_web_search 등 활성화), `vis_cap_groups` (LLM에 노출할 group whitelist), `wifi_ssid`/`wifi_password`.

- 구체 절차 한 줄 요약: "**보드 첫 부팅 → 폰을 AP `esp-claw-<MAC[3..5]>` (open)에 연결 → `http://192.168.4.1/` 캡티브 포털에서 home Wi-Fi + Anthropic key + Telegram bot token + en_cap_groups 한 번에 입력 → POST `/api/config` → `/api/restart`**"

## Task 4: Telegram Capability

- 코드 위치: `components/claw_capabilities/cap_im_tg/` (CMakeLists.txt + include/cap_im_tg.h + include/cmd_cap_im_tg.h + src/cap_im_tg.c + src/cmd_cap_im_tg.c + skills/skills_list.json + skills/cap_im_tg.md)
- Kconfig 활성화 옵션: **`CONFIG_APP_CLAW_CAP_IM_TG`** (정의 위치: `components/common/app_claw/Kconfig:61-66`, `bool "Enable Telegram capability"`, `default y`). cap_im_tg 자체에는 Kconfig가 없고 부모 `app_claw`의 Kconfig에서 게이트.
- 기본 활성화 여부 (DevKitC-1 sdkconfig): **yes** — `boards/espressif/esp32_S3_DevKitC_1/sdkconfig.defaults.board`에 Telegram 관련 override 없음 (grep 결과 0 hits) → Kconfig `default y` 그대로 적용. `application/edge_agent/sdkconfig.defaults`에도 override 없음.
- 비활성화 상태이면 활성화 방법: 기본 활성화이므로 별도 작업 불필요. 만약 끈 상태에서 켤 경우 menuconfig 경로 `(Top) → Application → Claw Application → Capabilities → Enable Telegram capability` (Kconfig.projbuild에서 menu hierarchy로 노출), 또는 `sdkconfig.defaults`에 `CONFIG_APP_CLAW_CAP_IM_TG=y` 추가.
- runtime 입력 필요 항목:
  - **NVS 키 `tg_bot_token`** (`app_config.c:62`, verbatim): Telegram BotFather 토큰. 비어있으면 `app_capabilities.c:318-321`에서 토큰 setter 호출을 skip하여 cap은 register 되지만 long-poll은 토큰 없이 작동 안함.
  - 그 외 Telegram 전용 추가 NVS 키 없음 (chat_id whitelist 등 별도 secret 불필요).
  - 첨부파일 root dir은 `paths->im_attachment_root` 자동 주입, max_inbound_file_bytes는 컴파일 상수 (`APP_IM_ATTACHMENT_MAX_BYTES`).
- en_cap_groups 항목: **`cap_im_tg`** (verbatim, `app_capabilities.c:544` & `:601`). cap group 식별자는 컴포넌트 디렉토리명과 동일. en_cap_groups 빈 문자열일 경우 `app_cap_build_group_map`이 `select_all_if_empty=true`로 호출되어 빌드된 모든 cap이 자동 활성 (`app_capabilities.c:677-682`) — 따라서 en_cap_groups를 비워둬도 cap_im_tg는 enabled.
- Telegram bot 사용 흐름 1줄 요약: "**HTTP long-poll (`https://api.telegram.org/bot<token>/getUpdates`, timeout=20s) 방식. poll task가 메시지 받으면 event_router로 inbound publish → LLM에 전달, tool_use 응답은 `tg_send_message` outbound binding (`app_claw.c:257`)으로 dispatch.**"
- 한국어 메시지 처리 우려 사항: **이상 없음** — `cap_im_tg.c:214`에서 `Content-Type: application/json`로 POST, 본문 직렬화는 cJSON (`#include "cJSON.h"`, `cap_im_tg.c:19`)이 담당하여 UTF-8 escape 자동 처리. Telegram Bot API는 JSON UTF-8 native이므로 한국어 송수신 정상. 폰트는 사용자 클라이언트(Telegram 앱) 책임이라 보드측 우려 없음.

## Task 5.5: Anthropic provider + tool_use dispatch

- Anthropic provider 파일 위치:
  - `components/claw_modules/claw_core/src/llm/backends/claw_llm_backend_anthropic.h` (10 lines, vtable accessor only)
  - `components/claw_modules/claw_core/src/llm/backends/claw_llm_backend_anthropic.c` (915 lines, full backend impl: init/chat/infer_media/deinit)
  - 등록 위치: `components/claw_modules/claw_core/src/llm/claw_llm_runtime.c:97-99` (`find_backend("anthropic") → claw_llm_backend_anthropic_vtable()`), 그리고 `claw_llm_runtime.c:63-74`에 `id="anthropic"`, `default_base_url="https://api.anthropic.com/v1"`, `chat_path="/messages"`, `supports_tools=true`, `supports_vision=true` profile 등록. Profile alias: `claw_llm_runtime.c:119-121` `"claude" → "anthropic"`. 빌드 포함: `components/claw_modules/claw_core/CMakeLists.txt:8` (Kconfig 게이트 없이 항상 컴파일).

- tool_use content_block 파싱 위치: `claw_llm_backend_anthropic.c:458-553` 함수 `parse_chat_response()`. 핵심 verbatim 발췌:
  - `:458` `cJSON_ArrayForEach(block, content) {`
  - `:459` `cJSON *type_json = cJSON_GetObjectItem(block, "type");`
  - `:470` `} else if (strcmp(type_json->valuestring, "tool_use") == 0) {`
  - `:526-541`: `dst = &out_response->tool_calls[tool_index]; ... dup_json_string(cJSON_GetObjectItem(block, "id"), &dst->id); ... dup_json_string(cJSON_GetObjectItem(block, "name"), &dst->name); dst_input = cJSON_GetObjectItem(block, "input"); ... input_json = cJSON_PrintUnformatted(dst_input); ... dst->arguments_json = input_json;`
  - 즉 Anthropic 응답의 `content[].type == "tool_use"` 블록을 정확히 인식하여 `id`/`name`/`input`을 추출, `claw_llm_response_t.tool_calls[]` 배열로 정상 변환. 텍스트 블록(`type == "text"`)도 별도 누적되어 `out_response->text`에 합쳐짐(`:464-503`). Conditional/Kconfig 게이트 없음 — 무조건 실행.

- 로컬 dispatch 함수 호출: `components/claw_modules/claw_core/src/claw_core.c:1000-1003` (provider-agnostic loop):
  ```
  err = claw_core_call_cap(response->tool_calls[i].name,
                           response->tool_calls[i].arguments_json,
                           request,
                           &tool_output);
  ```
  - 호출 시점: `claw_core.c:1267-1295` 메인 iteration 루프 — `llm_response.tool_call_count == 0`이면 plain-text finish, 아니면 `append_assistant_tool_calls() → append_tool_results_message()` 순으로 진행하며 후자가 `:1000`의 `claw_core_call_cap()`을 호출. 이후 `claw_cap` 모듈의 registry로 dispatch (`claw_cap.c:1659` `claw_cap_execute_fn execute = ...;`).
  - tool_result 회신: `claw_core.c:1028-1037`에서 OpenAI-style `{"role":"tool","tool_call_id":...,"content":...}` 메시지를 `runtime_messages` 배열에 append, 다음 iteration에서 `claw_llm_runtime_chat()` 재호출. Anthropic backend의 `convert_messages_to_anthropic()` (`claw_llm_backend_anthropic.c:212-316`)이 `role=="tool"` 메시지를 `:238-249`에서 `anthropic_make_tool_result_message()`로 변환하여 `{"role":"user","content":[{"type":"tool_result","tool_use_id":...,"content":...}]}` 형태로 Anthropic spec에 맞춰 재포장. 양방향 round-trip 완전 구현됨.

- OpenAI provider와의 parity: **full** — 둘 다 `claw_llm_backend_vtable_t` (`claw_llm_runtime.h:12-29`)를 구현하고 동일한 `claw_llm_response_t.tool_calls[]` (`claw_llm_types.h:67-78`)에 결과를 채움. OpenAI backend도 `claw_llm_backend_openai_compatible.c:139-181`에서 `message.tool_calls` 배열을 동일 구조체로 채움 (id/name/arguments_json). dispatch loop (`claw_core.c:1267+`)는 backend를 알지 못한 채 동작 — `runtime->backend->chat()` (`claw_llm_runtime.c:283`) 한 줄을 통해 vtable 방식으로 추상화됨. 즉 provider 교체는 NVS `llm_profile` / `llm_backend_type` 한 줄만 바뀌면 됨 (`app_claw.c:100-101`, `:270-271`).

- 분류: **(a) Full parity**

- LLM provider 결정: **Anthropic Claude Sonnet** (NVS `llm_profile=anthropic` or `llm_profile=claude`, `llm_backend_type=anthropic` (생략 가능, profile default가 anthropic), `llm_api_key=<sk-ant-…>`, `llm_model=claude-sonnet-…`, base_url 기본 `https://api.anthropic.com/v1`)

- 일정 영향: **0**

- 한국어 tool-calling 안정성 우려: 없음 — request body는 cJSON으로 직렬화되며 UTF-8 그대로 전송, Anthropic API는 JSON UTF-8 native. tool name/arguments는 ASCII identifier + JSON이라 인코딩 무관. 모델 자체의 한국어 의도→tool selection 품질은 Sonnet 4.x/4.5 수준에서 검증된 영역으로 추가 우려 없음.

### Self-Review (Task 5.5)

- Anthropic provider .c 파일 915줄 전체를 line-by-line 읽음 (parse_chat_response, build_chat_body, convert_messages_to_anthropic, convert_tools_to_anthropic, anthropic_chat, anthropic_infer_media, init/deinit, vtable accessor 모두 확인). grep만으로 답하지 않음.
- tool_use 파싱은 `parse_chat_response()` 안에 무조건 실행되는 ArrayForEach 루프 (`:458-473` 1차 카운트, `:515-552` 2차 추출). `#ifdef`/Kconfig/runtime flag 게이트 없음 확인.
- 분류 (a)는 데이터 기반: (1) 동일 vtable 인터페이스 (`claw_llm_runtime.h:12-29`), (2) 동일 결과 구조체 (`claw_llm_types.h:67-78`), (3) 동일 dispatch 호출지점 (`claw_core.c:1000` 한 곳에서 양 provider 모두 처리), (4) 동일 multi-round message replay (`convert_messages_to_anthropic` `:238-249`에서 OpenAI-style `role=tool` 메시지를 Anthropic-style `tool_result` 블록으로 자동 재포장).
- Edge case: Anthropic은 `tool_choice={"type":"auto"}`를 tools 배열이 비어있지 않을 때만 추가 (`:597-611`) — 안전. system prompt는 `body.system` 최상위 필드로 분리 송신 (`:581`) — Anthropic spec 준수.
