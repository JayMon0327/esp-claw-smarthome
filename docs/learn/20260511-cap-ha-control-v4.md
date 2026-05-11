# cap_ha_control v4 — safety fixes + ha_automation (HA REST 위임) 작업 기록

> **컨텍스트:** PR #1 (cap_ha_control v3) 머지 직후 시작한 v4 작업. v3 /review 5개 finding 처리 + 자동화 등록/수정/제거 typed tool 추가. 12 commits, branch `feat/cap-ha-control-v4` (base `origin/main` 7f14e69), 모든 작업은 subagent-driven (fresh implementer per task, controller 직접 review).

## 무엇을 만들었나

### Safety fixes (v3 review 5 findings → 5 commits)

| Commit | 항목 | 핵심 변경 |
|---|---|---|
| `b13c676` | P1 race fix — `s_cache_registry` mutex | FreeRTOS non-recursive mutex로 refresh + 3개 lookup_in 호출 + top_candidates 보호. `s_static_registry`는 write-once이라 lock-free 유지. `active_friendly_names`는 mutex 안 잡고 top_candidates에 위임 (재진입 deadlock 방지). |
| `5dc34de` | P2 hex 검증 | `cap_ha_color_to_rgb`의 `#FFGG00` 같은 invalid hex가 `(0xFF, 0, 0)`으로 silent parse되던 문제. `isxdigit((unsigned char)color[i])` 로 6자리 검증 후 strtol. |
| `acf1cc9` | P2 entity cap | `parse_registry`의 calloc count가 untrusted HA JSON에서 옴 — 10K entries → 1.5MB 폭발. `CAP_HA_MAX_REGISTRY_ENTRIES = 64`로 cap + WARN 로그 + truncate. **추가로 `cJSON_ArrayForEach` 루프에 `if (written >= count) break;` 가드 필요 — 원래 plan에 빠져있었음 (heap overflow 위험)**. implementer가 catch. |
| `bdcf921` | P3 description refresh | `compose_description()`이 `cap_ha_group_init`에서 1회만 호출 → boot-fetch가 발견한 entity가 LLM description에 안 반영. `static` 제거하고 `cap_ha_compose_description()`로 노출, boot-fetch task의 refresh 성공 후 재호출. claw_cap이 descriptor.description을 LLM request마다 live-read하는 걸 spike로 확인 (`claw_cap.c:276`, `claw_cap_build_llm_tools_json` → `claw_cap_add_capped_description`). |
| `b62180c` | P3 HTTPS + `--set-insecure` | `https://` URL scheme 감지 + `ha_insecure` NVS 플래그. POST/GET 두 함수 양쪽에서 `is_https` / `insecure` 계산, `crt_bundle_attach = (is_https && !insecure) ? esp_crt_bundle_attach : NULL`, `skip_cert_common_name_check = insecure`, insecure on일 때 매 요청 WARN. 커스텀 CA pem import는 v4.1. |

### ha_automation typed tool (7 commits, Tasks 6.1–6.7)

새로 LLM에 노출되는 두 번째 typed tool. v3 ha_control 옆에 같은 descriptor 배열로 등록.

