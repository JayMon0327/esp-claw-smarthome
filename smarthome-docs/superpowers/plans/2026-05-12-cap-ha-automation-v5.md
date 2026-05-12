# cap_ha_automation v5 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** v4 state-trigger 의 두 가지 실사용 한계 해결. (A) `to` 만 지정한 state trigger 가 HA 의 attribute-update 재발화 quirk 로 매번 fire 되는 버그 — domain-pair opposite `from` 을 firmware 가 auto-fill. (B) typed payload 에 `condition` 객체 노출 — `time_range` / `weekday` / `state` 조건으로 "10시–18시 사이만 / 다른 entity 꺼져 있을 때만 / 평일에만" 시나리오 커버.

**Architecture:** `build_ha_trigger_array` 의 state 분기에 `from` auto-fill 로직 (resolved entity domain → opposite state 매핑). 신규 `build_ha_condition_array(condition_in, &out, err_msg, ...)` 가 root-level `condition` 객체를 HA-native condition array 로 번역. `do_create` 와 `do_update` 가 user-provided condition 을 weekly trigger 의 auto-emit weekday condition 과 merge (AND). 명시적 `from` 입력은 그대로 통과 (override 가능).

**Tech Stack:** ESP-IDF v5.5.4, cJSON, HA condition API. v4 ha_automation 위에 incremental 변경 — typed tool 구조 + write-through cache + state-trigger 기반 모두 유지.

**Trade-off / 범위:** `for` (지속시간), template trigger, attribute change, OR/NOT 복합 condition 은 v6 후속. v5 는 단일 `condition` 객체만 받음 (단일 condition 으로도 "시간 + 요일" 같은 복합 케이스는 `time_range` 하나로 처리 가능 — weekday 와 time_range 동시는 weekly trigger + condition 조합). MAYBE 항목 (numeric_state / fan / media_player domain 확장 / board:* 자동화 / CA cert pem) 은 모두 deferred.

**Source:** v4 PRs #5/#6 머지 후 (2026-05-12) E2E 검증에서 사용자 발견 — state-trigger 의 `to`-only 가 HA 측에서 attribute-update 마다 fire (`automation.hyeongwanmun_doeosenseo_yeolrim_hwajangsil_jomyeong_kyeogi` 동작 불량). 사용자 수동 fix `automation.hyeongwanmun_yeolrim_hwajangsil_jomyeong_kyeogi` 가 `from: "off"` 추가로 transition 강제 → 의도대로 동작. + 사용자 별도 요청: "도어센서 ON / 10시–18시 사이 / 조명 ON" 시나리오 위한 condition typed payload (Track C deferred from v4).

