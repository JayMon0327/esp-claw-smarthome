# cap_ha_control v3 — 작업 기록 (2026-05-11)

## 목표
v2의 거짓 성공 / multi-round arg drift / token 노출 / raw MCP 우회를 architecture 차원에서 차단하는 단일 typed tool 도입.

## 핵심 발상
LLM은 자연어 → typed payload 변환만, firmware가 validate / resolve / dispatch / 한국어 message 작성. LLM은 message verbatim echo. 거짓 성공을 합성할 표면 자체 없음.

## 적용된 변경 (Tasks 1–13, 17 — code/config 완료)

- **Task 1 (commit `6a0db5c`, `b8655f6`):** 단기 patch — `cap_mcp_client.mcp_call_tool`이 빈 `arguments`를 cJSON-기반으로 reject, 응답 content 안의 실패 marker로도 `isError:true` 강제. `APP_SYSTEM_PROMPT_COMMON`에 verbatim echo + `ha_control`만 사용 + `lua_*`/`mcp_call_tool` 금지 라인. OOM guard + MEMORY_FULL seam 공백 수정 (review fixup).
- **Task 2 (commit `e2ab522`, `ed2280c`):** 신규 component `cap_ha_control` scaffold. 13 새 파일 + app_claw 4 wiring 지점 (Kconfig, CMakeLists.txt, idf_component.yml, app_capabilities.c with BOTH `s_capability_group_entries[]` AND `s_capability_group_infos[]`). Stub들에 NULL/zero-size guards 추가 (review fixup).
- **Task 3 (commit `f868906`, `db2accb`):** Schema validate + dispatch entry. target/action/brightness_pct/color/kelvin 파싱, 누락/enum 외 reject (한국어), board ↔ HA 분기. Board success path OOM fallback (review fixup).
- **Task 4 (commit `3e2adea`):** 정적 registry — `entities.default.json` 4 entries (화장실 조명/거실 커튼/거실 콘센트/보드 RGB), EMBED_TXTFILES + boot 시 parse. `_binary_entities_default_json_start/end` symbol 검증.
- **Task 5 (commit `91a89cb`):** Resolve cascade 3 stage — id exact → friendly_name exact → normalized (whitespace strip + 한국어 trailing particle 등/의/은/는/이/가/을/를/도 drop).
- **Task 6 (commit `d4b37c8`):** HA REST HTTP + NVS — `cap_ha_http_post_service` / `_get_states` (esp_http_client + Bearer + crt_bundle). NVS namespace `ha_ctl`의 `ha_url` (160B) + `ha_token` (4096B). Transport success = ESP_OK (caller checks 2xx/4xx/5xx) — `ESP_ERR_HTTP_CONNECT`로 401 메시지 가리지 않음.
- **Task 7 (commit `80c7e97`):** Service mapping — light(turn_on/off/toggle), cover(open/close/toggle → open_cover/close_cover/toggle), switch(turn_on/off/toggle). 부조합 reject. brightness_pct/color/kelvin attach with silent-drop + WARN when entity doesn't support. Color table + `#rrggbb`. Korean success/failure composer.
- **Task 8 (commit `c3d0766`):** `board:onboard_rgb` 분기 — GPIO 48, WS2812, RGB direct + brightness_pct scale (HSV round-trip 없음, lua_module_led_strip 패턴 일치). `espressif/led_strip ^3.0.3` 핀.
- **Task 9 (commit `d1586e7`):** Boot-fetch + NVS entity_cache — `cap_ha_resolve_init`이 NVS cache load + Wi-Fi 후 background task가 `/api/states` GET → light/cover/switch 필터 → NVS `ha_ctl/entity_cache` blob 저장. Resolve cascade가 static∪cache 검색 (static 우선, dedupe by id). `boot_fetch_task` forward decl을 파일 상단 `#if`-가드로 둠 — nested-extern 회피.
- **Task 10 (commit `6b83542`):** Console commands (argtable3) — `ha_control --call <json>` / `--resolve <target>` / `--refresh-registry` / `--set-url <url>` / `--set-token <token>`.
- **Task 11 (commit `25460ad`):** Active registry → tool description inject. `compose_description()`이 friendly_names를 description에 합성, `s_ha_descriptors`를 non-const로 두고 init 시점 patch. 부팅 cadence로 description 갱신.
- **Task 12 (commit `da38051`):** sdkconfig.defaults에 `CONFIG_APP_CLAW_CAP_HA_CONTROL=y` + `CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y` 핀. `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS = "cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system"` — LLM이 typed surface만 봄, raw `cap_lua` / `cap_mcp_client`은 enabled but invisible. ha_url/ha_token 입력은 console-only (wizard 통합은 v4의 NVS namespace 통합과 함께).
- **Task 13:** v2 cleanup. `worktree-develop` branch에는 v2 lua skill / demo_secrets 가 처음부터 없었음 (이 변경은 sibling branch `day1-n16r8-hpm-fix`의 commit `e5670e0`에 머무름) — develop에서는 no-op로 trivially 완료.

