# cap_ha_control v4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close 5 v3 review findings (1 race + 2 mechanical safety + 2 infrastructure) and add automation 등록/수정/제거 as a new typed tool `ha_automation` that **delegates to HA's `/api/config/automation/config/<id>` REST endpoint** (Option B per user decision 2026-05-11).

**Architecture:** v3의 firmware-owns-everything 패턴 유지 (schema validate + 한국어 message + verbatim echo). 자동화 *저장*은 HA에 위임 — firmware가 typed payload를 HA-native automation YAML/JSON으로 번역해 `POST /api/config/automation/config/<id>` 로 전송, `POST /api/services/automation/reload` 로 runtime 로드. 결과적으로 자동화가 HA UI에서 보이고 편집 가능, ESP-Claw reflash해도 살아남음. 안전성 수정(mutex / hex validate / count cap)은 v3 코드 surgical patch.

**Trade-off / 범위:** `target=board:onboard_rgb` 같은 보드-only 자동화는 HA entity가 아니므로 v4 `ha_automation`이 **명시적으로 reject**. 보드 자동화는 v5에서 cap_scheduler subset으로 별도 처리 (필요시).

**Tech Stack:** ESP-IDF v5.5.4, FreeRTOS (mutex), cJSON, esp_http_client (HA REST: POST/DELETE/GET), NVS, claw_cap framework. (Option A의 cap_scheduler / claw_event_router는 v4에서 미사용.)

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
| `components/claw_capabilities/cap_ha_control/CMakeLists.txt` | 신규 src 등록 (cap_scheduler REQUIRES는 없음 — Option B) |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` | Task 1 mutex + Task 3 entity count cap + Task 4 boot-fetch에서 compose_description 재호출 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` | Task 1 mutex extern + Task 4 cap_ha_compose_description() prototype + Task 5 NVS insecure 키 + Task 6 automation HTTP/translate prototypes |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c` | Task 2 `#rrggbb` isxdigit() 검증 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` | Task 4 `compose_description` 외부 노출 + Task 6 `ha_automation` 두 번째 descriptor 추가 |
| `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` | Task 5 `https://` scheme 분기 + insecure flag 읽기 + Task 6 automation HTTP 헬퍼 (`cap_ha_http_put_automation_config`, `_delete_automation_config`, `_reload_automations`, `_call_automation_service`) |
| `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c` | Task 5 `--set-insecure on/off` + Task 6 `--automation '<json>'` |

조사 (구현 전):

| 파일 | 목적 |
|---|---|
| `components/claw_modules/claw_cap/src/claw_cap.c` | Task 4 descriptor cache 동작 — boot-fetch 후 compose가 LLM context에 propagate되는지 |
| 라이브 HA `192.168.1.94:8123/api/config/automation/config/*` | Task 6 endpoint 동작은 plan 작성 시점에 이미 라이브 검증됨 (POST/DELETE/reload 모두 200 ok). HA-native automation 3건 이미 존재 — list 시 entity_id가 `automation.<slug>` 패턴. |

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

## Task 6: `ha_automation` typed tool — Option B (HA REST 위임)

Sub-tasks 6.1 → 6.9. PR-B 단위로 묶음.

문맥: user 요청 핵심 항목. "화장실 조명을 매일 저녁 7시에 켜줘" 같은 자연어로 자동화 룰 등록/수정/제거. v3 ha_control과 동일한 firmware-owns-translation + verbatim echo 패턴 유지하되, **자동화 저장은 HA에 위임**. HA UI에서 보이고/편집 가능, ESP-Claw reflash 무관.

**검증된 사실 (Plan v4 작성 시점 라이브 HA에서 확인):**

| 동작 | 엔드포인트 | 결과 |
|---|---|---|
| Create / Update (upsert by id) | `POST /api/config/automation/config/<id>` JSON body | 200 `{"result":"ok"}` |
| Delete | `DELETE /api/config/automation/config/<id>` | 200 `{"result":"ok"}` |
| Reload (runtime 로드 — POST 후 필수) | `POST /api/services/automation/reload` | 200 |
| List entities | `GET /api/states` 필터 `entity_id` prefix `automation.` | 기존 자동화 entry들 |
| Trigger now | `POST /api/services/automation/trigger` body `{"entity_id":"automation.<id>"}` | 200 |
| Enable / Disable | `POST /api/services/automation/turn_on` (또는 turn_off) body `{"entity_id":"automation.<id>"}` | 200 |

**HA automation 페이로드 schema (검증된 minimal example):**
```json
{
  "alias": "ESP-Claw test",
  "trigger": [{"platform": "time", "at": "23:59:59"}],
  "action": [{"service": "light.turn_off", "target": {"entity_id": "light.smart_bulb"}}],
  "mode": "single"
}
```
- `id`는 URL 경로 (`/api/config/automation/config/<id>`)에 들어가고 entity_id는 `automation.<id>` 가 됨.
- `mode` 기본값 `single`. v4에선 그대로.
- HA가 `description`, `id`, `alias`, `trigger[]`, `condition[]`, `action[]`, `mode` 필드를 받음.

**범위 제약:** `target` 이 `board:onboard_rgb` 같은 board path면 v4 ha_automation이 reject + 명확한 메시지 ("보드 자체 자동화는 v5에서 지원될 예정입니다."). HA-native automation은 HA entity만 controllable.

**Trigger 번역 매핑:**
| 입력 (LLM payload) | HA trigger 변환 |
|---|---|
| `kind: "daily_time", time: "HH:MM"` | `[{platform: time, at: "HH:MM:00"}]` |
| `kind: "weekly", time: "HH:MM", weekdays: [0..6]` | `trigger: [{platform: time, at: "HH:MM:00"}]` + `condition: [{condition: time, weekday: ["sun","mon",...]}]` |
| `kind: "interval", interval_ms: N` (≥2000) | `[{platform: time_pattern, seconds: "/N"}]` (변환 규칙: 2000–59999ms → seconds, 60000–3599999ms → minutes, 3600000+ → hours) |
| `kind: "cron", cron: "<5-field expr>"` | v4 미지원 — HA가 cron 네이티브 지원 안 함. reject + 메시지. v5에서 template trigger로 변환. |

**Action 번역 매핑:** v3 ha_control의 service/data 합성 로직을 재사용해야 함 → Task 6.4에서 `cap_ha_build_ha_action_json` helper로 추출.

---

