# cap_ha_control v3 Plan — Review Revision (2026-05-08)

## 컨텍스트
- Plan: `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool.md`
- Status: 구현 시작 전, plan-only revision
- Reviewer: plan-reviewer (2 라운드)

이번에 작성한 cap_ha_control typed-tool plan을 plan-reviewer에 두 번 보냈다.
1라운드 8개 항목, 2라운드 5개 항목 — 총 13개 모두 plan에 반영했다.
모두 코드 리뷰가 아니라 **plan 자체에 박힌 잘못된 가정 / placeholder / 시그니처 mismatch** 였다.

## 라운드 1: 8 items

| # | 문제 | 수정 |
|---|---|---|
| 1 | app_claw build wiring 누락 | Task 2에 CMakeLists.txt + idf_component.yml step 추가 — 안 넣으면 `#include "cap_ha_control.h"` 빌드 실패 |
| 2 | `group_init` 시그니처 잘못됨 | `claw_cap_lifecycle_fn` 은 `esp_err_t (*)(void)` — Task 2/11에서 `(group, user_ctx)` 인자 제거 |
| 3 | `led_strip_set_pixel_hsv` 의존 | 실제로는 API 존재하지만 in-repo lua_module과 일관되게 `set_pixel(r,g,b)` + RGB scaling. 버전 핀 `^3.0.3` |
| 4 | wizard NVS namespace split | wizard는 `app_config` namespace, cap_ha_http_*는 `ha_ctl` namespace — v3 console-only 결정, wizard 통합은 v4 |
| 5 | "256자 제한 제거" 과장 | API는 caller-provided buffer 그대로 — URL 160B / token 4096B로 정정. 진짜 제거는 `_alloc` helper로 v4 |
| 6 | MCP fail이 로깅만 | response root JSON에 `isError:true` 명시 주입 |
| 7 | vis_cap_groups default 위치 | sdkconfig가 아니라 `app_config.c:43` `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS` 매크로 |
| 8 | skills_list.json 전체 교체 위험 | Python in-place patch로 4개 id만 surgical 제거 |

## 라운드 2: 5 items

| # | 문제 | 수정 |
|---|---|---|
| 1 | `s_capability_group_infos[]` 누락 | app_capabilities.c의 두 번째 테이블(UI/listing)에도 cap_ha_control entry 추가 |
| 2 | HTTP error handling이 401 메시지를 가림 | `cap_ha_http_post_service`가 non-2xx에서 `ESP_ERR_HTTP_CONNECT` 반환 → caller의 failure composer가 `http_err != ESP_OK` 분기로 빠져 "network err" 메시지가 나옴. 전송 성공 시 ESP_OK 반환으로 변경, status 판정은 caller |
| 3 | MCP 패치가 실제 함수와 안 맞음 | 함수는 `cap_mcp_call_remote_tool(const char *, cJSON **)`인데 plan은 `char *output, size_t output_size`. VLA `wrapped[output_size]` + JSON escaping 문제. cJSON object 조작으로 재작성 |
| 4 | `boot_fetch_task` forward decl을 함수 안에 둠 | 일부 toolchain에서 nested extern 경고/에러. 파일 상단 #if 가드와 함께 |
| 5 | `/api/states` 16KB 부족 | 실제 HA states 응답은 흔히 잘림. `CAP_HA_STATES_BUF_BYTES = 64*1024` 별도 분리, 그래도 truncate 가능성은 best-effort로 문서화 |

## 무엇을 배웠나
- **plan 검증은 plan을 짜는 사람이 못 잡는다.** 같은 가정이 plan 안에서 일관되게 반복되면 self-review로는 안 보인다. plan-reviewer 2회로 13개 발견.
- **구현 함수 시그니처는 plan 작성 시점에 정확히 옮겨라.** "비슷할 거다"로 적은 코드는 reviewer가 grep로 즉시 잡는다 (round 2의 #3 MCP 패치, #1 두 번째 테이블 누락).
- **NVS namespace split 같은 architecture 결정은 plan에서 명시적으로 lock-in.** "wizard에 필드 추가만 하면 됨" 같은 단순화가 wizard ↔ runtime 데이터 흐름을 깨는 sleepy bug를 만든다 (round 1의 #4).
- **HTTP layer error contract는 caller 흐름과 함께 설계.** transport 성공/실패와 protocol-level status를 같은 return value로 짜면 메시지 합성이 망가진다 (round 2의 #2).

## 다음
- plan v3는 13개 항목 반영 완료. 실행 모드(Subagent-Driven vs Inline) 선택 후 Task 1부터 진행.
- 실행 중 추가로 plan에서 빠진 게 발견되면 plan을 즉시 patch하고 같은 commit에 plan diff를 같이 묶는다 (코드와 plan이 갈라지는 걸 방지).
