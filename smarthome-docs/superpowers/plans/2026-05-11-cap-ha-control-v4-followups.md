# cap_ha_control v4 follow-ups — Plan

> **Source:** `/review` of PR #1 (cap_ha_control v3 typed tool) on 2026-05-11. 5 findings recorded as v4 follow-up tasks per user decision to not block v3 land.
>
> **Status:** ready to execute. Each task has files, line refs, acceptance criteria, and verification.
>
> **Branch suggestion:** `feat/cap-ha-control-v4` off the v3 land commit.

**Goal:** close the 5 known v3 issues without expanding scope (no new HA domains, no wizard rework). Each task is independent — can ship as separate PRs if preferred.

**Build hygiene:** worktree at `.claude/worktrees/develop` still needs `gen_bmgr_codes/` + `sdkconfig` seeded from main on first build (see `memory/project_worktree_build_env.md`).

---

## Task 1 (P1) — Thread-safe registry refresh

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` (if mutex helper exposed)

**Problem:** `cap_ha_resolve_refresh_from_ha()` at line 312–314 frees `s_cache_registry.items` and re-parses while concurrent callers iterate the same global via `lookup_in()` / `cap_ha_resolve_top_candidates()`. Window is ~50ms during boot_fetch_task's refresh, or whenever console `--refresh-registry` runs.

**Approach (preferred — FreeRTOS mutex):**

1. Add `static SemaphoreHandle_t s_registry_mutex = NULL;` at top of `cap_ha_control_resolve.c`.
2. In `cap_ha_resolve_init` (before `parse_registry` on static), create:
   ```c
   s_registry_mutex = xSemaphoreCreateMutex();
   if (!s_registry_mutex) return ESP_ERR_NO_MEM;
   ```
3. Wrap every `s_cache_registry` read in `cap_ha_resolve_target`, `cap_ha_resolve_top_candidates`, `cap_ha_resolve_active_friendly_names` with `xSemaphoreTake(s_registry_mutex, portMAX_DELAY)` / `xSemaphoreGive` around the iterate block. Copy out the entity to a local `cap_ha_entity_t` before releasing — the caller mustn't hold the lock while running HTTP.
4. In `cap_ha_resolve_refresh_from_ha` line 312–314 (the free + reassign + parse_registry block), take the same mutex around `free(s_cache_registry.items); s_cache_registry = ...; parse_registry(...)`.
5. `s_static_registry` is write-once at init — no mutex needed for reads after init. Document this invariant in a comment.

**Alternative (atomic pointer swap, lock-free):** maintain `s_cache_registry` as a pointer (`cap_ha_registry_t *s_cache_registry`); refresh allocates a new struct, atomic-swaps the pointer, then frees the old one after a grace period (RCU-style). More complex; mutex is fine for ESP-IDF's typical concurrency profile.

**Acceptance:**
- Stress test: console loop `while true; do ha_control --refresh-registry & ha_control --call '{"target":"화장실 조명","action":"toggle"}'; done` runs ≥ 60s without crash/heap-corruption.
- `idf.py build` clean; `idf.py size` shows minimal flash/RAM growth.

**Verification:** unit-ish on-board test — script that emits 10 parallel `event_router --emit-message` calls during a refresh, then checks `cap_ha_resolve_target` still returns valid entities.

---

## Task 2 (P2) — `#rrggbb` invalid-hex validation

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c:44-51`

**Problem:** `strtol(..., 16)` returns 0 for non-hex chars, so `"#FFGG00"` silently parses as `(0xFF, 0x00, 0x00)` — red — instead of being rejected. The LLM can synthesize bad hex codes, and the user gets the wrong color with no indication.

**Approach:**
```c
if (color[0] == '#' && strlen(color) == 7) {
    for (size_t i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)color[i])) return ESP_ERR_INVALID_ARG;
    }
    /* ...existing strtol calls... */
}
```
Add `#include <ctype.h>` if not already pulled in.

**Acceptance:**
- `ha_control --call '{"target":"board:onboard_rgb","action":"turn_on","color":"#FFGG00"}'` returns `{"success":false,"message":"지원하지 않는 색상입니다 (color=#FFGG00).",...}`.
- Valid hex like `#A1B2C3` still works.

**Verification:** console call with bad hex returns `success:false` with the existing "지원하지 않는 색상입니다" message (already the rejection path in `cap_ha_control_board.c`).

---

## Task 3 (P2) — Entity count cap in `parse_registry`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c:50` area

**Problem:** `cap_ha_entity_t *items = calloc((size_t)count, sizeof(*items));` where `count` comes from untrusted JSON. Each entity is ~152 bytes. A malformed or malicious HA could push 10K entries → 1.5 MB allocation. ESP32-S3 with PSRAM survives but fragments heap; smaller variants would OOM.

