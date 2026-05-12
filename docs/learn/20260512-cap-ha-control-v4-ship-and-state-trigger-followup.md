# cap_ha_control v4 ship + state-trigger follow-up — 학습 정리 (2026-05-12)

> **컨텍스트:** PR #3 (cap_ha_control v4) 빌드/PR 오픈 후 실 보드 + 실 HA 통합 검증 사이클 + 사용자 Telegram 시연에서 발견된 typed-tool scope cut 한계. v3 ship 사이클 학습이 그대로 재현된 두 번째 사례.

## 무엇을 배웠나

### 1. v3 학습 재확인 — plan-time curl 검증은 happy-path POST 만 본다

v3 ship 학습 (`docs/learn/20260511-cap-ha-control-v3-ship-report.md`) 에서 "plan-review 2 라운드 + spec/code review 통과 후에도 on-board 검증에서 critical bug 4개" 라고 적었다. v4 에서도 plan-time 에 HA endpoint 응답 schema 를 curl 로 직접 확인했음에도 **두 개의 demo-blocker** 가 처음 실 보드 + 실 HA 통합 시점에 드러났다:

- **Bug 1 (plural schema):** HA 2024.x+ 가 GET 응답을 `triggers`/`actions` (plural) 로 정규화. POST 는 둘 다 받지만 GET 는 plural 만 반환. update merge 가 singular delete (no-op) + add → body 에 양쪽 키 모두 존재 → HA 400 `"Cannot specify both 'trigger' and 'triggers'"`.
- **Bug 2 (entity_id 슬러그):** HA 가 runtime entity_id 를 `alias` 슬러그에서 계산 (`alias="v4-update-debug"` → `automation.v4_update_debug`). 우리 firmware 는 `entity_id = "automation." + config_id` 가정. service 호출이 잘못된 entity_id 에도 HA 가 silent-200 반환 → 디버깅을 더 어렵게 만듦.

둘 다 fix `c04c845`. 첫째는 config 에 추가하는 키를 plural 로 통일, 둘째는 `resolve_entity_id_by_config_id` 헬퍼 추가 (GET /api/states + attributes.id 매칭).

**원칙 (재강조):** API 검증은 happy-path POST 한 번이 아니라 **GET → mutate → PUT 사이클** + 실제 effect (lamp 발화 여부) 까지 chain end-to-end. HA service 호출은 invalid entity_id 에도 200 반환하니 응답 코드만으로는 동작 보장 안 됨.

### 2. typed-tool scope cut 은 항상 사용자 시연에서 드러난다

v4 의 ha_automation typed-tool input schema 가 `trigger.kind` 를 **enum {daily_time, weekly, interval}** 로만 제약했다. 의도된 scope cut — typed-tool 의 LLM 합성 검증 가능성을 높이려고 trigger 종류를 시간 기반 3개로 한정.

그러나 ship 직후 사용자 시연 (Telegram 2026-05-12 09:34) 첫 자연어 질문이:
> "현관문 도어센서가 열리면, 화장실 조명이 켜지는 자동화 만들어줘"

state 기반 trigger. typed-tool layer 에서 schema enum 밖이라 LLM 이 등록 시도조차 못 함. ESP-Claw 는 YAML 예제 + UI 안내로 우회. 사용자가 정당한 질문을 함:
> "HA API로 자동화 등록 구현한 거 맞지? 혹시 MCP로 구현했어?"

**답:** HA REST API 직접 호출 (`cap_ha_control_http.c` 의 `POST /api/config/automation/config/<id>`). MCP 아님. 구현 mechanism 은 state-trigger 도 가능. **막힌 건 typed-tool schema 의 enum 제약.**

**원칙:** typed-tool 의 schema 는 LLM 합성 검증 가능성과 사용자 시나리오 커버리지의 **trade-off** 다. plan-time 에 사용자 자연어 시나리오를 5~10 개 미리 sample 해서, schema 가 그것들을 모두 표현할 수 있는지 확인해야 한다. v4 plan 에서는 시간 기반 시연만 가정했고 (`매일 밤 11시 끄기`), 센서 기반 시나리오를 안 봤다. 그래서 ship 직후 첫 사용자 시연에서 한계가 드러남.

### 3. demo-blocker fix 가 architecture 비용을 만들 수 있다 — 다음 PR 정제 후보

Bug 2 fix (`resolve_entity_id_by_config_id`) 는 동작은 되지만 **매 service 호출마다 `/api/states` 50KB GET** 을 한다. trigger/enable/disable 셋 모두. 정상 동작이지만 RT 가 느리고 (~100ms 추가) 네트워크 비용이 큼.

다음 PR 의 정제 후보:
- 등록 시점에 NVS 에 `(config_id → entity_id)` 매핑 영속 저장.
- service 호출 시 NVS lookup 우선, miss 시에만 fallback to `/api/states`.
- do_remove 시 매핑 entry 도 같이 삭제.

**원칙:** demo-blocker 는 빠르게 해결하되, fix 의 architecture 비용을 follow-up 로그에 명시. 작업 종료가 "ship됐다" 가 아니라 "ship됐고, X 후속작업이 남았다" 로 닫혀야 다음 작업자가 픽업 가능.

### 4. HA service의 silent-no-op 정책은 firmware-side에서 명시적 실패로 바꿔야 한다

`POST /api/services/automation/trigger {entity_id: "automation.does-not-exist"}` → HA 200 응답. 자동화는 발화 안 됨. 사용자 / LLM 에게는 success 가 신호된다. 사실상 false-positive.

c04c845 의 resolver-then-fallback 패턴은 이걸 일부 완화했지만, resolver miss 시 fallback to `automation.<config_id>` 호출하는 경로는 여전히 silent-200 risk. 다음 PR 에서 resolver miss 시 service 호출 자체를 skip + 명확한 실패 메시지 반환하는 게 옳다.

**원칙:** 외부 시스템의 forgiving 응답 (HA의 silent-200, 또는 다른 API의 무시 정책) 을 신뢰하지 말고, firmware 가 발화 가능성 검증 후에만 호출. 발화 불가능하면 명시적 fail.

## 다음에 비슷한 작업 할 때

- **plan-time API 검증은 POST + GET-mutate-PUT 사이클까지 미리 돌릴 것.** Happy-path POST 만 보면 정규화 차이를 놓친다.
- **typed-tool schema 의 enum 은 plan-time 에 사용자 자연어 시나리오 5~10 개 sample 로 커버리지 확인.** ship 직후 첫 시나리오에서 한계 드러나는 게 가장 비싼 cost.
- **외부 service 의 silent-success 응답을 신뢰하지 말 것.** firmware-side resolve 검증 후 호출, 아니면 명시적 fail.
- **demo-blocker fix 의 architecture 비용은 항상 follow-up 로그에 기록.** "동작은 되지만 매번 50KB GET" 같은 비용을 묻으면 다음 작업자가 다시 발견해야 함.

## 참고

- v4 implementation learn: `docs/learn/20260511-cap-ha-control-v4.md`
- v4 완료 보고서 (이 작업): `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`
- v3 ship 사이클 학습: `docs/learn/20260511-cap-ha-control-v3-ship-report.md`
- PR #3 (코드): https://github.com/JayMon0327/esp-claw-smarthome/pull/3
- PR #4 (이 문서 포함): 이 PR