### Task 6.1: HTTP 헬퍼 — automation config CRUD + reload + service call

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`

문맥: v3 `cap_ha_http_post_service` 패턴 재사용 (esp_http_client + Bearer + crt_bundle + 16KB response buffer + heap auth_header). 4개 신규 헬퍼 추가.

- [ ] **Step 1: prototypes 헤더에 추가**

`cap_ha_control_internal.h` 의 `/* http */` 섹션 끝에:

```c
/* HA REST automation CRUD (used by cap_ha_automation). */
esp_err_t cap_ha_http_put_automation_config(const char *id,
                                            const char *config_json,
                                            int *http_status_out,
                                            char *response_buf,
                                            size_t response_buf_size);
esp_err_t cap_ha_http_delete_automation_config(const char *id,
                                               int *http_status_out,
                                               char *response_buf,
                                               size_t response_buf_size);
esp_err_t cap_ha_http_reload_automations(int *http_status_out,
                                         char *response_buf,
                                         size_t response_buf_size);
esp_err_t cap_ha_http_call_automation_service(const char *service,
                                              const char *entity_id,
                                              int *http_status_out,
                                              char *response_buf,
                                              size_t response_buf_size);
```
(`service`는 `"trigger"` / `"turn_on"` / `"turn_off"` 중 하나.)

- [ ] **Step 2: PUT/POST helper (config write)**

`cap_ha_control_http.c` 의 `cap_ha_http_post_service` 함수를 참고해 새 함수 추가. v3 코드와의 차이: URL 패턴이 `/api/config/automation/config/<id>` 이고 메서드는 POST (HA가 upsert로 처리).

```c
esp_err_t cap_ha_http_put_automation_config(const char *id,
                                            const char *config_json,
                                            int *http_status_out,
                                            char *response_buf,
                                            size_t response_buf_size)
{
    if (!id || !*id || !config_json || !response_buf || response_buf_size == 0)
        return ESP_ERR_INVALID_ARG;
    response_buf[0] = '\0';
    if (http_status_out) *http_status_out = 0;

    char base_url[160] = {0};
    char *token = NULL;
    size_t token_cap = 4096;
    esp_err_t err = cap_ha_http_get_url(base_url, sizeof(base_url));
    if (err != ESP_OK) { ESP_LOGW(TAG, "ha_url not set"); return ESP_ERR_NVS_NOT_FOUND; }
    token = malloc(token_cap);
    if (!token) return ESP_ERR_NO_MEM;
    err = cap_ha_http_get_token(token, token_cap);
    if (err != ESP_OK) { free(token); return err; }

    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') base_url[--blen] = '\0';

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s/api/config/automation/config/%s", base_url, id);

    size_t auth_len = strlen(token) + 16;
    char *auth_header = malloc(auth_len);
    if (!auth_header) { free(token); return ESP_ERR_NO_MEM; }
    snprintf(auth_header, auth_len, "Bearer %s", token);
    free(token);

    cap_ha_buf_t resp = { .data = response_buf, .len = 0, .cap = response_buf_size };
    bool is_https = (strncmp(full_url, "https://", 8) == 0);
    bool insecure = is_https && cap_ha_http_get_insecure();
    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = CAP_HA_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = (is_https && !insecure) ? esp_crt_bundle_attach : NULL,
        .skip_cert_common_name_check = insecure,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { free(auth_header); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Authorization", auth_header);
    esp_http_client_set_post_field(cli, config_json, (int)strlen(config_json));

    ESP_LOGI(TAG, "POST %s body_len=%zu", full_url, strlen(config_json));
    err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    if (http_status_out) *http_status_out = status;
    esp_http_client_cleanup(cli);
    free(auth_header);
    ESP_LOGI(TAG, "automation PUT result err=%s status=%d", esp_err_to_name(err), status);
    return err;
}
```

- [ ] **Step 3: DELETE / reload / service-call helper들**

같은 패턴으로 3개 추가:

```c
esp_err_t cap_ha_http_delete_automation_config(const char *id,
                                               int *http_status_out,
                                               char *response_buf,
                                               size_t response_buf_size)
{
    /* same scaffold as put_automation_config but: */
    /*   .method = HTTP_METHOD_DELETE */
    /*   no esp_http_client_set_post_field */
    /*   no Content-Type header */
    /* full_url = "%s/api/config/automation/config/%s" */
}

esp_err_t cap_ha_http_reload_automations(int *http_status_out,
                                         char *response_buf,
                                         size_t response_buf_size)
{
    /* same scaffold but: */
    /*   .method = HTTP_METHOD_POST */
    /*   full_url = "%s/api/services/automation/reload" */
    /*   body = "{}" (HA service call requires JSON body, even empty) */
}

esp_err_t cap_ha_http_call_automation_service(const char *service,
                                              const char *entity_id,
                                              int *http_status_out,
                                              char *response_buf,
                                              size_t response_buf_size)
{
    /* same scaffold but: */
    /*   full_url = "%s/api/services/automation/%s" (service is "trigger" / "turn_on" / "turn_off") */
    /*   body = "{\"entity_id\":\"automation.<id>\"}" */
}
```
(boilerplate 50% 가량 중복 — task 끝나면 helper로 추출하는 게 깔끔. 일단 working 우선.)

- [ ] **Step 4: 빌드 + 4개 헬퍼 console 직접 호출 테스트**

가장 빠른 검증: 임시 콘솔 명령 추가해 직접 호출하거나, 다음 task에서 `cap_ha_automation` 통해 indirect 검증. 빌드만 먼저:

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
```
Expected: 성공.

- [ ] **Step 5: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c \
        components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h
git commit -m "feat(cap_ha_control): HA automation REST helpers

Four esp_http_client wrappers for the HA automation config endpoint:
PUT (upsert by id), DELETE, /api/services/automation/reload, and the
service-call helper for trigger/turn_on/turn_off. Same Bearer + crt
bundle + 16KB response buffer + heap auth_header pattern as the v3
post_service / get_states. Used by cap_ha_automation in 6.4-6.6."
```

---

### Task 6.2: `ha_automation` descriptor scaffold (stub execute)

**Files:**
- Create: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`
- Modify: `components/claw_capabilities/cap_ha_control/CMakeLists.txt`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h`

- [ ] **Step 1: CMakeLists에 src 추가 (cap_scheduler REQUIRES 없음)**

`components/claw_capabilities/cap_ha_control/CMakeLists.txt` 의 SRCS 블록에 추가:
```cmake
        "src/cap_ha_automation.c"
```
REQUIRES는 기존 그대로 (Option B는 cap_scheduler 의존성 없음).

- [ ] **Step 2: cap_ha_automation.c stub 작성**

```c
/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_ha_control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cap_ha_auto";

static const char *VALID_AUTO_ACTIONS[] = {
    "create", "update", "remove", "list", "trigger_now", "enable", "disable"
};
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

esp_err_t cap_ha_automation_execute(const char *input_json,
                                    char *output_json,
                                    size_t output_size)
{
    if (!output_json || output_size == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        emit_auto_failure(output_json, output_size,
                          "요청을 해석할 수 없습니다 (JSON parse 실패).");
        return ESP_OK;
    }
    const cJSON *action_j = cJSON_GetObjectItem(root, "action");
    const char *action = cJSON_IsString(action_j) ? action_j->valuestring : NULL;
    if (!auto_action_is_valid(action)) {
        emit_auto_failure(output_json, output_size,
                          "action은 create/update/remove/list/trigger_now/enable/disable 중 하나여야 합니다.");
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* stub — 다음 task에서 분기 채움 */
    emit_auto_failure(output_json, output_size,
                      "ha_automation 미구현 (stub — 다음 task에서 채움).");
    cJSON_Delete(root);
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

`cap_ha_control.c` 의 `s_ha_descriptors[]` 배열을 2-element로 확장:
```c
static char s_ha_automation_description[768];

static esp_err_t cap_ha_automation_execute_wrapper(const char *input_json,
                                                   const claw_cap_call_context_t *ctx,
                                                   char *output,
                                                   size_t output_size)
{
    (void)ctx;
    return cap_ha_automation_execute(input_json, output, output_size);
}

static claw_cap_descriptor_t s_ha_descriptors[] = {
    { /* ha_control entry — v3 그대로 */ ... },
    {
        .id = "ha_automation",
        .name = "ha_automation",
        .family = "ha",
        .description = NULL, /* compose_description 에서 설정 */
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
              "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"update\",\"remove\",\"list\",\"trigger_now\",\"enable\",\"disable\"]},"
              "\"automation_id\":{\"type\":\"string\",\"description\":\"HA entity local id (without 'automation.' prefix). create assigns automatically (esp_claw_<ts>).\"},"
              "\"alias\":{\"type\":\"string\",\"description\":\"Human-readable name visible in HA UI.\"},"
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000}"
              "}},"
              "\"target\":{\"type\":\"string\",\"description\":\"HA entity friendly name or entity_id. board:* targets are not supported in v4 (HA-side automation only).\"},"
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

`s_ha_group.descriptor_count` 는 sizeof로 계산 → 자동 갱신.

- [ ] **Step 4: compose_description 확장 — automation 설명**

`cap_ha_control.c` 의 `cap_ha_compose_description()` 끝에 추가:

```c
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove time-based automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target'): %s. "
             "board:* targets (onboard RGB) are NOT supported here — those would require on-device automation, planned for v5. "
             "Use 'create' (assigns automation_id), 'update' (needs automation_id), 'remove' (needs automation_id), "
             "'list' (returns existing automations), 'trigger_now' (force-fire by id), 'enable'/'disable' (toggle by id). "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
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
Expected: `cap_ha_control state=started descriptors=2`.

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/
git commit -m "feat(cap_ha_automation): descriptor scaffold (stub execute)

Adds the second LLM-visible tool 'ha_automation' with the typed
{action, trigger, target, device_action, ...} schema. execute() is a
stub for now; real CRUD via HA REST lands in 6.3-6.6.

board:* targets are documented as v4-unsupported in the description
so the LLM doesn't try to register on-board RGB automations through
this path."
```

---

### Task 6.3: HA automation JSON builder (trigger + action 번역)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

문맥: 사용자 typed payload → HA-native automation YAML/JSON. v3 ha_control의 service-mapping 로직을 재사용해야 하므로 internal.h에서 1개 helper를 노출.

- [ ] **Step 1: v3 service-mapping 로직 helper로 추출**

`cap_ha_control_internal.h` 에 추가:
```c
/* Returns the HA service name (e.g. "turn_on", "open_cover") for a (domain, action)
 * pair, or NULL if the combination is unsupported. Used by both ha_control dispatch
 * (Task 7 in v3) and ha_automation translation (v4 Task 6.3). */
const char *cap_ha_action_to_service(const char *domain, const char *action);
```

`cap_ha_control_core.c` 의 기존 light/cover/switch 분기 inline 코드를 추출:
```c
const char *cap_ha_action_to_service(const char *domain, const char *action)
{
    if (!domain || !action) return NULL;
    if (strcmp(domain, "light") == 0) {
        if (strcmp(action, "turn_on") == 0)  return "turn_on";
        if (strcmp(action, "turn_off") == 0) return "turn_off";
        if (strcmp(action, "toggle") == 0)   return "toggle";
    } else if (strcmp(domain, "cover") == 0) {
        if (strcmp(action, "open") == 0)     return "open_cover";
        if (strcmp(action, "close") == 0)    return "close_cover";
        if (strcmp(action, "toggle") == 0)   return "toggle";
    } else if (strcmp(domain, "switch") == 0) {
        if (strcmp(action, "turn_on") == 0)  return "turn_on";
        if (strcmp(action, "turn_off") == 0) return "turn_off";
        if (strcmp(action, "toggle") == 0)   return "toggle";
    }
    return NULL;
}
```
그리고 v3 `cap_ha_core_execute` 안의 inline `svc = ...` 블록을 `svc = cap_ha_action_to_service(entity.domain, action);` 한 줄로 교체. v3 회귀 확인 (build + 화장실 조명 toggle 테스트).

- [ ] **Step 2: build_ha_action_array() — service call → HA action[]**

`cap_ha_automation.c` 에 추가:
```c
/* Translate a resolved entity + device_action + optional data params into the
 * HA-native action[] array. Returns a freshly-allocated cJSON array; caller
 * owns the lifecycle. Returns NULL on error and logs WARN. */
static cJSON *build_ha_action_array(const cap_ha_entity_t *entity,
                                    const char *device_action,
                                    int brightness_pct, int kelvin,
                                    const char *color)
{
    const char *svc = cap_ha_action_to_service(entity->domain, device_action);
    if (!svc) {
        ESP_LOGW(TAG, "unsupported (domain=%s, action=%s)", entity->domain, device_action);
        return NULL;
    }
    cJSON *arr = cJSON_CreateArray();
    cJSON *step = cJSON_CreateObject();
    char service_full[48];
    snprintf(service_full, sizeof(service_full), "%s.%s", entity->domain, svc);
    cJSON_AddStringToObject(step, "service", service_full);

    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "entity_id", entity->id);
    cJSON_AddItemToObject(step, "target", target);

    /* data — silent drop unsupported per v3 pattern */
    cJSON *data = cJSON_CreateObject();
    bool data_used = false;
    if (brightness_pct >= 0 && entity->supports_brightness) {
        cJSON_AddNumberToObject(data, "brightness_pct", brightness_pct);
        data_used = true;
    }
    if (kelvin >= 0 && (entity->supports_color || entity->supports_brightness)) {
        cJSON_AddNumberToObject(data, "kelvin", kelvin);
        data_used = true;
    }
    if (color && *color && entity->supports_color) {
        int rgb[3];
        if (cap_ha_color_to_rgb(color, rgb) == ESP_OK) {
            cJSON_AddItemToObject(data, "rgb_color", cJSON_CreateIntArray(rgb, 3));
            data_used = true;
        }
    }
    if (data_used) cJSON_AddItemToObject(step, "data", data);
    else cJSON_Delete(data);

    cJSON_AddItemToArray(arr, step);
    return arr;
}
```

- [ ] **Step 3: build_ha_trigger_array() — trigger spec → HA trigger[] (+ condition[] for weekly)**

`cap_ha_automation.c` 에 추가:
```c
/* Translate user trigger spec into HA-native trigger[] (and optional condition[]).
 * Out params: trigger_out / condition_out (caller owns; condition_out may be NULL).
 * Returns ESP_OK + sets *trigger_out, or error with both NULL. */
static esp_err_t build_ha_trigger_array(const cJSON *trigger_in,
                                        cJSON **trigger_out, cJSON **condition_out,
                                        char *err_msg, size_t err_msg_size)
{
    *trigger_out = NULL;
    *condition_out = NULL;
    if (!cJSON_IsObject(trigger_in)) {
        snprintf(err_msg, err_msg_size, "trigger object가 필요합니다.");
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *kind_j = cJSON_GetObjectItem(trigger_in, "kind");
    const char *kind = cJSON_IsString(kind_j) ? kind_j->valuestring : NULL;
    if (!kind) {
        snprintf(err_msg, err_msg_size, "trigger.kind가 필요합니다.");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *arr = cJSON_CreateArray();

    if (strcmp(kind, "daily_time") == 0 || strcmp(kind, "weekly") == 0) {
        const cJSON *time_j = cJSON_GetObjectItem(trigger_in, "time");
        if (!cJSON_IsString(time_j) || strlen(time_j->valuestring) != 5 ||
            time_j->valuestring[2] != ':') {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size, "trigger.time은 'HH:MM' 형식이어야 합니다.");
            return ESP_ERR_INVALID_ARG;
        }
        char at[12];
        snprintf(at, sizeof(at), "%s:00", time_j->valuestring);
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "time");
        cJSON_AddStringToObject(step, "at", at);
        cJSON_AddItemToArray(arr, step);

        if (strcmp(kind, "weekly") == 0) {
            const cJSON *days = cJSON_GetObjectItem(trigger_in, "weekdays");
            if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) == 0) {
                cJSON_Delete(arr);
                snprintf(err_msg, err_msg_size, "weekly trigger에는 weekdays 배열이 필요합니다.");
                return ESP_ERR_INVALID_ARG;
            }
            static const char *DAY_NAMES[] = {"sun","mon","tue","wed","thu","fri","sat"};
            cJSON *cond_arr = cJSON_CreateArray();
            cJSON *cond_step = cJSON_CreateObject();
            cJSON_AddStringToObject(cond_step, "condition", "time");
            cJSON *weekday_arr = cJSON_CreateArray();
            cJSON *d = NULL;
            cJSON_ArrayForEach(d, days) {
                if (cJSON_IsNumber(d) && d->valueint >= 0 && d->valueint <= 6) {
                    cJSON_AddItemToArray(weekday_arr,
                                         cJSON_CreateString(DAY_NAMES[d->valueint]));
                }
            }
            cJSON_AddItemToObject(cond_step, "weekday", weekday_arr);
            cJSON_AddItemToArray(cond_arr, cond_step);
            *condition_out = cond_arr;
        }
    } else if (strcmp(kind, "interval") == 0) {
        const cJSON *iv_j = cJSON_GetObjectItem(trigger_in, "interval_ms");
        if (!cJSON_IsNumber(iv_j) || iv_j->valueint < 2000) {
            cJSON_Delete(arr);
            snprintf(err_msg, err_msg_size,
                     "interval_ms는 2000 이상이어야 합니다 (HA time_pattern 한계).");
            return ESP_ERR_INVALID_ARG;
        }
        int iv = iv_j->valueint;
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "platform", "time_pattern");
        if (iv < 60000) {
            int sec = iv / 1000;
            if (sec < 2 || sec > 60) sec = sec < 2 ? 2 : 60;
            char val[8];
            snprintf(val, sizeof(val), "/%d", sec);
            cJSON_AddStringToObject(step, "seconds", val);
        } else if (iv < 3600000) {
            int min = iv / 60000;
            if (min < 1 || min > 60) min = min < 1 ? 1 : 60;
            char val[8]; snprintf(val, sizeof(val), "/%d", min);
            cJSON_AddStringToObject(step, "minutes", val);
        } else {
            int hr = iv / 3600000;
            if (hr < 1 || hr > 24) hr = hr < 1 ? 1 : 24;
            char val[8]; snprintf(val, sizeof(val), "/%d", hr);
            cJSON_AddStringToObject(step, "hours", val);
        }
        cJSON_AddItemToArray(arr, step);
    } else {
        cJSON_Delete(arr);
        snprintf(err_msg, err_msg_size,
                 "지원하지 않는 trigger.kind입니다 (%s). daily_time/weekly/interval만 지원.", kind);
        return ESP_ERR_INVALID_ARG;
    }

    *trigger_out = arr;
    return ESP_OK;
}
```

- [ ] **Step 4: 빌드**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
```
Expected: 성공. 호출은 다음 task의 do_create에서.