**Approach:**
```c
#define CAP_HA_MAX_REGISTRY_ENTRIES 64

/* in parse_registry, after computing count: */
if (count > CAP_HA_MAX_REGISTRY_ENTRIES) {
    ESP_LOGW(TAG, "registry entry count %d exceeds cap %d; truncating",
             count, CAP_HA_MAX_REGISTRY_ENTRIES);
    count = CAP_HA_MAX_REGISTRY_ENTRIES;
}
```
The cap applies to BOTH static and cache parses (same function). 64 is generous for a typical home — most demos have ≤ 10 entities.

**Acceptance:**
- Synthesized 100-entry `entities.default.json` → boot log `registry entry count 100 exceeds cap 64; truncating` + `loaded 64 static entities`.
- Real HA `/api/states` continues to work (typical responses are ≤ 50 light/cover/switch entries).

---

## Task 4 (P3) — Description refresh after boot-fetch

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` (boot_fetch_task)

**Problem:** `compose_description()` runs once in `cap_ha_group_init` BEFORE boot-fetch enriches `s_cache_registry`. The dynamic "Active devices: ..." list shows only the 4 static entities — even after boot-fetch adds HA-discovered entities to the cache. v3 docs this: "Boot-fetch updates take effect on the next reboot." v4 closes the gap.

**Approach (two-stage):**

1. Export `compose_description()` from `cap_ha_control.c` (rename to `cap_ha_compose_description` and declare in `cap_ha_control_internal.h`).
2. After `cap_ha_resolve_refresh_from_ha()` succeeds in `boot_fetch_task`, call `cap_ha_compose_description()` to rebuild `s_ha_description` from the now-enriched registry.
3. **Critical question:** does `s_ha_descriptors[0].description` get re-read by claw_core after boot, or is the descriptor snapshot cached in the LLM tools context? Verify by reading `components/claw_modules/claw_cap/src/claw_cap.c` for how `claw_cap_add_capped_description` is invoked — if it's called per-LLM-request, the new description propagates; if it's cached, we need to invalidate.
4. If cached, add a `claw_cap_invalidate_tool_description(group_id, cap_id)` API on `claw_cap` so cap_ha_control can poke it. Out of scope for this v4 task if the API doesn't exist — punt to v5 with a TODO.

**Acceptance:**
- Boot board with empty NVS cache → first boot description shows 4 static entities, post-boot-fetch description shows ≥ 5 (static + HA-discovered).
- Subsequent LLM message uses the enriched description (verify via `cap_llm_inspect` if available, otherwise via Telegram message that names an HA-only entity).

**Risk:** if the descriptor is cached at register-time in claw_core, this task needs claw_cap API work first. Investigate before committing to the approach.

---

## Task 5 (P3) — HTTPS support (Bearer token cleartext)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` (URL scheme handling)
- Modify: `components/claw_capabilities/cap_ha_control/CMakeLists.txt` (esp-tls REQUIRES if not transitively pulled)
- Update: spec / docs

**Problem:** `http://192.168.1.94:8123/api/services/...` + `Authorization: Bearer <token>` — token sent in plaintext on the LAN. On any shared/guest Wi-Fi, sniffable.

**Approach:**

1. Accept `https://` URLs in `cap_ha_http_set_url`. esp_http_client + `crt_bundle_attach` already in cfg — TLS infrastructure is wired but unused for `http://` URLs.
2. For HA setups using self-signed certs (common for local HA on Pi), document the option to:
   - Either disable cert verification (`.skip_cert_common_name_check = true` and remove `crt_bundle_attach`) with a console flag like `ha_control --insecure`,
   - Or import the user's HA cert into NVS as a custom CA bundle (`esp_http_client_config_t.cert_pem`).
3. v4 ships option A (insecure flag for self-signed) with a WARN log on every request: `WARNING: HA URL uses HTTPS without certificate verification`.
4. v5 adds option B (custom CA import via `ha_control --set-ca <pem>`).

**Acceptance:**
- `ha_control --set-url https://192.168.1.94:8123` + `ha_control --insecure on` → POST works against self-signed HA.
- `ha_control --set-url http://...` continues to work (no regression).
- Cleartext WARN logged.

**Pairs with:** v4 NVS secure storage (HA token currently stored in plaintext NVS — separate spec item, listed in v3 learn log).

---

## Task 6 (FEATURE) — 자동화 등록 / 수정 / 제거 (`ha_automation` typed tool)

