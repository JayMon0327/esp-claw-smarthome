# cap_ha_automation — binary_sensor state value normalize (post-v5 hotfix)

> **컨텍스트:** v5 (PR #8) push 직후 사용자 텔레그램 실사용에서 발견. v5 의 `state trigger from auto-fill` 이 의도대로 동작하지 않는 케이스가 production 에서 잡힘.

## 무엇이 잘못 동작했나

LLM 이 자연어 "도어 센서 열림" 을 typed payload `{trigger:{kind:"state", entity:"...door_sensor...", to:"open"}}` 로 매핑. firmware 는 그 `to:"open"` 을 그대로 HA 에 보냄. 결과:

```yaml
triggers:
  - platform: state
    entity_id: binary_sensor.smart_door_sensor_mun
    to: open       # ← HA 는 이 자동화를 등록은 하지만 영원히 fire 안 함
```

이유: `binary_sensor` entity 의 실제 state 값은 `on` / `off`. UI 가 device_class 별로 "Open" / "Closed" 라벨로 *보여줄 뿐*. trigger 의 `to` 비교는 state 의 실제 값과 literal string match — `binary_sensor` 의 state 가 literal `"open"` 인 적은 없으므로 trigger 가 절대 fire 하지 않음.

v5 Task 1 의 `opposite_state(binary_sensor, "open")` 는 매핑 없어서 NULL 반환 → `from` 도 안 채워짐. 즉 v5 의 두 가지 보호 (from auto-fill + transition 강제) 가 모두 무력화.

## 핫픽스 — 도메인-aware state value normalize

`opposite_state` 위에 신규 helper `normalize_state_value(domain, value)`:

- `binary_sensor` / `light` / `switch` / `input_boolean` 에서 `"open"`→`"on"`, `"opened"`→`"on"`, `"closed"`→`"off"` 정규화.
- `cover` / `lock` 은 native 어휘 유지 (각각 `open/closed` / `locked/unlocked` 이 정상).

적용 위치 3곳:
1. `build_ha_trigger_array` state 분기의 `to` 값.
2. 같은 분기의 `from` 값 (caller 명시한 경우).
3. `build_ha_condition_array` 의 state condition `state` 값.

`opposite_state` 호출은 normalize 된 to 로 — `to:"open"` 이 들어와도 normalize 후 `"on"` → opposite `"off"` → from auto-fill 작동.

normalize 가 실제 일어나면 `ESP_LOGI(TAG, "state trigger to normalized: open -> on (domain=binary_sensor)")` 로 시리얼에 기록 — 운영 시 진단 용이.

descriptor schema 도 강화: trigger.to / from + condition.state 의 description 에 도메인별 어휘 + 자동 정규화 동작 명시. compose_description 에 `"binary_sensor (door/window/motion) uses 'on'/'off' NOT 'open'/'closed'"` 한 줄 추가 — LLM 가 직접 올바른 값 쓰도록 prompting + firmware backstop 2중.

## 검증 (CLI-only, 보드 + HA 실시간)

LLM 이 보낸 모양 그대로 firmware path 직접 호출:

```
ha_control --automation={"action":"create","target":"light.smart_bulb",...,
  "trigger":{"kind":"state","entity":"binary_sensor.smart_door_sensor_mun","to":"open"},
  "alias":"v5_hotfix_normalize_test"}
```

펌웨어 로그:
```
cap_ha_auto: state trigger to normalized: open -> on (domain=binary_sensor)
cap_ha_auto: state trigger from auto-fill: off -> on (domain=binary_sensor)
cap_ha_http: automation PUT result err=ESP_OK status=200
```

HA 측 저장 결과:
```yaml
triggers:
  - platform: state
    entity_id: binary_sensor.smart_door_sensor_mun
    to: on        # ← 정규화됨
    from: off     # ← v5 auto-fill 이 정규화된 to 로 정확히 동작
```

사용자가 수동으로 fix 했던 자동화 모양과 byte-for-byte 일치.

## 무엇을 배웠나

### 1. typed tool 의 schema description 은 LLM-natural language 와 충돌 가능

UI 의 라벨 ("Open" / "Closed") 와 internal state 값 ("on" / "off") 이 다른 도메인에서, LLM 은 사용자가 한 자연어 입력 ("열림") 을 UI 라벨로 매핑하기 쉬움 — 그게 사람에게 더 natural. 그러나 HA API 는 internal state 값을 요구.

**원칙:** schema description 이 LLM 의 자연어→tool argument 변환에서 "옳은 어휘" 를 *명시* 해야 한다. 그것조차 LLM 이 어길 수 있으므로 firmware backstop normalize 도 필수. 두 layer 가 합쳐져야 user-facing 으로 일관 동작.

### 2. "자동화 등록 success" 와 "자동화 작동" 은 같지 않다

HA REST 는 schema 가 valid 한 한 자동화 등록을 무조건 받아들임 (`to:"open"` 도 `result:"ok"`). 그러나 entity state 가 그 값이 될 수 없으면 trigger 는 영원히 매칭 안 함. v5 검증 사이클 (PR #8 의 7 commit 리뷰) 에서도 schema 호환성은 검증됐지만 *런타임 fire 검증* 은 사용자 영역으로 미뤘던 결과 — 이번 hotfix 가 그 gap 채움.

**원칙:** typed tool 의 검증은 "HA 가 받았다" 가 아니라 "의도한 이벤트에서 fire 한다" 까지가 단위. CLI 만으로는 시뮬레이션 어려운 부분 (실제 센서 transition) 이 있으므로, schema 시점의 명시적 정규화로 가능한 한 많은 케이스를 firmware 가 책임진다.

### 3. v5 review 사이클의 blind spot — code-quality 리뷰가 못 잡은 이유

v5 의 5-task 리뷰는 implementer → spec → code-quality → branch-wide adversarial 까지 돌았지만 이 버그는 못 잡았다. 이유는 모든 리뷰어가 plan + 코드 컨텍스트 안에서 *코드의 자기 정합성* 만 검증 — `opposite_state(binary_sensor, "open")` 가 NULL 반환하는 게 plan 의 의도와 일치 (cover 도메인이 "open" 쓴다고 가정). 실제 LLM 어휘 흐름은 plan 자체에 캡쳐 안 됨.

**원칙:** typed tool 변경의 리뷰는 "plan 명세 일치" 외에 "LLM 이 실제로 어떤 argument 모양을 보낼 가능성이 높은가" 도 합리적 의심해야 함. 가능한 한 prompt-shape 시나리오 한두 개를 review 의 입력으로.

## 참고

- 핫픽스 PR: 동일 PR #8 에 commit 추가 (push 직후 hotfix, 별도 PR 안 만듦).
- v5 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md`.
- v5 메인 learn: `docs/learn/20260513-cap-ha-automation-v5.md`.
- 사용자 reported 시점: Telegram, 2026-05-12 14:31 KST (esp_claw_1073 / esp_claw_1136 자동화 등록은 성공, 실제 fire 안 됨).
