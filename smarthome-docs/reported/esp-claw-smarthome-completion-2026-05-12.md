# ESP-Claw 스마트홈 v4 완료 보고서 (2026-05-12)

> **컨텍스트:** PR #1 (cap_ha_control v3) 머지 직후 시작한 v4 후속 작업. v3 /review 5개 finding 처리 + LLM-가시 자동화 등록/수정/제거 typed tool 추가. 모든 작업은 subagent-driven (fresh implementer per task, controller 직접 review). 보드 검증 + 실 HA 통합까지 완료.

---

## 1. PR 현황

- **PR #3 (코드):** `feat(cap_ha_control): v4 — safety fixes + ha_automation typed tool` — https://github.com/JayMon0327/esp-claw-smarthome/pull/3
  - Base: `origin/main` (`7f14e69`, v3 머지 직후)
  - Head: `feat/cap-ha-control-v4`
  - 15 commits (13 implementation/docs + 2 post-flash fix/learn)
  - 빌드 green, on-board E2E 검증 완료

- **PR #4 (이 문서):** 완료 보고서 + 후속 진행사항 정리

---

## 2. 구현 완료 항목

### 2.1 v3 /review 5 findings 처리 (PR #1 후속)

| Commit | Finding | 핵심 변경 |
|---|---|---|
| `b13c676` | **P1 mutex** | FreeRTOS non-recursive mutex로 `s_cache_registry` 보호. refresh + 3개 lookup_in + top_candidates 모두 critical section. `s_static_registry`는 write-once이라 lock-free 유지. `active_friendly_names` 는 mutex 안 잡고 top_candidates 위임 (재진입 deadlock 방지). |
| `5dc34de` | **P2 hex 검증** | `#FFGG00` 같은 invalid hex가 silent `(0xFF, 0, 0)` parse되던 문제. `isxdigit((unsigned char))` 6자 검증 후 strtol. |
| `acf1cc9` | **P2 entity cap** | `parse_registry` calloc count가 untrusted HA JSON에서 옴. `CAP_HA_MAX_REGISTRY_ENTRIES=64` cap + WARN. 추가로 `cJSON_ArrayForEach` 루프에 `if (written >= count) break;` 가드 — plan에 빠진 OOB write 위험을 implementer subagent가 catch. |
| `bdcf921` | **P3 desc refresh** | `compose_description()`이 `cap_ha_group_init`에서 1회만 호출되어 boot-fetch 결과 반영 안 됨. static 제거 + 노출, boot_fetch_task의 refresh 성공 후 재호출. claw_cap이 description을 LLM request마다 live-read하는 걸 spike로 확인 → invalidate API 추가 불필요. |
| `b62180c` | **P3 HTTPS + --insecure** | `https://` URL scheme 감지 + `ha_insecure` NVS flag. POST/GET 두 함수 양쪽 `is_https` 계산 + `crt_bundle_attach` gating + `skip_cert_common_name_check`. insecure on일 때 매 요청 WARN. 커스텀 CA pem import 는 v4.1. |

### 2.2 새 typed tool `ha_automation` (HA REST 위임, Option B)

LLM에 노출되는 두 번째 typed tool (`ha_control` 옆에 같은 descriptor 배열로 등록).

