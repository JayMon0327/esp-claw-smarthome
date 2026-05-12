# cap_ha_automation — state-trigger 지원 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** v4 `ha_automation` typed tool 에 새 trigger kind `"state"` 추가. "도어센서가 열리면 거실 조명 켜줘" 같은 entity-state-change 기반 자동화를 LLM 이 typed payload 로 등록 가능하게 한다.

**Architecture:** `build_ha_trigger_array()` 에 `kind == "state"` 분기 추가. `trigger.entity` (friendly name 또는 entity_id) + `trigger.to` (필수) + `trigger.from` (optional) → HA platform `{"platform":"state","entity_id":"<resolved>","to":"<to>","from":"<from?>"}`. `cap_ha_resolve_target()` 가 binary_sensor / sensor 도 잡도록 boot-fetch 도메인 필터 확장.

**Tech Stack:** ESP-IDF v5.5.4, cJSON, HA state platform (`https://www.home-assistant.io/docs/automation/trigger/#state-trigger`).

**Trade-off / 범위:** `for` (duration), template trigger, attribute change 등 advanced state trigger 옵션은 v4 에서 제외. 가장 흔한 demo path (sensor on/off → light on/off) 만 커버. 추후 `for` 추가는 trigger object 에 `for_ms` 필드 한 줄 + builder 한 줄로 확장 가능.

**Source:** v4 ship 후 사용자 후속 항목 2 ("state-trigger 지원 — 도어센서 → 조명 시나리오, ~50–80 LoC").

**Worktree:** Implement in `feat/cap-ha-control-v4` (v4 worktree). For parallel execution with c04c845 follow-up track, fork a fresh worktree off `feat/cap-ha-control-v4` HEAD (`95a35cb`).

---

## File Plan

| 파일 | 작업 | 책임 |
|---|---|---|
| `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` | Modify | `build_ha_trigger_array()` 에 state 분기 추가 (~30 LoC) |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` | Modify | descriptor `input_schema_json` 에 kind=state + entity/from/to 필드 추가 + description 문구 보강 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` | Modify | boot-fetch domain filter 에 binary_sensor + sensor 허용 (1줄) |
| `docs/learn/20260512-cap-ha-automation-state-trigger.md` | Create | 마무리 학습 로그 |

LoC 추정: cap_ha_automation.c +35, cap_ha_control.c +6, cap_ha_control_resolve.c +1, learn log 별도 = ~42 LoC code + docs. 사용자 추정 ~50–80 LoC 안에 들어옴.

---

## Pre-flight

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git status -s
git log --oneline -5
```

Expected: 깨끗하거나 plan 1개 modified. 최신 commit `95a35cb`. 보드 + HA 가용 시 E2E 검증 가능; 빌드만이면 불필요.

빌드 명령:
```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build
```

---

## Task 1: boot-fetch domain filter 확장 (binary_sensor / sensor 허용)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c:313-314`

배경: `cap_ha_resolve_target("도어센서", &entity)` 로 friendly name → entity_id 매핑이 작동하려면 boot-fetch 가 binary_sensor 도메인 entity 도 cache 에 포함해야 한다. 현재는 light/cover/switch 만 보존. (정적 registry `entities.default.json` 도 binary_sensor 없지만 그건 dev 가 수동 추가 — 동적 fetch 만 확장.)

- [ ] **Step 1: domain filter 라인 교체**

`cap_ha_control_resolve.c` line 313-314:
```c
        if (strcmp(domain, "light") != 0 && strcmp(domain, "cover") != 0 &&
            strcmp(domain, "switch") != 0) continue;
```

교체:
```c
        if (strcmp(domain, "light") != 0 && strcmp(domain, "cover") != 0 &&
            strcmp(domain, "switch") != 0 && strcmp(domain, "binary_sensor") != 0 &&
            strcmp(domain, "sensor") != 0) continue;
```

binary_sensor / sensor 는 actuator 가 아니라 `cap_ha_action_to_service(domain, "turn_on")` 이 NULL 반환 → ha_control 의 turn_on/off 호출은 어차피 reject. 하지만 ha_automation 의 `trigger.entity` 해석엔 필요.

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK.

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_control_resolve): include binary_sensor + sensor in boot-fetch