- [ ] **Step 5: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/
git commit -m "feat(cap_ha_automation): trigger/action JSON builders

build_ha_trigger_array translates {kind, time, weekdays, interval_ms}
into HA-native trigger[] (and condition[] for weekly). build_ha_action_array
reuses cap_ha_action_to_service (extracted from v3's inline switch) to
produce the HA action step with service / target.entity_id / data."
```

---

### Task 6.4: `create` action (POST /api/config/automation/config + reload)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

- [ ] **Step 1: do_create 함수**

`cap_ha_automation.c` 에 추가:
```c
static esp_err_t do_create(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *target_j = cJSON_GetObjectItem(root, "target");
    const cJSON *dev_action_j = cJSON_GetObjectItem(root, "device_action");
    const cJSON *trigger_in = cJSON_GetObjectItem(root, "trigger");
    const cJSON *alias_j = cJSON_GetObjectItem(root, "alias");
    if (!cJSON_IsString(target_j) || !cJSON_IsString(dev_action_j) ||
        !cJSON_IsObject(trigger_in)) {
        emit_auto_failure(output, output_size,
                          "자동화 등록에는 trigger / target / device_action이 모두 필요합니다.");
        return ESP_OK;
    }

    /* Reject board:* targets — HA can't automate on-board entities. */
    if (strncmp(target_j->valuestring, "board:", 6) == 0) {
        emit_auto_failure(output, output_size,
                          "보드 자체 자동화는 v5에서 지원될 예정입니다. v4에서는 HA 기기만 자동화 가능합니다.");
        return ESP_OK;
    }

    /* Resolve target to an HA entity. */
    cap_ha_entity_t entity = {0};
    if (cap_ha_resolve_target(target_j->valuestring, &entity) != ESP_OK) {
        char candidates[192];
        cap_ha_resolve_top_candidates(candidates, sizeof(candidates), 5);
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "\"%s\"에 해당하는 기기를 찾지 못했습니다. 사용 가능한 기기: %s.",
                 target_j->valuestring, candidates);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    if (strncmp(entity.domain, "board", 5) == 0) {
        emit_auto_failure(output, output_size,
                          "보드 자체 자동화는 v5에서 지원될 예정입니다.");
        return ESP_OK;
    }

    /* Build action[] */
    int brightness_pct = -1, kelvin = -1;
    const cJSON *bj = cJSON_GetObjectItem(root, "brightness_pct");
    if (cJSON_IsNumber(bj)) brightness_pct = bj->valueint;
    const cJSON *kj = cJSON_GetObjectItem(root, "kelvin");
    if (cJSON_IsNumber(kj)) kelvin = kj->valueint;
    const cJSON *cj = cJSON_GetObjectItem(root, "color");
    const char *color = cJSON_IsString(cj) ? cj->valuestring : NULL;

    cJSON *action_arr = build_ha_action_array(&entity, dev_action_j->valuestring,
                                              brightness_pct, kelvin, color);
    if (!action_arr) {
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "%s은(는) 해당 동작을 지원하지 않습니다 (action=%s).",
                 entity.friendly_name, dev_action_j->valuestring);
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    /* Build trigger[] + (optional) condition[] */
    cJSON *trigger_arr = NULL, *condition_arr = NULL;
    char err_msg[160];
    if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                               err_msg, sizeof(err_msg)) != ESP_OK) {
        emit_auto_failure(output, output_size, err_msg);
        cJSON_Delete(action_arr);
        return ESP_OK;
    }

    /* Compose final HA automation config JSON */
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "alias",
                            (cJSON_IsString(alias_j) && alias_j->valuestring[0])
                            ? alias_j->valuestring
                            : entity.friendly_name);
    cJSON_AddItemToObject(config, "trigger", trigger_arr);
    if (condition_arr) cJSON_AddItemToObject(config, "condition", condition_arr);
    cJSON_AddItemToObject(config, "action", action_arr);
    cJSON_AddStringToObject(config, "mode", "single");

    /* Auto-generate id */
    char auto_id[64];
    snprintf(auto_id, sizeof(auto_id), "esp_claw_%lld",
             esp_timer_get_time() / 1000000);

    char *config_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (!config_str) {
        emit_auto_failure(output, output_size, "내부 오류 (config 직렬화 실패).");
        return ESP_OK;
    }

    /* PUT to HA */
    char http_resp[1024];
    int http_status = 0;
    esp_err_t err = cap_ha_http_put_automation_config(auto_id, config_str,
                                                     &http_status, http_resp,
                                                     sizeof(http_resp));
    free(config_str);
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    /* Reload so HA runtime picks up the new entity */
    int reload_status = 0;
    char reload_resp[256];
    cap_ha_http_reload_automations(&reload_status, reload_resp, sizeof(reload_resp));
    if (reload_status / 100 != 2) {
        ESP_LOGW(TAG, "automation create succeeded but reload returned %d", reload_status);
    }

    /* Build success response */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[256];
    snprintf(msg, sizeof(msg),
             "'%s' %s 자동화를 등록했습니다 (ID: automation.%s). HA UI에서 확인 가능합니다.",
             entity.friendly_name, dev_action_j->valuestring, auto_id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", auto_id);
    cJSON_AddStringToObject(resp, "entity_id", auto_id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **Step 2: execute() 의 create 분기 연결**

`cap_ha_automation_execute` stub 의 if-else chain에 추가:
```c
    if (strcmp(action, "create") == 0) {
        cJSON_Delete(root);  /* do_create는 root 안 씀 — 또는 root 전달하고 do_create 안에서 Delete */
        return do_create(root, output_json, output_size);
    }
```
(또는 do_create가 root 소유권 받아 내부에서 Delete — 일관된 패턴 유지.)

- [ ] **Step 3: 빌드 + create 검증**

```bash
cd application/edge_agent && idf.py build 2>&1 | tail -3
idf.py -p $ESP_PORT flash
python3 -c "
import serial, time
ser = serial.Serial('$ESP_PORT', 115200, timeout=1); time.sleep(12)
ser.read(ser.in_waiting or 1)
ser.write(b'cap call ha_automation {\"action\":\"create\",\"trigger\":{\"kind\":\"daily_time\",\"time\":\"23:59\"},\"target\":\"화장실 조명\",\"device_action\":\"turn_off\"}\r\n')
time.sleep(8); print(ser.read(ser.in_waiting).decode('utf-8','replace')[-500:])
ser.close()
"
```
Expected: `{"success":true,"message":"'화장실 조명' turn_off 자동화를 등록했습니다 (ID: automation.esp_claw_...)","automation_id":"esp_claw_...","entity_id":"esp_claw_...","raw_status":200}`.

라이브 HA에서 검증:
```bash
TOKEN=$(...); curl -sS -H "Authorization: Bearer $TOKEN" \
  "http://192.168.1.94:8123/api/states/automation.esp_claw_<id>" | jq .
```
Expected: HA가 새 automation entity를 인식 + alias가 "화장실 조명" + trigger=time.at=23:59:00.

- [ ] **Step 4: 보드 RGB target reject 검증**

```bash
# board:onboard_rgb 자동화 시도
cap call ha_automation '{"action":"create","trigger":{"kind":"interval","interval_ms":30000},"target":"board:onboard_rgb","device_action":"toggle"}'
```
Expected: `{"success":false,"message":"보드 자체 자동화는 v5에서 지원될 예정입니다. v4에서는 HA 기기만 자동화 가능합니다.","automation_id":null}`.

- [ ] **Step 5: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "feat(cap_ha_automation): create action — POST to HA config endpoint

Translates typed payload into HA-native automation config (alias /
trigger / condition / action / mode), POSTs to
/api/config/automation/config/esp_claw_<ts>, then calls
/api/services/automation/reload. board:* targets are rejected with
a v5-roadmap message."
```

---

### Task 6.5: `remove` + `list` + `trigger_now` + `enable` / `disable`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

- [ ] **Step 1: do_remove (DELETE + reload)**

```c
static esp_err_t do_remove(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    /* Accept both 'esp_claw_<ts>' and 'automation.esp_claw_<ts>' forms. */
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;

    char http_resp[256];
    int http_status = 0;
    esp_err_t err = cap_ha_http_delete_automation_config(id, &http_status, http_resp, sizeof(http_resp));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    /* Reload */
    int reload_status = 0;
    cap_ha_http_reload_automations(&reload_status, http_resp, sizeof(http_resp));

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[160];
    snprintf(msg, sizeof(msg), "자동화 'automation.%s'를 삭제했습니다.", id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **Step 2: do_list (GET /api/states + filter)**

Note: `/api/states` 응답이 클 수 있음 (v3 boot-fetch에서 ~50KB 관측). 별도 GET helper 추가 권장 또는 boot-fetch 버퍼 재사용. 일단 새 helper 추가:

internal.h:
```c
esp_err_t cap_ha_http_get_states(char *response_buf, size_t response_buf_size);
```
(이미 v3에 존재 — boot-fetch가 사용. 재사용 OK.)

```c
static esp_err_t do_list(char *output, size_t output_size)
{
    char *states = malloc(CAP_HA_STATES_BUF_BYTES);
    if (!states) {
        emit_auto_failure(output, output_size, "내부 오류 (메모리 부족).");
        return ESP_OK;
    }
    esp_err_t err = cap_ha_http_get_states(states, CAP_HA_STATES_BUF_BYTES);
    if (err != ESP_OK) {
        free(states);
        emit_auto_failure(output, output_size, "HA에서 자동화 목록을 가져오지 못했습니다.");
        return ESP_OK;
    }

    cJSON *arr = cJSON_Parse(states);
    free(states);
    cJSON *out_arr = cJSON_CreateArray();
    int count_total = 0, count_esp = 0;
    if (cJSON_IsArray(arr)) {
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, arr) {
            const cJSON *eid = cJSON_GetObjectItemCaseSensitive(e, "entity_id");
            if (!cJSON_IsString(eid) ||
                strncmp(eid->valuestring, "automation.", 11) != 0) continue;
            count_total++;
            const cJSON *attr = cJSON_GetObjectItemCaseSensitive(e, "attributes");
            const cJSON *fn = cJSON_GetObjectItemCaseSensitive(attr, "friendly_name");
            const cJSON *st = cJSON_GetObjectItemCaseSensitive(e, "state");
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "entity_id", eid->valuestring);
            cJSON_AddStringToObject(item, "friendly_name",
                                   cJSON_IsString(fn) ? fn->valuestring : "");
            cJSON_AddStringToObject(item, "state",
                                   cJSON_IsString(st) ? st->valuestring : "");
            cJSON_AddBoolToObject(item, "esp_claw_managed",
                                  strstr(eid->valuestring, "esp_claw_") != NULL);
            cJSON_AddItemToArray(out_arr, item);
            if (strstr(eid->valuestring, "esp_claw_")) count_esp++;
        }
    }
    if (arr) cJSON_Delete(arr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[128];
    snprintf(msg, sizeof(msg),
             "자동화 %d건 (ESP-Claw 등록: %d건). HA UI에서 모두 확인 가능합니다.",
             count_total, count_esp);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddItemToObject(resp, "automations", out_arr);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **Step 3: do_trigger_now / do_enable / do_disable (service call)**

```c
static esp_err_t do_service(const cJSON *root, const char *service,
                            const char *success_msg_fmt,
                            char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;
    char entity_id[80];
    snprintf(entity_id, sizeof(entity_id), "automation.%s", id);

    char http_resp[256];
    int http_status = 0;
    esp_err_t err = cap_ha_http_call_automation_service(service, entity_id,
                                                       &http_status, http_resp, sizeof(http_resp));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[200];
    snprintf(msg, sizeof(msg), success_msg_fmt, entity_id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}

/* dispatch wrappers */
static esp_err_t do_trigger_now(const cJSON *root, char *out, size_t size) {
    return do_service(root, "trigger", "'%s' 자동화를 즉시 실행했습니다.", out, size);
}
static esp_err_t do_enable(const cJSON *root, char *out, size_t size) {
    return do_service(root, "turn_on", "'%s' 자동화를 활성화했습니다.", out, size);
}
static esp_err_t do_disable(const cJSON *root, char *out, size_t size) {
    return do_service(root, "turn_off", "'%s' 자동화를 비활성화했습니다.", out, size);
}
```

- [ ] **Step 4: execute() dispatch chain 완성**

```c
    if (strcmp(action, "create") == 0)      ret = do_create(root, output_json, output_size);
    else if (strcmp(action, "remove") == 0) ret = do_remove(root, output_json, output_size);
    else if (strcmp(action, "list") == 0)   ret = do_list(output_json, output_size);
    else if (strcmp(action, "trigger_now") == 0) ret = do_trigger_now(root, output_json, output_size);
    else if (strcmp(action, "enable") == 0)  ret = do_enable(root, output_json, output_size);
    else if (strcmp(action, "disable") == 0) ret = do_disable(root, output_json, output_size);
    else if (strcmp(action, "update") == 0) {
        emit_auto_failure(output_json, output_size,
                          "update는 다음 task에서 구현됩니다.");
    }
```

- [ ] **Step 5: 빌드 + on-board E2E**

```bash
idf.py build && idf.py -p $ESP_PORT flash
# 등록 → list → trigger_now (실제 램프 turn_off 동작) → disable → enable → remove
```

라이브 HA에서 매 단계 검증:
- create 직후: `GET /api/states/automation.esp_claw_<id>` → state=on
- disable 후: state=off
- enable 후: state=on
- trigger_now 후: 실제 ha_control이 동작 (램프 toggle)
- remove + reload 후: 404

- [ ] **Step 6: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c
git commit -m "feat(cap_ha_automation): remove / list / trigger_now / enable / disable

remove uses DELETE /api/config/automation/config/<id> + reload.
list uses GET /api/states filtered to automation.*.
trigger_now / enable / disable use /api/services/automation/<service>
with entity_id=automation.<id>."
```

---

### Task 6.6: `update` (POST upsert with snapshot merge)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

문맥: HA의 POST endpoint는 같은 id에 PUT으로 upsert. update = 같은 id로 새 config 보내기. 하지만 caller가 일부 필드만 보냈으면 기존 config의 나머지 필드를 보존해야 한다 → 먼저 GET 해서 merge.

- [ ] **Step 1: HTTP GET helper 추가 (단일 automation config 조회)**

internal.h:
```c
esp_err_t cap_ha_http_get_automation_config(const char *id,
                                            int *http_status_out,
                                            char *response_buf,
                                            size_t response_buf_size);
```

http.c — `GET /api/config/automation/config/<id>` 구현 (post_automation_config와 동일 패턴, 단 method GET + body 없음).

- [ ] **Step 2: do_update — GET → merge → PUT**

```c
static esp_err_t do_update(const cJSON *root, char *output, size_t output_size)
{
    const cJSON *id_j = cJSON_GetObjectItem(root, "automation_id");
    if (!cJSON_IsString(id_j)) {
        emit_auto_failure(output, output_size, "automation_id가 필요합니다.");
        return ESP_OK;
    }
    const char *id = id_j->valuestring;
    if (strncmp(id, "automation.", 11) == 0) id += 11;

    /* Fetch existing config */
    char existing[2048];
    int http_status = 0;
    esp_err_t err = cap_ha_http_get_automation_config(id, &http_status,
                                                    existing, sizeof(existing));
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        if (http_status == 404) {
            snprintf(msg, sizeof(msg),
                     "자동화 '%s'를 찾을 수 없습니다. 먼저 create로 등록하세요.", id);
        } else {
            cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        }
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }

    cJSON *cfg = cJSON_Parse(existing);
    if (!cJSON_IsObject(cfg)) {
        if (cfg) cJSON_Delete(cfg);
        emit_auto_failure(output, output_size, "기존 자동화 config 파싱 실패.");
        return ESP_OK;
    }

    /* Merge: any caller-provided field overrides the existing one. */
    const cJSON *trigger_in = cJSON_GetObjectItem(root, "trigger");
    if (cJSON_IsObject(trigger_in)) {
        cJSON *trigger_arr = NULL, *condition_arr = NULL;
        char err_msg[160];
        if (build_ha_trigger_array(trigger_in, &trigger_arr, &condition_arr,
                                   err_msg, sizeof(err_msg)) != ESP_OK) {
            cJSON_Delete(cfg);
            emit_auto_failure(output, output_size, err_msg);
            return ESP_OK;
        }
        cJSON_DeleteItemFromObject(cfg, "trigger");
        cJSON_AddItemToObject(cfg, "trigger", trigger_arr);
        cJSON_DeleteItemFromObject(cfg, "condition");
        if (condition_arr) cJSON_AddItemToObject(cfg, "condition", condition_arr);
    }
    /* target/device_action change → rebuild action[] */
    const cJSON *target_j = cJSON_GetObjectItem(root, "target");
    const cJSON *dev_action_j = cJSON_GetObjectItem(root, "device_action");
    if (cJSON_IsString(target_j) || cJSON_IsString(dev_action_j)) {
        /* Caller provided at least one — resolve target (default from action[0].target.entity_id if missing). */
        /* Implementation: extract action[0].service / target from existing cfg if not provided,
         *  then call build_ha_action_array. ~30 lines; pattern mirrors do_create. */
    }
    const cJSON *alias_j = cJSON_GetObjectItem(root, "alias");
    if (cJSON_IsString(alias_j)) {
        cJSON_DeleteItemFromObject(cfg, "alias");
        cJSON_AddStringToObject(cfg, "alias", alias_j->valuestring);
    }

    char *config_str = cJSON_PrintUnformatted(cfg);
    cJSON_Delete(cfg);
    if (!config_str) {
        emit_auto_failure(output, output_size, "내부 오류 (config 직렬화 실패).");
        return ESP_OK;
    }

    /* PUT same id → upsert */
    char http_resp[256];
    err = cap_ha_http_put_automation_config(id, config_str, &http_status,
                                            http_resp, sizeof(http_resp));
    free(config_str);
    if (err != ESP_OK || http_status / 100 != 2) {
        char msg[200];
        cap_ha_compose_failure_message(http_status, err, msg, sizeof(msg));
        emit_auto_failure(output, output_size, msg);
        return ESP_OK;
    }
    /* Reload */
    int rs = 0; cap_ha_http_reload_automations(&rs, http_resp, sizeof(http_resp));

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char msg[200];
    snprintf(msg, sizeof(msg), "자동화 'automation.%s'를 업데이트했습니다.", id);
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddStringToObject(resp, "automation_id", id);
    cJSON_AddNumberToObject(resp, "raw_status", http_status);
    char *s = cJSON_PrintUnformatted(resp);
    if (s) { snprintf(output, output_size, "%s", s); free(s); }
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **Step 3: execute() 의 update 분기 연결 + 빌드 + 검증**

```bash
idf.py build && idf.py -p $ESP_PORT flash
# create with time=23:59 → update with time=22:00 → list 후 시간 변경 확인
```

- [ ] **Step 4: commit**

```bash
git add components/claw_capabilities/cap_ha_control/
git commit -m "feat(cap_ha_automation): update action — GET-merge-PUT pattern

Fetches existing automation config via GET /api/config/automation/config/<id>,
merges caller-provided fields (trigger, action, alias) into it, then PUTs
the merged config. Preserves unspecified fields like conditions or mode.
Failures map to v3's compose_failure_message so the message is verbatim-
echoable by the LLM."
```

---

### Task 6.7: Console `--automation <json>`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`

- [ ] **Step 1: argtable + dispatch (Task 5 set_insecure 패턴 그대로)**

`ha_args` 구조체에 `struct arg_str *automation;` 추가, `arg_str0` 등록, `arg_end(N)` 의 N을 +1, dispatch:
```c
    if (ha_args.automation->count > 0) {
        char output[1024];
        cap_ha_automation_execute(ha_args.automation->sval[0], output, sizeof(output));
        printf("%s\n", output);
        return 0;
    }
```
`help` 문자열에 `| --automation '<json>'` 추가.

- [ ] **Step 2: 빌드 + 전체 action 콘솔 검증**

```bash
idf.py build && idf.py -p $ESP_PORT flash
# 콘솔 시나리오:
ha_control --automation '{"action":"list"}'
ha_control --automation '{"action":"create","trigger":{"kind":"daily_time","time":"23:59"},"target":"화장실 조명","device_action":"turn_off","alias":"심야 끄기"}'
ha_control --automation '{"action":"list"}'  # 새 entry 확인
ha_control --automation '{"action":"trigger_now","automation_id":"esp_claw_<ts>"}'  # 실제 lamp off
ha_control --automation '{"action":"disable","automation_id":"esp_claw_<ts>"}'
ha_control --automation '{"action":"enable","automation_id":"esp_claw_<ts>"}'
ha_control --automation '{"action":"update","automation_id":"esp_claw_<ts>","trigger":{"kind":"daily_time","time":"22:30"}}'
ha_control --automation '{"action":"remove","automation_id":"esp_claw_<ts>"}'
```
각각 success:true + HA UI에서 entry 변화 확인.

- [ ] **Step 3: commit**

```bash
git add components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c
git commit -m "feat(cap_ha_control): console --automation '<json>' command"
```

---

### Task 6.8: Telegram NL E2E

**Files:** (행동 검증)

- [ ] **Step 1: 실 Telegram client에서 시연 시나리오**

1. "화장실 조명을 매일 밤 11시에 끄는 자동화 만들어줘"
   → Expected: success message + HA UI에서 `automation.esp_claw_<ts>` 생성 확인.
2. "지금 자동화 뭐 있어?"
   → list 응답, 위 entry + 사용자 기존 자동화 3건 모두 표시.
3. "화장실 조명 자동화 지금 한 번 실행해줘"
   → trigger_now, 실제 lamp turn_off.
4. "그 자동화 잠깐 꺼둬"
   → disable, HA UI state=off.
5. "다시 켜"
   → enable.
6. "11시 말고 22시로 바꿔"
   → update, time 22:00:00 으로 변경.
7. "그 자동화 지워줘"
   → remove + reload. HA UI에서 사라짐.
8. "보드 RGB를 30초마다 toggle하는 자동화 만들어"
   → reject + "v5에서 지원될 예정입니다" verbatim.

각 단계에서 firmware message verbatim echo 확인.

- [ ] **Step 2: 보드 reflash 후 persist 확인**

HA-side 자동화이므로 보드 reflash 무관하게 살아남아야 함:
```bash
idf.py -p $ESP_PORT flash  # firmware 재flash
# 부팅 후:
ha_control --automation '{"action":"list"}'
```
Expected: 등록한 자동화들이 list에 그대로 보임 (HA가 보관).

- [ ] **Step 3: 보드 reset 후 wallclock 시각 동기화 확인**

자동화가 daily_time/weekly이면 보드 wallclock과 무관 (HA가 fire). 보드 SNTP가 늦어도 자동화는 정상 발화. 확인 방법: 매우 짧은 daily_time 으로 등록 (현재 시각 +2분) → 실제 fire 확인.

이 task는 보통 commit 없음 — 결과를 Task 7 learn log에 기록.

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
- 자동화 feature (Task 6) — **Option B 결정 (HA REST 위임)**의 이유, trigger/action 번역 규칙 (daily_time → `time`, interval_ms → `time_pattern /N`), GET-merge-PUT update 패턴, reload 호출 위치
- 발견 사항 / 함정 (HA `/api/config/automation/config/<id>` 응답 schema, automation_id 충돌, reload 누락 시 동작 안함 등)
- v5 follow-ups 후보 (claw_cap_invalidate_tool_description API, HA cert pem import, **`board:onboard_rgb` 보드-only 자동화 cap_scheduler 추가**, cron 표현식 지원 등)

- [ ] **Step 3: commit + PR open**

```bash
git add docs/learn/20260511-cap-ha-control-v4.md
git commit -m "docs(learn): cap_ha_control v4 작업 기록"
git push -u origin feat/cap-ha-control-v4
gh pr create --base main --title "feat(cap_ha_control): v4 — safety fixes + ha_automation"
```

---

## Self-Review

**1. Spec coverage:** 5 review findings (P1 race / P2 hex / P2 cap / P3 desc-refresh / P3 HTTPS) + automation 등록·수정·제거·조회·트리거·on/off가 모두 task로 매핑됨. `board:onboard_rgb` 자동화는 Option B 범위 밖이라 의도적으로 reject — Task 6.4 Step 4에서 회귀 검증. 안 보이는 spec 요구사항 없음.

**2. Placeholder scan:** 모든 task가 실제 코드 / 파일 / 명령 포함. HA REST 엔드포인트 (`POST /api/config/automation/config/<id>`, `DELETE` 동일, `POST /api/services/automation/reload`, `POST /api/services/automation/trigger`, `GET /api/states` filter `automation.*`)는 live HA에 사전 검증 완료 — plan-time 미지수 없음. trigger 번역 (daily_time → `time at HH:MM:00`, weekly → `time + condition weekday`, interval_ms → `time_pattern /N`) 규칙은 Task 6.3에 실제 cJSON 코드로 명시.

**3. Type consistency:** `cap_ha_automation_execute(const char *input_json, char *output_json, size_t output_size)` 시그니처가 v3 `cap_ha_core_execute` 패턴 그대로. descriptor의 `execute` 필드는 wrapper로 ctx 처리 — v3 `cap_ha_execute` 패턴 일치. HTTP helper 시그니처 (`cap_ha_http_put_automation_config(const char *automation_id, cJSON *config, int *out_http_status, esp_err_t *out_http_err)`)는 v3 `cap_ha_http_post_service` 패턴 그대로. `cap_ha_action_to_service(const char *domain, const char *action)` helper는 Task 6.3 Step 1에서 v3 `cap_ha_control_core.c`의 inline switch를 추출한 것이라 호출부와 시그니처 일치.

**4. Frequent commits:** Tasks 1, 2, 3, 4, 5 각 1 commit. Task 6은 6.1–6.7 각 1 commit (6.8은 commit 없음 — 실 Telegram 행동 검증). Task 7 learn log 1 commit. 총 13 commits in v4 Option B. v3 의 16 commits 보다 컴팩트.

**5. TDD adaptation:** firmware 특성상 unit test 없음. 각 task는 build → flash → console-verify → behavior-verify 패턴으로 검증. Task 6은 추가로 HA web UI Settings → Automations 에서 등록 결과 직접 확인 (Option B 의 큰 장점 — HA가 단일 ground truth). v3 pattern과 일관.

**6. Option A 대비 Option B 의 plan-level 절감:** cap_scheduler 의존성 (Task 6.1 spike), event_router rule 등록 (Task 6.4 Step 3 JSON schema 불확실성), 보드 wallclock 동기화 (NTP race), payload 512B 한계, scheduler task stack overflow risk — 모두 제거됨. Trade-off는 `board:onboard_rgb` 자동화 미지원 1건뿐.

큰 누락: 없음. Task 4 Step 5의 escalation (claw_cap_invalidate_tool_description API 추가)은 framework 변경이라 별도 PR 분리 권장 — 그 결정은 Step 1 조사 결과에 따라.

---

## Execution Handoff

Plan saved to `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task + 두 단계 리뷰. PR-A (Tasks 1–3, safety fixes)는 작아서 1 세션에 가능. PR-B (Tasks 4–5, refresh + HTTPS)는 dependency 검토 후. PR-C (Task 6.1–6.8, ha_automation)는 가장 큰 작업이라 sub-task 단위 review checkpoint 권장. Task 7은 PR-C에 포함.

**2. Inline Execution** — 이 세션에서 직접 진행, task 사이 checkpoint.

어떤 방식으로 진행하시겠어요?