| Commit | 단계 | 추가/수정 |
|---|---|---|
| `c1c08fb` | 6.1 HTTP helpers | 4개 신규: `put_automation_config` (POST upsert), `delete_automation_config`, `reload_automations`, `call_automation_service` (trigger/turn_on/turn_off). v3 `post_service` 패턴 그대로 (Bearer + crt_bundle gating + 16KB resp buf + heap auth_header). |
| `57bb19e` | 6.2 descriptor scaffold | `s_ha_descriptors[]` 2-element로 확장, `ha_automation` descriptor (action enum / trigger object / target / device_action / brightness_pct / color / kelvin / automation_id / alias). `s_ha_automation_description[1024]` (`-Werror=format-truncation` 회피 위해 plan의 768 → 1024). description에서 `board:*` 명시적 미지원. stub execute가 action enum 검증만. |
| `3506c0c` | 6.3 JSON builders | `cap_ha_action_to_service(domain, action)`를 v3 inline switch에서 추출해 internal.h에 노출. `build_ha_action_array` (service / target.entity_id / data; light/cover/switch domain 별). `build_ha_trigger_array` (`daily_time`/`weekly` → `time at HH:MM:00` + weekly는 `condition time weekday`, `interval` → `time_pattern /N` with 2s–60s seconds, 1min–60min minutes, 1h–24h hours 분기 + 클램프). |
| `bafa7a5` | 6.4 create | `do_create`: target=board:* reject (target string AND post-resolve `entity.domain` 두 단 검사) → resolve → build action+trigger → compose config → `auto_id = esp_claw_<sec>` (esp_timer_get_time/1000000) → PUT → reload → success message verbatim format. |
| `607caec` | 6.5 remove/list/trigger_now/enable/disable | `do_remove` (DELETE + reload), `do_list` (GET /api/states + filter `automation.*` + `esp_claw_managed` flag via substring), `do_service` helper + 3 wrappers (`trigger`/`turn_on`/`turn_off`). 모든 핸들러 `automation.` prefix를 strip + 다시 prepend하는 normalize 패턴. |
| `8dbf0df` | 6.6 update | GET-merge-PUT. `cap_ha_http_get_automation_config` HTTP helper 추가. 404면 "먼저 create로 등록하세요". 누락 필드는 caller가 안 보내면 기존 cfg의 `action[0].target.entity_id` / `action[0].service` 의 `.` 뒤 부분을 fallback. board:* reject 동일. |
| `c9050db` | 6.7 console `--automation` | `cmd_cap_ha_control.c`에 `arg_str0` + dispatch. `cap_ha_automation_execute(json, output, sizeof(output))` 직접 호출. `arg_end(3) → arg_end(4)`. |
| `c04c845` | post-flash fix | 보드 검증에서 발견된 2개 demo-blocker 버그 (아래 "v3 ship 사이클 학습 재확인" 섹션 참조). |

## 무엇을 배웠나

### 1. Plan-time spec gap을 implementer가 잡았다 — `cJSON_ArrayForEach` + cap

Task 3 (entity cap)에서 plan은 "count를 64로 cap" 만 명시. 그런데 `parse_registry`는 `cJSON_ArrayForEach`로 전체 JSON 배열을 iterate. `count`만 cap하고 루프는 안 막으면, JSON에 100 entries 있을 때 `items[64-99]`에 OOB write. v3 plan 작성자가 놓친 부분을 implementer subagent가 catch + 추가했다.

**원칙:** "수치 cap" 만으로는 부족. 루프와 cap을 같이 점검. 다음 비슷한 작업은 plan-review에서 "loop bound도 명시" 항목을 추가.

### 2. Plan-reviewer는 architecture, implementer-subagent는 코드 디테일

v3 ship 사이클에서 "plan-review 통과 후에도 on-board 검증에서 critical bug 4개"를 학습했다. v4에서는 그 학습을 살려 plan-time에 architecture (Option B HA REST 위임) + endpoint 검증을 미리 끝냈고, plan 외 디테일 (claw_cap descriptor cache 동작, cap_ha_entity_t 필드명, argtable arg_end count) 같은 코드-레벨 미지수는 spike steps + implementer 책임으로 위임했다. plan-review 절약 + implementer가 실제 코드에서 더 정확히 판단.

v3 16 commits → v4 12 commits, 큰 후속 review iteration 없이 모두 첫 시도 통과.

### 3. claw_cap framework의 description은 live-read — invalidate API 필요 없었음

Task 4 Step 1 spike에서 `claw_cap_build_llm_tools_json` (claw_cap.c:276)이 매 LLM 요청 시점에 `slot->descriptor.description` 포인터를 dereference하는 걸 확인 → buffer 갱신만 하면 자동 전파. plan의 Step 5 escalation (`claw_cap_invalidate_tool_description` API 추가)은 불필요.

