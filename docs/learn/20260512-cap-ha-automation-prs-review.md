# cap_ha_automation v4 follow-up PRs — pre-merge /review 사이클 학습

> **컨텍스트:** PR #5 (c04c845 정제) + PR #6 (state-trigger) 생성 직후 pre-merge /review 진행. SHOULD-FIX 2건 surfaced + 즉시 적용, P3 1건은 사용자 결정으로 defer.

## 무엇을 잡아냈나

| Finding | PR | 분류 | 처리 |
|---|---|---|---|
| `do_create` 의 redundant `eid_cache_put` (NVS 마모) | #5 | SHOULD-FIX P3 | commit `7dda175` 즉시 적용 |
| state trigger `to_j` 빈 문자열 허용 (`cJSON_IsString` 가 `""` accept) | #6 | SHOULD-FIX P3 | commit `870fd0d` 즉시 적용 (entity_j 도 동일 강화) |
| `ha_control` "Active devices" 목록에 sensor 노출 | #6 | SHOULD-FIX P3 | v5 defer (LLM graceful fail 동작 확인 후 결정) |
| `cJSON_DeleteItemFromObjectCaseSensitive(obj, first->string)` 가 곧 해제될 메모리 pointer | #5 | NIT | 동작 안전, 가독성만 trade-off, defer |
| `eid_cache_invalidate` 가 키 부재시에도 blob rewrite | #5 | NIT | 호출 빈도 낮아 영향 미미, defer |
| description buffer 1024 가 tight | #6 | NIT | 다음 schema 확장 시 같이 bump |

## 무엇을 배웠나

### 1. Plan-time "이건 알지만 그냥 둠" 주석은 /review 대상

PR #5 의 `do_create` 의 명시적 `eid_cache_put` 은 plan-time 에 "redundant 지만 fast-path 일관성 위해 유지" 주석으로 의도적 유지 결정이었음. 그런데 /review 가 "fast-path 적중은 do_create 직후엔 발생 안 함 (방금 자기가 caching 함)" 으로 정당화 논리의 결함을 짚어냄. **Plan-time 에 "redundant but defensive" 라고 자기 변호한 코드는 /review 에서 다시 도마 위에 오를 가능성이 높다** — 코드 리뷰는 의도가 아니라 결과로 판단한다.

**원칙:** Plan 단계에서 "필요는 없지만 안전망" 으로 들어간 코드는 `// REVIEWER:` 같은 표식으로 명시적으로 reviewer 주의 환기. 또는 그냥 빼라.

### 2. `cJSON_IsString` 은 빈 문자열도 accept — type 검증과 값 검증 분리 필요

PR #6 의 state-trigger 가 `cJSON_IsString(to_j)` 만 검증 → `to_j` 가 `""` 일 때 통과. 같은 함수의 `from_j` 는 `&& from_j->valuestring[0]` 으로 빈 문자열 거르고 있어 **같은 함수 내 비대칭** 이었다. /review 가 패턴 비대칭으로 catch.

**원칙:** cJSON validation 은 두 단계 (type + 값) 가 흔히 묶음. `cJSON_IsString(x) && x->valuestring[0]` 패턴을 ha_automation 전체에서 통일 권장. macro 화 가능 (`#define CJSON_IS_NONEMPTY_STR(x) (cJSON_IsString(x) && (x)->valuestring[0])`) 하지만 v4 에선 inline 통일로 충분.

### 3. trigger 만 추가하고 condition 빼면 demo 시나리오의 70% 만 커버

PR #6 의 state-trigger 추가 후 사용자가 "10–18시 사이 + 도어센서 ON" 같은 시나리오를 typed payload 로 등록 가능한지 질문. **현재 ha_automation 은 trigger 와 weekly 의 weekday condition 만 지원, 일반 condition 객체는 노출 X**. HA 자동화의 실세계 시나리오 대부분은 trigger + condition 조합 (시간 윈도우, 다른 entity state 게이트 등) 이라 typed payload 의 표현력 한계가 빠르게 드러남.

**원칙:** HA 같은 외부 시스템 통합 시 schema 노출 범위 결정은 단일 기능 단위 (trigger 만) 가 아니라 시나리오 단위 (trigger + condition + action 묶음) 로 평가해야 demo 직전 surprise 가 적다. v4 는 demo 우선으로 condition 을 v5 defer 결정 — HA UI 후편집으로 우회 가능.

### 4. 워크트리 fork + 병렬 subagent + /review 한 사이클로 묶이는 흐름

`v4-c04c845` + `v4-state-trigger` 두 worktree fork → 두 subagent 병렬 → /review 가 두 PR 동시 검토 → 두 워크트리 동시 fix-up commit. 워크트리 단위 isolation 이 review 결과 적용을 conflict 없이 병렬화. 한 base (`feat/cap-ha-control-v4`) 에서 2-track stacking 패턴이 cap_ha_control 같은 mid-size 작업에 잘 맞음 — 더 큰 작업은 stacking 깊이가 늘어 review 부담이 커질 수 있음.

**원칙:** 비겹침 함수 단위 분리가 가능한 follow-up 묶음은 worktree fork + 병렬 PR 이 single-branch 직렬보다 효율적. base 가 살아있는 (미머지) 통합 branch 면 stacking 으로 진행, base 가 main 이면 main 으로 직접 PR.

## 다음에 v5 에서

- `condition` typed payload 노출 (time_range / weekday / state) — Track C 후보, ~60–90 LoC
- `ha_control` 의 "Active devices" 와 ha_automation 의 "Referenceable entities" 를 분리 list — sensor 가 ha_control 노이즈가 되는 문제
- `CJSON_IS_NONEMPTY_STR` 같은 validation macro — 전체 ha_automation 에서 패턴 통일

## 참고

- 관련 PRs: [#5](https://github.com/JayMon0327/esp-claw-smarthome/pull/5) (c04c845 정제), [#6](https://github.com/JayMon0327/esp-claw-smarthome/pull/6) (state-trigger)
- Fix-up commits: `7dda175` (PR #5), `870fd0d` (PR #6)
- 관련 plans: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-c04c845-followups.md`, `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-state-trigger.md`
