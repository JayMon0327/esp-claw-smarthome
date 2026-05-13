# ESP-Claw 스마트홈 v5 완료 보고서 (2026-05-13)

> **컨텍스트:** PR #5 (v4 c04c845 follow-ups) / PR #6 (state-trigger) 머지 직후 시작한 v5 후속 작업. v4 state-trigger 의 실사용 한계 두 가지 (HA attribute-update 재발화 + condition typed payload 부재) 를 해결. **PR push 후 사용자 텔레그램 실 사용에서 잡힌 LLM 어휘 mismatch hotfix 까지 같은 PR 에 묶어 ship**. 모든 작업은 subagent-driven (fresh implementer/spec/code-quality per task) + 보드 + 실 HA 라이브 검증으로 마무리.

- **이전 단계**: [v4 완료보고 (2026-05-12)](./esp-claw-smarthome-completion-2026-05-12.md)
- **다음 단계**: 사용자 결정 — v6 후보 (OR/NOT 복합 condition, `for` 지속시간, on-device board:* automation, builder rigor 정리) 중 우선순위 선택

---

## 0. 요약 (TL;DR)

v4 ship 후 실제 데모 환경에서 두 가지 문제가 발견됐고, v5 사이클에서 둘 다 해결한 뒤 production-grade로 main 안착.

1. **v4 state-trigger 의 `to:"on"` 단독 등록이 HA 의 attribute-update 마다 재발화** (zigbee binary_sensor 의 정기 health check 가 fire 시킴). firmware-side 에서 **domain-pair opposite `from` 자동 채움** 으로 해결 — LLM/사용자 둘 다 추가 작업 없이 transition 의미 강제.

2. **demo 시나리오 ("도어 열림 + 10–18시 사이 + 조명 ON") 가 trigger 만으로는 표현 불가능**. typed payload 에 **`condition` 객체 (time_range / weekday / state)** 신설 + `do_create` 가 weekly trigger 의 auto-emit weekday condition 과 AND-merge.

추가로 **PR push 직후 사용자 텔레그램 라이브 발견**: LLM 이 "도어 열림" 자연어를 `to:"open"` 으로 매핑 (UI 의 "Open/Closed" 라벨 영향) — `binary_sensor` 의 실제 state 값은 `on`/`off` 라 자동화는 등록되지만 영원히 fire 안 함. **도메인-aware state value 정규화** (`open`→`on`, `closed`→`off`) 핫픽스로 같은 PR 에 묶어 처리.

**핵심 결과**:
- ✅ 8 commit / +428/-12 LOC / 2 code files + 2 learn log → PR #8 main 머지 (`8deedb6`)
- ✅ CLI-only 라이브 검증 5종 통과 (HA REST schema 호환 + 펌웨어 path direct + console end-to-end)
- ✅ 핫픽스 후 LLM-shape 잘못된 payload 도 firmware backstop 으로 정상 자동화 생성
- ✅ 메인 branch + 로컬 worktree + remote branch 클린업까지 사이클 완전 종료

---

## 1. PR 현황