**원칙:** Framework 동작이 불명확한 부분은 spike step으로 분리, escalation path는 미리 plan에 적되 실제 동작 확인 후 happy path 채택. v4 cross-component 변경 없이 같은 효과.

### 4. -Werror=format-truncation: 빌드 검증이 plan 가정 잡아준 사례

Task 6.2에서 `s_ha_automation_description[768]`이 estimated 413+256 byte interpolation 초과 → gcc-15 `-Werror=format-truncation` 에러. 1024로 bump (sibling `s_ha_description`과 동일). plan-time에 sprintf 결과 길이 추정 안 했음.

**원칙:** snprintf target buffer는 format string + 가장 큰 dynamic 부분 합으로 보수적으로 sizing. 미루다가 빌드 단계에서 잡히는 게 차라리 다행.

### 5. v4에서 의도적으로 빼낸 것 — `board:onboard_rgb` 자동화

Option B (HA REST 위임)의 trade-off로 board-only 자동화는 v4에서 명시적 reject. Task 6.4의 `do_create`가 `target=board:*` 도 reject, 어떤 entity로 resolve되어도 `domain=board` 면 reject. v5에서 cap_scheduler 추가로 보드 자체 자동화 지원 예정.

**원칙:** Scope cut을 architecture로 명시적 처리 (reject + 명확한 v5 메시지). LLM이 reject 메시지 verbatim echo → 사용자에게 "왜 안되는지" + "언제 가능한지" 동시 전달.

## v3 ship 사이클 학습 재확인 — 보드 검증에서만 발견되는 외부 시스템 의존 버그

v3 ship 학습 ("plan-review 2 라운드 + spec/code review 통과 후에도 on-board 검증에서 critical bug")가 v4에서도 그대로 재현됐다. v4 plan-time에 HA endpoint 응답 schema를 직접 curl로 검증했음에도, 두 개의 demo-blocker가 처음 실 보드 + 실 HA 통합 시점에 드러났다.

### Bug 1 — HA modern schema: `trigger` → `triggers` (plural)

- 증상: `update` 시 HA 400 `"Cannot specify both 'trigger' and 'triggers'"`.
- 원인: HA 2024.x+ 가 config 내부 저장을 plural 키로 정규화. POST는 둘 다 받지만, GET는 plural 만 반환. 우리 `do_update` 는 `DeleteItem("trigger")` (no-op, 존재 안 함) + `AddItem("trigger", new)` → 결과적으로 양쪽 키 모두 PUT body에 들어감.
- 수정 (`c04c845`): config object에 추가할 때 모두 plural 키 사용 (`triggers`/`actions`/`conditions`). update merge에서는 singular+plural 둘 다 delete 후 plural add. fallback action[0] 파싱도 plural 우선 → singular 폴백. action service 필드도 `service` / 모던 `action` 둘 다 인식.

### Bug 2 — HA가 alias 슬러그로 entity_id 생성 → trigger/enable/disable silent no-op

- 증상: `automation_id: esp_claw_115` + `alias: "v4-update-debug"` 로 등록한 자동화에 대해 `automation.trigger {entity_id: "automation.esp_claw_115"}` 호출 → HA 200 응답이지만 실제로 발화 안 됨. HA UI에서 자동화는 `automation.v4_update_debug` 로 노출.
- 원인: HA는 config의 `id` 필드는 내부 storage 키로만 사용하고, runtime entity_id 는 `alias` 슬러그에서 계산. 우리 firmware는 "URL id == entity local id" 라고 가정 (틀림). 게다가 HA service 호출은 잘못된 entity_id에도 silently 200 반환 → 디버깅 어려움.
- 수정 (`c04c845`): `resolve_entity_id_by_config_id(config_id, out_entity_id)` 헬퍼 추가 (GET /api/states + `attributes.id == config_id` 매칭). do_create 응답은 이제 `{automation_id: esp_claw_X, entity_id: automation.<actual_slug>}` 양쪽 다 반환. do_service (trigger/enable/disable) 는 호출 전에 resolver로 실 entity_id 확보. do_list 의 `esp_claw_managed` 분류는 `attributes.id` 의 `esp_claw_` 접두 검사로 변경 (이전 substring 매치는 잘못된 entity_id를 봐서 항상 0).

