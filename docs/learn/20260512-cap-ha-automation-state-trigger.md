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
