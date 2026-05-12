# cap_ha_automation — c04c845 후속 정제 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** c04c845 가 도입한 `resolve_entity_id_by_config_id()` 의 세 가지 후속 정제. (1) `/api/states` 64KB GET 매번 발생 → NVS 캐시로 fast-path. (2) `do_service` 의 silent fallback (`automation.<id>` 추정) 제거 → resolver miss explicit fail. (3) cache layer 도입으로 헬퍼 책임이 분리되도록 시그니처 정리.

**Architecture:** `config_id → entity_id` 매핑을 NVS `ha_ctl/eid_cache` 에 JSON 블롭으로 저장 (최대 32 항목, FIFO drop). `resolve_entity_id_by_config_id()` 는 cache → HA states GET → cache write-through 순서. `do_create` 는 성공 후 cache 갱신, `do_remove` 는 invalidate. `do_service` 는 cache+HA 둘 다 miss 시 "automation_id 를 찾을 수 없습니다 (list 명령으로 확인)" 명시적 실패 — c04c845 의 `snprintf(entity_id, ..., "automation.%s", id)` silent fallback 라인을 제거.

**Tech Stack:** ESP-IDF v5.5.4, NVS (existing `ha_ctl` namespace), cJSON.

**Source:** c04c845 commit + v4 ship 후 사용자 후속 항목 1 ("c04c845 fix 후속 정제 — entity_id NVS 캐싱 / resolver miss explicit fail / helper 시그니처").

**Worktree:** Implement in `feat/cap-ha-control-v4` (v4 worktree). For parallel execution with state-trigger track, fork a fresh worktree off `feat/cap-ha-control-v4` HEAD (`95a35cb`).

---

## File Plan

| 파일 | 작업 | 책임 |
|---|---|---|
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` | Modify | NVS 키 매크로 `CAP_HA_NVS_KEY_EID_CACHE` 추가 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` | Modify | static eid_cache_{lookup,put,invalidate} 추가, `resolve_entity_id_by_config_id` cache-through, `do_create` write-through, `do_remove` invalidate, `do_service` explicit-fail |
| `docs/learn/20260512-cap-ha-automation-c04c845-followups.md` | Create | Task 6 마무리 학습 로그 |

---

## Pre-flight

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git status -s
git log --oneline -5
```

Expected: 깨끗하거나 `smarthome-docs/...followups.md` 1개 modified. 최신 commit `95a35cb` (docs(learn): on-board E2E findings). 보드 + USB + HA 192.168.1.94:8123 가용 (E2E 검증용; 빌드만 하려면 불필요).

빌드 명령은 모든 task 마지막에 동일:
```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build
```

---

## Task 1: NVS 캐시 헬퍼 + `resolve_entity_id_by_config_id` cache-through

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c:192-237` (replace existing helper)

- [ ] **Step 1: NVS 키 매크로 추가**

`cap_ha_control_internal.h` 의 기존 `CAP_HA_NVS_KEY_INSECURE` 줄 아래에 추가:

```c
#define CAP_HA_NVS_KEY_EID_CACHE   "eid_cache"   /* JSON: {"<config_id>": "<entity_id>"} */
#define CAP_HA_EID_CACHE_MAX       32            /* config_id 매핑 최대 항목 (FIFO drop) */
```

(NVS 키 길이 제한 15 — `eid_cache` 9 char 안전.)

- [ ] **Step 2: `cap_ha_automation.c` 상단 include 보강**

기존 include 블록 (line 5-12) 아래에 추가:

```c
#include "nvs.h"
```

- [ ] **Step 3: 캐시 헬퍼 3개 추가 (`resolve_entity_id_by_config_id` 바로 위에 삽입, 현재 line ~192)**

