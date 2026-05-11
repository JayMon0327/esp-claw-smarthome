# cap_ha_control Typed Tool — Design (v3)

**Status.** Brainstorming approved (2026-05-08). Implementation plan to be written via `superpowers:writing-plans` from this design.

**Inputs.**
- 사용자 spec 제출: `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool-spec.md`
- v2 plan (shipped): `smarthome-docs/superpowers/plans/2026-05-07-bathroom-light-and-rgb-mvp.md`
- v2 시연 결과: `docs/learn/20260508-bathroom-rgb-demo-result-and-rgb-gpio.md`

---

## 1. Goal & 핵심 발상

자연어 스마트홈 제어를 LLM의 자유도(자연어 → 인자 추론)와 firmware의 통제(검증 가능한 단일 실행 path)로 분리한다.

- **LLM의 책임**: 사용자 한국어 발화를 typed payload (`target/action/brightness_pct/color/kelvin`)로 변환.
- **Firmware의 책임**: payload validate → entity resolve → HA REST 또는 onboard board 분기 → 실행 결과로 사용자 응답 문구를 생성. **사용자에게 보일 한 줄 한국어 message는 cap_ha_control이 만든다. LLM은 verbatim echo만.**

이 분리가 v2의 두 결함을 동시에 차단한다.
1. v2의 multi-round arg drift — `arguments`를 LLM이 채우다 빈 채로 흘러 HA가 애매하게 실패. v3는 `target/action`을 schema validate 단계에서 즉시 reject.
2. v2의 거짓 성공 — RGB GPIO 38로 driver는 OK 떨어뜨렸지만 실물 미점등인데 LLM이 "켰습니다" 응답. v3는 message가 cap에서 만들어지므로 LLM이 거짓 응답을 합성할 표면 자체 없음.

## 2. Architecture

**컴포넌트.** 신규 단일 컴포넌트 `components/claw_capabilities/cap_ha_control/`. claw_cap descriptor 1개 (`ha_control`)만 LLM-visible.

**LLM-visible cap_groups (NVS `vis_cap_groups`).** `cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`.

**평소 LLM 비노출**: `cap_mcp_client`, `cap_lua`, `cap_lua_run`, `cap_lua_edit`. 등록은 enabled로 두되 vis에서 제외 — LLM이 raw MCP / Lua 우회 path 만들 수 없음.

**HA 통신.** REST API 직접 — POST `/api/services/<domain>/<svc>` (action), GET `/api/states` (boot-fetch registry). Bearer는 NVS `ha_token`. cap_mcp_client 기반 MCP 경로 사용 안 함.

**상위 데이터 흐름.**

```
사용자(자연어)
  → LLM (typed payload 작성)
    → cap_ha_control_execute(input_json)
       1. parse → {target, action, brightness_pct?, color?, kelvin?}
       2. validate (target/action 누락 시 reject)
       3. resolve(target) → entity (board:* | HA entity_id) | reject
       4. branch:
          - "board:*"     → board driver (led_strip)
          - HA entity     → POST /api/services/<entity.domain>/<svc> + service_data
       5. compose human Korean message (firmware-controlled)
       6. return {success, message, entity_id?, raw_status?}
  ← LLM이 message를 verbatim echo (system prompt 강제)
```

## 3. `ha_control` Tool Schema

**LLM-visible input schema** (`claw_cap_descriptor.input_schema_json`):

```json
{
  "type": "object",
  "properties": {
    "target":         {"type": "string", "description": "Korean friendly name from active registry, or board:<slug>. Required."},
    "action":         {"type": "string", "enum": ["turn_on","turn_off","toggle","open","close"]},
    "brightness_pct": {"type": "integer", "minimum": 1, "maximum": 100},
    "color":          {"type": "string", "description": "Korean/English color name (yellow/red/purple/...) or '#rrggbb'"},
    "kelvin":         {"type": "integer", "minimum": 2000, "maximum": 6500}
  },
  "required": ["target", "action"]
}
```

