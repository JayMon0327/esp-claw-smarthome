# cap_ha_control v4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close 5 v3 review findings (1 race + 2 mechanical safety + 2 infrastructure) and add automation 등록/수정/제거 as a new typed tool `ha_automation` built on `cap_scheduler`.

**Architecture:** v3의 firmware-owns-everything 패턴 그대로. 자동화는 LLM이 `{action, trigger, target, device_action, ...}` 만 채우고 firmware가 cap_scheduler에 entry를 등록 + 자기-event를 publish하도록 설정. Scheduler fire 시점에 event_router 룰이 entry의 `payload_json`을 `cap_ha_core_execute()`에 직접 전달 (LLM 우회, 결정적). 안전성 수정(mutex / hex validate / count cap)은 v3 코드 surgical patch.

**Tech Stack:** ESP-IDF v5.5.4, FreeRTOS (mutex), cJSON, esp_http_client, NVS, claw_cap framework, cap_scheduler (cron/interval/once), claw_event_router (rule-based dispatch).

**Spec:** `smarthome-docs/superpowers/specs/2026-05-08-cap-ha-control-typed-tool-design.md` (v3) + this plan (v4 deltas).

**Source of v4 task list:** `/review` of PR #1 (cap_ha_control v3) + user request for automation. Recorded in `docs/learn/20260508-cap-ha-control-v3.md` "Pre-landing review findings" section.

---

## File Plan