```c
/* ─── entity_id 캐시 (NVS, JSON 블롭 단일 키) ─────────────────────────
 * HA modern schema 는 자동화 entity_id 를 alias slug 에서 파생하므로
 * config_id (esp_claw_<ts>) → entity_id (automation.<slug>) 매핑은
 * 외부로부터 받아야 한다 (cap_ha_http_get_states). 매 service 호출마다
 * 64KB GET 은 비싸므로 NVS 에 캐싱. 32 항목 cap, 초과시 임의 1개 drop.
 * 단일 블롭이라 lookup/put 모두 read-parse-mutate-write 라 동기 성능
 * 비싸지 않지만 (수십 byte JSON), 빈번한 write 가 NVS 마모 유발 가능 —
 * do_create / do_remove 만 mutate 이고 lookup 은 read-only 라 충분.
 */
static esp_err_t eid_cache_load(cJSON **out_obj)
{
    *out_obj = NULL;
    nvs_handle_t h;
    if (nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return ESP_ERR_NOT_FOUND;
    size_t need = 0;
    esp_err_t err = nvs_get_blob(h, CAP_HA_NVS_KEY_EID_CACHE, NULL, &need);
    if (err != ESP_OK || need == 0) { nvs_close(h); return ESP_ERR_NOT_FOUND; }
    char *blob = malloc(need + 1);
    if (!blob) { nvs_close(h); return ESP_ERR_NO_MEM; }
    err = nvs_get_blob(h, CAP_HA_NVS_KEY_EID_CACHE, blob, &need);
    nvs_close(h);
    if (err != ESP_OK) { free(blob); return ESP_ERR_NOT_FOUND; }
    blob[need] = '\0';
    cJSON *obj = cJSON_Parse(blob);
    free(blob);
    if (!cJSON_IsObject(obj)) { if (obj) cJSON_Delete(obj); return ESP_ERR_NOT_FOUND; }
    *out_obj = obj;
    return ESP_OK;
}

static esp_err_t eid_cache_store(cJSON *obj)
{
    char *blob = cJSON_PrintUnformatted(obj);
    if (!blob) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) { free(blob); return err; }
    err = nvs_set_blob(h, CAP_HA_NVS_KEY_EID_CACHE, blob, strlen(blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    free(blob);
    return err;
}

static esp_err_t eid_cache_lookup(const char *config_id,
                                  char *out_entity_id, size_t out_size)
{
    if (!config_id || !out_entity_id || out_size == 0) return ESP_ERR_INVALID_ARG;
    cJSON *obj = NULL;
    if (eid_cache_load(&obj) != ESP_OK) return ESP_ERR_NOT_FOUND;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, config_id);
    esp_err_t r = ESP_ERR_NOT_FOUND;
    if (cJSON_IsString(v) && v->valuestring[0]) {
        snprintf(out_entity_id, out_size, "%s", v->valuestring);
        r = ESP_OK;
    }
    cJSON_Delete(obj);
    return r;
}

static esp_err_t eid_cache_put(const char *config_id, const char *entity_id)
{
    if (!config_id || !entity_id) return ESP_ERR_INVALID_ARG;
    cJSON *obj = NULL;
    if (eid_cache_load(&obj) != ESP_OK) {
        obj = cJSON_CreateObject();
        if (!obj) return ESP_ERR_NO_MEM;
    }
    /* 동일 키 있으면 replace, 없으면 추가. cap 초과시 첫 항목 drop. */
    cJSON_DeleteItemFromObjectCaseSensitive(obj, config_id);
    if (cJSON_GetArraySize(obj) >= CAP_HA_EID_CACHE_MAX) {
        cJSON *first = obj->child;
        if (first && first->string) {
            ESP_LOGI(TAG, "eid_cache full, drop oldest entry %s", first->string);
            cJSON_DeleteItemFromObjectCaseSensitive(obj, first->string);
        }
    }
    cJSON_AddStringToObject(obj, config_id, entity_id);
    esp_err_t err = eid_cache_store(obj);
    cJSON_Delete(obj);
    return err;
}

static esp_err_t eid_cache_invalidate(const char *config_id)
{
    if (!config_id) return ESP_ERR_INVALID_ARG;
    cJSON *obj = NULL;
    if (eid_cache_load(&obj) != ESP_OK) return ESP_OK; /* no cache → nothing to drop */
    cJSON_DeleteItemFromObjectCaseSensitive(obj, config_id);
    esp_err_t err = eid_cache_store(obj);
    cJSON_Delete(obj);
    return err;
}
```