state-trigger 지원의 사전 작업. ha_automation 의 trigger.entity 가
'현관 도어센서' 같은 friendly name 으로 들어오면 cap_ha_resolve_target
이 매핑할 수 있어야 한다. binary_sensor / sensor 는 actuator 아니라
ha_control 의 turn_on/off 호출에 잡혀도 cap_ha_action_to_service 가
NULL 반환해 reject 되므로 ha_control 안전성에는 영향 없음.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `build_ha_trigger_array()` 에 state 분기 추가

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c:114-186` (insert new branch before final `else`)

- [ ] **Step 1: state 분기 코드 삽입**

기존 `build_ha_trigger_array()` 의 `} else if (strcmp(kind, "interval") == 0) {` 블록 (line ~152) 다음, `} else {` (line ~181, "지원하지 않는 trigger.kind") 이전 위치에 새 분기 추가:

기존 line 180-186:
```c
        cJSON_AddItemToArray(arr, step);
    } else {
        cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 trigger.kind입니다 (%s). daily_time/weekly/interval만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }
```

교체:
```c
        cJSON_AddItemToArray(arr, step);
    } else if (strcmp(kind, "state") == 0) {
        /* HA state platform 분기 — door sensor → light 같은 entity-state-change.
         * 입력: trigger.entity (friendly name or entity_id), trigger.to (필수),
         *       trigger.from (optional). 'for' (duration), template, attribute
         *       change 같은 advanced 옵션은 v4 에서 제외 (v5 후속). */
        const cJSON *entity_j = cJSON_GetObjectItem(trigger_in, "entity");
        const cJSON *to_j     = cJSON_GetObjectItem(trigger_in, "to");
        const cJSON *from_j   = cJSON_GetObjectItem(trigger_in, "from");
        if (!cJSON_IsString(entity_j) || !cJSON_IsString(to_j)) {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "state trigger 에는 entity 와 to 가 모두 필요합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        /* friendly_name 으로 들어오면 registry 로 entity_id 해석, 이미 'domain.<x>'
         * 형식이면 그대로 사용 (registry 미적재 entity 도 사용자가 직접 지정 가능). */
        const char *resolved_eid = NULL;
        cap_ha_entity_t e = {0};
        if (strchr(entity_j->valuestring, '.') != NULL &&
            strncmp(entity_j->valuestring, "board:", 6) != 0) {
            /* entity_id 같이 보이면 verbatim */
            resolved_eid = entity_j->valuestring;
        } else if (cap_ha_resolve_target(entity_j->valuestring, &e) == ESP_OK) {
            if (strcmp(e.domain, "board") == 0) {
                cJSON_Delete(arr);
                snprintf(err_msg, err_msg_size,
                         "보드 entity (%s) 는 state trigger 의 entity 로 사용할 수 없습니다.",
                         entity_j->valuestring);
                return ESP_ERR_INVALID_ARG;
            }
            resolved_eid = e.id;
        } else {
            cJSON_Delete(arr);
            char cand[192];
            cap_ha_resolve_top_candidates(cand, sizeof(cand), 5);
            snprintf(err_msg, err_msg_size,
                     "trigger.entity \"%s\" 를 해석하지 못했습니다. 후보: %s.",
                     entity_j->valuestring, cand);
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "state");
        cJSON_AddStringToObject(step, "entity_id", resolved_eid);
        cJSON_AddStringToObject(step, "to", to_j->valuestring);
        if (cJSON_IsString(from_j) && from_j->valuestring[0]) {
            cJSON_AddStringToObject(step, "from", from_j->valuestring);
        }
        cJSON_AddItemToArray(arr, step);
    } else {
        cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 trigger.kind입니다 (%s). "
                 "daily_time/weekly/interval/state 만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }
```

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK. `cap_ha_resolve_target` / `cap_ha_resolve_top_candidates` 는 이미 internal.h 에 노출되어 있어 추가 include 불필요.

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): state-trigger 분기 (door sensor → light)

build_ha_trigger_array 에 kind="state" 분기 추가. trigger.entity 가
friendly_name 이면 cap_ha_resolve_target 로 해석, entity_id 형식
('binary_sensor.front_door') 이면 verbatim. trigger.to 필수,
trigger.from optional. board:* entity 는 reject (HA-side automation
이라 의미 없음). 미해석 entity 는 후보 목록 포함한 명시적 fail.

Advanced options (for/template/attribute) 은 v5 후속.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: descriptor schema + description 업데이트

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c:71-76` (trigger schema 객체)
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c:100-107` (`cap_ha_compose_description` automation 문구)

- [ ] **Step 1: input_schema_json 의 trigger 객체 확장**

기존 line 71-76:
```c
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000}"
              "}},"
```

교체:
```c
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"state\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id of the entity whose state change fires the automation (e.g., '현관 도어센서' or 'binary_sensor.front_door').\"},"
                "\"to\":{\"type\":\"string\",\"description\":\"state: required target state ('on'/'off'/'open'/'closed' etc).\"},"
                "\"from\":{\"type\":\"string\",\"description\":\"state: optional previous state. If omitted, fires on any transition into 'to'.\"}"
              "}},"
```

- [ ] **Step 2: `cap_ha_compose_description` automation 문구 보강**

기존 line 100-107:
```c
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove time-based automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target'): %s. "
             "board:* targets (onboard RGB) are NOT supported here — those would require on-device automation, planned for v5. "
             "Use 'create' (assigns automation_id), 'update' (needs automation_id), 'remove' (needs automation_id), "
             "'list' (returns existing automations), 'trigger_now' (force-fire by id), 'enable'/'disable' (toggle by id). "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
```

교체:
```c
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target' or in trigger.entity): %s. "
             "board:* targets (onboard RGB) are NOT supported here — those would require on-device automation, planned for v5. "
             "Trigger kinds: 'daily_time' (HH:MM), 'weekly' (HH:MM + weekdays[]), 'interval' (interval_ms ≥ 2000), "
             "'state' (entity + to + optional from — e.g., door sensor opens → light on). "
             "Use 'create' (assigns automation_id), 'update' (needs automation_id), 'remove' (needs automation_id), "
             "'list' (returns existing automations), 'trigger_now' (force-fire by id), 'enable'/'disable' (toggle by id). "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
```

(`s_ha_automation_description` 버퍼는 v4 Task 6.2 에서 이미 1024 로 충분히 잡혀 있음 — 약간 길어지지만 format-truncation 여유 있음.)

- [ ] **Step 3: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK, no format-truncation warning.

- [ ] **Step 4: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_control): expose state-trigger in ha_automation descriptor

input_schema_json 의 trigger.kind enum 에 'state' 추가, trigger 객체에
entity/from/to properties 추가. description 문구에 trigger kinds 전체
요약 한 줄 추가 — LLM 이 schema 만 봐도 door-sensor→light 시나리오를
구성할 수 있도록.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Learn log

**Files:**
- Create: `docs/learn/20260512-cap-ha-automation-state-trigger.md`

- [ ] **Step 1: Learn log 작성**

```markdown
# cap_ha_automation — state-trigger 지원 (door sensor → light)

> **컨텍스트:** v4 ship 직후 (`feat/cap-ha-control-v4` HEAD = 95a35cb) 의 두 번째 후속 트랙. door sensor → light 같은 entity-state-change 자동화를 LLM 이 typed payload 로 등록 가능하게.

## 무엇을 만들었나

| Task | Commit (head) | 핵심 변경 |
|---|---|---|
| 1 | feat: binary_sensor/sensor in boot-fetch | `cap_ha_control_resolve.c` domain 필터 확장 — friendly_name 으로 도어센서 해석 가능. |
| 2 | feat: state-trigger 분기 | `build_ha_trigger_array` 에 kind="state" 추가. trigger.entity (friendly 또는 entity_id) + trigger.to 필수 + trigger.from optional → HA platform state 객체. |
| 3 | feat: descriptor schema | input_schema_json 의 trigger.kind enum 에 "state" + entity/from/to 필드 + description 문구. |

## 무엇을 배웠나

### 1. registry 도메인 필터가 ha_automation 의 trigger.entity 범위를 결정한다

v3 cap_ha_control 은 actuator (light/cover/switch) 만 dynamic registry 에 보존. v4 ha_automation 도 같은 가정. state-trigger 추가하려면 sensor 도메인도 registry 에 들어와야 한다 — 그러나 ha_control 의 turn_on/off 가 sensor 에 호출되어도 `cap_ha_action_to_service("sensor", "turn_on")` 이 NULL 반환해 자연스럽게 reject. 즉 도메인 필터 완화는 안전.

**원칙:** registry 범위 결정은 "할 수 있는 action" 이 아니라 "참조 가능한 entity" 기준이 더 정확. action 적합성은 `action_to_service` 가 별도로 거른다.

### 2. trigger.entity 의 verbatim vs registry 해석 분기

friendly_name (`"도어센서"`) 과 entity_id (`"binary_sensor.front_door"`) 둘 다 받아야 사용자 자연어 + 정확한 entity_id 양쪽 모두 지원. 분기 기준은 `strchr('.')` — entity_id 는 항상 `domain.local_id` 형태. `board:` prefix 는 별도 처리해 false-positive 방지.

**원칙:** 사용자 인풋 형식이 두 가지면 단순한 syntactic 분기 (점 포함 여부) 가 가장 robust. registry 부재시에도 entity_id 직접 입력으로 우회 가능.

### 3. v4 에서 의도적으로 빼낸 것 — for / template / attribute

state-trigger 의 advanced 옵션 (`for: 5s` 지속시간 조건, template trigger, attribute change) 은 demo path 에 불필요. trigger object 한 줄 추가 + builder 한 줄 추가로 v5 에 확장 가능. v4 의 단순한 (entity, to, from) triple 이 90% case 커버.

**원칙:** 외부 시스템 통합의 schema 확장은 demo path 기준으로 좁게 시작 — v3/v4 의 trigger kind 추가 패턴이 좋은 reference.

## 관련

- 관련 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-state-trigger.md`
- 병렬 트랙: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-c04c845-followups.md`
- 다음 후속 (v5 후보): `for_ms` duration, template trigger, attribute change.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add docs/learn/20260512-cap-ha-automation-state-trigger.md
git commit -m "$(cat <<'EOF'
docs(learn): cap_ha_automation state-trigger 지원

door sensor → light 시나리오를 typed payload 로 등록하는 trigger.kind
"state" 분기 + registry 필터 + descriptor schema 정리.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage:**
- state-trigger 지원 (door sensor → light scenario) → Task 2 (build_ha_trigger_array 분기) + Task 3 (descriptor schema). ✅
- friendly_name → entity_id 해석 → Task 1 (registry 도메인 필터 확장) + Task 2 의 entity verbatim/resolve 분기. ✅
- LoC budget (~50–80) → 추정 ~42 LoC code (Task 1 +1, Task 2 +35, Task 3 +6). ✅

**Placeholder scan:** none. 모든 step 에 실제 코드 + 명령.

**Type consistency:** `build_ha_trigger_array` 시그니처 그대로 유지 (out 파라미터 + err_msg 버퍼). `cap_ha_entity_t` 의 `.id` / `.domain` 필드명 일치. trigger.kind enum 값 "state" 는 descriptor schema + builder 모두 동일 spelling.

**E2E 검증 (사용자가 보드 + door sensor 가용시):**
1. HA UI 에서 binary_sensor (예: `binary_sensor.front_door`) 가 friendly_name "현관 도어센서" 로 보이는지 확인.
2. 보드 reflash → boot-fetch 로그에서 `kept N entities` 가 이전보다 많은지 (sensor 포함) 확인.
3. console: `--automation '{"action":"create","target":"화장실 조명","device_action":"turn_on","trigger":{"kind":"state","entity":"현관 도어센서","to":"on"}}'`
4. HA UI 의 자동화 목록에 새 자동화가 보이고, "현관 도어센서" 가 trigger 의 entity 로 들어있는지 확인.
5. 실제 센서를 트리거 (문 열기) → 화장실 조명 켜지는지 확인.

---

## Execution Handoff

플랜 저장 완료. 두 가지 옵션:

1. **Subagent-Driven (recommended)** — Task 1–4 각각 fresh subagent dispatch + controller review.
2. **Inline Execution** — 현재 세션에서 task 별 checkpoint 로 진행.

병렬 실행: 이 플랜은 c04c845-followups 플랜과 같은 worktree 의 다른 함수만 건드리므로 **별도 worktree 를 fork 해 두 트랙 동시 실행 가능**. 마지막에 양쪽 모두 정상 빌드 확인 후 한 쪽 base 로 다른 쪽 rebase + 통합 빌드.
