# cap_ha_control v3 ship + report — 학습 정리 (2026-05-11)

> **컨텍스트:** PR #1 (cap_ha_control v3) main 머지 후, completion report 작성 시점에 돌아본 전체 과정 정리. v2 → v3 architecture 전환, plan-review 2 라운드, 17 task 구현, on-board 검증, review/v4 plan/완료보고서까지의 사이클 학습.

## 무엇을 배웠나

### 1. Plan-reviewer 다회차 통과 후에도 실 보드 검증에서 critical bug가 나온다

plan-review 2 라운드에서 13개 이슈를 잡았고 (구현 함수 시그니처 mismatch, NVS namespace split 같은 plan-level 가정 오류 위주), 구현 중 spec-reviewer + code-quality reviewer로 또 ~5개 이슈를 잡았다. 그럼에도 on-board 검증에서 추가로 4개 firmware bug가 나왔다:

- `cap_ha_http_*`의 `auth_header[4128]` 스택 오버플로우 — 6KB task 스택을 거의 다 먹어 `Backtrace: |<-CORRUPTED` + 무한 reboot. heap으로 이전.
- esp_http_client가 401을 `ESP_ERR_NOT_SUPPORTED`로 반환 → compose_failure_message 분기 순서 때문에 "인증 실패" 메시지가 "network err"에 가려졌음. **v3 데모 guardrail의 가장 중요한 fix.**
- `claw_cap` description 256B 한계 → 동적 active devices 리스트 + verbatim echo 룰이 잘려 LLM 미도달.
- `vis_cap_groups` NVS stale 값 → erase-flash 안 한 보드는 새 default 적용 안 됨.

**원칙:** plan/code review는 plan-vs-code mismatch와 명시적 패턴 위반은 잘 잡지만, 런타임 자원 (스택 크기, NVS 영속성, 외부 라이브러리 에러 코드 매핑) 같은 *환경 의존* 버그는 실 보드에 올려 봐야 잡힌다. 다음 비슷한 작업은 **plan → impl → on-board의 3-stage 검증**을 1주기로 잡고 시간 배분.

### 2. CLI 시뮬레이션은 진짜 검증이 아니다 (false negative 위험)

Task 16 (Telegram NL E2E) 검증할 때 `event_router --emit-message`로 시뮬레이트했고, LLM이 ha_control을 호출하지 않아 "실패"라고 결론냈다. 그런데 user가 실 Telegram client에서 `/start` 후 시연해보니 정상 동작. 차이의 원인:
- console emit_message가 real Telegram message envelope을 완전히 복제 못 함 (username/metadata 누락)
- 시뮬레이션 중 누적된 session history가 LLM을 "clarification mode"에 가둠 → 새 메시지가 prior turn 연속으로 해석됨
- 보드 reboot으로도 session history가 안 지워짐 (NVS/fatfs persistent)

**그러나 시뮬레이션 시도가 진짜 버그 2개를 잡았다** (description 256B truncation, vis_cap_groups NVS stale). 시뮬레이션은 검증 도구가 아니라 *발견 도구*로 가치 있음.

**원칙:** "시뮬레이션 실패 == 기능 실패"로 결론내기 전에, 시뮬레이션이 실제 환경을 얼마나 잘 복제하는지 자문. 의심스러우면 user에게 실제 환경에서 확인 요청.

### 3. 환경 의존 산출물은 plan에 명시해야 한다 (worktree 빌드 환경 회수)

`.claude/worktrees/develop` worktree에서 첫 빌드가 실패했다. 원인:
- `application/edge_agent/components/gen_bmgr_codes/gen_*` (11 파일) — gitignored 생성 산출물, `bmgr_patch.py`가 patch만 하고 generate는 안 함
- `application/edge_agent/sdkconfig` — FLASHSIZE_16MB 필요 (sdkconfig.defaults에 명시 안 됨)

Main worktree에서 복사해야 빌드 가능. 이건 plan에 없던 사전 조건이었고, 약 30분 detour를 만들었다. `memory/project_worktree_build_env.md`에 기록.

**원칙:** plan은 "clean worktree에서 시작" 같은 가정을 적되, *clean*의 정의를 명시. gitignored 산출물이 빌드에 필요하면 그것도 적기.

### 4. NVS 영속성 = 새 default가 기존 보드에 자동 적용 안 됨

Task 12에서 `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS`를 새 값으로 설정했지만, 기존 보드 NVS에 v2 default 값이 박혀 있어서 코드의 default가 fallback으로 쓰이지 않았다. 결과: cap_ha_control이 firmware에는 등록되지만 LLM에는 안 보임.