**Worktree:** Base `main` HEAD (PR #5/#6 머지 후). 신규 worktree `.claude/worktrees/v5` 에서 작업.

---

## File Plan

| 파일 | 작업 | 책임 |
|---|---|---|
| `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` | Modify | Task 1: state-trigger `from` auto-fill (~25 LoC) / Task 2: `build_ha_condition_array` 신규 (~70 LoC) / Task 3: `do_create` + `do_update` 통합 (~25 LoC) |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` | Modify | Task 4: descriptor `input_schema_json` 에 `condition` 추가 + `cap_ha_compose_description` 문구 (~15 LoC) |
| `docs/learn/20260513-cap-ha-automation-v5.md` | Create | Task 5: 마무리 학습 로그 |
| `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md` | (이 문서 자체) | plan commit 으로 branch 에 포함 |

LoC 추정: cap_ha_automation.c +120, cap_ha_control.c +15 → ~135 LoC code + docs.

---

## Pre-flight

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
git fetch origin main --quiet
git worktree add /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5 -b feat/cap-ha-control-v5 origin/main
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
# Bootstrap build env (per project memory note)
SRC=/Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
cp -a "$SRC"/application/edge_agent/components/gen_bmgr_codes/. application/edge_agent/components/gen_bmgr_codes/
cp "$SRC"/application/edge_agent/sdkconfig application/edge_agent/sdkconfig
```

빌드 명령은 모든 task 마지막에 동일:
```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
idf.py build
```

---

## Task 1: state-trigger `from` auto-fill (domain-pair opposite)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` — state 분기 (현재 line ~178-237)

배경: v4 의 state-trigger 가 `to: "on"` 만 보낼 때, HA 의 `state` platform 은 attribute-update 같은 non-transitioning 이벤트에도 fire (특히 zigbee binary_sensor 의 정기 health check). user 가 `from: "off"` 추가하면 HA 가 transition 강제. firmware 가 알아서 domain-pair opposite 를 채워주면 LLM 의 typed payload 가 단순해지면서도 동작 정확.

- [ ] **Step 1: opposite state 매핑 helper 추가**

`build_ha_trigger_array` 함수 바로 위 (line ~95 직전) 에 static helper 추가:

```c
/* Return the natural opposite of `to_val` for `domain`, or NULL if no
 * sensible default exists. Used in state trigger to force HA transition
 * semantics — `to: "on"` alone fires on every attribute-update, but
 * pairing with `from: "off"` makes HA require an actual transition. */
static const char *opposite_state(const char *domain, const char *to_val)
{
    if (!domain || !to_val) return NULL;
    if (strcmp(domain, "binary_sensor") == 0 ||
        strcmp(domain, "light") == 0 ||
        strcmp(domain, "switch") == 0 ||
        strcmp(domain, "input_boolean") == 0) {
        if (strcmp(to_val, "on")  == 0) return "off";
        if (strcmp(to_val, "off") == 0) return "on";
    } else if (strcmp(domain, "cover") == 0) {
        if (strcmp(to_val, "open")   == 0) return "closed";
        if (strcmp(to_val, "closed") == 0) return "open";
    } else if (strcmp(domain, "lock") == 0) {
        if (strcmp(to_val, "locked")   == 0) return "unlocked";
        if (strcmp(to_val, "unlocked") == 0) return "locked";
    }
    return NULL;
}
```

- [ ] **Step 2: state 분기에서 from auto-fill**

기존 (현 PR #6 merged, main HEAD 기준):
```c
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "state");
        cJSON_AddStringToObject(step, "entity_id", resolved_eid);
        cJSON_AddStringToObject(step, "to", to_j->valuestring);
        if (cJSON_IsString(from_j) && from_j->valuestring[0]) {
            cJSON_AddStringToObject(step, "from", from_j->valuestring);
        }
        cJSON_AddItemToArray(arr, step);
```

교체:
```c
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "state");
        cJSON_AddStringToObject(step, "entity_id", resolved_eid);
        cJSON_AddStringToObject(step, "to", to_j->valuestring);

        /* from 우선순위:
         * 1) caller 가 명시 → verbatim 통과 (override 가능)
         * 2) 없으면 domain-pair opposite auto-fill (transition 강제)
         * 3) opposite 가 없는 domain (e.g., media_player) 은 omit
         *    — HA 가 default 동작 사용 (any → to). 이 경우 attribute-update
         *      재발화 가능성 있음을 description 에 기재. */
        if (cJSON_IsString(from_j) && from_j->valuestring[0]) {
            cJSON_AddStringToObject(step, "from", from_j->valuestring);
        } else {
            /* resolved_eid 는 항상 "domain.local" 형식. 도메인 추출. */
            const char *dot = strchr(resolved_eid, '.');
            char dom[20] = {0};
            if (dot && (size_t)(dot - resolved_eid) < sizeof(dom)) {
                memcpy(dom, resolved_eid, dot - resolved_eid);
                const char *auto_from = opposite_state(dom, to_j->valuestring);
                if (auto_from) {
                    cJSON_AddStringToObject(step, "from", auto_from);
                    ESP_LOGI(TAG, "state trigger from auto-fill: %s -> %s (domain=%s)",
                             auto_from, to_j->valuestring, dom);
                }
            }
        }
        cJSON_AddItemToArray(arr, step);
```

- [ ] **Step 3: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK, `cap_ha_automation` 경고 없음.

- [ ] **Step 4: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): state-trigger from-pair auto-fill

v4 state-trigger 가 `to: "on"` 만 지정시 HA 가 attribute-update 마다
re-fire (zigbee binary_sensor 정기 refresh 같은 non-transition 이벤트).
사용자 수동 fix (from: "off" 추가) 로 동작 확인됨 — firmware 가 동일한
domain-pair opposite 를 auto-fill 해 LLM/사용자가 transition 의미를 매번
명시하지 않아도 정확히 동작하게.

매핑: binary_sensor / light / switch / input_boolean 의 on↔off,
cover 의 open↔closed, lock 의 locked↔unlocked. 그 외 도메인은 omit
(HA default 동작). 명시 from 은 그대로 통과 (override 가능).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `build_ha_condition_array` 신규 (time_range / weekday / state)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` — `build_ha_trigger_array` 함수 다음 위치에 신규 함수 추가 (현 line ~237 직후)

- [ ] **Step 1: build_ha_condition_array 추가**

`build_ha_trigger_array` 의 닫는 `}` 다음에 (state 분기 추가 위치 다음) 신규 함수 삽입:

```c
/* Translate a user condition spec into HA-native condition[].
 * Out: condition_out (caller owns, may be NULL on empty/error).
 * Supported kinds:
 *   - time_range: { kind: "time_range", after: "HH:MM", before: "HH:MM" }
 *       하나 또는 양쪽 모두 가능. after only = "after 시각부터 자정까지",
 *       before only = "자정부터 before 시각까지", 둘 다 = 그 구간.
 *   - weekday: { kind: "weekday", weekdays: [0..6] }  (0=Sunday)
 *   - state:   { kind: "state", entity: "<friendly|entity_id>", state: "..." }
 *
 * 'for' duration, OR/NOT 복합, template, numeric_state, sun condition 등은
 * v6 후속.
 */