| Commit | 단계 | 추가/수정 |
|---|---|---|
| `c1c08fb` | 6.1 HTTP helpers | 4개 신규: `put_automation_config` (POST upsert), `delete_automation_config`, `reload_automations`, `call_automation_service` (trigger/turn_on/turn_off). v3 `post_service` 패턴 그대로 (Bearer + crt_bundle gating + 16KB resp buf + heap auth_header). |
| `57bb19e` | 6.2 descriptor scaffold | `s_ha_descriptors[]` 2-element 확장, `ha_automation` schema (action enum / trigger object / target / device_action / brightness_pct / color / kelvin / automation_id / alias). `s_ha_automation_description[1024]`. description에 `board:*` 명시적 미지원. |
| `3506c0c` | 6.3 JSON builders | `cap_ha_action_to_service(domain, action)`를 v3 inline switch에서 추출해 internal.h 노출. `build_ha_action_array` (service / target / data). `build_ha_trigger_array` (daily_time/weekly→time+condition, interval→time_pattern with 2s–60s seconds, 1min–60min minutes, 1h–24h hours 분기 + 클램프). |
| `bafa7a5` | 6.4 create | board:* reject (target string AND post-resolve `entity.domain` 두 단 검사) → resolve → build action+trigger → compose config → `auto_id=esp_claw_<sec>` → PUT → reload → success message verbatim format. |
| `607caec` | 6.5 lifecycle | `do_remove` (DELETE+reload), `do_list` (GET /api/states + filter `automation.*`), `do_service` helper + 3 wrappers (`trigger`/`turn_on`/`turn_off`). 모든 핸들러 `automation.` prefix normalize. |
| `8dbf0df` | 6.6 update | GET-merge-PUT 패턴. `cap_ha_http_get_automation_config` HTTP helper 추가. 404 시 "먼저 create로 등록하세요". target/device_action 누락 시 기존 `action[0].target.entity_id` / `action[0].service` 의 `.` 뒤 부분 fallback. |
| `c9050db` | 6.7 console | `cmd_cap_ha_control.c`에 `--automation '<json>'` 추가. `cap_ha_automation_execute(json, output, sizeof(output))` 직접 호출 + 결과 stdout 출력. |

### 2.3 보드 검증 중 발견한 2개 demo-blocker → fix `c04c845`

**Bug 1: HA modern schema (plural keys)**
- 증상: update PUT → 400 `"Cannot specify both 'trigger' and 'triggers'. Please use 'triggers' only."`
- 원인: HA 2024.x+가 GET 응답을 plural로 정규화. 우리 do_update가 `DeleteItem("trigger")` (singular, no-op) + `AddItem("trigger", new)` → 결과적으로 양쪽 키 모두 PUT body에 포함.
- Fix: config에 추가시 모두 plural keys (`triggers`/`actions`/`conditions`). update merge에서 singular+plural 둘 다 delete 후 plural add. fallback action[0] 파싱도 plural 우선 → singular 폴백. service field도 modern `action` / legacy `service` 둘 다 인식.

**Bug 2: entity_id alias slug mismatch — silent no-op**
- 증상: trigger/enable/disable이 HA에서 200 응답이지만 실제 자동화 발화 안 됨.
- 원인: HA가 runtime entity_id 를 `alias` 슬러그에서 계산 (예: alias="v4-update-debug" → entity_id="automation.v4_update_debug"). 우리 firmware 는 `entity_id = "automation." + config_id` 가정 (틀림). HA `automation.trigger` 서비스는 invalid entity_id에도 silently 200 → 디버깅 어려움.
- Fix: `resolve_entity_id_by_config_id(config_id, out_entity_id)` 헬퍼 추가 — GET `/api/states` + `attributes.id == config_id` 매칭으로 실제 entity_id 확보. do_create 응답에 양쪽 모두 반환 (`automation_id`=config_id, `entity_id`=실제 슬러그 형태). do_service는 호출 전 resolver, do_list의 `esp_claw_managed` 분류도 `attributes.id` 접두 검사로 변경.

---

## 3. 검증 결과 (실 보드 + 실 HA, 2026-05-12)

| Test | Result | Evidence |
|---|---|---|
| Boot + boot-fetch description refresh | ✅ | `cap_ha_resolve: boot-fetch: description refreshed with 4+2 entities` |
| `#FFGG00` reject | ✅ | `"지원하지 않는 색상입니다 (color=#FFGG00)"` |
| `#A1B2C3` valid → LED set | ✅ | success + 회청색 |
| v3 `ha_control` 회귀 (`light.smart_bulb` toggle) | ✅ | POST 200 + 한국어 메시지 verbatim |
| **Mutex stress** (15× `--refresh-registry` + `--call` 동시) | ✅ | 0 backtraces, 0 resets, 0 CORRUPTED, 30/30 POST 200 |
| ha_automation `create` daily_time 23:59 turn_off | ✅ | `{automation_id: esp_claw_32, entity_id: automation.v4_e2e_test}` |
| `list` | ✅ | `자동화 4건 (ESP-Claw 등록: 1건)` |
| `trigger_now` (실 lamp turn_off) | ✅ | resolver가 `automation.v4_e2e_test` 변환 + 발화 |
| `disable` → HA UI state=off | ✅ | service 호출 + state 변화 |
| `enable` → state=on | ✅ | |
| `update` time→22:00 | ✅ | GET → merge → PUT 200 (plural-key fix 검증) |
| `remove` + reload | ✅ | DELETE 200, entity 사라짐 |
| `board:onboard_rgb` reject | ✅ | "보드 자체 자동화는 v5에서 지원될 예정입니다" verbatim |