해결: `POST http://<board_ip>/api/config` + restart로 NVS 갱신. 또는 erase-flash.

**v4 follow-up:** app_config에 마이그레이션 룰 추가 (현재 값에 cap_ha_control 없으면 한 번만 강제 추가). 이건 firmware 패턴 일반화 가치 있음 — 모든 NVS 기반 설정 default 변경에 동일 문제 발생.

**원칙:** NVS에 영속되는 설정의 default를 바꿀 때는 **마이그레이션 경로** 같이 설계. 단순히 default 값 변경만으로는 기존 사용자에게 안 닿음.

### 5. 작은 architecture 결정 하나가 데모 guardrail 완성도를 좌우한다

v3 핵심 발상은 "LLM이 거짓 성공을 합성할 표면 자체를 없앤다". 이걸 architecture로 풀어내려면 다음이 *모두* 맞아야 했다:
- (a) firmware가 한국어 메시지를 합성 (Task 7 cap_ha_compose_*)
- (b) LLM이 그 메시지를 verbatim echo (Task 1 system prompt)
- (c) tool description이 LLM 컨텍스트에 잘리지 않고 도달 (claw_cap 1024B fix)
- (d) HTTP 401이 "network err"이 아닌 "인증 실패" 메시지로 매핑 (df783ea fix)
- (e) v2 surface (cap_lua, mcp_call_tool, activate_skill lua_module_*)가 차단됨 (system prompt + vis_cap_groups)

(c)와 (d)가 빠졌으면 LLM은 description을 다 못 보거나 401 시 "네트워크 문제"라고 합성했을 것 — 둘 다 거짓 성공의 변형. 5개 조건 중 1개 빠지면 전체 guardrail이 무력화되는 구조.

**원칙:** "architecture 차원 차단"은 마지막 1%까지 전부 맞춰야 작동. 80% 완성된 architecture는 60% 효과가 아니라 0% 효과인 경우가 흔함. 모든 layer를 *실제로* 통과시켜 검증.

### 6. v3는 단일 typed tool로 동작하지만 v2 path가 catalog에 남아 있다

`fatfs_image/skills/lua_module_led_strip.md` 등 v2 시점 lua skill들이 fatfs에 그대로 있어서 LLM이 `activate_skill("lua_module_led_strip")`로 우회 가능. Task 13에서 v2 lua/skill 파일들을 지웠지만 develop branch에는 처음부터 없던 상태였고, fatfs 동기화 메커니즘이 component 디렉터리에서 자동 sync하기 때문에 lua_module_*는 여전히 catalog에 들어감.

차선책: system prompt에 "Never call activate_skill for any skill whose id starts with 'lua_module_'" 명시 추가. real Telegram에서는 잘 동작하지만, 본질적 해결은 v4의 `roll_chat_session` persistent history clear + skills_list 필터링 강화.

**원칙:** 새 path를 만들 때 old path를 "사용 안 하기"만으로는 부족함. catalog/registry/discovery 단에서 *실제로 제거*되어야 LLM이 우회 못 함. v3에선 prompt rule로 막았지만 architecture 차원은 아님.

## 다음에 비슷한 작업 할 때

- plan-review 2 라운드 + spec/code review만으로 충분하다고 단정하지 말기. *실 보드* 검증 사이클을 1주기 배정.
- 시뮬레이션 실패는 결론이 아니라 발견의 시작점. 실제 환경에서 다시 확인.
- gitignored 빌드 산출물도 plan의 사전 조건에 명시.
- NVS-영속 설정 default 변경은 마이그레이션 경로 같이 설계.
- "architecture 차원 차단"은 모든 layer를 *통과시켜* 검증. 1개 layer가 leak되면 전체 무력화.
- v2 surface 제거는 prompt rule이 아니라 catalog/registry 단에서 해야 진짜 차단.

## 참고

- v3 plan/spec: `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool.md`, `smarthome-docs/superpowers/specs/2026-05-08-cap-ha-control-typed-tool-design.md`
- v3 implementation learn log: `docs/learn/20260508-cap-ha-control-v3.md`
- v4 follow-up plan: `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`
- 완료 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-11.md`
- PR #1: https://github.com/JayMon0327/esp-claw-smarthome/pull/1 (MERGED)
- PR #2: https://github.com/JayMon0327/esp-claw-smarthome/pull/2 (this report)