static esp_err_t build_ha_condition_array(const cJSON *condition_in,
                                          cJSON **condition_out,
                                          char *err_msg, size_t err_msg_size)
{
    *condition_out = NULL;
    if (!cJSON_IsObject(condition_in)) {
        snprintf(err_msg, err_msg_size, "condition 은 객체여야 합니다.");
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *kind_j = cJSON_GetObjectItem(condition_in, "kind");
    const char *kind = cJSON_IsString(kind_j) ? kind_j->valuestring : NULL;
    if (!kind) {
        snprintf(err_msg, err_msg_size, "condition.kind 가 필요합니다.");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *arr = cJSON_CreateArray();
    cJSON *step = cJSON_CreateObject();

    if (strcmp(kind, "time_range") == 0) {
        const cJSON *after_j  = cJSON_GetObjectItem(condition_in, "after");
        const cJSON *before_j = cJSON_GetObjectItem(condition_in, "before");
        bool has_after  = cJSON_IsString(after_j)  && after_j->valuestring[0];
        bool has_before = cJSON_IsString(before_j) && before_j->valuestring[0];
        if (!has_after && !has_before) {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "time_range condition 에는 after / before 중 하나 이상 필요합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        /* HH:MM validate (간단 — 5 chars + ':' at index 2). */
        if (has_after && (strlen(after_j->valuestring) != 5 || after_j->valuestring[2] != ':')) {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size, "condition.after 는 'HH:MM' 형식이어야 합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        if (has_before && (strlen(before_j->valuestring) != 5 || before_j->valuestring[2] != ':')) {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size, "condition.before 는 'HH:MM' 형식이어야 합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        cJSON_AddStringToObject(step, "condition", "time");
        if (has_after) {
            char buf[12]; snprintf(buf, sizeof(buf), "%s:00", after_j->valuestring);
            cJSON_AddStringToObject(step, "after", buf);
        }
        if (has_before) {
            char buf[12]; snprintf(buf, sizeof(buf), "%s:00", before_j->valuestring);
            cJSON_AddStringToObject(step, "before", buf);
        }
    } else if (strcmp(kind, "weekday") == 0) {
        const cJSON *days = cJSON_GetObjectItem(condition_in, "weekdays");
        if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) == 0) {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "weekday condition 에는 weekdays 배열이 필요합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        static const char *DAY_NAMES[] = {"sun","mon","tue","wed","thu","fri","sat"};
        cJSON_AddStringToObject(step, "condition", "time");
        cJSON *weekday_arr = cJSON_CreateArray();
        cJSON *d = NULL;
        cJSON_ArrayForEach(d, days) {
            if (cJSON_IsNumber(d) && d->valueint >= 0 && d->valueint <= 6) {
                cJSON_AddItemToArray(weekday_arr,
                                     cJSON_CreateString(DAY_NAMES[d->valueint]));
            }
        }
        cJSON_AddItemToObject(step, "weekday", weekday_arr);
    } else if (strcmp(kind, "state") == 0) {
        const cJSON *entity_j = cJSON_GetObjectItem(condition_in, "entity");
        const cJSON *state_j  = cJSON_GetObjectItem(condition_in, "state");
        if (!cJSON_IsString(entity_j) || !entity_j->valuestring[0] ||
            !cJSON_IsString(state_j)  || !state_j->valuestring[0]) {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "state condition 에는 entity 와 state (둘 다 비어있지 않은 문자열) 가 필요합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        /* entity 해석 — state trigger 와 동일 패턴 (verbatim 또는 resolve). */
        const char *resolved_eid = NULL;
        cap_ha_entity_t e = {0};
        if (strchr(entity_j->valuestring, '.') != NULL &&
            strncmp(entity_j->valuestring, "board:", 6) != 0) {
            resolved_eid = entity_j->valuestring;
        } else if (cap_ha_resolve_target(entity_j->valuestring, &e) == ESP_OK) {
            resolved_eid = e.id;
        } else {
            cJSON_Delete(step); cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "condition.entity \"%s\" 를 해석하지 못했습니다.", entity_j->valuestring);
            return ESP_ERR_INVALID_ARG;
        }
        cJSON_AddStringToObject(step, "condition", "state");
        cJSON_AddStringToObject(step, "entity_id", resolved_eid);
        cJSON_AddStringToObject(step, "state", state_j->valuestring);
    } else {
        cJSON_Delete(step); cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 condition.kind 입니다 (%s). "
                 "time_range / weekday / state 만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_AddItemToArray(arr, step);
    *condition_out = arr;
    return ESP_OK;
}
```

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK. 함수 자체는 caller 없음 (Task 3 에서 호출 추가) — unused-function 경고 가능. 다음 task 가 즉시 호출하므로 무시 (또는 `__attribute__((unused))` 추가하지 말고 Task 3 까지 묶어 squash 도 가능).

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): build_ha_condition_array (time_range/weekday/state)

신규 condition typed payload 빌더. 3 kind:
- time_range (after/before "HH:MM", 하나 또는 양쪽)
- weekday (weekdays[] 0=Sun..6=Sat)
- state (entity friendly_name 또는 entity_id, state)

trigger.kind=weekly 의 auto-emit weekday condition 과 별도 — caller 가
원하는 condition 을 명시적으로 보냄. 단일 condition 객체로 ~70% demo
시나리오 커버 ("10시-18시", "평일만", "다른 entity 꺼져있을 때만").

OR/NOT 복합, for 지속시간, template, numeric_state, sun condition 은
v6 후속. 호출 통합은 다음 commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `do_create` + `do_update` condition 통합

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` — do_create 의 config 조립부 (현 line ~295-310) + do_update merge 부 (현 line ~548-565)

- [ ] **Step 1: do_create 에서 condition 읽기 + builder 호출 + merge**

기존 do_create 의 condition 관련 부분 (현 PR #6 merged 후 main 의 line ~295-310):
```c
    cJSON *trigger_arr = NULL, *condition_arr = NULL;
    char err_msg[160];
    if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                               err_msg, sizeof(err_msg)) != ESP_OK) {
        emit_auto_failure(output, output_size, err_msg);
        cJSON_Delete(action_arr);
        return ESP_OK;
    }

    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "alias", ... );
    cJSON_AddItemToObject(config, "triggers", trigger_arr);
    if (condition_arr) cJSON_AddItemToObject(config, "conditions", condition_arr);
    cJSON_AddItemToObject(config, "actions", action_arr);
```

교체:
```c
    cJSON *trigger_arr = NULL, *condition_arr = NULL;
    char err_msg[160];
    if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                               err_msg, sizeof(err_msg)) != ESP_OK) {
        emit_auto_failure(output, output_size, err_msg);
        cJSON_Delete(action_arr);
        return ESP_OK;
    }

    /* User-provided condition (independent of trigger.weekly's auto-emit). */
    const cJSON *user_cond = cJSON_GetObjectItem(root, "condition");
    if (cJSON_IsObject(user_cond)) {
        cJSON *user_cond_arr = NULL;
        char cond_err[160];
        if (build_ha_condition_array(user_cond, &user_cond_arr,
                                     cond_err, sizeof(cond_err)) != ESP_OK) {
            emit_auto_failure(output, output_size, cond_err);
            cJSON_Delete(action_arr);
            if (trigger_arr)   cJSON_Delete(trigger_arr);
            if (condition_arr) cJSON_Delete(condition_arr);
            return ESP_OK;
        }
        /* Merge: weekly auto-condition 이 있으면 AND, 없으면 user-only. */
        if (condition_arr) {
            cJSON *step = NULL;
            cJSON_ArrayForEach(step, user_cond_arr) {
                cJSON *dup = cJSON_Duplicate(step, true);
                if (dup) cJSON_AddItemToArray(condition_arr, dup);
            }
            cJSON_Delete(user_cond_arr);
        } else {
            condition_arr = user_cond_arr;
        }
    }

    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "alias", ... );  /* (기존 그대로) */
    cJSON_AddItemToObject(config, "triggers", trigger_arr);
    if (condition_arr) cJSON_AddItemToObject(config, "conditions", condition_arr);
    cJSON_AddItemToObject(config, "actions", action_arr);
```

(주의: alias 라인은 기존 코드 그대로 유지 — 위 예시의 `...` 부분은 실제 코드에서 `(cJSON_IsString(alias_j) && alias_j->valuestring[0]) ? alias_j->valuestring : entity.friendly_name`)

- [ ] **Step 2: do_update 에서 condition 처리**

기존 do_update 의 trigger merge 부 (현 line ~548-565):
```c
        cJSON_DeleteItemFromObject(cfg, "trigger");
        cJSON_DeleteItemFromObject(cfg, "triggers");
        cJSON_AddItemToObject(cfg, "triggers", trigger_arr);
        cJSON_DeleteItemFromObject(cfg, "condition");
        cJSON_DeleteItemFromObject(cfg, "conditions");
        if (condition_arr) cJSON_AddItemToObject(cfg, "conditions", condition_arr);
    }
```

(이 블록은 trigger 가 caller 에서 들어왔을 때만 진입. condition 만 update 하는 케이스 별도 처리 필요.)

이 블록 다음 (`}` 뒤) 에 추가:
```c
    /* condition 만 단독 update (trigger 안 보내고 condition 만 보낸 경우),
     * 또는 trigger 와 함께 update (위 블록에서 weekly auto-condition 만
     * 들어가 있을 수 있음) 모두 처리. */
    const cJSON *user_cond = cJSON_GetObjectItem(root, "condition");
    if (cJSON_IsObject(user_cond)) {
        cJSON *user_cond_arr = NULL;
        char cond_err[160];
        if (build_ha_condition_array(user_cond, &user_cond_arr,
                                     cond_err, sizeof(cond_err)) != ESP_OK) {
            cJSON_Delete(cfg);
            emit_auto_failure(output, output_size, cond_err);
            return ESP_OK;
        }
        /* user 가 condition 명시했으면 기존 conditions 를 완전 교체.
         * 이전 trigger update 블록에서 weekly auto-condition 이 들어갔을 수
         * 있으므로 그것까지 함께 교체됨 — 사용자 의도 우선. */
        cJSON_DeleteItemFromObject(cfg, "condition");
        cJSON_DeleteItemFromObject(cfg, "conditions");
        cJSON_AddItemToObject(cfg, "conditions", user_cond_arr);
    }
```

- [ ] **Step 3: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK.

- [ ] **Step 4: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): wire condition payload into do_create + do_update

do_create: root.condition 객체 발견시 build_ha_condition_array 호출,
weekly trigger 의 auto-emit weekday condition 과 AND merge (배열 concat).
do_update: condition 만 단독 update 또는 trigger 와 함께 update 모두 처리,
user condition 명시시 기존 conditions 완전 교체 (의도 우선).

이로써 "도어센서 ON / 10-18시 사이만 / 조명 ON" 같은 시나리오를 typed
payload 단일 호출로 등록 가능.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: descriptor schema + description 업데이트

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` — descriptor `input_schema_json` (현 line ~65-86) + `cap_ha_compose_description` 문구 (현 line ~99-110)

- [ ] **Step 1: input_schema_json 에 condition 객체 추가**

기존 schema 의 `trigger` 객체 정의 직후 (closing `}}` 다음, target 정의 직전) 에 condition 객체 삽입:

기존 (state trigger 추가 후):
```c
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"state\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id...\"},"
                "\"to\":{\"type\":\"string\",\"description\":\"state: required target state ('on'/'off'/'open'/'closed' etc).\"},"
                "\"from\":{\"type\":\"string\",\"description\":\"state: optional previous state. If omitted, fires on any transition into 'to'.\"}"
              "}},"
              "\"target\":{\"type\":\"string\",\"description\":\"HA entity friendly name or entity_id. board:* targets are not supported in v4 (HA-side automation only).\"},"
```

교체 (state trigger 의 from description 도 같이 업데이트):
```c
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"state\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id...\"},"
                "\"to\":{\"type\":\"string\",\"description\":\"state: required target state ('on'/'off'/'open'/'closed' etc).\"},"
                "\"from\":{\"type\":\"string\",\"description\":\"state: optional previous state. If omitted, firmware auto-fills the domain-pair opposite (binary_sensor/light/switch on<->off, cover open<->closed, lock locked<->unlocked) to force a HA transition. Specify explicitly to override.\"}"
              "}},"
              "\"condition\":{\"type\":\"object\",\"description\":\"Optional gate that must be true at trigger time. Single object — for AND, compose at the calling layer (v6 will add OR/NOT).\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"time_range\",\"weekday\",\"state\"]},"
                "\"after\":{\"type\":\"string\",\"description\":\"time_range: 'HH:MM' lower bound (inclusive). Omit for 'before only'.\"},"
                "\"before\":{\"type\":\"string\",\"description\":\"time_range: 'HH:MM' upper bound (inclusive). Omit for 'after only'.\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekday: 0=Sunday\"},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id whose current state is gated.\"},"
                "\"state\":{\"type\":\"string\",\"description\":\"state: required current state value (e.g., 'off' to fire only when the entity is off).\"}"
              "}},"
              "\"target\":{\"type\":\"string\",\"description\":\"HA entity friendly name or entity_id. board:* targets are not supported in v4 (HA-side automation only).\"},"
```

- [ ] **Step 2: cap_ha_compose_description 문구 보강**

기존 (PR #6 merged 후):
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

교체:
```c
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target' or in trigger.entity / condition.entity): %s. "
             "board:* targets (onboard RGB) are NOT supported here — those require on-device automation (deferred). "
             "Trigger kinds: 'daily_time' (HH:MM), 'weekly' (HH:MM + weekdays[]), 'interval' (interval_ms ≥ 2000), "
             "'state' (entity + to; firmware auto-fills 'from' as the domain-pair opposite to force HA transition semantics — pass 'from' explicitly to override). "
             "Optional 'condition' object gates the trigger: 'time_range' (after/before HH:MM), 'weekday' (weekdays[]), 'state' (entity + state). "
             "Example: door sensor opens between 10:00–18:00 → light on. "
             "Use 'create' (assigns automation_id), 'update' (needs automation_id), 'remove' (needs automation_id), "
             "'list' (returns existing automations), 'trigger_now' (force-fire by id), 'enable'/'disable' (toggle by id). "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
```

⚠️ **버퍼 크기 점검:** `s_ha_automation_description[1024]` 가 v4 review 에서 "tight" NIT 였음. condition 설명 추가로 base format string + friendly_names 보간 합쳐 1024 근접 가능. **사전 대비:** 같은 commit 에서 `s_ha_automation_description[1024]` → `[1536]` bump.

기존:
```c
static char s_ha_automation_description[1024];
```

교체:
```c
static char s_ha_automation_description[1536];
```

- [ ] **Step 3: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK, `-Werror=format-truncation` 경고 없음.

- [ ] **Step 4: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_control): condition schema + description buffer bump

descriptor 의 input_schema_json 에 'condition' 객체 properties 추가
(kind: time_range/weekday/state + 각 kind 별 필드). trigger.from
description 도 auto-fill 동작 명시. cap_ha_compose_description 문구에
condition 사용 예시 ("도어센서 / 10-18시 사이 / 조명 ON") 한 줄 추가.

s_ha_automation_description 버퍼 1024→1536: v4 review NIT 였던 tight
size 가 condition 추가로 임박 (-Werror=format-truncation 사전 회피).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Learn log + 마무리

**Files:**
- Create: `docs/learn/20260513-cap-ha-automation-v5.md`

- [ ] **Step 1: Learn log 작성**

```markdown
# cap_ha_automation v5 — state-trigger transition fix + condition typed payload

> **컨텍스트:** v4 PR #5/#6 머지 (2026-05-12) 직후 E2E 에서 사용자가 발견한 state-trigger 의 attribute-update 재발화 버그 + 미루어둔 Track C (condition typed payload) 를 v5 로 묶어 ship.

## 무엇을 만들었나

| Task | Commit (head) | 핵심 변경 |
|---|---|---|
| 1 | feat: state-trigger from-pair auto-fill | `opposite_state(domain, to)` helper + state 분기에 from auto-fill. binary_sensor/light/switch/input_boolean on↔off, cover open↔closed, lock locked↔unlocked. 명시 from 은 override. |
| 2 | feat: build_ha_condition_array | 3 kind (time_range / weekday / state) condition builder. 각 kind 별 필드 validation, state 는 entity friendly_name resolve. |
| 3 | feat: condition 통합 (do_create + do_update) | root.condition 읽기 + build_ha_condition_array + weekly auto-condition 과 AND merge. do_update 는 user condition 명시시 기존 완전 교체. |
| 4 | feat: descriptor schema + description | input_schema_json 에 condition 객체 + trigger.from 설명 갱신. 버퍼 1024→1536 (review NIT 사전 대비). |

## 무엇을 배웠나

### 1. HA `state` platform 의 attribute-update 재발화는 firmware-side workaround 가 정답

v4 PR #6 ship 후 사용자 실사용 시 `to: "on"` 만으로 등록한 자동화가 의도와 다르게 매번 fire — HA 의 zigbee binary_sensor 가 정기 health-check 으로 state event 를 보내고 platform 이 "to 매칭" 으로 잡음. 사용자 수동 수정 (from: "off" 추가) 으로 transition 강제 → 해결.

**원칙:** HA 같은 외부 시스템은 docs 의 의도 (transition 만 fire) 와 실제 동작 (attribute-update 도 fire) 차이가 잦다. firmware 가 "사용자 의도" (door 열리면 → 자동화) 와 "HA 정확한 호출 방식" (from+to transition) 사이를 메우는 게 typed tool 의 일이다. domain-aware default 가 LLM/사용자의 일을 줄이고 동작 안정성도 보장.

### 2. condition 노출은 trigger 추가의 70% 시나리오 추가 커버

v4 의 state-trigger 단독은 "도어 열리면 조명" 시나리오만 처리. v5 의 condition (특히 time_range) 추가로 "도어 열리면 + 10-18시 사이만 + 조명" 같은 실용 시나리오 등록 가능. weekly trigger 의 auto-emit weekday condition 과 user-provided condition 의 AND merge 로 "주말만 + 시간 윈도우" 도 가능.

**원칙:** schema 확장은 단일 기능 단위가 아니라 시나리오 단위 (trigger + condition + action 묶음) 로 평가하면 demo 직전 surprise 줄어든다. v4 의 trigger-only 결정도 의미는 있었으나 (incremental ship) 실제 demo 에서 condition 부족이 빠르게 드러남.

### 3. domain-pair opposite 매핑은 도메인별로 명시적으로 둔다

`opposite_state` 함수가 도메인별 분기 (binary_sensor / light / switch / input_boolean / cover / lock) — table-driven 도 가능하지만 v5 수준에선 switch-case 가 명확. 새 도메인 추가시 명시적 변경 강제 (table 의 빈 항목 missed 위험 방지). 매핑 없는 도메인 (media_player, fan 등) 은 omit + description 에 명시.

**원칙:** 외부 시스템의 도메인별 동작 차이는 firmware 의 small switch-case 가 어차피 가독성 좋다. 매핑 누락은 의도된 default 동작 (no auto-fill) 으로 graceful fail.

## 다음 v6 후보

- `for` 지속시간 condition + trigger ("3분 이상 켜져 있을 때만")
- OR/NOT 복합 condition
- numeric_state trigger + sensor.* 도메인 재도입 (별도 cache 분리로 NVS 폭발 방지)
- template / sun condition
- attribute change trigger (state 외 attribute 변경 감지)
- board:onboard_rgb 자동화 (cap_scheduler subset, Option A path)

## 참고

- 관련 PR: TBD (이 plan 실행시 생성)
- v4 관련 PRs: #5 (c04c845 정제), #6 (state-trigger) — main 머지 완료
- E2E 학습: `docs/learn/20260512-cap-ha-automation-prs-review.md` (v4 review 사이클)
```

- [ ] **Step 2: Plan 파일도 같이 commit (이 문서)**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5
# Plan 파일은 v4 worktree 에서 작성됨 — 복사
cp /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md \
   smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md
git add docs/learn/20260513-cap-ha-automation-v5.md \
        smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md
git commit -m "$(cat <<'EOF'
docs(plan+learn): cap_ha_automation v5

state-trigger transition fix (firmware-side from auto-fill) + condition
typed payload (time_range/weekday/state) — v4 E2E 에서 사용자 발견한
attribute-update 재발화 버그 + Track C deferred 통합. plan 파일과
learn log 동시 ship.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## E2E 검증 (사용자 영역, 보드 + HA 가용시)

각 task 별 빌드만 자동, 보드 E2E 는 user-driven. 4 task 모두 commit 후:

1. **빌드 + flash:**
   ```bash
   cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v5/application/edge_agent
   source ~/.gstack/projects/esp-claw/secrets.env
   idf.py -p $ESP_PORT flash
   ```

2. **Task 1 검증 (from auto-fill):** state-trigger 자동화 등록 (entity_id 로 어떤 binary_sensor), HA UI 자동화 trace 에서 trigger 가 `entity_id: ..., from: off, to: on` 으로 보이는지 확인. 실제 센서를 열어 fire, 닫아도 같은 자동화가 다시 fire 안 되는지 (transition 강제 검증) — v4 시점엔 닫아도 attribute-update 로 다시 fire 가능했음.

3. **Task 2-4 검증 (condition):** 
   ```
   ha_control --automation {"action":"create","target":"light.smart_bulb","device_action":"turn_on","trigger":{"kind":"state","entity":"binary_sensor.<your_sensor>","to":"on"},"condition":{"kind":"time_range","after":"10:00","before":"18:00"},"alias":"v5-cond-test"}
   ```
   - 09:30 시점에 센서 열어도 light 안 켜짐
   - 11:00 시점에 센서 열면 light 켜짐
   - HA UI 의 자동화 conditions 섹션에 time 조건 보이는지 확인

4. **weekly + user condition merge:**
   ```
   ha_control --automation {"action":"create","target":"light.smart_bulb","device_action":"turn_on","trigger":{"kind":"weekly","time":"09:00","weekdays":[1,2,3,4,5]},"condition":{"kind":"time_range","after":"08:00","before":"10:00"},"alias":"v5-merge-test"}
   ```
   - HA config 의 conditions 가 [weekday 조건, time_range 조건] 2개 array 로 보이는지

5. **자연어 path (LLM-driven):**
   > "현관 도어센서가 10시에서 18시 사이에 열리면 화장실 조명 켜줘"
   
   LLM 이 trigger.state + condition.time_range 묶어 단일 호출 보내는지.

---

## Self-Review

**Spec coverage:**
- (A) state-trigger from auto-fill → Task 1 (opposite_state + state branch). ✅
- (B) condition typed payload → Task 2 (builder) + Task 3 (do_create/do_update 통합) + Task 4 (schema). ✅
- 학습 + plan ship → Task 5. ✅

**Placeholder scan:** none.

**Type consistency:** `opposite_state` 시그니처 (`const char*, const char*) → const char*`) Task 1+Task 2 모두 동일. `build_ha_condition_array` 의 out-param 패턴 (`cJSON **, err_msg buf`) 이 `build_ha_trigger_array` 와 일치. condition_arr ownership 흐름: builder → caller → cJSON_AddItemToObject (config 에 trans-owned). do_update 의 user_cond_arr ownership: builder → 직접 `cJSON_AddItemToObject` 로 cfg 에 trans-owned.

**Memory ownership 검증:**
- Task 3 의 `cJSON_Duplicate(step, true)` 결과는 dup, AddItemToArray 에 ownership 이전. 원본 user_cond_arr 는 끝에 `cJSON_Delete(user_cond_arr)` 로 해제.
- Error path 의 `cJSON_Delete(action_arr)` + `cJSON_Delete(trigger_arr/condition_arr)` 누락 없는지 step 별 점검.

**Edge cases:**
- `condition` 객체에 `kind` 없으면 → ESP_ERR_INVALID_ARG + "kind 가 필요합니다" 메시지.
- `time_range` 에 after/before 둘 다 없으면 → 에러.
- `state` condition 의 entity 해석 실패 → 에러 + 후보 노출 (state-trigger 와 동일 패턴).
- `condition` 객체 자체가 array (잘못된 타입) → cJSON_IsObject(false) → "객체여야 합니다" 에러.

---

## Execution Handoff

플랜 저장 완료 (이 파일). 두 가지 옵션:

1. **Subagent-Driven (recommended)** — Task 1–5 fresh subagent dispatch.
2. **Inline Execution** — 현재 세션에서 task 별 checkpoint.

기본 fork 위치: `origin/main` HEAD (PR #5/#6 머지 후). 단일 worktree (`.claude/worktrees/v5`) — v5 의 두 항목은 같은 함수군 (`build_ha_trigger_array` + 새 `build_ha_condition_array` + do_create/do_update) 을 거의 동시에 건드려 분리 의미 없음, 직렬 진행.
