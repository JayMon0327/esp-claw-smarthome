# cap_ha_automation v5 — state-trigger transition fix + condition typed payload

> **컨텍스트:** v4 PR #5/#6 머지 (2026-05-12) 직후 E2E 에서 사용자가 발견한 state-trigger 의 attribute-update 재발화 버그 + 미루어둔 Track C (condition typed payload) 를 v5 로 묶어 ship.

## 무엇을 만들었나

| Task | Commit (head) | 핵심 변경 |
|---|---|---|
| 1 | feat: state-trigger from-pair auto-fill | `opposite_state(domain, to)` helper + state 분기에 from auto-fill. binary_sensor/light/switch/input_boolean on↔off, cover open↔closed, lock locked↔unlocked. 명시 from 은 override. |
| 2 | feat: build_ha_condition_array | 3 kind (time_range / weekday / state) condition builder. 각 kind 별 필드 validation, state 는 entity friendly_name resolve. |
| 3 | feat: condition 통합 (do_create + do_update) | `root.condition` 읽기 + `build_ha_condition_array` + weekly auto-condition 과 AND merge. do_update 는 user condition 명시시 기존 완전 교체. |
| 4 | feat: descriptor schema + description | input_schema_json 에 condition 객체 + trigger.from 설명 갱신. 버퍼 1024→1536 (review NIT 사전 대비). |
| 4 (polish) | docs: stale v4/v6 version refs 제거 | code-quality 리뷰 보강 — target/condition description 에서 LLM 가시 영역의 버전 메타 정리. |

## 무엇을 배웠나

### 1. HA `state` platform 의 attribute-update 재발화는 firmware-side workaround 가 정답

v4 PR #6 ship 후 사용자 실사용 시 `to: "on"` 만으로 등록한 자동화가 의도와 다르게 매번 fire — HA 의 zigbee binary_sensor 가 정기 health-check 으로 state event 를 보내고 platform 이 "to 매칭" 으로 잡음. 사용자 수동 수정 (from: "off" 추가) 으로 transition 강제 → 해결.

**원칙:** HA 같은 외부 시스템은 docs 의 의도 (transition 만 fire) 와 실제 동작 (attribute-update 도 fire) 차이가 잦다. firmware 가 "사용자 의도" (door 열리면 → 자동화) 와 "HA 정확한 호출 방식" (from+to transition) 사이를 메우는 게 typed tool 의 일이다. domain-aware default 가 LLM/사용자의 일을 줄이고 동작 안정성도 보장.

### 2. condition 노출은 trigger 추가의 70% 시나리오 추가 커버

v4 의 state-trigger 단독은 "도어 열리면 조명" 시나리오만 처리. v5 의 condition (특히 time_range) 추가로 "도어 열리면 + 10–18시 사이만 + 조명" 같은 실용 시나리오 등록 가능. weekly trigger 의 auto-emit weekday condition 과 user-provided condition 의 AND merge 로 "주말만 + 시간 윈도우" 도 가능.

**원칙:** schema 확장은 단일 기능 단위가 아니라 시나리오 단위 (trigger + condition + action 묶음) 로 평가하면 demo 직전 surprise 줄어든다. v4 의 trigger-only 결정도 의미는 있었으나 (incremental ship) 실제 demo 에서 condition 부족이 빠르게 드러났다.

### 3. domain-pair opposite 매핑은 도메인별로 명시적으로 둔다

`opposite_state` 함수가 도메인별 분기 (binary_sensor / light / switch / input_boolean / cover / lock) — table-driven 도 가능하지만 v5 수준에선 switch-case 가 명확. 새 도메인 추가시 명시적 변경 강제 (table 의 빈 항목 missed 위험 방지). 매핑 없는 도메인 (media_player, fan 등) 은 omit + description 에 명시.

**원칙:** 외부 시스템의 도메인별 동작 차이는 firmware 의 small switch-case 가 어차피 가독성 좋다. 매핑 누락은 의도된 default 동작 (no auto-fill) 으로 graceful fail.

### 4. 코드 리뷰가 잡아낸 두 가지 builder rigor 흠집 — v6 follow-up

Task 2 / Task 3 code-quality 리뷰가 `build_ha_condition_array` 의 두 빈틈을 지적했다. 이번 ship 에는 미포함 (기존 trigger builder 의 precedent 따름 + 상위 layer 인 descriptor schema 가 `minimum:0, maximum:6` 으로 enum 강제), 다음 사이클에 정리.

1. **OOM 시 silent skip:** Task 3 의 merge loop `cJSON_Duplicate(step, true)` 가 NULL 을 반환하면 그 entry 만 조용히 누락. heap 고갈 시 데이터가 잘림 — 같은 패턴이 trigger builder 에도 있으므로 v6 에서 일관되게 explicit failure 로 전환.
2. **weekday all-invalid → 빈 배열:** `weekdays: [7, 8, 99]` 같이 모두 out-of-range 면 filter 후 빈 배열이 그대로 HA 로 — HA 는 빈 weekday 를 "never" 로 해석. descriptor schema 가 기본 방어 (`minimum:0, maximum:6`) 지만 builder 가 stand-alone 으로도 안전해야 v6 에서 OR/NOT 같은 복합 condition 진입 시 신뢰 가능.

**원칙:** typed tool 의 builder 는 schema 의 도움 없이 단독으로도 일관된 실패/성공 의미를 줘야 한다. graceful skip 은 "silent loss" 와 같다.

## 다음 v6 후보

- `for` 지속시간 condition + trigger ("3분 이상 켜져 있을 때만")
- OR/NOT 복합 condition
- numeric_state trigger + sensor.* 도메인 재도입 (별도 cache 분리로 NVS 폭발 방지)
- template / sun condition
- attribute change trigger (state 외 attribute 변경 감지)
- board:onboard_rgb 자동화 (cap_scheduler subset, Option A path)
- builder rigor: OOM rollback + weekday all-invalid → explicit failure (Task 4 review 의 두 follow-up)

## 참고

- 관련 PR: TBD (이 plan 실행시 생성)
- v4 관련 PRs: #5 (c04c845 정제), #6 (state-trigger) — main 머지 완료
- v5 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md` (PR #7 docs 로 main 에 이미 존재 — 이 branch 에서는 그대로 가져옴)
- E2E 학습: `docs/learn/20260512-cap-ha-automation-prs-review.md` (v4 review 사이클)
