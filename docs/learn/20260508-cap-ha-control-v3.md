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

## E2E 결과 (Tasks 14–16 — 실 보드 필요, user 실행)

이 세션은 보드 / USB / Wi-Fi / HA / Telegram에 접근 못 함. 다음 단계는 user가 직접:

1. **Task 14 — flash + boot 검증**
   ```bash
   cd application/edge_agent
   source ~/.gstack/projects/esp-claw/secrets.env
   idf.py -p "$ESP_PORT" erase-flash
   idf.py -p "$ESP_PORT" flash monitor
   ```
   Wizard에서 Wi-Fi / LLM (openai gpt-5-mini + key) / Telegram bot token 입력. Boot 후 monitor에서 `Registered capability group: cap_ha_control` + `cap_ha_resolve: loaded N static entities` (N ≥ 4) 확인. 콘솔에서:
   ```
   ha_control --set-url http://192.168.1.94:8123
   ha_control --set-token <new_HA_token>
   ```
   각각 `set_url: ESP_OK` / `set_token: ESP_OK` 확인. 보드 reset 후 `boot-fetch: kept N entities, NVS store=ESP_OK` 로그 확인.

2. **Task 15 — console 단독 검증 (spec § 11 unit-ish 1–7)** — plan 2730–2787행 그대로 실행.

3. **Task 16 — Telegram NL E2E** — `/start` 후 6 시나리오, 멀티라운드 20회, false-success 차단 (invalid token 강제 후 verbatim "인증 실패" 확인) — plan 2799–2832행.

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