### 두 버그가 plan-review에서 안 잡힌 이유

- Bug 1: plan-time에 POST 만 curl 로 검증, GET-merge-PUT 사이클은 안 돌려봤음. POST는 singular/plural 둘 다 수용해 "동작 확인"이 정상 케이스로 받아들여졌다.
- Bug 2: plan-time에 `/api/services/automation/trigger` curl 검증이 "200 응답" 만 확인했지 실제 자동화가 발화했는지 확인 안 했음. HA의 silent-no-op 정책이 false positive 만듦.

### 원칙 (v3 학습 재강조)

- 외부 시스템 통합 시 **happy-path 응답 코드 ≠ 실제 동작**. HA service 호출이 200 반환해도 entity_id 가 잘못되면 no-op. 실제 effect (lamp turn_off 발화) 까지 chain end-to-end 검증 필요.
- API 응답 schema는 plan-time GET 한 번이 아니라 **GET → mutate → PUT 사이클** 까지 돌려봐야 정규화 차이 (singular vs plural) 발견됨.
- 향후 비슷한 작업: plan-time spike 단계에 "POST 한 번 + 그 결과를 GET 다시 + 그 응답을 PUT 다시 + 실제 어떤 entity가 변했는지" 까지 짚는 micro-E2E 필수.

## 다음에 v5에서

- `board:onboard_rgb` 자동화 — cap_scheduler subset (cron / interval / daily_time) + event_router 룰로 firmware-local 등록. v4 ha_automation의 reject 메시지가 v5 path로 redirect 가능.
- 커스텀 HA CA cert pem import — Task 5에서 deferred. esp_http_client `cert_pem` 필드 + NVS에 PEM string 보관 + 콘솔 명령 `--set-ca <path>`.
- `cap_ha_action_to_service`에 더 많은 HA domain 지원 — fan / climate / media_player / vacuum 등 v3+v4 모두 빠진 부분.
- `claw_cap_invalidate_tool_description` API — 현재는 live-read라 필요 없지만 다른 cap이 캐시 패턴 쓰게 되면 framework-level API 가치 있음.
- `cap_ha_resolve_active_friendly_names`의 `s_cache_registry.count` 락-프리 읽기에 `/* lock-free read; bounded re-read inside top_candidates */` 코멘트 추가 (v4 Task 1 code review에서 NIT으로 flag됨).
- cap_ha_group_init이 mutex 생성 실패 (`ESP_ERR_NO_MEM`) 시에도 `compose_description()` 호출 → NULL deref 가능 (Task 1 code review의 SHOULD_FIX #1). mutex create OOM은 보통 시스템 전반이 hosed 상태라 hot bug는 아님.

## 참고

- v4 plan: `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`
- v3 ship 학습: `docs/learn/20260511-cap-ha-control-v3-ship-report.md`
- v3 implementation 학습: `docs/learn/20260508-cap-ha-control-v3.md`
- v3 완료 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-11.md`
- 검증 절차 (이 PR 머지 전 필수):
  - PR-A 회귀: ha_control mutex stress / `#FFGG00` reject / 100-entry truncation log
  - PR-B 회귀: boot-fetch 후 description refresh / `--set-insecure on` + https HA
  - PR-C E2E: ha_automation create/list/trigger_now/disable/enable/update/remove / `board:*` reject / 보드 reflash 후 persist