- [ ] **Step 4: `resolve_entity_id_by_config_id` 를 cache-through 로 재작성**

기존 `static esp_err_t resolve_entity_id_by_config_id(...)` 함수 본체 (line ~201–237) 를 통째로 교체:

```c
static esp_err_t resolve_entity_id_by_config_id(const char *config_id,
                                                char *out_entity_id,
                                                size_t out_size)
{
    if (!config_id || !*config_id || !out_entity_id || out_size == 0)
        return ESP_ERR_INVALID_ARG;
    out_entity_id[0] = '\0';

    /* 1) NVS 캐시 fast-path. */
    if (eid_cache_lookup(config_id, out_entity_id, out_size) == ESP_OK)
        return ESP_OK;

    /* 2) HA /api/states slow-path + 캐시 갱신. */
    char *states = malloc(CAP_HA_STATES_BUF_BYTES);
    if (!states) return ESP_ERR_NO_MEM;
    esp_err_t err = cap_ha_http_get_states(states, CAP_HA_STATES_BUF_BYTES);
    if (err != ESP_OK) { free(states); return err; }

    cJSON *arr = cJSON_Parse(states);
    free(states);
    if (!cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, arr) {
        const cJSON *eid = cJSON_GetObjectItemCaseSensitive(e, "entity_id");
        if (!cJSON_IsString(eid) ||
            strncmp(eid->valuestring, "automation.", 11) != 0) continue;
        const cJSON *attr = cJSON_GetObjectItemCaseSensitive(e, "attributes");
        const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(attr, "id");
        if (cJSON_IsString(id_j) && strcmp(id_j->valuestring, config_id) == 0) {
            snprintf(out_entity_id, out_size, "%s", eid->valuestring);
            result = ESP_OK;
            break;
        }
    }
    cJSON_Delete(arr);
    if (result == ESP_OK) {
        esp_err_t cerr = eid_cache_put(config_id, out_entity_id);
        if (cerr != ESP_OK) ESP_LOGW(TAG, "eid_cache_put failed: %s", esp_err_to_name(cerr));
    }
    return result;
}
```

- [ ] **Step 5: 빌드 검증**

Run:
```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -30
```
Expected: `Project build complete.` + no warnings about cap_ha_automation.

- [ ] **Step 6: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h \
        components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): NVS cache for config_id → entity_id

c04c845 introduced resolve_entity_id_by_config_id() which calls
/api/states (64KB GET) on every do_service invocation. Cache the
result in NVS so subsequent enable/disable/trigger_now skip the
upstream round-trip.

Single-blob JSON layout under ha_ctl/eid_cache, 32-entry cap with
FIFO drop on overflow. Cache miss falls through to HA states GET
and write-throughs the result.

do_create/do_remove cache integration come in follow-up commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `do_create` write-through

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c:345-349` (success path)

- [ ] **Step 1: do_create 성공 직후 cache 갱신 추가**

기존 c04c845 line 345-349:
```c
    char resolved_eid[96] = {0};
    if (resolve_entity_id_by_config_id(auto_id, resolved_eid, sizeof(resolved_eid)) != ESP_OK) {
        snprintf(resolved_eid, sizeof(resolved_eid), "automation.%s", auto_id);
        ESP_LOGW(TAG, "post-create entity_id lookup miss; using fallback %s", resolved_eid);
    }
```

교체 (cache 갱신 한 줄 추가; fallback 로직은 do_create 에서는 유지 — HA eventual consistency 때문):
```c
    char resolved_eid[96] = {0};
    esp_err_t reid_err = resolve_entity_id_by_config_id(auto_id, resolved_eid, sizeof(resolved_eid));
    if (reid_err == ESP_OK) {
        /* cache put 은 resolve_entity_id_by_config_id slow-path 가 이미 했지만
         * 명시적 재호출은 cache fast-path 적중 케이스 (이 시점엔 거의 없음)
         * 에서도 일관성 보장. NVS dup write 는 1ms 미만. */
        eid_cache_put(auto_id, resolved_eid);
    } else {
        /* eventual consistency: POST 직후 /api/states reflect 안된 경우.
         * fallback 으로 "automation.<auto_id>" 형태 사용하되 cache 에는
         * 넣지 않음 — 잘못된 mapping 으로 service 호출이 silently no-op 할 수
         * 있어 cache 오염이 더 위험. */
        snprintf(resolved_eid, sizeof(resolved_eid), "automation.%s", auto_id);
        ESP_LOGW(TAG, "post-create entity_id lookup miss (err=%s); "
                      "using fallback %s without caching",
                 esp_err_to_name(reid_err), resolved_eid);
    }