## 빌드 환경 발견 사항 (plan에 없던 회수)
- `.claude/worktrees/develop`는 첫 빌드에 필요한 gitignored 산출물(`application/edge_agent/components/gen_bmgr_codes/gen_*`, `application/edge_agent/sdkconfig` — 16MB flash size 포함)이 없어 main worktree에서 복사 후 빌드 (`memory/project_worktree_build_env.md` 참고).
- ESP-IDF의 `bmgr_patch.py`는 `gen_board_device_config.c`를 *생성*하지 않고 *patch*만 한다 — pre-existing 파일이 필요. board_manager codegen은 `idf.py build` 외부의 일회성 step.

## E2E 결과 (Tasks 14–15 on-board, 2026-05-11)

보드: ESP32-S3 N16R8 clone, GPIO 48 onboard RGB, USB-Serial-JTAG (CH343), `/dev/cu.usbmodem5AE70179831`, Wi-Fi `hyodol_practice` STA 192.168.1.106, HA `http://192.168.1.94:8123`.

**Task 14 — boot 검증**
- `idf.py flash` (erase 없이; 기존 wizard NVS 보존). 부팅 로그:
  - `cap_ha_resolve: loaded 4 static entities` ✓
  - `claw_cap: Configured 4 LLM-visible capability groups` (default 5개 entries 중 4개가 vis로 잡힘 — NVS의 stale 값. Telegram E2E 전에 정리 필요)
  - `cap groups` 콘솔에서 `cap_ha_control state=started descriptors=1` 확인 ✓
  - `cap list` 출력에 `ha_control [ha] ... Active devices (use these names verbatim in 'target'): 화장실 조명, 거실 커튼, 거실 콘센트, 보드 RGB. After this tool returns, respond to the user with the result 'message' field VERBATIM. Do not invent confirmation messages.` ✓ — Task 11 dynamic description 동작.
- `ha_control --set-url http://192.168.1.94:8123` → `set_url: ESP_OK` ✓
- `ha_control --set-token <token>` → `set_token: ESP_OK` ✓
- 보드 reset 후 boot-fetch 로그:
  - `GET http://192.168.1.94:8123/api/states`
  - `GET result err=ESP_OK status=200 resp_len=51868` (HA 응답 ~50KB, 64KB 버퍼 안에 fit ✓)
  - `boot-fetch: kept 2 entities, NVS store=ESP_OK` ✓ (HA에 등록된 light/cover/switch 중 enable 상태 2개)

**Task 15 — console verification (실측)**
| Step | 입력 | 결과 |
|---|---|---|
| 1. light turn_on (HA REST) | `ha_control --call {"target":"light.smart_bulb","action":"turn_on","brightness_pct":60,"color":"yellow"}` | `POST /api/services/light/turn_on body={"entity_id":"light.smart_bulb","brightness_pct":60,"rgb_color":[255,255,0]} status=200` → `{"success":true,"message":"화장실 조명 yellow 60%을(를) 켰습니다.","entity_id":"light.smart_bulb","raw_status":200}` — **실제 램프 ON** ✓ |
| 2. target 누락 reject | `{"action":"turn_on"}` | `{"success":false,"message":"요청 정보가 부족합니다 (target/action 누락).","entity_id":null,"raw_status":null}` ✓ |
| 3. invalid action | `{"target":"board:onboard_rgb","action":"blink"}` | `{"success":false,"message":"지원하지 않는 동작입니다 (action=blink).",...}` ✓ |
| 4. light turn_off | `{"target":"light.smart_bulb","action":"turn_off"}` | `POST status=200` → `{"success":true,"message":"화장실 조명을(를) 껐습니다.","entity_id":"light.smart_bulb","raw_status":200}` ✓ |
| 6. 보드 RGB purple | `{"target":"board:onboard_rgb","action":"turn_on","color":"purple"}` | `{"success":true,"message":"보드 RGB를 purple 켰습니다.","entity_id":"board:onboard_rgb","raw_status":null}` — **실제 LED purple** ✓ |
| 6. 보드 RGB red @ 50% | `{"target":"board:onboard_rgb","action":"turn_on","color":"red","brightness_pct":50}` | `{"success":true,"message":"보드 RGB를 red 50%로 켰습니다.","entity_id":"board:onboard_rgb","raw_status":null}` ✓ |
| 6. 보드 RGB off | `{"target":"board:onboard_rgb","action":"turn_off"}` | `{"success":true,"message":"보드 RGB를 껐습니다.",...}` ✓ |
| 7. resolve board:onboard_rgb | `ha_control --resolve board:onboard_rgb` | `resolve: id=board:onboard_rgb friendly=보드 RGB domain=board brightness=1 color=1` ✓ |