신규 (모두 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/` 기준):

| 파일 | 작업 | 책임 |
|---|---|---|
| `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` | create | 자동화 CRUD 본구현 (`cap_ha_automation_create/update/remove/list/execute`) |

수정:

| 파일 | 작업 |
|---|---|
| `components/claw_capabilities/cap_ha_control/CMakeLists.txt` | REQUIRES에 `cap_scheduler` 추가 + 신규 src 등록 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` | Task 1 mutex + Task 3 entity count cap + Task 4 boot-fetch에서 compose_description 재호출 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` | Task 1 mutex extern + Task 4 cap_ha_compose_description() prototype + Task 5 NVS insecure 키 + Task 6 prototypes |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c` | Task 2 `#rrggbb` isxdigit() 검증 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` | Task 4 `compose_description` 외부 노출 + Task 6 `ha_automation` 두 번째 descriptor 추가 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` | Task 5 `https://` scheme 분기 + insecure flag 읽기 |
| `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c` | Task 5 `--set-insecure on/off` + Task 6 `--automation create/list/remove/update/trigger` |
| `application/edge_agent/main/router_rules/router_rules.json` 또는 동등 | Task 6 `ha_automation_fire` 이벤트를 잡아 cap_ha_core_execute로 dispatch하는 룰 추가 |

조사 (구현 전):

| 파일 | 목적 |
|---|---|
| `components/claw_modules/claw_cap/src/claw_cap.c` | Task 4 descriptor cache 동작 — boot-fetch 후 compose가 LLM context에 propagate되는지 |
| `components/claw_capabilities/cap_scheduler/src/cap_scheduler.c` (특히 fire 경로) | Task 6 publish_event_fn이 어떤 task context에서 실행되는지 + stack budget |
| `components/claw_modules/claw_event_router/` | Task 6 rule JSON 작성 위치 + 매칭 문법 |

learn log:

| 파일 | 작업 |
|---|---|
| `docs/learn/20260511-cap-ha-control-v4.md` | create (Task 7 최종) |

---

## Pre-flight

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw
git status -s
```
Expected: clean. 보드 + USB + HA + Telegram 준비. `~/.gstack/projects/esp-claw/secrets.env`의 `ESP_PORT / HA_PI_IP / HA_LONG_LIVED_TOKEN / TELEGRAM_BOT_TOKEN` 채워져 있고 v3 firmware가 보드에 flash된 상태. `idf.py build && idf.py -p $ESP_PORT flash`로 baseline 확인.

각 task는 독립 commit. PR-A (Tasks 1–3)는 small safety PR, PR-B (Task 6)는 자동화 feature PR, PR-C/D (Tasks 5/4)는 infrastructure PR. 작업 분리는 user 결정.

---

## Task 1: Mutex around `s_cache_registry` (P1, race fix)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

문맥: v3 review P1 finding. `cap_ha_resolve_refresh_from_ha()` 가 free + reassign + parse_registry를 lock 없이 수행 — `cap_ha_resolve_target` / `_top_candidates` 가 같은 전역 iterate. boot_fetch_task의 refresh 또는 console `--refresh-registry` 와 LLM-triggered ha_control 호출이 겹치면 use-after-free 윈도우.

- [ ] **Step 1: 현재 동시성 surface 확인**

```bash
grep -n "s_cache_registry" components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
```
Expected: 8개 라인 — 정의(line 24), refresh의 write 3개(312–314), target/top_candidates/active의 read 5개. 모두 lock-free.

- [ ] **Step 2: mutex 변수 + 초기화 추가**

`cap_ha_control_resolve.c` 상단 `static cap_ha_registry_t s_cache_registry = {0};` 다음에:

```c
static SemaphoreHandle_t s_cache_mutex = NULL;
```

(`freertos/semphr.h`는 이미 `freertos/task.h` 통해 transitively 들어옴 — 안 들어오면 `#include "freertos/semphr.h"` 추가.)

`cap_ha_resolve_init` 본문 맨 앞 (existing `size_t len = ...` 직전)에 추가:

```c
    if (!s_cache_mutex) {
        s_cache_mutex = xSemaphoreCreateMutex();
        if (!s_cache_mutex) return ESP_ERR_NO_MEM;
    }
```

- [ ] **Step 3: refresh 경로 critical section으로 감싸기**

`cap_ha_resolve_refresh_from_ha` 의 line 312–314 (free + reset + parse_registry) 블록을:

```c
    if (store_err == ESP_OK) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        if (s_cache_registry.items) free(s_cache_registry.items);
        s_cache_registry = (cap_ha_registry_t){0};
        parse_registry(blob, &s_cache_registry);
        xSemaphoreGive(s_cache_mutex);
    }
```

- [ ] **Step 4: read 경로에서 entity copy-out으로 변경**

`cap_ha_resolve_target` 의 `lookup_in(&s_cache_registry, ...)` 호출 3곳 (stage 1/2/3)을 critical section으로 감싸기. 가장 깔끔한 패턴: `lookup_in`이 이미 결과를 `*out`에 byte-copy 하므로, lookup 호출만 mutex로 감싸면 됨.

기존:
```c
    if (lookup_in(&s_cache_registry,  target, true, false, false, out)) return ESP_OK;
```

3곳 모두 다음으로 치환:
```c
    {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        bool found = lookup_in(&s_cache_registry, target, true, false, false, out);
        xSemaphoreGive(s_cache_mutex);
        if (found) return ESP_OK;
    }
```
(by_id/exact_friendly/norm_friendly 파라미터는 stage 별로 다르게.)

- [ ] **Step 5: top_candidates / active_friendly_names도 같은 패턴**

`cap_ha_resolve_top_candidates`의 `regs[r]` 루프에서 `r == 1` (cache 차례) 일 때 mutex take/give 필요. 단순화 위해 함수 전체를 mutex로 감싸기:

```c
esp_err_t cap_ha_resolve_top_candidates(char *out_csv, size_t out_size, size_t max)
{
    if (!out_csv || out_size == 0) return ESP_ERR_INVALID_ARG;
    out_csv[0] = '\0';
    size_t emitted = 0;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cap_ha_registry_t *regs[2] = { &s_static_registry, &s_cache_registry };
    for (int r = 0; r < 2 && emitted < max; r++) {
        for (size_t i = 0; i < regs[r]->count && emitted < max; i++) {
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
    xSemaphoreGive(s_cache_mutex);
    if (emitted == 0) snprintf(out_csv, out_size, "(none)");
    return ESP_OK;
}
```

`cap_ha_resolve_active_friendly_names` 는 top_candidates 호출만 하므로 추가 mutex 불필요 (재진입 안전성: `xSemaphoreTake`는 recursive-take가 아니므로 같은 task가 다시 take하면 deadlock — 그러나 `cap_ha_resolve_active_friendly_names`는 mutex 안 잡고 그냥 top_candidates에 위임하므로 OK).

**Invariant 주석 추가** (`s_cache_mutex` 정의 직전):
```c
/* s_cache_mutex guards s_cache_registry. s_static_registry is write-once
 * at init and read-only afterwards; no mutex needed. */
```

- [ ] **Step 6: 빌드**

```bash
cd application/edge_agent
idf.py build 2>&1 | tail -5
```
Expected: `Project build complete.` 경고 없음.

- [ ] **Step 7: On-board 스트레스 검증**

```bash
idf.py -p $ESP_PORT flash
# monitor 띄운 채로 다른 터미널에서:
python3 -c "
import serial, time, threading
ser = serial.Serial('$ESP_PORT', 115200, timeout=1)
time.sleep(12)
while ser.in_waiting: ser.read(ser.in_waiting); time.sleep(0.05)
def hammer():
    for _ in range(20):
        ser.write(b'ha_control --refresh-registry\r\n')
        time.sleep(0.5)
        ser.write(b'ha_control --call {\"target\":\"board:onboard_rgb\",\"action\":\"toggle\"}\r\n')
        time.sleep(0.5)
t = threading.Thread(target=hammer); t.start(); t.join()
out = b''
end = time.time() + 5
while time.time() < end:
    if ser.in_waiting: out += ser.read(ser.in_waiting)
    time.sleep(0.05)
print('crashes:', out.count(b'Backtrace'), 'rsts:', out.count(b'rst:0x'))
"
```
Expected: `crashes: 0 rsts: 0`.

- [ ] **Step 8: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "$(cat <<'EOF'
fix(cap_ha_control): guard s_cache_registry with FreeRTOS mutex

PR #1 /review P1 finding. cap_ha_resolve_refresh_from_ha was freeing
and replacing s_cache_registry.items while cap_ha_resolve_target and
cap_ha_resolve_top_candidates iterated the same global. Window was
narrow (~50ms during boot-fetch or console --refresh-registry) but
user-triggerable.

Add a per-component mutex; refresh wraps the free+reassign+parse in a
critical section, reads (lookup, top_candidates) wrap their s_cache
access. s_static_registry is write-once at init and remains lock-free.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `#rrggbb` invalid hex validation (P2)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c`

문맥: v3 review P2 finding. `strtol(..., 16)`이 non-hex char에 0을 반환 → `"#FFGG00"` 가 silently `(0xFF, 0, 0)` red로 잡힘. LLM이 잘못된 hex를 생성하면 사용자가 잘못된 색을 받음.

- [ ] **Step 1: include 추가 + 검증 코드 삽입**

`cap_ha_control_core.c` 상단 include 영역에:
```c
#include <ctype.h>
```

`cap_ha_color_to_rgb` 함수 (line 41–62) 의 `if (color[0] == '#' && strlen(color) == 7) {` 블록 안 첫 줄에 추가:

```c
    if (color[0] == '#' && strlen(color) == 7) {
        for (size_t i = 1; i < 7; i++) {
            if (!isxdigit((unsigned char)color[i])) return ESP_ERR_INVALID_ARG;
        }
        char r[3] = { color[1], color[2], 0 };
        /* ...existing strtol calls unchanged... */
```

- [ ] **Step 2: 빌드**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
```
Expected: 성공.

- [ ] **Step 3: On-board 검증 — invalid hex reject**

```bash
idf.py -p $ESP_PORT flash
# wait boot, then:
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1)
time.sleep(12); ser.read(ser.in_waiting or 1)
ser.write(b'ha_control --call {\"target\":\"board:onboard_rgb\",\"action\":\"turn_on\",\"color\":\"#FFGG00\"}\r\n')
time.sleep(3); print(ser.read(ser.in_waiting).decode('utf-8', 'replace'))
ser.close()
"
```
Expected: `{"success":false,"message":"지원하지 않는 색상입니다 (color=#FFGG00).",...}`. LED 안 변함.

- [ ] **Step 4: Valid hex 회귀 검증**

```bash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(2)
ser.write(b'ha_control --call {\"target\":\"board:onboard_rgb\",\"action\":\"turn_on\",\"color\":\"#A1B2C3\"}\r\n')
time.sleep(3); print(ser.read(ser.in_waiting).decode('utf-8', 'replace'))
ser.close()
"
```
Expected: `success:true` + LED가 회청색.

- [ ] **Step 5: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c
git commit -m "fix(cap_ha_control): validate #rrggbb hex chars before strtol

PR #1 /review P2. strtol returns 0 for non-hex chars, so #FFGG00 was
silently parsed as (0xFF, 0, 0). Validate each char with isxdigit()
before parsing; return ESP_ERR_INVALID_ARG on bad input so the caller
emits the same '지원하지 않는 색상입니다' message.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Entity count cap in `parse_registry` (P2)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`

문맥: v3 review P2 finding. `calloc((size_t)count, sizeof(*items))` 의 count가 untrusted JSON에서 옴. 악의/오설정 HA가 10K entries 보내면 ~1.5MB allocation, heap fragmentation 위험. 64로 cap.

- [ ] **Step 1: 매크로 정의 추가**

`cap_ha_control_internal.h` 의 `#define CAP_HA_STATES_BUF_BYTES (64 * 1024)` 다음 줄에:

```c
#define CAP_HA_MAX_REGISTRY_ENTRIES 64
```

- [ ] **Step 2: parse_registry에 cap 적용**

`cap_ha_control_resolve.c` 의 `parse_registry` 함수 안, `int count = cJSON_GetArraySize(entities);` 다음에:

```c
    if (count > CAP_HA_MAX_REGISTRY_ENTRIES) {
        ESP_LOGW(TAG, "registry entry count %d exceeds cap %d; truncating",
                 count, CAP_HA_MAX_REGISTRY_ENTRIES);
        count = CAP_HA_MAX_REGISTRY_ENTRIES;
    }
```

- [ ] **Step 3: 빌드 + 회귀 검증**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
```
Expected: 성공. boot 후 monitor에 `cap_ha_resolve: loaded 4 static entities` 그대로 (entities.default.json 은 4개 entries).

- [ ] **Step 4: Synthetic 100-entry 테스트 (옵션)**

`data/entities.default.json` 을 100 entries 짜리 syntheticー로 임시 교체해 build + flash + boot 로그에 `registry entry count 100 exceeds cap 64; truncating` + `loaded 64 static entities` 확인 후 원래 4-entry로 revert.

(시간 없으면 skip — boot-fetch 가 큰 HA에 붙으면 같은 path 가 자동 검증됨.)

- [ ] **Step 5: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h
git commit -m "fix(cap_ha_control): cap registry entries at 64

PR #1 /review P2. parse_registry called calloc with untrusted count
from cJSON_GetArraySize — a malicious or misconfigured HA could send
10K entries and exhaust heap (each entity is 152 bytes). Cap at 64;
log warn + truncate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Description refresh after boot-fetch (P3)

**Files:**
- Investigate: `components/claw_modules/claw_cap/src/claw_cap.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

문맥: v3 review P3. `compose_description()`이 `cap_ha_group_init`에서 1회만 호출되고, boot-fetch가 추가한 entities는 다음 boot까지 LLM description에 반영 안 됨. v1 dynamic registry tier가 절반만 효과.

- [ ] **Step 1: claw_cap descriptor cache 동작 조사**

```bash
grep -n "description\|claw_cap_add_capped_description" components/claw_modules/claw_cap/src/claw_cap.c | head -10
```

`claw_cap_add_capped_description` 호출 경로 추적 — descriptor의 `.description` 포인터가 LLM tools 컨텍스트로 변환되는 시점이 (a) `register_group` 한 번인지 (b) 매 LLM 요청 시점인지 결정.

`claw_cap.c`에서 tools list를 LLM 컨텍스트로 export하는 함수를 찾아 `s_ha_descriptors[0].description` 포인터를 매번 새로 읽는지 확인.

**결정 트리:**
- 매번 읽음 → Step 2로. `compose_description()` 만 재호출하면 자동 반영.
- 한 번만 캐시 → Step 5(escalation)로. claw_cap API 작업 필요.

- [ ] **Step 2: `compose_description` 외부 노출**

`cap_ha_control.c` 의 `static void compose_description(void)` → `void cap_ha_compose_description(void)` 로 시그니처 변경 (static 제거, 이름에 prefix).

`cap_ha_control_internal.h` 의 `/* color + message helpers ... */` 블록에 추가:

```c
void cap_ha_compose_description(void);
```

`cap_ha_group_init` 안의 `compose_description();` 호출도 `cap_ha_compose_description();` 으로 변경.

- [ ] **Step 3: boot_fetch_task에서 refresh 후 재호출**

`cap_ha_control_resolve.c` 의 `boot_fetch_task` (line ~360 부근, `cap_ha_resolve_refresh_from_ha()` 호출 직후):

```c
    esp_err_t err = cap_ha_resolve_refresh_from_ha();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "boot-fetch failed: %s (will use static-only registry)",
                 esp_err_to_name(err));
    } else {
        cap_ha_compose_description();  /* refresh LLM-visible description with discovered entities */
        ESP_LOGI(TAG, "boot-fetch: description refreshed with %zu+%zu entities",
                 s_static_registry.count, s_cache_registry.count);
    }
    vTaskDelete(NULL);
```

- [ ] **Step 4: 빌드 + 보드 검증**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
# wait boot + boot-fetch (~12s), then console:
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(14)
ser.read(ser.in_waiting or 1)
ser.write(b'cap list\r\n'); time.sleep(3)
out = ser.read(ser.in_waiting).decode('utf-8', 'replace')
for line in out.splitlines():
    if 'ha_control' in line and '[ha]' in line: print(line[:300])
ser.close()
"
```
Expected: `Active devices (...): 화장실 조명, 거실 커튼, 거실 콘센트, 보드 RGB, <HA-discovered entities...>` — boot-fetch가 발견한 추가 entity가 description에 보임.

- [ ] **Step 5: Step 1에서 "한 번만 캐시"였을 때의 escalation**

`claw_cap.c` 에 `claw_cap_invalidate_tool_description(const char *group_id, const char *cap_id)` API를 추가하고, `cap_ha_compose_description()` 끝에서 호출. claw_cap 내부의 tools-list 캐시를 비워 다음 LLM 요청에 새 description이 들어가도록.

(이 task는 cross-component API 변경이라 별도 commit + PR 권장.)

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control.c \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c
git commit -m "feat(cap_ha_control): refresh tool description after boot-fetch

PR #1 /review P3. compose_description only ran in cap_ha_group_init,
before boot-fetch enriched s_cache_registry. The 'Active devices' list
in the LLM description showed only the 4 static entries until next
reboot.

Export compose_description and call it from boot_fetch_task after
cap_ha_resolve_refresh_from_ha succeeds. claw_cap reads descriptor
description per LLM request, so the new list propagates without
explicit invalidation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: HTTPS support + `--insecure` flag (P3)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`

문맥: v3 review P3. `http://...` URL이면 Bearer token이 LAN 평문 전송. esp_http_client + `crt_bundle_attach`는 이미 cfg에 있어서 https 인프라는 wired인데 미사용. v4에선 `https://` URL 허용 + self-signed HA 위해 `--insecure on` 옵션 추가. CA bundle import는 v5.

- [ ] **Step 1: NVS 키 + getter/setter 추가**

`cap_ha_control_internal.h` 의 `#define CAP_HA_NVS_KEY_CACHE ...` 다음에:

```c
#define CAP_HA_NVS_KEY_INSECURE    "ha_insecure"  /* bool: skip TLS cert verify */
```

같은 파일 `/* http */` 섹션 prototypes 끝에:

```c
esp_err_t cap_ha_http_set_insecure(bool insecure);
bool cap_ha_http_get_insecure(void);
```

`cap_ha_control_http.c` 의 setter/getter 영역 (`cap_ha_http_set_token` 다음)에 추가:

```c
esp_err_t cap_ha_http_set_insecure(bool insecure)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, CAP_HA_NVS_KEY_INSECURE, insecure ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool cap_ha_http_get_insecure(void)
{
    nvs_handle_t h;
    if (nvs_open(CAP_HA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0;
    nvs_get_u8(h, CAP_HA_NVS_KEY_INSECURE, &v);
    nvs_close(h);
    return v != 0;
}
```

- [ ] **Step 2: POST / GET 함수에서 scheme 분기**

`cap_ha_control_http.c` 의 `esp_http_client_config_t cfg = {...}` 블록 (POST + GET 양쪽, line ~135 부근, line ~205 부근):

기존:
```c
    esp_http_client_config_t cfg = {
        .url = full_url,
        ...
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
```

다음으로 교체:
```c
    bool is_https = (strncmp(full_url, "https://", 8) == 0);
    bool insecure = is_https && cap_ha_http_get_insecure();
    if (insecure) {
        ESP_LOGW(TAG, "TLS verification disabled (--set-insecure on) — token sent over insecure HTTPS");
    }
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_POST,  /* GET 함수에선 HTTP_METHOD_GET */
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = (is_https && !insecure) ? esp_crt_bundle_attach : NULL,
        .skip_cert_common_name_check = insecure,
    };
```

- [ ] **Step 3: Console 명령 추가**

`cmd_cap_ha_control.c` 의 `ha_args` 구조체에 추가:

```c
    struct arg_str *set_insecure;
```

`cmd_cap_ha_control_register` 의 argtable 초기화에:

```c
    ha_args.set_insecure = arg_str0(NULL, "set-insecure", "<on|off>",
                                    "Skip TLS cert verify for https:// HA URLs (demo only)");
```

`arg_end(2)` 를 `arg_end(3)` 으로 변경 (argtable error counting).

`cmd_ha_control` 함수 안 (set_url/set_token 분기 다음에):

```c
    if (ha_args.set_insecure->count > 0) {
        const char *v = ha_args.set_insecure->sval[0];
        bool ins = (strcmp(v, "on") == 0 || strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
        esp_err_t err = cap_ha_http_set_insecure(ins);
        printf("set_insecure: %s (value=%s)\n", esp_err_to_name(err), ins ? "on" : "off");
        return (err == ESP_OK) ? 0 : 1;
    }
```

`help` 문자열에 `| --set-insecure <on|off>` 추가.

- [ ] **Step 4: 빌드**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
```
Expected: 성공.

- [ ] **Step 5: On-board 검증 — http:// 회귀**

```bash
idf.py -p $ESP_PORT flash
# wait boot, then:
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
ser.write(b'ha_control --call {\"target\":\"light.smart_bulb\",\"action\":\"toggle\"}\r\n')
time.sleep(8); print(ser.read(ser.in_waiting).decode('utf-8','replace'))
ser.close()
"
```
Expected: 기존 동작 그대로 — `POST http://...` + `status=200` + 한국어 메시지.

- [ ] **Step 6: HTTPS 검증 (HA에 self-signed TLS 설정 후)**

HA를 https://...8123으로 설정 + self-signed cert 사용. 보드에서:
```
ha_control --set-url https://192.168.1.94:8123
ha_control --set-insecure on
ha_control --call '{"target":"light.smart_bulb","action":"toggle"}'
```
Expected: monitor에 `WARNING: TLS verification disabled` + `POST https://...` + `status=200`. Insecure off로 돌리면 cert verify fail.

(HA TLS 설정이 demo 환경 밖이면 이 단계는 skip + plan에 "deferred to v4.1" 명시.)

- [ ] **Step 7: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c \
        components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c
git commit -m "feat(cap_ha_control): https + --set-insecure for self-signed HA

PR #1 /review P3. Bearer token was sent in cleartext over LAN. Accept
https:// URLs by detecting scheme; gate crt_bundle_attach + skip
cert verify on a new NVS flag (ha_ctl/ha_insecure) toggled via
'ha_control --set-insecure on|off'. WARN logged on every request
when insecure is on.

Custom CA import (esp_http_client cert_pem) deferred to v4.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `ha_automation` typed tool (FEATURE — 자동화 등록/수정/제거)

Sub-tasks 6.1 → 6.8. PR-B 단위로 묶음.

문맥: user 요청 핵심 항목. "화장실 조명을 매일 저녁 7시에 켜줘" 같은 자연어로 자동화 룰 등록/수정/제거. v3 ha_control과 동일한 firmware-owns-everything + verbatim echo 패턴. cap_scheduler 인프라 위에 작성 — 새 자동화 엔진 만들지 않음.

**검증된 사실 (Plan v4 작성 전 조사):**
- `cap_scheduler` 가 `cap_scheduler_add/update/remove/trigger_now/get_snapshot/list_json` 같은 C API를 공개 — direct invocation 가능.
- `cap_scheduler_item_t` 에 `payload_json[512]` + `event_type[32]` + `event_key[96]` + `text[256]` 필드 — fire 시 publish되는 event에 실릴 정보.
- kind: ONCE / INTERVAL / CRON — daily_time/weekly는 CRON으로 변환.
- fire callback은 `cap_scheduler_config_t.publish_event` (claw_event_publish_fn) — 이미 event_router에 연결돼 있을 것.

**Architecture:**
1. LLM이 `ha_automation` typed tool 호출 → firmware가 schema validate + scheduler entry 합성.
2. `cap_scheduler_item_t.event_type = "ha_automation_fire"`, `payload_json = "<ha_control JSON>"` 으로 entry add.
3. Scheduler fire 시점에 publish_event가 발화 → event_router 룰 `ha_automation_fire_rule` 가 매칭 → `payload_json` 추출 → `cap_ha_core_execute(payload_json, out, sizeof(out))` 직접 호출 (LLM 우회, 결정적).
4. 결과는 monitor log + (선택) Telegram notify rule로 사용자에게 전달.

---

### Task 6.1: Spike — cap_scheduler fire 경로 확인

**Files:** 조사만 (read-only).

문맥: scheduler가 fire 시점에 어떤 task context에서 publish_event를 부르는지, payload_json이 그대로 evt에 실리는지 확인. v3에서 boot_fetch_task의 6KB stack 오버플로우를 겪었으므로 같은 함정 회피.

- [ ] **Step 1: scheduler task spec 읽기**

```bash
grep -n "task_stack_size\|xTaskCreate\|publish_event" components/claw_capabilities/cap_scheduler/src/cap_scheduler.c | head -20
```
확인: scheduler task의 stack 크기 (보통 4–8KB), publish_event 호출이 scheduler task context 안인지.

- [ ] **Step 2: event_router rule schema 확인**

```bash
ls application/edge_agent/fatfs_image/router_rules/ 2>/dev/null || find . -name "router_rules*.json" | head -3
head -50 application/edge_agent/fatfs_image/router_rules/router_rules.json 2>/dev/null
```
확인: rule JSON의 매칭 키 (`event_type`, `source_cap`, `channel` 등) + actions 시그니처.

- [ ] **Step 3: claw_event_router의 fire dispatch 코드 위치 찾기**

```bash
grep -rn "action.*cap_call\|invoke_cap\|cap_execute" components/claw_modules/claw_event_router/ 2>/dev/null | head -10
```
확인: rule action 중 "cap_call" / "tool_invoke" 류가 있는지 — 있으면 rule에서 직접 cap_ha_core_execute 호출 가능. 없으면 별도 handler 등록 필요.

- [ ] **Step 4: 결정 + 문서화**

이 task는 commit 없음 — 결과를 task 6.4 (event_router 룰) + 6.5 (handler) 작성 시 반영. 발견 사항을 `docs/learn/20260511-cap-ha-control-v4.md` 의 "조사 결과" 섹션 (Task 7에서 작성)에 메모.

만약 (a) scheduler publish_event가 4KB 이하 stack에서 호출되고 (b) event_router에 cap_call 액션이 없으면, **별도 dispatch task 만들기**가 필요. 그 결정을 task 6.5에서 commit 메시지에 명시.

---

### Task 6.2: `ha_automation` descriptor 등록 + stub execute

**Files:**
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`
- Modify: `components/claw_capabilities/cap_ha_control/CMakeLists.txt`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`

- [ ] **Step 1: CMakeLists에 src 추가 + cap_scheduler REQUIRES**

`components/claw_capabilities/cap_ha_control/CMakeLists.txt`의 SRCS 블록에 추가:
```cmake
        "src/cap_ha_automation.c"
```
REQUIRES 블록 끝에 추가:
```cmake
        cap_scheduler
```

- [ ] **Step 2: cap_ha_automation.c stub 작성**

```c
/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_auto";

esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size)
{
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;
    (void)input_json;
    ESP_LOGW(TAG, "stub: ha_automation not yet implemented");
    snprintf(output_json, output_size,
             "{\"success\":false,\"message\":\"ha_automation 미구현 (stub).\"}");
    return ESP_OK;
}
```

- [ ] **Step 3: prototype + 두 번째 descriptor 등록**

`cap_ha_control_internal.h` 의 `/* core */` 섹션에 추가:

```c
esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size);
```

`cap_ha_control.c` 의 `s_ha_descriptors[]` 배열에 두 번째 entry 추가 (`s_ha_descriptors` 길이 2로 확장):

```c
static char s_ha_automation_description[512];

static esp_err_t cap_ha_automation_execute_wrapper(const char *input_json,
                                                   const claw_cap_call_context_t *ctx,
                                                   char *output,
                                                   size_t output_size)
{
    (void)ctx;
    return cap_ha_automation_execute(input_json, output, output_size);
}

static claw_cap_descriptor_t s_ha_descriptors[] = {
    { /* ha_control — unchanged */ ... },
    {
        .id = "ha_automation",
        .name = "ha_automation",
        .family = "ha",
        .description = NULL, /* set in compose_description */
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
              "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"update\",\"remove\",\"list\",\"trigger_now\"]},"
              "\"automation_id\":{\"type\":\"string\",\"description\":\"update/remove/trigger_now needs this. create assigns automatically.\"},"
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"cron\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":1000},"
                "\"cron\":{\"type\":\"string\",\"description\":\"power-user only\"}"
              "}},"
              "\"target\":{\"type\":\"string\",\"description\":\"same as ha_control.target\"},"
              "\"device_action\":{\"type\":\"string\",\"enum\":[\"turn_on\",\"turn_off\",\"toggle\",\"open\",\"close\"]},"
              "\"brightness_pct\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},"
              "\"color\":{\"type\":\"string\"},"
              "\"kelvin\":{\"type\":\"integer\",\"minimum\":2000,\"maximum\":6500}"
            "},"
            "\"required\":[\"action\"]}",
        .execute = cap_ha_automation_execute_wrapper,
    },
};
```

`s_ha_group.descriptor_count` 는 `sizeof / sizeof` 로 계산되므로 자동 갱신됨 — 코드 변경 불필요.

- [ ] **Step 4: compose_description 확장 — automation도 active devices 리스트 inject**

`cap_ha_control.c` 의 `compose_description()` 끝에 추가:

```c
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove time-based automation for HA devices and onboard hardware. "
             "Same target names as ha_control. Active devices: %s. "
             "When this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
    s_ha_descriptors[1].description = s_ha_automation_description;
```

- [ ] **Step 5: 빌드 + 등록 확인**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
ser.write(b'cap groups\r\n'); time.sleep(2)
out = ser.read(ser.in_waiting).decode('utf-8','replace')
for line in out.splitlines():
    if 'cap_ha_control' in line: print(line[:200])
ser.close()
"
```
Expected: `cap_ha_control state=started descriptors=2` (1 → 2).

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/
git commit -m "feat(cap_ha_control): ha_automation descriptor scaffold (stub execute)

Adds the second LLM-visible tool 'ha_automation' with the typed
{action, trigger, target, device_action, ...} schema. execute() is a
stub that returns success:false for now; real CRUD lands in 6.3-6.7."
```

---

### Task 6.3: `create` — single-shot automation via daily_time / interval

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

문맥: 가장 단순한 trigger.kind부터. daily_time은 CRON으로 변환 ("HH:MM" → `M H * * *`), interval은 native INTERVAL.

- [ ] **Step 1: schema validate + dispatch 헬퍼**

`cap_ha_automation.c` 상단에 추가:

```c
#include "cap_scheduler.h"

static const char *VALID_AUTO_ACTIONS[] = {"create", "update", "remove", "list", "trigger_now"};
#define VALID_AUTO_ACTIONS_COUNT (sizeof(VALID_AUTO_ACTIONS) / sizeof(VALID_AUTO_ACTIONS[0]))

static bool auto_action_is_valid(const char *a)
{
    if (!a || !*a) return false;
    for (size_t i = 0; i < VALID_AUTO_ACTIONS_COUNT; i++) {
        if (strcmp(a, VALID_AUTO_ACTIONS[i]) == 0) return true;
    }
    return false;
}

static void emit_auto_failure(char *output, size_t output_size, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddNullToObject(root, "automation_id");
    char *s = cJSON_PrintUnformatted(root);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    else {
        snprintf(output, output_size,
                 "{\"success\":false,\"message\":\"내부 오류\",\"automation_id\":null}");
    }
    cJSON_Delete(root);
}
```

- [ ] **Step 2: daily_time → cron 변환 헬퍼**

```c
/* Convert "HH:MM" (24h KST) to "M H * * *" cron expression. */
static esp_err_t time_to_cron(const char *hhmm, char *out, size_t out_size)
{
    if (!hhmm || strlen(hhmm) != 5 || hhmm[2] != ':') return ESP_ERR_INVALID_ARG;
    int h = atoi(hhmm);
    int m = atoi(hhmm + 3);
    if (h < 0 || h > 23 || m < 0 || m > 59) return ESP_ERR_INVALID_ARG;
    snprintf(out, out_size, "%d %d * * *", m, h);
    return ESP_OK;
}

/* weekly → cron "M H * * day1,day2,..." */
static esp_err_t weekly_to_cron(const char *hhmm, const cJSON *weekdays,
                                char *out, size_t out_size)
{
    if (!cJSON_IsArray(weekdays) || cJSON_GetArraySize(weekdays) == 0)
        return ESP_ERR_INVALID_ARG;
    char base[16];
    if (time_to_cron(hhmm, base, sizeof(base)) != ESP_OK) return ESP_ERR_INVALID_ARG;
    /* base is "M H * * *" — replace last * with day list */
    char *star = strrchr(base, '*');
    if (!star) return ESP_FAIL;
    *star = '\0';
    snprintf(out, out_size, "%s", base);
    size_t pos = strlen(out);
    cJSON *day = NULL;
    int wrote = 0;
    cJSON_ArrayForEach(day, weekdays) {
        if (!cJSON_IsNumber(day) || day->valueint < 0 || day->valueint > 6) continue;
        pos += snprintf(out + pos, out_size - pos, "%s%d", wrote ? "," : "", day->valueint);
        wrote++;
    }
    if (!wrote) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}
```

- [ ] **Step 3: create 분기 본구현**

```c
static esp_err_t do_create(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *trigger = cJSON_GetObjectItem(root, "trigger");
    const cJSON *target_j = cJSON_GetObjectItem(root, "target");
    const cJSON *dev_action_j = cJSON_GetObjectItem(root, "device_action");

    if (!cJSON_IsObject(trigger) || !cJSON_IsString(target_j) ||
        !cJSON_IsString(dev_action_j)) {
        emit_auto_failure(output, output_size,
                          "자동화 등록에는 trigger / target / device_action이 모두 필요합니다.");
        return ESP_OK;
    }

    cap_scheduler_item_t item = {0};
    item.enabled = true;
    item.max_runs = 0;  /* 0 = unlimited */
    snprintf(item.event_type, sizeof(item.event_type), "ha_automation_fire");
    snprintf(item.event_key, sizeof(item.event_key), "ha_auto");
    snprintf(item.source_channel, sizeof(item.source_channel), "ha");

    /* Build payload_json = a self-contained ha_control payload. */
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "target", target_j->valuestring);
    cJSON_AddStringToObject(payload, "action", dev_action_j->valuestring);
    const cJSON *bright = cJSON_GetObjectItem(root, "brightness_pct");
    if (cJSON_IsNumber(bright)) cJSON_AddNumberToObject(payload, "brightness_pct", bright->valueint);
    const cJSON *color = cJSON_GetObjectItem(root, "color");
    if (cJSON_IsString(color)) cJSON_AddStringToObject(payload, "color", color->valuestring);
    const cJSON *kelvin = cJSON_GetObjectItem(root, "kelvin");
    if (cJSON_IsNumber(kelvin)) cJSON_AddNumberToObject(payload, "kelvin", kelvin->valueint);
    char *payload_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payload_str) {
        emit_auto_failure(output, output_size, "내부 오류 (payload 직렬화 실패).");
        return ESP_OK;
    }
    if (strlen(payload_str) >= sizeof(item.payload_json)) {
        free(payload_str);
        emit_auto_failure(output, output_size,
                          "자동화 payload가 너무 큽니다 (512B 한계).");
        return ESP_OK;
    }
    strlcpy(item.payload_json, payload_str, sizeof(item.payload_json));
    free(payload_str);

    /* Trigger kind */
    const cJSON *kind_j = cJSON_GetObjectItem(trigger, "kind");
    const char *kind = cJSON_IsString(kind_j) ? kind_j->valuestring : NULL;
    if (!kind) {
        emit_auto_failure(output, output_size, "trigger.kind가 필요합니다.");
        return ESP_OK;
    }
    if (strcmp(kind, "daily_time") == 0) {
        const cJSON *time_j = cJSON_GetObjectItem(trigger, "time");
        if (!cJSON_IsString(time_j) ||
            time_to_cron(time_j->valuestring, item.cron_expr, sizeof(item.cron_expr)) != ESP_OK) {
            emit_auto_failure(output, output_size,
                              "trigger.time 형식이 잘못됐습니다 (예: \"19:00\").");
            return ESP_OK;
        }
        item.kind = CAP_SCHEDULER_ITEM_CRON;
    } else if (strcmp(kind, "weekly") == 0) {
        const cJSON *time_j = cJSON_GetObjectItem(trigger, "time");
        const cJSON *days = cJSON_GetObjectItem(trigger, "weekdays");
        if (!cJSON_IsString(time_j) ||
            weekly_to_cron(time_j->valuestring, days, item.cron_expr, sizeof(item.cron_expr)) != ESP_OK) {
            emit_auto_failure(output, output_size,
                              "trigger.time / weekdays 형식이 잘못됐습니다.");
            return ESP_OK;
        }
        item.kind = CAP_SCHEDULER_ITEM_CRON;
    } else if (strcmp(kind, "interval") == 0) {
        const cJSON *iv = cJSON_GetObjectItem(trigger, "interval_ms");
        if (!cJSON_IsNumber(iv) || iv->valueint < 1000) {
            emit_auto_failure(output, output_size,
                              "trigger.interval_ms는 1000 이상이어야 합니다.");
            return ESP_OK;
        }
        item.kind = CAP_SCHEDULER_ITEM_INTERVAL;
        item.interval_ms = iv->valueint;
    } else if (strcmp(kind, "cron") == 0) {
        const cJSON *cron = cJSON_GetObjectItem(trigger, "cron");
        if (!cJSON_IsString(cron) || strlen(cron->valuestring) >= sizeof(item.cron_expr)) {
            emit_auto_failure(output, output_size, "trigger.cron 형식이 잘못됐습니다.");
            return ESP_OK;
        }
        strlcpy(item.cron_expr, cron->valuestring, sizeof(item.cron_expr));
        item.kind = CAP_SCHEDULER_ITEM_CRON;
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "지원하지 않는 trigger.kind입니다 (%s).", kind);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    /* Auto-generate id: ha_auto_<unix_ts> */
    int64_t now_us = esp_timer_get_time();
    snprintf(item.id, sizeof(item.id), "ha_auto_%lld", now_us / 1000000);

    esp_err_t err = cap_scheduler_add(&item);
    if (err != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "자동화 등록에 실패했습니다 (err=%s).", esp_err_to_name(err));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    /* Success response with the assigned id. */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[192];
    if (item.kind == CAP_SCHEDULER_ITEM_CRON) {
        snprintf(msg, sizeof(msg),
                 "'%s' %s 자동화를 등록했습니다 (ID: %s).",
                 target_j->valuestring, dev_action_j->valuestring, item.id);
    } else {
        snprintf(msg, sizeof(msg),
                 "%lldms 간격 '%s' %s 자동화를 등록했습니다 (ID: %s).",
                 item.interval_ms, target_j->valuestring, dev_action_j->valuestring, item.id);
    }
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", item.id);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

`#include "esp_timer.h"` 추가.

- [ ] **Step 4: execute()를 do_create로 dispatch**

`cap_ha_automation_execute` 의 stub 본문을 다음으로 교체:

```c
esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size)
{
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        emit_auto_failure(output_json, output_size, "요청을 해석할 수 없습니다 (JSON parse 실패).");
        return ESP_OK;
    }

    const cJSON *action_j = cJSON_GetObjectItem(root, "action");
    const char *action = cJSON_IsString(action_j) ? action_j->valuestring : NULL;
    if (!auto_action_is_valid(action)) {
        emit_auto_failure(output_json, output_size,
                          "action은 create/update/remove/list/trigger_now 중 하나여야 합니다.");
        cJSON_Delete(root);
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (strcmp(action, "create") == 0) {
        ret = do_create(root, output_json, output_size);
    } else {
        emit_auto_failure(output_json, output_size,
                          "이 action은 아직 구현되지 않았습니다 (다음 task에서 구현).");
    }
    cJSON_Delete(root);
    return ret;
}
```

- [ ] **Step 5: 빌드 + create 검증**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
ser.write(b'cap call ha_automation {\"action\":\"create\",\"trigger\":{\"kind\":\"interval\",\"interval_ms\":5000},\"target\":\"board:onboard_rgb\",\"device_action\":\"toggle\"}\r\n')
time.sleep(5); print(ser.read(ser.in_waiting).decode('utf-8','replace'))
ser.close()
"
```
Expected: `{"success":true,"message":"5000ms 간격 'board:onboard_rgb' toggle 자동화를 등록했습니다 (ID: ha_auto_...).","automation_id":"ha_auto_..."}` + monitor에 scheduler entry 등록 로그.

`scheduler --list` 로 entry 확인 — kind=INTERVAL, payload_json에 ha_control JSON, event_type=ha_automation_fire.

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "feat(cap_ha_automation): create action (daily_time/weekly/interval/cron)

ha_automation.create dispatches schema-validated triggers to
cap_scheduler_add. payload_json carries a self-contained ha_control
JSON so the fire handler (next task) can execute it directly without
re-invoking the LLM."
```

---

### Task 6.4: event_router 룰 + fire dispatch

**Files:**
- Modify: `application/edge_agent/fatfs_image/router_rules/router_rules.json` (또는 components/.../router_rules/...)
- Create or modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` (fire handler function)

문맥: Task 6.3에서 entry 등록까지 동작. fire 시점에 `event_router`가 `ha_automation_fire` 타입 이벤트를 받아 payload_json을 cap_ha_core_execute로 dispatch하도록 룰 추가.

- [ ] **Step 1: Task 6.1에서 발견한 fire dispatch 방식에 따라 분기**

(a) `event_router` 가 `cap_call` 또는 동등한 action을 지원 → JSON 룰로 직접 cap_ha_control / ha_control invoke. cap_ha_automation.c 추가 코드 없음.

(b) 없음 → cap_ha_automation.c에 `cap_ha_automation_handle_fire(const claw_event_t *evt)` 핸들러 함수를 만들고, init 시점에 event_router에 register. 룰은 단순 source 매칭만.

Task 6.1의 spike 결과로 어느 쪽인지 결정. 아래 Step 2는 (b) 가정 — (a)면 룰 JSON만 추가하고 Step 3로 점프.

- [ ] **Step 2: fire handler 함수 추가**

`cap_ha_automation.c` 에 추가:

```c
#include "claw_event_router.h"

/* Invoked by event_router when scheduler fires a ha_automation entry.
 * The event's payload_json holds a self-contained ha_control JSON. */
static esp_err_t handle_automation_fire(const claw_event_t *evt, void *ctx)
{
    (void)ctx;
    if (!evt || !evt->payload_json || !*evt->payload_json) {
        ESP_LOGW(TAG, "automation fire with empty payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "automation fire: %s", evt->payload_json);
    char output[768];
    esp_err_t err = cap_ha_core_execute(evt->payload_json, output, sizeof(output));
    ESP_LOGI(TAG, "automation result: err=%s output=%.200s",
             esp_err_to_name(err), output);
    return err;
}

esp_err_t cap_ha_automation_init(void)
{
    return claw_event_router_register_handler("ha_automation_fire", handle_automation_fire, NULL);
}
```

(`claw_event_router_register_handler` 시그니처는 6.1 spike에서 확인된 실제 API로 교체.)

`cap_ha_control_internal.h` 의 `/* core */` 섹션에 추가:
```c
esp_err_t cap_ha_automation_init(void);
```

`cap_ha_control.c` 의 `cap_ha_group_init` 끝에 (cmd_register 직전):
```c
    err = cap_ha_automation_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "automation_init failed: %s", esp_err_to_name(err));
    }
```

- [ ] **Step 3: 룰 JSON 추가 (option a) — example**

`application/edge_agent/fatfs_image/router_rules/router_rules.json` 에 새 entry 추가 (실제 매칭 키는 6.1 결과 따라):

```json
{
  "id": "ha_automation_fire_rule",
  "match": { "event_type": "ha_automation_fire" },
  "actions": [
    { "type": "log", "level": "info", "message": "ha_automation fire: ${payload_json}" }
  ]
}
```

만약 event_router 가 `cap_invoke` 같은 action을 지원하면:
```json
{
  "actions": [
    { "type": "cap_invoke", "cap": "ha_control", "input_from": "payload_json" }
  ]
}
```

- [ ] **Step 4: 빌드 + fire 검증**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
# wait boot, create automation, force trigger:
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
ser.write(b'cap call ha_automation {\"action\":\"create\",\"trigger\":{\"kind\":\"interval\",\"interval_ms\":2000},\"target\":\"board:onboard_rgb\",\"device_action\":\"toggle\"}\r\n')
time.sleep(3); out = ser.read(ser.in_waiting).decode('utf-8','replace')
print('CREATE:', out[-200:])
# wait 5s for 2 fires:
time.sleep(6); out = ser.read(ser.in_waiting).decode('utf-8','replace')
print('FIRES:', [l for l in out.splitlines() if 'automation' in l or 'cap_ha_board' in l][:6])
ser.close()
"
```
Expected: 5초 동안 보드 RGB가 2번 toggle (2초마다). monitor 로그에 `automation fire: {...}` + `automation result: err=ESP_OK output=...success:true...` 2회.

- [ ] **Step 5: cleanup — 등록한 자동화 제거**

```bash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(2)
ser.write(b'scheduler --list\r\n'); time.sleep(2)
print(ser.read(ser.in_waiting).decode('utf-8','replace'))
ser.close()
"
# 출력에서 ha_auto_<id> 찾아서:
# scheduler --remove --id ha_auto_<id>
```

- [ ] **Step 6: commit**

```bash
git add application/edge_agent/fatfs_image/router_rules/router_rules.json \
        components/claw_capabilities/cap_ha_control/
git commit -m "feat(cap_ha_automation): fire dispatch — scheduler → cap_ha_core_execute

Registers a handler for event_type=ha_automation_fire that pulls
payload_json off the event and runs it through cap_ha_core_execute
without re-invoking the LLM. Verified on-board: interval=2000ms
automation toggles board RGB every 2s for 4s, no LLM round-trip."
```

---

### Task 6.5: `remove` + `list` + `trigger_now`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

- [ ] **Step 1: do_remove / do_list / do_trigger_now 함수 추가**

```c
static esp_err_t do_remove(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    esp_err_t err = cap_scheduler_remove(id_j->valuestring);
    cJSON *resp = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(resp, "success", true);
        char msg[160];
        snprintf(msg, sizeof(msg), "자동화 '%s'를 삭제했습니다.", id_j->valuestring);
        cJSON_AddStringToObject(resp, "message", msg);
        cJSON_AddStringToObject(resp, "automation_id", id_j->valuestring);
    } else {
        cJSON_AddBoolToObject(resp, "success", false);
        char msg[160];
        snprintf(msg, sizeof(msg), "자동화 삭제 실패 (id=%s, err=%s).",
                 id_j->valuestring, esp_err_to_name(err));
        cJSON_AddStringToObject(resp, "message", msg);
        cJSON_AddNullToObject(resp, "automation_id");
    }
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_list(char *output, size_t output_size)
{
    /* cap_scheduler_list_json fills a JSON array of {id, kind, enabled, next_fire_ms}. */
    char *buf = malloc(2048);
    if (!buf) { emit_auto_failure(output, output_size, "내부 오류 (메모리 부족)."); return ESP_OK; }
    esp_err_t err = cap_scheduler_list_json(buf, 2048);
    if (err != ESP_OK) {
        free(buf);
        emit_auto_failure(output, output_size, "자동화 목록 조회 실패.");
        return ESP_OK;
    }
    /* Filter to ha_auto_* prefix entries — caller's payload may want raw or summary. */
    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    cJSON *out_arr = cJSON_CreateArray();
    cJSON *it = NULL;
    if (cJSON_IsArray(arr)) {
        cJSON_ArrayForEach(it, arr) {
            const cJSON *id_j = cJSON_GetObjectItem(it, "id");
            if (cJSON_IsString(id_j) && strncmp(id_j->valuestring, "ha_auto_", 8) == 0) {
                cJSON_AddItemToArray(out_arr, cJSON_Duplicate(it, 1));
            }
        }
    }
    if (arr) cJSON_Delete(arr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[64];
    snprintf(msg, sizeof(msg), "자동화 %d건 조회됨.", cJSON_GetArraySize(out_arr));
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddItemToObject(resp, "automations", out_arr);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t do_trigger_now(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    esp_err_t err = cap_scheduler_trigger_now(id_j->valuestring);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    char msg[160];
    if (err == ESP_OK) {
        snprintf(msg, sizeof(msg), "자동화 '%s'를 즉시 실행했습니다.", id_j->valuestring);
    } else {
        snprintf(msg, sizeof(msg), "자동화 즉시 실행 실패 (id=%s, err=%s).",
                 id_j->valuestring, esp_err_to_name(err));
    }
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id_j->valuestring);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **Step 2: execute() dispatch 확장**

`cap_ha_automation_execute` 의 if-else chain에 추가:

```c
    } else if (strcmp(action, "remove") == 0) {
        ret = do_remove(root, output_json, output_size);
    } else if (strcmp(action, "list") == 0) {
        ret = do_list(output_json, output_size);
    } else if (strcmp(action, "trigger_now") == 0) {
        ret = do_trigger_now(root, output_json, output_size);
    } else if (strcmp(action, "update") == 0) {
        emit_auto_failure(output_json, output_size,
                          "update는 다음 task에서 구현됩니다.");
    }
```

- [ ] **Step 3: 빌드 + remove/list 검증**

```bash
idf.py build && idf.py -p $ESP_PORT flash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
# create
ser.write(b'cap call ha_automation {\"action\":\"create\",\"trigger\":{\"kind\":\"interval\",\"interval_ms\":60000},\"target\":\"board:onboard_rgb\",\"device_action\":\"turn_on\"}\r\n')
time.sleep(3); print('CREATE:', ser.read(ser.in_waiting).decode('utf-8','replace')[-200:])
# list
ser.write(b'cap call ha_automation {\"action\":\"list\"}\r\n'); time.sleep(3)
print('LIST:', ser.read(ser.in_waiting).decode('utf-8','replace')[-400:])
# (extract id from LIST output, then remove)
ser.close()
"
```
Expected: create 후 list가 1건 반환 + automation_id 포함. remove 시 success:true.

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "feat(cap_ha_automation): remove / list / trigger_now actions"
```

---

### Task 6.6: `update`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

문맥: update는 기존 entry를 잡아 새 spec으로 교체. cap_scheduler_update는 같은 id로 전체 item 덮어쓰기.

- [ ] **Step 1: do_update 함수**

`do_create` 로직을 부분 재사용 — `automation_id`가 있다는 점 + scheduler_update 호출 대신.

```c
static esp_err_t do_update(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }

    /* Snapshot the existing entry first to preserve fields the caller omits. */
    cap_scheduler_snapshot_t snap = {0};
    if (cap_scheduler_get_snapshot(id_j->valuestring, &snap) != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "자동화 '%s'를 찾을 수 없습니다.", id_j->valuestring);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    cap_scheduler_item_t item = snap.item;  /* copy */

    /* Apply optional fields from the request. Mirror do_create's logic. */
    /* (trigger update + target/device_action/etc — same code shape; refactor a
     *  shared helper if duplication grows.) */
    /* ...same trigger/payload assembly as do_create, but using `item` instead
     *  of fresh struct, and skipping the id auto-generation step... */

    esp_err_t err = cap_scheduler_update(&item);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    char msg[160];
    if (err == ESP_OK) {
        snprintf(msg, sizeof(msg), "자동화 '%s'를 업데이트했습니다.", item.id);
    } else {
        snprintf(msg, sizeof(msg), "자동화 업데이트 실패 (id=%s, err=%s).",
                 item.id, esp_err_to_name(err));
    }
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", item.id);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

(do_create의 trigger/payload 빌드 로직을 helper로 추출하면 약 50줄 중복 회피 — 수정 task인 만큼 helper 추출 권장.)

- [ ] **Step 2: dispatch + 빌드 + 검증**

execute()의 `"update"` 분기를 `do_update(root, ...)` 호출로 교체.

```bash
idf.py build && idf.py -p $ESP_PORT flash
# create with interval=10000 → update to interval=2000 → verify fires accelerate
```

- [ ] **Step 3: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "feat(cap_ha_automation): update action — modify existing entry by id"
```

---

### Task 6.7: Console `--automation` 명령

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`

- [ ] **Step 1: argtable + dispatch 추가**

`ha_args` 구조체 + arg 등록 + dispatch (set_url/set_token 패턴 그대로):

```c
    struct arg_str *automation;
    /* ... */
    ha_args.automation = arg_str0(NULL, "automation", "<json>",
                                  "ha_automation payload (action=create/update/remove/list/trigger_now)");
    /* arg_end(N) 의 N을 기존 +1 */

    /* cmd_ha_control 안: */
    if (ha_args.automation->count > 0) {
        char output[1024];
        cap_ha_automation_execute(ha_args.automation->sval[0], output, sizeof(output));
        printf("%s\n", output);
        return 0;
    }
```

`help` 문자열에 `| --automation '<json>'` 추가.

- [ ] **Step 2: 빌드 + 모든 action 콘솔로 검증**

```bash
idf.py build && idf.py -p $ESP_PORT flash
# 콘솔에서 5개 action 시나리오 차례로:
ha_control --automation '{"action":"list"}'
ha_control --automation '{"action":"create","trigger":{"kind":"daily_time","time":"19:00"},"target":"화장실 조명","device_action":"turn_on"}'
ha_control --automation '{"action":"list"}'
ha_control --automation '{"action":"trigger_now","automation_id":"ha_auto_<id>"}'   # 즉시 화장실 조명 ON
ha_control --automation '{"action":"update","automation_id":"ha_auto_<id>","trigger":{"kind":"daily_time","time":"20:00"},"target":"화장실 조명","device_action":"turn_on"}'
ha_control --automation '{"action":"remove","automation_id":"ha_auto_<id>"}'
```
Expected: 각각 성공 메시지 + 보드 reboot 후에도 NVS persist (`cap_scheduler` 가 자동 저장).

- [ ] **Step 3: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c
git commit -m "feat(cap_ha_control): console --automation <json> command

Wires the existing argtable3 console to cap_ha_automation_execute so
demo prep and debug can exercise CRUD without going through the LLM."
```

---

### Task 6.8: Telegram NL E2E + cleanup

**Files:** (행동 검증)

- [ ] **Step 1: Telegram 시나리오 검증**

실 Telegram client에서:
1. "보드 RGB를 30초마다 toggle하는 자동화 만들어줘" → `자동화 등록했습니다 (ID: ha_auto_...)`. monitor에 30초 간격 LED toggle 확인.
2. "지금 자동화 뭐가 있어?" → list 응답, 위 자동화 보임.
3. "그 자동화 지워줘" → success, monitor 토글 멈춤.
4. "매일 저녁 7시에 화장실 조명 켜는 자동화 만들어" → daily_time 등록, list로 확인. (7시 실제 fire는 시간상 시뮬레이트 — `trigger_now` 또는 콘솔로 검증.)
5. "그 자동화를 8시로 바꿔" → update 성공.

각 단계에서 message verbatim echo 확인 — LLM이 자체 합성 0회.

- [ ] **Step 2: 보드 reboot 후 persist 확인**

```bash
idf.py -p $ESP_PORT monitor
# Ctrl-T R로 reset
# 부팅 후 ha_control --automation '{"action":"list"}' → 등록한 자동화들 그대로 보임
```

- [ ] **Step 3: commit (no-op or doc)**

행동 검증만이라 코드 변경 없으면 commit 없음. 결과를 다음 task의 learn log에 기록.

---

## Task 7: 최종 보안 audit + learn log

**Files:**
- Create: `docs/learn/20260511-cap-ha-control-v4.md`

- [ ] **Step 1: 토큰 누출 + v3 잔재 grep**

```bash
git grep -E "Bearer [A-Za-z0-9]{20,}|eyJ[A-Za-z0-9_-]{20,}"
git grep -lE "bathroom_light_on|bathroom_light_off|rgb_purple_on|rgb_purple_off" -- ':(exclude)docs/learn/' ':(exclude)smarthome-docs/'
```
Expected: 둘 다 0 hits.

- [ ] **Step 2: learn log 작성**

`docs/learn/20260511-cap-ha-control-v4.md` 에 다음 내용:
- v3 review 5 findings 처리 결과 (Tasks 1–5 commit refs)
- 자동화 feature (Task 6) — architecture 결정, payload 직렬화 한계 (512B), CRON 변환 방식, persist 검증
- 발견 사항 / 함정 (Task 6.1 spike 결과, fire dispatch 방식, scheduler task stack 등)
- v5 follow-ups 후보 (claw_cap_invalidate_tool_description API, HA cert pem import, scheduler payload 한계 ↑ 등)

- [ ] **Step 3: commit + PR open**

```bash
git add docs/learn/20260511-cap-ha-control-v4.md
git commit -m "docs(learn): cap_ha_control v4 작업 기록"
git push -u origin feat/cap-ha-control-v4
gh pr create --base main --title "feat(cap_ha_control): v4 — safety fixes + ha_automation"
```

---

## Self-Review

**1. Spec coverage:** 5 review findings + automation 모두 task로 매핑됨. 안 보이는 spec 요구사항 없음.

**2. Placeholder scan:** 모든 task가 실제 코드 / 파일 / 명령 포함. Task 6.4 Step 3의 룰 JSON은 event_router 실제 schema 발견 후 (6.1 spike) 정확해질 것 — 현재는 두 가지 가능성 (cap_invoke action 존재 여부)을 명시. 이건 plan-time에 해결 불가능한 미지수라 spike task로 분리한 게 맞음.

**3. Type consistency:** `cap_ha_automation_execute(input_json, output_json, output_size)` 시그니처가 v3 `cap_ha_core_execute` 패턴 그대로. descriptor의 `execute` 필드는 wrapper로 ctx 처리 — v3 `cap_ha_execute` 패턴 일치. `cap_scheduler_item_t` 필드 (id/kind/cron_expr/payload_json/event_type)는 헤더 파일에서 직접 확인된 이름 사용. `claw_event_router_register_handler` 시그니처는 6.1 spike에서 실제 API로 교체될 것 — 그 전까지는 가상.

**4. Frequent commits:** Tasks 1, 2, 3, 4, 5 각 1 commit. Task 6은 6.2–6.7 각 1 commit (6.1, 6.8은 commit 없음 — spike + 행동 검증). Task 7 learn log 1 commit. 총 12 commits in v4.

**5. TDD adaptation:** firmware 특성상 unit test 없음. 각 task는 build → flash → console-verify → behavior-verify 패턴으로 검증. v3 patterned과 일관.

큰 누락: 없음. Task 4 Step 5의 escalation (claw_cap_invalidate_tool_description API 추가)은 6.1 spike 같은 dependency가 있어 별도 commit으로 분리 권장 — 그 결정은 Step 1 조사 결과에 따라.

---

## Execution Handoff

Plan saved to `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task + 두 단계 리뷰. PR-A (Tasks 1–3)는 작아서 1 세션에 가능. PR-B (Task 6.1–6.8)는 큰 작업이라 sub-task 단위로 review checkpoint. PR-C/D (Tasks 5, 4)는 dependency 검토 후.

**2. Inline Execution** — 이 세션에서 직접 진행, task 사이 checkpoint.

어떤 방식으로 진행하시겠어요?