```

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK.

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): do_create write-through eid cache

After successful resolution post-PUT, persist (auto_id, resolved_eid)
into eid_cache so subsequent service calls hit the fast-path. On
eventual-consistency miss (HA /api/states not yet reflected), keep
the fallback string but do NOT cache it — a wrong mapping cached
here would cause silent no-ops in later trigger/enable/disable.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `do_remove` invalidate

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c:367-401` (do_remove)

- [ ] **Step 1: do_remove 성공 직후 cache 무효화 추가**

기존 do_remove 의 success 응답 build 직전 (현재 line ~387–388 의 `cap_ha_http_reload_automations(...)` 호출 직후) 에 추가:

기존:
```c
    int reload_status = 0;
    cap_ha_http_reload_automations(&reload_status, http_resp, sizeof(http_resp));

    cJSON *resp = cJSON_CreateObject();
```

교체:
```c
    int reload_status = 0;
    cap_ha_http_reload_automations(&reload_status, http_resp, sizeof(http_resp));

    /* HA 에서 사라졌으니 cache 항목도 invalidate. */
    eid_cache_invalidate(id);

    cJSON *resp = cJSON_CreateObject();
```

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK.

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
feat(cap_ha_automation): do_remove invalidates eid cache

After DELETE + reload succeed, drop the (config_id, entity_id)
mapping so a subsequent re-create with the same id (unlikely but
possible) doesn't pick up a stale entry.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `do_service` explicit-fail on resolver miss

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c:462-503` (do_service)

- [ ] **Step 1: silent fallback 제거 + 명시적 실패 메시지**

기존 c04c845 line ~474-480:
```c
    char entity_id[96];
    /* Try resolving via attributes.id first (covers our esp_claw_<ts> form).
     * If not found, fall back to "automation.<id>" — works when caller passed
     * the already-slugified entity_id local part. */
    if (resolve_entity_id_by_config_id(id, entity_id, sizeof(entity_id)) != ESP_OK) {
        snprintf(entity_id, sizeof(entity_id), "automation.%s", id);
    }
```

교체:
```c
    char entity_id[96];
    /* config_id (esp_claw_<ts>) 형식이면 resolver 가 cache + HA states 로 해석.
     * 사용자가 이미 slug 화된 'automation.<slug>' 형식의 entity_id 를 그대로
     * 넘긴 경우엔 resolver 가 NOT_FOUND 반환 — 그때만 입력값 그대로 사용. */
    if (strncmp(id, "esp_claw_", 9) == 0) {
        /* 우리 firmware 가 만든 config_id 인데 매핑 못 찾으면 fail. silent
         * fallback ('automation.esp_claw_<ts>') 은 HA 가 slug 와 다른 entity
         * 로 등록했을 때 service 가 200 응답하면서도 실제론 no-op 하는
         * 버그를 만든다 (c04c845 commit message 참조). */
        if (resolve_entity_id_by_config_id(id, entity_id, sizeof(entity_id)) != ESP_OK) {
            char msg[240];
            snprintf(msg, sizeof(msg),
                     "automation_id '%s' 에 해당하는 HA 자동화를 찾을 수 없습니다. "
                     "list 명령으로 현재 등록된 entity_id 를 확인하세요.", id);
            emit_auto_failure(output, output_size, msg);
            return ESP_OK;
        }
    } else {
        /* 'living_room_lights' 같은 사용자 slug — resolver 가 못 잡지만 그대로
         * 사용 가능한 패턴. resolver 가 잡으면 더 정확하니 시도하되, 실패해도
         * verbatim 통과. */
        if (resolve_entity_id_by_config_id(id, entity_id, sizeof(entity_id)) != ESP_OK) {
            snprintf(entity_id, sizeof(entity_id), "automation.%s", id);
        }
    }