**Console 한국어 인자 한계**: `ha_control --resolve "화장실 조명"`이 NOT_FOUND를 돌려줌. esp_console (linenoise)이 따옴표 내부 공백을 args separator로 처리하는 듯. 영향은 console 디버그용 resolve뿐 — `--call` JSON 안의 friendly_name은 JSON parsing이므로 정상.

**Task 16 §3 — false-success block (early verify)**
- `ha_control --set-token invalid_token_xyz` → set OK
- `ha_control --call {"target":"light.smart_bulb","action":"turn_on"}` → `POST status=401 resp_len=17` → `{"success":false,"message":"HA 인증에 실패했습니다 (토큰 확인 필요).","entity_id":"light.smart_bulb","raw_status":401}` ✓
- Token 복구 → 정상 동작 즉시 회복 ✓

**On-board에서 발견된 2개 firmware 버그 (이 세션 내 수정 + 재flash)**
- `commit 32f527c`: `cap_ha_http_post_service` / `_get_states`의 `char auth_header[4128]`이 6KB boot-fetch task stack을 오버플로우. boot-fetch가 `GET /api/states` 직후 `Backtrace: ... |<-CORRUPTED` + `rst:0xc (RTC_SW_CPU_RST)` 무한 reboot 루프. heap으로 옮겨 해결.
- `commit df783ea`: esp_http_client가 HTTP 401에 `ESP_ERR_NOT_SUPPORTED`를 반환 — `cap_ha_compose_failure_message`의 분기 순서가 http_err를 먼저 검사해 "network err=..."로 잘못 잡았음. http_status==401/403을 최우선으로 재정렬해 "HA 인증에 실패했습니다" 메시지 회복. 이 fix가 v3의 핵심 데모 guardrail — invalid token → LLM이 verbatim "인증 실패"로 응답 → false success 합성 차단.

**남은 Task 16 — Telegram NL E2E (user 실행)**
보드 firmware는 final 상태로 flash 완료. Telegram client에서 메시지를 보내야 검증 가능 (CLI 접근 불가):
1. `/start` 새 세션 → "화장실 조명 켜줘" / "화장실 60%로 노란빛" / "보드 LED 보라색" 등 6 시나리오 — message verbatim echo 확인.
2. 1 세션 20회 mixed — success rate 90%+ 확인.
3. invalid token → "인증 실패" verbatim 확인 (firmware 측은 이미 검증됨).
4. **사전 점검**: 보드 콘솔에서 `cap list` 출력에 `mcp_call_tool` / `mcp_list_tools` / cap_lua tools가 LLM-visible로 나오면 `app_config` NVS의 `vis_cap_groups`를 `cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`로 명시 설정 (현재 NVS에 v2 시점 값이 남아 있어 새 default가 적용 안 됨).

## v2 architecture와 비교
- v2: argument-free 4 lua skill, LLM이 path만 선택 — 인자 표면 0이지만 entity 추가 = lua 4개 추가, 거짓 성공은 LLM 자체 합성에 의존.
- v3: typed payload 1개 tool, LLM이 인자 채움 + firmware schema reject + message verbatim — entity는 registry에 추가만, 거짓 성공은 architecture 차원 차단.

## 빌드 환경 보안 audit (Task 17)
- `git grep "Bearer [A-Za-z0-9]{20,}"`/`"eyJ[A-Za-z0-9_-]{20,}"`: 0 hits ✓
- `git grep` v2 path 잔재 (docs/learn, smarthome-docs 제외): 0 hits ✓
- `git ls-tree HEAD demo_secrets.lua`: 부재 ✓

## 잔여 / 다음
- Tasks 14–16: 실 보드 검증 (user 실행).
- climate / fan / media_player / scene 도메인 → v4
- HA secure NVS storage (현재 평문) → v4
- 다중 entity 복합 명령의 부분 실패 처리 → v4
- Setup wizard ha_url/ha_token 필드 추가 + NVS namespace 통합 (`app_config` ↔ `ha_ctl`) → v4
- `cap_ha_http_get_url_alloc(char **out)` / `_token_alloc` 으로 caller-buffer 한계 완전 제거 → v4

## Review 인사이트
Plan을 따라 implementation 중 발견한 plan-vs-code mismatch:
- `claw_cap_execute_fn` 시그니처가 plan에서 `(descriptor, input_json, output, output_size, user_ctx)`였으나 실제 `claw_cap.h:62-65`는 `(input_json, const claw_cap_call_context_t *ctx, output, output_size)` — `cap_mcp_client.c:74-77` 패턴과 일치하도록 채택.
- Plan stub의 `cap_ha_control_http.c`에 `ESP_ERR_NVS_NOT_FOUND`만 사용하면서 `nvs.h` include 빠짐 — Task 2에서 발견, 추가.

이런 종류의 mismatch는 plan-reviewer 2회 통과 후에도 남는다 — 실제 컴파일/실행 시점에만 잡히는 layer.