- **PR #8 (코드):** `feat(cap_ha_automation): v5 — state-trigger from auto-fill + condition typed payload` — https://github.com/JayMon0327/esp-claw-smarthome/pull/8
  - Base: `origin/main` (`5470d73`, PR #7 머지 직후)
  - Head: `feat/cap-ha-control-v5` (8 commits → 머지 후 자동 삭제)
  - Merge commit: `8deedb6` (2026-05-12 06:26 UTC)
  - 빌드 green 매 commit, on-board E2E 검증 완료, 핫픽스 포함

- **PR (이 문서):** v5 완료 보고서 + 후속 항목 정리

---

## 2. 구현 완료 항목

### 2.1 v5 핵심 5 Task (subagent-driven)

각 task 마다 implementer → spec compliance reviewer → code-quality reviewer 3단 검증 + 최종 branch-wide adversarial 리뷰.

| Commit | Task | 핵심 변경 |
|---|---|---|
| `8758980` | **Task 1**: state-trigger `from` 자동 채움 | `opposite_state(domain, to)` 헬퍼 + state 분기에서 caller 가 `from` 안 보내면 domain-pair opposite (binary_sensor/light/switch/input_boolean: on↔off, cover: open↔closed, lock: locked↔unlocked) 으로 채움. 명시 from 은 verbatim override. ESP_LOGI 로 추적 로그. |
| `eba9164` | **Task 2**: `build_ha_condition_array` 빌더 신설 | 3 kind 지원 — `time_range` (after/before "HH:MM" 한쪽 또는 양쪽 + HH:MM:00 으로 정규화), `weekday` (weekdays[] 0=Sun..6=Sat → HA-native "mon"..."sun"), `state` (entity friendly_name 또는 entity_id resolve + state value). 모든 error path 가 step+arr 둘 다 `cJSON_Delete`. v6 deferred 항목 (for / OR/NOT / template / numeric_state) 함수 헤더 주석에 명시. |
| `a0f745f` | **Task 3**: do_create + do_update 에 condition 통합 | `root.condition` 객체 발견시 `build_ha_condition_array` 호출. **do_create**: 결과를 weekly trigger 의 auto-emit weekday condition 과 AND-merge (배열 concat via `cJSON_Duplicate(true)`). **do_update**: user condition 명시시 기존 conditions 완전 교체 (의도 우선). 에러 path 의 메모리 cleanup 4개 객체 (action_arr/trigger_arr/condition_arr/cfg) 모두 처리. |
| `9edf02e` | **Task 4**: descriptor schema + 버퍼 bump | `input_schema_json` 에 신규 `condition` 객체 (kind enum + after/before/weekdays/entity/state). `trigger.from` description 에 auto-fill 동작 명시. compose_description 에 "Example: door sensor opens between 10:00–18:00 → light on" 추가. **버퍼 `s_ha_automation_description[1024]` → `[1536]`** (v4 review NIT 의 tight size 가 condition 추가로 임박 — `-Werror=format-truncation` 사전 회피). |
| `5affb4f` | **Task 4 정리** (code-quality 리뷰 피드백) | descriptor 의 `condition` description 에 "v6 will add OR/NOT" 라는 forward-ref 제거 → "multi-condition is not yet supported" 로 변경. `target` description 의 "not supported in v4" → "not supported here". LLM-가시 스키마 문자열에 내부 버전 메타데이터 누출 차단. |
| `f6b3fcd` | **Task 5**: learn 로그 | `docs/learn/20260513-cap-ha-automation-v5.md` 작성. 4가지 학습 (HA platform 의 attribute-update 재발화는 firmware workaround / condition 노출이 시나리오 커버 70% 추가 / domain-pair 매핑은 switch-case / 코드 리뷰가 잡은 builder rigor 흠집 = v6 follow-up). |

### 2.2 최종 branch-wide 리뷰 후속 정리

| Commit | 내용 |
|---|---|
| `ac004dd` | `cap_ha_automation.c` 의 3개 user-visible 에러 메시지에서 stale "보드 자체 자동화는 v5에서 지원될 예정 / v4에서는…" 제거. Task 4 polish 가 descriptor strings 처리한 것의 짝. main 머지 시점에 모든 LLM/user-facing 텍스트가 일관. |

### 2.3 ✨ Post-push hotfix (사용자 라이브 리포트 → 같은 PR 에 추가)

| Commit | 발견 + 핵심 변경 |
|---|---|
| `fa42827` | **fix(cap_ha_automation): binary_sensor state value 정규화 (open↔closed → on↔off)** — 사용자가 PR push 직후 텔레그램으로 "현관문 도어 센서가 열렸을 때 화장실 조명 켜주는 자동화 등록" 발화. LLM 이 "열림" 을 자연스럽게 `to:"open"` 으로 매핑 (UI label 영향). HA 가 등록은 받지만 (`result:"ok"`) `binary_sensor` 의 실제 state value 는 literal `on`/`off` 라 trigger 영원히 매칭 안 됨. `opposite_state(binary_sensor, "open")` 도 NULL → v5 의 from auto-fill 도 무력화. **`normalize_state_value(domain, value)` 신설**: binary_sensor/light/switch/input_boolean 에서 `"open"`→`"on"`, `"opened"`→`"on"`, `"closed"`→`"off"`. cover/lock 은 native 어휘 유지. 적용 3 위치 (trigger.to, trigger.from, condition.state) + `opposite_state` 호출도 normalized to 받음. descriptor schema/compose_description 에 도메인별 어휘 명시 강화 — LLM 가 직접 옳은 값 쓰도록 prompting + firmware backstop 2중. 정규화 발생시 ESP_LOGI 추적. |

---

## 3. 검증 결과 (실 보드 + 실 HA, 2026-05-12)

### 3.1 v5 핵심 검증 (PR push 전)

| Test | Result | Evidence |
|---|---|---|
| 빌드 클린 (8 commit 각각) | ✅ | `idf.py build` 매 commit `Project build complete`, no `-Werror=format-truncation` |
| 부팅 + WiFi STA + HA REST + entity 캐시 | ✅ | `cap_ha_resolve: boot-fetch: description refreshed with 4+12 entities` |
| Capability 등록 (HA control: groups=14, caps=60) | ✅ | `Register HA control cap ok` |
| **HA REST schema 호환** (5 케이스) | ✅ | firmware-shape JSON 을 직접 POST → HA `result:ok` → GET back 동일 모양 |
| Test 1: state to-only + time_range condition | ✅ | input `to:"on"`, condition `{kind:"time_range",after:"10:00",before:"18:00"}` → 저장 `{from:"off",to:"on"}` + `{condition:"time",after:"10:00:00",before:"18:00:00"}` |
| Test 2: weekly + user time_range AND-merge | ✅ | 저장된 conditions[] = `[weekday=mon..fri, time after/before]` 2개 |
| Test 3: state condition (entity gate) | ✅ | `condition.state` + entity_id + state 그대로 보존 |
| Test 4 (negative): state trigger missing `to` | ⚠ | HA 가 `result:ok` 반환 — schema validation 느슨, **firmware-side 검증이 valuable** 확인 |
| **펌웨어 path direct (console `ha_control --automation=...`)** | ✅ | LLM 없이 firmware 코드 path 그대로 호출 가능 — 가장 결정적 검증 경로 |
| Test 5: 펌웨어 from auto-fill 실제 발화 (input `to:"on"`) | ✅ | 시리얼 로그 `cap_ha_auto: state trigger from auto-fill: off -> on (domain=binary_sensor)` |
| Test 6: 펌웨어 do_create AND-merge (weekly + user time_range) | ✅ | 시리얼 + HA GET 모두 conditions 2개 정확히 들어감 |
| Test 7: 펌웨어 do_remove cleanup path | ✅ | `cap_ha_http: DELETE ... status=200` + reload 200 + HA list 변동 |

### 3.2 핫픽스 검증 (PR push 후)

사용자 라이브 리포트 재현:

```
ha_control --automation={"action":"create",...,
  "trigger":{"kind":"state","entity":"binary_sensor.smart_door_sensor_mun","to":"open"},
  "alias":"v5_hotfix_normalize_test"}
```

| Test | Result | Evidence |
|---|---|---|
| `to:"open"` 입력 → 정규화 로그 | ✅ | `cap_ha_auto: state trigger to normalized: open -> on (domain=binary_sensor)` |
| 정규화된 to 로 from auto-fill | ✅ | `cap_ha_auto: state trigger from auto-fill: off -> on (domain=binary_sensor)` |
| HA POST 결과 | ✅ | `automation PUT result err=ESP_OK status=200` |
| HA-side 저장 모양 | ✅ | `triggers: [{platform: state, entity_id: ..., to: on, from: off}]` — **사용자가 수동으로 fix 했던 모양과 byte-for-byte 일치** |
| 펌웨어 cleanup path | ✅ | `cap_ha_http: DELETE ... status=200`, HA list 4개 변동 없음 |

---

## 4. 핵심 학습

### 4.1 typed tool 의 schema description 은 LLM 의 자연어 어휘와 충돌 가능 (Most important)

UI 의 device_class 라벨 ("Open"/"Closed") 과 entity 의 실제 state value ("on"/"off") 가 다른 도메인에서, LLM 은 사용자의 자연어 입력 ("열림") 을 UI 라벨로 매핑한다 — 사람-친화적이지만 HA API 가 받는 값이 아님. **schema description 만으로는 막을 수 없고, firmware-side normalize backstop 이 필수**.

**원칙:** typed tool 의 검증은 "HA 가 schema 받았다" 가 아니라 "의도한 이벤트에서 fire 한다" 까지가 최소 단위. PR review 사이클 (이번엔 implementer → spec → code-quality → adversarial 까지 돌았는데도) 은 *plan + 코드 자기 정합성* 만 검증할 뿐 *LLM 가 실제로 어떤 argument 모양으로 호출하는가* 는 잡지 못한다. 가능한 한 prompt-shape 시나리오 한두 개를 review 의 입력으로 같이 줘야 함.

### 4.2 HA `state` platform 의 attribute-update 재발화는 firmware-side workaround 가 정답

v4 PR #6 ship 직후 실사용에서 `to:"on"` 만 등록된 자동화가 의도와 다르게 매번 fire — HA 의 zigbee binary_sensor 가 정기 health-check 으로 state event 를 보내고 platform 이 "to 매칭" 으로 잡음. 사용자 수동 fix (from:"off" 추가) 로 해결 → v5 가 자동화. **HA 같은 외부 시스템은 docs 의 의도 (transition 만 fire) 와 실제 동작 (attribute-update 도 fire) 차이가 흔하다.** firmware 가 사용자 의도 ("door 열리면 → 자동화") 와 HA 정확한 호출 방식 (from+to transition) 사이를 메우는 게 typed tool 의 일.

### 4.3 condition 노출은 trigger 추가의 ~70% 시나리오 추가 커버

v4 state-trigger 단독은 "도어 열리면 조명" 만. v5 의 condition (특히 time_range) 추가로 "도어 열리면 + 10–18시 사이만 + 조명" 같은 실용 시나리오 커버. weekly trigger 의 auto-emit weekday condition 과 user-provided condition 의 AND-merge 로 "주말만 + 시간 윈도우" 까지 가능. **schema 확장은 단일 기능 단위가 아니라 시나리오 단위로 평가하면 demo 직전 surprise 가 줄어든다**.

### 4.4 code-quality 리뷰가 잡은 builder rigor 흠집 — v6 follow-up

v5 의 `build_ha_condition_array` + Task 3 의 merge 로직에 두 가지 빈틈이 리뷰에서 지적됐다. 이번 ship 에는 미포함 (기존 trigger builder 의 precedent 따름 + 상위 descriptor schema 의 minimum/maximum 가드 가 1차 방어):

1. **OOM 시 silent skip:** Task 3 의 merge loop `cJSON_Duplicate(step, true)` 가 NULL 반환하면 그 entry 만 조용히 누락. heap 고갈 시 데이터 손실 — 같은 패턴이 trigger builder 에도 있으므로 v6 에서 일관되게 explicit failure 로 전환.
2. **weekday all-invalid → 빈 배열:** `weekdays: [7, 8, 99]` 같이 모두 out-of-range 이면 filter 후 빈 배열이 그대로 HA 로 전달 — HA 가 빈 weekday 를 "never" 로 해석. descriptor schema 의 `minimum:0/maximum:6` 가 기본 방어이지만, builder 가 stand-alone 으로도 안전해야 v6 에서 OR/NOT 복합 condition 진입 시 신뢰 가능.

**원칙:** typed tool 의 builder 는 schema 의 도움 없이 단독으로도 일관된 실패/성공 의미를 줘야 한다. graceful skip 은 "silent data loss" 와 같다.

---

## 5. 알려진 한계 (v6 후보)

- **on-device automation (`board:*`)** — 현재 ha_automation 은 HA-side 만. board:onboard_rgb 같은 보드 직접 자동화는 별도 cap_scheduler subset 또는 새 capability 가 필요. v4/v5 모두 deferred.
- **`for` 지속시간 condition + trigger** — "3분 이상 켜져 있을 때만" 같은 시나리오.
- **OR/NOT 복합 condition** — 현재 단일 condition 객체만 (firmware-internal AND-merge 만 weekly 의 auto-emit weekday 와).
- **numeric_state trigger + sensor.* 도메인 재도입** — v3 에서 NVS 폭발 우려로 boot-fetch 에서 빠진 sensor 도메인 다시. 별도 cache 분리로 안전성 확보.
- **template / sun condition** — solar-time / sunrise / sunset 기반 자동화.
- **attribute change trigger** — state 외 attribute 변경 감지.
- **builder rigor 정리** (4.4 참조) — OOM rollback + weekday all-invalid 명시 실패.

---

## 6. 사이클 종료 상태

- ✅ PR #8 main 머지 (merge commit `8deedb6`)
- ✅ Remote branch `feat/cap-ha-control-v5` 자동 삭제 (`--delete-branch`)
- ✅ Local worktree `.claude/worktrees/v5` + local branch `feat/cap-ha-control-v5` 정리
- ✅ ESP32-S3 보드에 핫픽스 포함 펌웨어 (`fa42827`) 플래시 + 부팅 검증
- ✅ Learn 로그 2건 (v5 메인 + 핫픽스) main 에 안착
- ⏳ HA 측 잔존 자동화 (`to:"open"` 모양으로 핫픽스 이전 등록된 것들) — 사용자가 텔레그램 재등록 또는 HA UI 수정으로 정리 예정

---

## 7. 참고

- v5 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md`
- v5 메인 learn 로그: `docs/learn/20260513-cap-ha-automation-v5.md`
- v5 핫픽스 learn 로그: `docs/learn/20260512-cap-ha-state-value-normalize-hotfix.md`
- v4 완료 보고: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`
- 머지된 8 commit 시리즈:

```
fa42827 fix(cap_ha_automation): normalize binary_sensor state values (open↔closed → on↔off)  ← hotfix
ac004dd docs(cap_ha_automation): drop stale v4/v5 version refs in board error messages
f6b3fcd docs(learn): cap_ha_automation v5 — state-trigger fix + condition payload
5affb4f docs(cap_ha_control): remove stale v4/v6 version refs in descriptor
9edf02e feat(cap_ha_control): condition schema + description buffer bump
a0f745f feat(cap_ha_automation): wire condition payload into do_create + do_update
eba9164 feat(cap_ha_automation): build_ha_condition_array (time_range/weekday/state)
8758980 feat(cap_ha_automation): state-trigger from-pair auto-fill
```