**Files:**
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` (or 새 component `cap_ha_automation`로 분리할지 결정 필요)
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` (두 번째 descriptor 추가)
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` (자동화 prototypes)
- Modify: `components/claw_capabilities/cap_ha_control/include/cmd_cap_ha_control.h` (console 명령 확장)
- Modify: `application/edge_agent/components/app_config/app_config.c` 또는 sdkconfig (vis_cap_groups에 cap_scheduler 추가 검토 — 또는 자동화 작업을 cap_ha_control이 internally 위임)

**Goal:** Telegram에서 "화장실 조명을 매일 저녁 7시에 켜줘", "그 자동화 지워줘", "7시 자동화를 8시로 바꿔줘" 같은 자연어로 자동화 룰 등록/수정/제거. v3 ha_control의 typed-tool 패턴과 일관되게 단일 `ha_automation` 도구 1개.

**Architecture:** v3와 동일한 firmware-owns-everything 패턴. LLM은 `{action, trigger, target_payload, automation_id?}` 만 채우고, firmware가:
1. trigger spec(cron expr / interval / sunrise±) 파싱 + 검증
2. target_payload (ha_control과 동일한 schema)를 미리 직렬화해서 scheduler entry에 저장
3. `cap_scheduler.scheduler_add/update/remove` 로 위임
4. scheduler trigger fire 시점에 저장된 payload로 ha_control 호출
5. 한국어 메시지 합성 ("매일 저녁 7시 화장실 조명 켜기 자동화를 등록했습니다.")

LLM은 message verbatim echo (v3 system prompt rule 그대로 적용 — 거짓 성공 차단 자동화).

**Schema (proposed):**
```json
{
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["create", "update", "remove", "list"]},
    "automation_id": {"type": "string", "description": "remove/update에 필요. create 시 firmware가 자동 생성."},
    "trigger": {
      "type": "object",
      "properties": {
        "kind": {"type": "string", "enum": ["daily_time", "interval", "weekly", "cron"]},
        "time": {"type": "string", "description": "daily_time/weekly용 'HH:MM' (24h, KST)"},
        "weekdays": {"type": "array", "description": "weekly용 [0-6] (0=일요일)"},
        "interval_ms": {"type": "integer", "description": "interval용 밀리초"},
        "cron": {"type": "string", "description": "cron expr — power user 전용"}
      }
    },
    "target": {"type": "string", "description": "ha_control과 동일한 친숙명/entity_id"},
    "device_action": {"type": "string", "enum": ["turn_on", "turn_off", "toggle", "open", "close"]},
    "brightness_pct": {"type": "integer", "minimum": 1, "maximum": 100},
    "color": {"type": "string"},
    "kelvin": {"type": "integer", "minimum": 2000, "maximum": 6500}
  },
  "required": ["action"]
}
```
`create/update`: trigger + target + device_action 필수. `remove/list`: automation_id (또는 list는 nothing).

**Step-by-step:**

1. **cap_scheduler 인터페이스 조사**: `components/claw_capabilities/cap_scheduler/include/*.h` 읽어 외부 호출용 API 존재 여부 확인. 없으면 우선 cap descriptor `scheduler_add` 등을 internal-only로 노출하는 helper 만들기 (LLM에는 보이지 않게).

2. **Scheduler entry 페이로드 합의**: scheduler가 fire 시점에 어떤 메커니즘으로 ha_control을 호출할지 결정. 옵션:
   - (a) scheduler entry의 `text` 필드에 ha_control JSON 직렬화 저장 → trigger 시 event_router로 자기-자신에게 메시지 보내기 (LLM 경유 — 비효율)
   - (b) scheduler entry에 `action_type: "ha_control"` 메타데이터 + payload 직렬화 → trigger handler가 직접 `cap_ha_core_execute` 호출 (LLM 우회 — 권장)
   - (b)가 효율적 + LLM cost 0 + 결정적. (a)는 LLM이 매번 메시지 해석해 부정확성/cost 발생.

3. **`cap_ha_automation_create(...)`** 본구현:
   - trigger.kind에 따라 scheduler entry JSON 합성 (kind=interval/cron/...)
   - target+device_action+brightness_pct+color+kelvin을 ha_control payload JSON으로 직렬화해 scheduler entry의 `payload` 필드에 박음
   - `id` 자동 생성 (예: `ha_auto_<unix_ts>` 또는 `ha_auto_<friendly_name>_<action>_<random4>`)
   - `cap_scheduler` add API 호출
   - 한국어 confirmation 메시지 합성

4. **`cap_ha_automation_update(...)`** / **`cap_ha_automation_remove(...)`** / **`cap_ha_automation_list(...)`**: 각각 scheduler update/remove/list API에 위임 + 한국어 메시지.

5. **Scheduler trigger handler 등록**: scheduler fire 시점에 entry의 `action_type` 검사 → "ha_control"이면 payload 추출 → `cap_ha_core_execute(payload, output, sizeof(output))` 호출. 실패 시 log + (선택) Telegram notify.
   - 이 부분은 cap_scheduler가 fire callback을 어떻게 노출하는지에 따라 분기. 가장 작은 변경은 `event_router`의 `agent_stage_*` rule에 자동화 처리 rule을 추가해서 scheduler가 메시지/이벤트를 emit하면 그걸 자동화 entry로 인식하는 것.

6. **두 번째 descriptor 등록**: `cap_ha_control.c`의 `s_ha_descriptors[]` 배열에 `ha_automation` 추가 (`s_ha_descriptors`를 size 2로 확장). schema는 위 JSON. description은 "Register/modify/remove time-based automation for HA devices and onboard hardware. ..." + active devices list (v3 ha_control과 공유).

7. **Console 명령 확장**: `ha_control --automation create '<json>'` / `--automation list` / `--automation remove <id>` 추가.

8. **빌드 + commit + on-board 검증.**

**Acceptance:**
- Telegram "화장실 조명을 매일 저녁 7시에 켜줘" → `{"success":true,"message":"매일 19:00에 '화장실 조명' 켜기 자동화를 등록했습니다 (ID: ha_auto_...).","automation_id":"ha_auto_..."}` + LLM verbatim echo
- 등록 시점 +1분 안에 7시 시뮬레이트 (시스템 시간 조작 또는 scheduler `--trigger --id <id>` 콘솔)로 실제 ha_control 실행 확인 → 실 램프 ON
- "그 자동화 지워줘" → LLM이 list로 ID 조회 후 remove → 한국어 confirmation
- "7시를 8시로 바꿔줘" → update → confirmation
- 보드 reboot 후 자동화 룰 유지 (cap_scheduler는 NVS에 영속)

**Verification:**
- 콘솔: `ha_control --automation create '{"action":"create","trigger":{"kind":"daily_time","time":"19:00"},"target":"화장실 조명","device_action":"turn_on"}'` 후 `scheduler --list`로 entry 확인
- 시간 시뮬레이트: `scheduler --trigger --id <id>` → monitor에서 cap_ha_core_execute 호출 확인 + 실 램프 ON
- 보드 reboot → scheduler entries 영속 확인

**Risk / Open questions:**
- cap_scheduler가 외부에서 entry를 등록하는 helper API를 가지고 있는지 확인 필요 (descriptor만 있고 직접 호출용 C API 없으면 추가 필요)
- scheduler fire callback이 어디서 발화되는지 — `cap_scheduler` 내부 task에서 호출되면 cap_ha_core_execute가 그 task의 stack에서 실행됨. v3에서 boot_fetch_task의 6KB 스택 오버플로우 경험 — scheduler task의 stack 크기 사전 확인 필요.
- 자동화 룰의 firmware-쪽 저장이 `cap_scheduler`의 NVS namespace로 통일되는지, ha_control이 별도 보관해야 하는지. payload 직렬화 길이 한계도 함께.
- v3의 `cap_ha_compose_*` 메시지 합성과 자동화의 사용자 알림 메시지 사이 톤 일관성 (둘 다 한국어, verbatim echo 룰 동일 적용).

**Expected effort:** ~3–5 h CC (cap_scheduler 조사 + 6 step 구현 + on-board 검증). Tasks 1–5 보다 큰 작업 — 별도 PR 권장.

---

## Out of scope (not v4)

These v3 learn-log items stay deferred beyond v4 follow-up:

- climate / fan / media_player / scene domain support
- HA secure NVS storage (encrypted NVS partition)
- Multi-entity composite commands with partial-failure handling
- Setup wizard ha_url / ha_token field integration with NVS namespace unification (`app_config` ↔ `ha_ctl`)
- `cap_ha_http_get_url_alloc(char **out)` / `_token_alloc` helpers for unbounded caller buffers
- `roll_chat_session` persistent history clear (firmware-wide, not cap_ha_control-specific)

---

## Execution suggestion

| Order | Task | Estimated effort | PR 묶음 |
|---|---|---|---|
| 1 | Task 2 (#rrggbb hex validate) | ~15 min CC, low risk | PR-A |
| 2 | Task 3 (entity cap) | ~10 min CC, low risk | PR-A |
| 3 | Task 1 (mutex) | ~45 min CC, requires test | PR-A |
| 4 | Task 6 (자동화 등록/수정/제거) | ~3–5 h CC, feature | PR-B |
| 5 | Task 5 (HTTPS) | ~1 h CC, needs HA setup with TLS | PR-C |
| 6 | Task 4 (description refresh) | ~1–2 h CC, depends on claw_cap API | PR-D |

PR-A: 작은 safety fixes (Task 1–3) — v3 ship 직후 빠르게 land.
PR-B: 자동화 feature (Task 6) — user 요청 핵심 항목. cap_scheduler 인프라 위에 작성하므로 Task 1의 mutex 패턴을 참조해 thread safety도 일관성 있게 잡기. PR-A 이후 작업 권장 (race 패턴 통일).
PR-C, PR-D: 인프라/API 작업 — 별도 dependency 검토 후 분기.