**Description**에 다음 내용 박음 (LLM tool selection 안내):

- `ha_control`은 모든 스마트홈 제어의 단일 진입점. light/cover/switch + onboard board RGB.
- Active registry friendly_name 리스트 (runtime 갱신).
- "Result `message` field는 verbatim 응답하라. 자체 확인 문장 만들지 말 것."

**Schema validation 규칙 (cap_ha_control_core.c).**
- `target` 누락/빈 → reject
- `action` 누락/enum 외 → reject
- `brightness_pct` 범위 외 → reject
- domain/action 부조합 (e.g. cover + brightness_pct) → service mapping 단계에서 reject

## 4. Target Resolve (cascade 3 stages)

```
resolve(target):
  if target startswith "board:" → BOARD_BRANCH(target)

  // HA registry lookup, active = static ∪ NVS_cache
  for entry in active_registry:
    if entry.id == target            → match (entity_id)
    if entry.friendly_name == target → match (exact friendly_name)

  // Normalized retry — strip whitespace, drop trailing 조사 (등/은/는/이/가)
  norm = normalize(target)
  for entry in active_registry:
    if normalize(entry.friendly_name) == norm → match

  return reject  // top-5 candidates included in message
```

**Fuzzy matching 책임 분리 (concern #5).** 한국어 fuzzy 의미 매칭은 **LLM** 책임. cap_ha_control은 정확 매칭 + 단순 정규화만 담당. 이를 위해 **active registry friendly_name 리스트는 system prompt 또는 tool description에 inject** — LLM이 보고 정확한 friendly_name을 `target`으로 채워 호출.

**Reject message format.** `"\"<target>\"에 해당하는 기기를 찾지 못했습니다. 사용 가능한 기기: <상위 5개>."` LLM이 verbatim echo하면 사용자가 즉시 어떤 이름을 써야 하는지 안다.

## 5. Service Mapping (action → HA service + data)

| domain | action | HA service | service_data |
|---|---|---|---|
| `light` | `turn_on` | `light.turn_on` | `entity_id` + (`brightness_pct`, `color_name`/`rgb_color`, `kelvin`) optional |
| `light` | `turn_off` | `light.turn_off` | `entity_id` |
| `light` | `toggle` | `light.toggle` | `entity_id` |
| `cover` | `open` | `cover.open_cover` | `entity_id` |
| `cover` | `close` | `cover.close_cover` | `entity_id` |
| `cover` | `toggle` | `cover.toggle` | `entity_id` |
| `switch` | `turn_on` | `switch.turn_on` | `entity_id` |
| `switch` | `turn_off` | `switch.turn_off` | `entity_id` |
| `switch` | `toggle` | `switch.toggle` | `entity_id` |
| `board` | `turn_on` | (internal: led_strip set_pixel_hsv + refresh) | `color` (HSV 변환), `brightness_pct` (V scale, default 100) |
| `board` | `turn_off` | (internal: led_strip clear + refresh) | — |

**Color 매핑 테이블** (cap_ha_control 내부 상수). `light` 도메인은 `rgb_color` 우선 (deterministic, HA UI 컬러 변동 영향 없음). `#rrggbb`도 동일하게 rgb_color로 변환. `board` 도메인은 firmware에서 직접 HSV 변환:

| color | rgb_color | HSV (board path) |
|---|---|---|
| yellow | [255, 255, 0] | (60, 255, 255) |
| red | [255, 0, 0] | (0, 255, 255) |
| green | [0, 255, 0] | (120, 255, 255) |
| blue | [0, 0, 255] | (240, 255, 255) |
| purple | [128, 0, 255] | (270, 255, 255) |
| white | [255, 255, 255] | (0, 0, 255) |
| `#rrggbb` | parsed | RGB→HSV |

`brightness_pct` → board path V scale: V = round(255 * pct / 100).

**불일치 처리.**
- *action ↔ domain 부조합* (e.g. `target=거실 커튼` + `action=turn_on`): `"<friendly_name>은(는) 해당 동작을 지원하지 않습니다. (지원: open/close/toggle)"` reject.
- *data field ↔ entity.supports 부조합* (e.g. `cover` + `brightness_pct=50`): 지원 외 data field는 silent drop + WARN 로그. 핵심 action은 그대로 진행. LLM이 옵션을 ambitiously 채워도 전체 호출이 실패하지 않게.

## 6. Entity Registry

**2-tier storage.**

- **Tier v0 (정적 source of truth)**: `components/claw_capabilities/cap_ha_control/data/entities.default.json`. Build sync 통해 fatfs `data/ha/entities.default.json`로 복사. 사용자가 수동 5–10개 entry 등록.
- **Tier v1 (boot-fetch 캐시)**: 부팅 후 Wi-Fi up되면 background task가 `GET /api/states` 1회 호출. `light.*`/`cover.*`/`switch.*` 필터링해 NVS `ha_ctl/entity_cache` blob에 저장. HA 미접속이면 정적 파일만 active.
- **Active registry = static ∪ NVS_cache** (충돌 시 정적 파일 우선 — 사용자 한국어 friendly_name이 HA UI 영문 이름보다 우선해 매칭됨).

**정적 entry 형식.**
```json
{
  "entities": [
    {"id": "light.smart_bulb", "friendly_name": "화장실 조명", "domain": "light",
     "supports": ["brightness", "color"]},
    {"id": "cover.zemismart_smart_curtain", "friendly_name": "거실 커튼", "domain": "cover",
     "supports": []},
    {"id": "switch.living_room_outlet", "friendly_name": "거실 콘센트", "domain": "switch",
     "supports": []},
    {"id": "board:onboard_rgb", "friendly_name": "보드 RGB", "domain": "board",
     "supports": ["color", "brightness"]}
  ]
}
```

**Manual refresh.** Console 명령 `ha_control --refresh-registry`. 시연 직전에 사용자가 한 번 호출해 NVS 갱신.

**Active list runtime injection.** cap_ha_control이 LLM tool description의 friendly_name 리스트를 active registry로 매번 갱신 (또는 system prompt suffix). 정확한 inject 메커니즘은 plan 단계에서 결정 (`claw_cap_descriptor.input_schema_json`을 동적 빌드 vs 별도 hook).

## 7. HA Endpoints & Token

- **Service call**: `POST {ha_url}/api/services/<domain>/<service>` body `{"entity_id": "...", ...service_data}`, header `Authorization: Bearer <token>`.
- **States fetch**: `GET {ha_url}/api/states` (전체) — 응답 streaming + 도메인 화이트리스트(`light/cover/switch`)로 buffer 채우면서 필터, 16 KB cap.
- **Verification**: 사용자 결정에 따라 service call 응답만 신뢰 (HTTP 200 + body OK = success). 추가 state 폴링 없음.

**Token & URL storage (NVS).**
- `ha_url` (e.g. `http://192.168.1.94:8123`)
- `ha_token` (HA long-lived access token, 동적 alloc로 256자 제한 없음)
- 입력 경로: Setup Wizard 신규 필드 + console fallback `ha_control --set-url <url> --set-token <tok>`

## 8. Onboard Board Branch

`target == "board:onboard_rgb"` 분기:
- v1.3 N16R8 clone (CH343) 보드 = GPIO 48, COUNT 1로 박힘 (`docs/learn/20260508-bathroom-rgb-demo-result-and-rgb-gpio.md` 검증).
- `turn_on` + color/brightness → `led_strip.new(48, 1)` → set_pixel_hsv → refresh → 짧은 latch delay → close.
- `turn_off` → led_strip clear + refresh + close.
- 다른 board variant는 out of scope. board entry는 registry에 가짜 entity로만 존재.

## 9. Result Contract & LLM Echo

cap_ha_control_execute 반환 JSON:

```json
{
  "success": true,
  "message": "화장실 조명을 노란색 60%로 켰습니다.",
  "entity_id": "light.smart_bulb",
  "raw_status": 200
}
```

또는

```json
{
  "success": false,
  "message": "\"화장실 조명\"에 해당하는 기기를 찾지 못했습니다. 사용 가능한 기기: 거실 등, 침실 등, 거실 커튼, 거실 콘센트, 보드 RGB.",
  "entity_id": null,
  "raw_status": null
}
```

**System prompt 강제 (APP_SYSTEM_PROMPT_COMMON 추가 라인).**
- "When `ha_control` returns, respond using the `message` field VERBATIM (as-is, no rephrasing). Do NOT claim success unless `success: true`."
- "Do NOT call `mcp_call_tool`, `lua_run_script`, `lua_write_script`, or any lua_* tool. The only way to control devices is `ha_control`."

**핵심 invariant.** `success:true`는 (a) HA POST 200 + isError:false, 또는 (b) board led_strip ESP_OK, 둘 중 하나일 때만 true. message는 항상 firmware가 작성. 두 layer 모두 LLM이 거짓 성공 합성 표면 차단.

## 10. File Plan

**신규.**
```
components/claw_capabilities/cap_ha_control/
├── CMakeLists.txt
├── idf_component.yml          # esp_http_client, json, claw_cap, nvs deps
├── Kconfig                    # CONFIG_CAP_HA_CONTROL_ENABLED, BOOT_FETCH_ENABLED
├── data/
│   └── entities.default.json
├── include/
│   ├── cap_ha_control.h
│   └── cmd_cap_ha_control.h
└── src/
    ├── cap_ha_control.c        # claw_cap_descriptor + group_init
    ├── cap_ha_control_core.c   # execute(), schema validate, dispatch
    ├── cap_ha_control_resolve.c # target → entity, registry I/O
    ├── cap_ha_control_http.c   # /api/services, /api/states, Bearer
    ├── cap_ha_control_board.c  # board:onboard_rgb (led_strip)
    └── cmd_cap_ha_control.c    # console: --call/--resolve/--refresh-registry/--set-url/--set-token
```

**수정.**
| 파일 | 변경 |
|---|---|
| `application/edge_agent/main/idf_component.yml` 또는 root CMake | cap_ha_control 의존 추가 |
| `application/edge_agent/main/skills/skills_list.json` | bathroom_light_*, rgb_purple_* 4개 entry 제거 |
| `application/edge_agent/main/lua_scripts/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.lua` | delete |
| `application/edge_agent/main/skills/{bathroom_light_on,bathroom_light_off,rgb_purple_on,rgb_purple_off}.md` | delete |
| `application/edge_agent/main/lua_scripts/builtin/demo_secrets.lua` (또는 fatfs 경로) | delete |
| `.gitignore` | demo_secrets.lua 라인 제거 |
| `components/common/app_claw/app_claw.c` (`APP_SYSTEM_PROMPT_COMMON`) | verbatim echo 강제 + "lua_*/mcp_call_tool 금지" 라인 추가 |
| `components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c` | (단기 patch 통합) `arguments` 빈 객체/누락 reject + Error/FAIL 본문 시 success false 보장 |
| Setup Wizard frontend (web UI) | `ha_url`, `ha_token` 필드 + `vis_cap_groups` 기본값 갱신 |
| `application/edge_agent/sdkconfig.defaults` | `CONFIG_CAP_HA_CONTROL_ENABLED=y`, `CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y` |

## 11. Test Plan

**Unit-ish (보드 console).**
1. `ha_control --call '{"target":"화장실 조명","action":"turn_on","brightness_pct":60,"color":"yellow"}'` → 한국어 message + 실물 램프 ON.
2. `ha_control --call '{"action":"turn_on"}'` → reject (target missing).
3. `ha_control --call '{"target":"존재하지 않는 기기","action":"turn_on"}'` → reject + 후보 5개.
4. `ha_control --call '{"target":"거실 커튼","action":"turn_on"}'` → reject (cover ↔ turn_on 부조합).
4b. `ha_control --call '{"target":"거실 커튼","action":"close","brightness_pct":50}'` → success, brightness silent drop + WARN log.
5. `ha_control --call '{"target":"보드 RGB","action":"turn_on","color":"purple"}'` → 보드 LED 보라.
6. `ha_control --resolve "화장실 등"` → cascade 결과 (정확 일치 미스 → 정규화 일치).
7. `ha_control --refresh-registry` → /api/states + NVS 갱신 행 수.

**End-to-end (Telegram).**
1. 단일 라운드 — v2 4개 시나리오 + 신규 ("화장실 60%로", "거실 커튼 닫아", "보라색 켜").
2. 멀티라운드 20회 — concern #1 회귀 (target 빈 채 호출 0회 기대).
3. 거짓 성공 차단 — HA URL 오타로 강제 fail → LLM이 message verbatim echo ("HA 호출이 실패했습니다 (...)"), "꺼졌습니다"/"켰습니다" 자체 합성 0회.

**v2 cleanup 검증.**
- `git status` clean. deleted 4 lua + 4 md + demo_secrets.lua + .gitignore line.
- skills_list.json에서 4 entry 제거.
- fatfs storage.bin에서 builtin/bathroom_light* 부재.

**Token 누출 검사.**
- `git log --all -- "**/demo_secrets*"` 결과 무.
- `git grep -E "Bearer [A-Za-z0-9]{20,}|eyJ"` 결과 무.

## 12. Risk Register

1. `/api/states` 응답 large (수십 KB) — streaming + domain 화이트리스트 필터, 16 KB cap.
2. NVS entity_cache 크기 — 50 entity 가정 ~5–10 KB. blob 1개 저장.
3. Bearer 토큰 NVS 평문 — 시연용 risk 수용. v4+에서 `nvs_flash_secure_initialize`.
4. Setup Wizard ha_url/ha_token UX 부재 — fallback 콘솔 명령 동시 제공.
5. multi-round drift 회귀 — schema validation으로 차단. 90% 미만 시 system prompt에 friendly_name enum 강화 → 모델 격상.
6. board:onboard_rgb GPIO mismatch — v1.3 = GPIO 48 박음. 다른 variant out of scope.
7. v2 lua 삭제로 빌드 깨짐 — manifest 재생성 검증.

## 13. Out of Scope (v3)

- climate / fan / media_player 도메인 → v4+
- HA scene/script 호출 → v4+
- 다중 entity 복합 명령 ("화장실이랑 거실 둘 다 켜") — LLM이 ha_control 2회 호출하면 자연 처리되지만 실패 시 일부 성공/일부 실패 부분 결과 처리는 v4+
- HA MCP 동적 발견 (cap_mcp_client list_tools 기반 registry) → v4+
- 음성/카메라/메모리/scheduler 통합

## 14. Migration from v2

v3 ship 시점에 v2 4개 lua skill + skills_list entry + demo_secrets.lua **제거**. lua 파일은 삭제 (reference 유지 안 함). Tradeoff 수용 — v3 cap_ha_control이 v2 시연 4개 시나리오 모두 cover.

## 15. Demo Guardrails

- 시연 직전 Telegram 새 세션 (`/start`), erase-flash + 풀 플래시.
- vis_cap_groups에 cap_lua/cap_mcp_client 빠진 채 boot 로그 확인.
- system prompt에 verbatim echo + tool 금지 두 줄 살아있는지 cap_llm_inspect로 dump.
- HA 토큰은 NVS only — 시연 후 commit 전 git history grep 1회.
- gpt-5-mini로 시작, 90% 미만 시 system prompt 강화, 마지막 수단 모델 격상.

---

## Approval

Brainstorming 5/5 섹션 사용자 승인 (2026-05-08). 다음 단계: `superpowers:writing-plans`로 본 design을 단계별 task로 분해해 plan v3 작성 → `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool.md`.