---

## 4. 핵심 학습 — v3 ship 사이클 재확인

Plan-time curl 검증 (happy-path POST)이 통과해도 GET → mutate → PUT 사이클 / entity_id 슬러그 처리 같은 통합 디테일은 실 보드 + 실 HA 통합 시점에만 드러난다. v3 ship 사이클의 "plan-review 통과 후에도 critical bug 4개" 학습이 v4에서도 동일하게 재현됐다. HA `automation.trigger` 같은 service 호출이 잘못된 entity_id에도 silent-200 반환하는 정책은 false-positive 만들어 디버깅을 더 어렵게 함.

다음 비슷한 작업에서는 plan spike 단계에 **micro-E2E** (POST → GET 다시 → PUT 다시 → 실제 effect까지 확인) 필수.

자세한 학습 로그: `docs/learn/20260511-cap-ha-control-v4.md` + `docs/learn/20260512-cap-ha-control-v4-ship-and-state-trigger-followup.md`

---

# 후속 진행사항 (다음 PR 후보)

## Follow-up 1: c04c845 fix 후속 정제

c04c845가 demo-blocker 2개를 해결했지만 아키텍처적으로 다음 PR에서 보완 여지:

### 1-A. entity_id 매핑 NVS 캐싱 (가장 가치 있음)

- **현재:** `do_service` (trigger/enable/disable) 가 매 호출마다 `GET /api/states` (~50KB) 후 attributes.id 매칭. 매번 ~100ms + 네트워크 + 메모리.
- **제안:** 자동화 등록 시점 (do_create 끝) 에 NVS namespace `ha_auto_map` 에 `(config_id → entity_id)` 매핑 저장. do_service는 NVS lookup 우선, miss/stale 시에만 fallback to `/api/states`. do_remove는 매핑 entry도 같이 삭제.
- **이득:** service 호출당 50KB GET + 100ms 절약. RT 단축으로 LLM 대기 시간 감소.

### 1-B. resolver miss 시 explicit failure (silent-200 방지)

- **현재:** `resolve_entity_id_by_config_id` 가 NOT_FOUND 면 fallback to `automation.<config_id>` 형태로 service 호출. HA가 invalid entity_id에 silent-200 → 사용자에게 잘못된 success 응답.
- **제안:** resolver miss 면 service 호출 자체를 skip + `{"success":false, "message":"automation_id '<id>'를 찾을 수 없습니다."}` 반환. 명확한 실패 신호.

### 1-C. helper 시그니처 정리

- `build_ha_trigger_array` / `build_ha_action_array` 가 array만 반환 (key 결정은 caller 책임). 둘 다 (key, array) 쌍 반환으로 시그니처 변경하면 caller가 키 이름 (plural/singular) 신경 안 써도 됨.

---

## Follow-up 2: 상태(state) 기반 trigger 지원 — 도어센서 → 조명 같은 시나리오

### 발견 — 사용자 시연 (Telegram 로그 2026-05-12 09:34)

```
[2026. 5. 12. 오전 9:34] LEE Jaymon: 현관문 도어센서가 열리면, 화장실 조명이 켜지는 자동화 1
현관문 도어센서가 닫히면, 화장실 조명이 꺼지는 자동화 2
만들어줘

[2026. 5. 12. 오전 9:35] esp-claw: 현재 장치의 HA 자동화 등록 도구는 시간 기반 트리거
(일일/주간/인터벌)만 지원합니다. 문열림/닫힘(센서 상태)으로 바로 트리거되는 자동화는
이 인터페이스로 자동 생성할 수 없습니다. 대신 Home Assistant에 붙여넣을 수 있는
YAML 예제와 UI로 만드는 방법을 드립니다 — [...]
```

ESP-Claw 가 자동화 등록을 거부하고 YAML 예제 + UI 안내로 우회. 사용자의 정당한 질문이 따라옴: "**HA API로 자동화 등록 구현한 거 맞지? 혹시 MCP로 구현했어?**"

### 답