```

- [ ] **Step 2: 빌드 검증**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4/application/edge_agent
idf.py build 2>&1 | tail -10
```
Expected: build OK.

- [ ] **Step 3: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "$(cat <<'EOF'
fix(cap_ha_automation): do_service fails explicitly on eid miss

c04c845's silent fallback (snprintf "automation.%s", id) reintroduced
the original demo-blocker bug whenever the resolver miss path was hit
on an esp_claw_<ts> config_id — HA would 200-respond with no actual
effect, exactly the failure mode that resolution was meant to prevent.

Only fall back to verbatim "automation.<id>" when the caller passed
something that wasn't our config_id pattern (i.e., a user-managed
slug like 'living_room_lights'). For esp_claw_<ts> ids that miss
both cache and HA states, emit an explicit failure pointing users
to the list action.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Learn log + 마무리

**Files:**
- Create: `docs/learn/20260512-cap-ha-automation-c04c845-followups.md`

- [ ] **Step 1: Learn log 작성**

```markdown
# cap_ha_automation — c04c845 follow-up 정제 (NVS eid 캐시 + explicit fail)

> **컨텍스트:** v4 ship 직후 (`feat/cap-ha-control-v4` HEAD = 95a35cb) c04c845 가 도입한 resolve_entity_id_by_config_id 의 3가지 정제: (a) NVS 캐싱, (b) do_service silent fallback 제거, (c) cache write-through/invalidate 로 헬퍼 책임 분리.

## 무엇을 만들었나

| Task | Commit (head) | 핵심 변경 |
|---|---|---|
| 1 | feat: NVS cache | `ha_ctl/eid_cache` (JSON blob, 32 cap, FIFO drop) + `eid_cache_{lookup,put,invalidate}` static helpers. `resolve_entity_id_by_config_id` 가 cache → HA states GET → write-through 순서. |
| 2 | feat: do_create write-through | POST + reload 성공 후 매핑 cache. eventual consistency miss 시 fallback 사용하되 cache 오염 방지 위해 NVS 에는 안 적음. |
| 3 | feat: do_remove invalidate | DELETE 성공 후 cache 키 drop. |
| 4 | fix: do_service explicit-fail | esp_claw_<ts> 패턴은 resolver miss 시 verbatim fallback 금지 — c04c845 silent no-op 버그의 정확한 원인. 사용자 slug 는 verbatim 폴백 유지. |

## 무엇을 배웠나

### 1. c04c845 의 fallback 한 줄이 가장 위험한 코드였다

c04c845 가 `entity_id silent no-op` 버그를 잡으려고 resolver 를 도입했는데, 같은 함수 안에 `if (... != ESP_OK) snprintf("automation.%s", id);` 한 줄이 들어가면서 **resolver miss = 원래 버그 그대로 재발** 이 됐다. fallback 의 의도는 "사용자가 이미 slug 화된 entity_id 를 넘긴 케이스" 였지만 esp_claw_<ts> 패턴까지 똑같이 처리해 결국 의도와 정반대 효과. 패턴 분기 (`strncmp(id, "esp_claw_", 9) == 0`) 한 줄로 두 케이스 분리.

**원칙:** "안전한 fallback" 처럼 보이는 코드도 입력 분류 없이 일률 적용하면 원래 fix 의 의미를 무효화할 수 있다. 입력 origin 별로 fallback policy 다르게.

### 2. NVS cache 는 단일 JSON blob 이 multi-key 보다 쉽다

ESP-IDF NVS key 는 15 byte 제한이라 `esp_claw_<10digit>` 같은 자연스러운 키는 불가. 처음엔 hash 화나 prefix 단축을 고려했지만 단일 blob (`eid_cache` key 하나, JSON object value) 가 lookup/put/invalidate 모두 동기 read-parse-mutate-write 라 더 단순. 32 항목 cap 으로 blob 크기는 항상 수 KB 이하 — read 비용 무시 가능.

**원칙:** ESP NVS key 제약을 우회하기 위해 blob 화는 흔한 패턴. lookup 빈도가 매우 높으면 RAM 미러를 두지만 demo 수준에선 disk hit 충분.

### 3. write-through 와 cache invalidate 를 mutate 경로마다 책임 분리

`resolve_entity_id_by_config_id` slow-path 가 자동으로 write-through 하므로 `do_create` 의 명시적 `eid_cache_put` 은 사실상 redundant 지만 의도 명시 + cache 적중 케이스 일관성 위해 유지. `do_remove` 는 mutation 경로라 invalidate 필수. mutation 경로마다 cache 책임이 명확하면 stale read 추적이 쉽다.

## 관련

- 직접 영향 받은 commit: `c04c845 fix(cap_ha_automation): HA modern schema + entity_id resolution`
- 관련 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-c04c845-followups.md`
- 다음 후속: state-trigger 지원 (별도 plan / 다른 worktree 에서 병렬 진행)
```

- [ ] **Step 2: Commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/v4
git add docs/learn/20260512-cap-ha-automation-c04c845-followups.md
git commit -m "$(cat <<'EOF'
docs(learn): cap_ha_automation c04c845 follow-up 정제

NVS eid cache + do_service explicit-fail. c04c845 의 silent fallback
이 정확히 자기가 막으려던 silent no-op 버그를 재현하는 정확한
지점 + cache 도입으로 /api/states 빈번 호출 제거 정리.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage:**
- (1) entity_id NVS 캐싱 → Task 1 (helpers + resolve_entity_id_by_config_id integration) + Task 2 (do_create write-through) + Task 3 (do_remove invalidate). ✅
- (2) resolver miss explicit fail → Task 4. ✅
- (3) helper 시그니처 정리 → Task 1 의 cache layer 도입으로 `resolve_entity_id_by_config_id` 가 단일 책임 (cache+HA 결합) 으로 분리; caller 코드 (do_create / do_service) 는 cache 책임 명시적으로 가짐. ✅

**Placeholder scan:** none. 모든 step 에 실제 코드 + 명령.

**Type consistency:** `eid_cache_{load,store,lookup,put,invalidate}` naming 통일. `cJSON *` ownership: `eid_cache_load` 가 obj 를 caller 에 넘기고 caller 가 `cJSON_Delete`. `eid_cache_put/invalidate` 는 self-contained. NVS key/cap 매크로는 모두 internal.h 에 정의.

**E2E 검증 (선택, 사용자가 보드 가용시):**
1. console 명령으로 `--automation '{"action":"create","target":"화장실 조명","device_action":"turn_off","trigger":{"kind":"daily_time","time":"03:00"}}'`
2. `nvs_get_blob ha_ctl eid_cache` 출력에 `{"esp_claw_X":"automation.<slug>"}` 보이는지 확인 (console 에 직접 NVS dump 명령 없으면 reflash + boot 시 verbose log).
3. 같은 automation_id 로 `--automation '{"action":"trigger_now","automation_id":"esp_claw_X"}'` 호출 → 로그에 `/api/states` GET 안 일어나야 (cache hit).
4. 잘못된 id 로 `--automation '{"action":"enable","automation_id":"esp_claw_99999999"}'` → 응답 message 가 "automation_id '...' 에 해당하는 HA 자동화를 찾을 수 없습니다..." 인지 확인 (explicit fail).

---

## Execution Handoff

플랜 저장 완료. 두 가지 옵션:

1. **Subagent-Driven (recommended)** — Task 1–5 각각 fresh subagent dispatch + controller review.
2. **Inline Execution** — 현재 세션에서 task 별 checkpoint 로 진행.

병렬 실행: 이 플랜은 state-trigger 플랜과 같은 worktree (`feat/cap-ha-control-v4`) 의 다른 함수만 건드리므로 **별도 worktree 를 fork 해 두 트랙 동시 실행 가능**. 마지막에 양쪽 모두 정상 빌드 확인 후 한 쪽 base 로 다른 쪽 rebase + 통합 빌드.