**구현 방식: HA REST API 직접 호출 (MCP 아님).** 모든 commits 가 `cap_ha_control_http.c` 를 통해 `http://192.168.1.94:8123/api/config/automation/config/<id>` 에 직접 POST/GET/DELETE. 보드 검증 로그에서도 확인 가능:
```
I (33028) cap_ha_http: POST http://192.168.1.94:8123/api/config/automation/config/esp_claw_32 body_len=171
I (33048) cap_ha_http: automation reload result err=ESP_OK status=200
```

### 왜 ESP-Claw가 거부했나

v4 의 ha_automation typed-tool input schema 가 `trigger.kind` 를 **enum {daily_time, weekly, interval}** 로만 제약 (`cap_ha_automation.c` 의 `build_ha_trigger_array` 함수 + descriptor schema 의 enum). LLM은 schema enum 밖의 값을 생성하지 못해 등록 시도 자체가 typed-tool layer에서 차단된다.

이건 v4 의 **의도된 scope cut** 이었다 — typed-tool 의 LLM 합성 검증 가능성을 높이려고 trigger 종류를 시간 기반 3개로 한정. 그러나 사용자 시연에서 가장 자연스러운 자동화 ("센서가 X 되면 Y 해줘") 가 모두 state 기반이라 scope 가 너무 좁았다.

### 해결 방향 (다음 PR)

**구현 분량:** ~50–80 LoC + schema description 갱신.

1. **Schema 확장:** `cap_ha_control.c` 의 `s_ha_descriptors[1].input_schema_json` 에 `kind` enum 에 `"state"` 추가, `entity_id` / `from` / `to` 필드 추가.

2. **`build_ha_trigger_array` 에 state 분기 추가:**
   ```c
   } else if (strcmp(kind, "state") == 0) {
       const cJSON *entity_j = cJSON_GetObjectItem(trigger_in, "entity_id");
       const cJSON *from_j = cJSON_GetObjectItem(trigger_in, "from");
       const cJSON *to_j = cJSON_GetObjectItem(trigger_in, "to");
       if (!cJSON_IsString(entity_j) || !cJSON_IsString(to_j)) {
           snprintf(err_msg, err_msg_size,
                    "state trigger에는 entity_id와 to가 필요합니다.");
           return ESP_ERR_INVALID_ARG;
       }
       cJSON *step = cJSON_CreateObject();
       cJSON_AddStringToObject(step, "platform", "state");
       cJSON_AddStringToObject(step, "entity_id", entity_j->valuestring);
       if (cJSON_IsString(from_j)) cJSON_AddStringToObject(step, "from", from_j->valuestring);
       cJSON_AddStringToObject(step, "to", to_j->valuestring);
       cJSON_AddItemToArray(arr, step);
   }
   ```

3. **Target resolver 확장:** 현재 `cap_ha_resolve_target` 은 controllable entity (light/switch/cover) 만 다룸. 도어센서 (binary_sensor) 등 sensor entity 도 trigger.entity_id로 resolve 가능해야 함. resolver 의 도메인 화이트리스트에 binary_sensor / sensor / device_tracker 추가 + 친근한 이름 ("현관문 센서", "거실 인체감지") 매핑.

4. **Description 갱신:** `compose_description` 의 `s_ha_automation_description` 에 새 kind 노출 + 자연어 의도 예시 ("'현관문 열리면 조명 켜기' 같은 센서 트리거도 지원합니다").

5. **테스트:** 사용자 시나리오 2건 (열림→켜기 / 닫힘→끄기) 실 HA 등록 + 실제 센서 토글 시 자동화 발화 확인.

이 follow-up 은 v5 의 `board:onboard_rgb` 자동화 (cap_scheduler subset) 와 같이 묶어도 좋다 — 둘 다 trigger 종류 확장 작업.

---

## 참고

- v4 plan: `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`
- v4 implementation learn: `docs/learn/20260511-cap-ha-control-v4.md`
- v4 ship + state-trigger learn (2026-05-12): `docs/learn/20260512-cap-ha-control-v4-ship-and-state-trigger-followup.md`
- v3 ship 학습: `docs/learn/20260511-cap-ha-control-v3-ship-report.md`
- v3 완료 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-11.md`
- PR #3 (코드, MERGE 대기): https://github.com/JayMon0327/esp-claw-smarthome/pull/3
- PR #4 (이 문서): 이 PR
