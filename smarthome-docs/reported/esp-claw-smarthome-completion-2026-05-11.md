# ESP-Claw 스마트홈 컨트롤 — 구현 완료 보고서 (2026-05-11)

> **상태:** 구현 완료. main에 merge됨 (PR #1, merge commit `7f14e69`).
> **검증:** 실 보드 (ESP32-S3 N16R8 clone) + 실 HA (192.168.1.94:8123) + 실 Telegram client에서 end-to-end 동작 확인.

---

## 한 줄 요약

ESP-Claw가 Telegram 자연어로 **Home Assistant 기기**와 **보드 내장 RGB LED**를 단일 typed tool `ha_control` 하나로 컨트롤하는 단계까지 완성됐다. 거짓 성공 / 멀티라운드 인자 누락 / 토큰 노출 / raw MCP 우회 같은 v2의 architecture 차원 위험은 모두 차단됐다.

---

## 지금 가능한 것

### 1. 보드 내장 RGB LED 컨트롤 (GPIO 48 WS2812, 보드에 직결)

| 동작 | Telegram 예시 | 결과 |
|---|---|---|
| 켜기 | "보드 RGB 켜줘" | 흰색으로 ON |
| 색상 + 켜기 | "보드 RGB 보라색으로" | 보라색 ON |
| 색상 + 밝기 + 켜기 | "보드 LED 빨간색 50%" | 50% 밝기 빨강 ON |
| 끄기 | "보드 LED 꺼" | OFF |

지원 색상: `yellow / red / green / blue / purple / white / orange / pink` (이름) 또는 `#rrggbb` (HEX). 밝기는 1–100%.

내부 동작: `cap_ha_color_to_rgb`가 색 이름/HEX를 RGB triple로 변환 → `brightness_pct`로 RGB scale → `led_strip_set_pixel`로 직접 송신 (HSV 우회). 호출당 RMT 핸들을 열고 닫아 자원 점유 없음.

### 2. Home Assistant 기기 컨트롤 (HA REST API)

| 도메인 | 가능한 동작 | Telegram 예시 |
|---|---|---|
| `light` (조명) | turn_on / turn_off / toggle, brightness 1–100%, color, kelvin 2000–6500 | "화장실 조명 노란색 60%로 켜줘", "화장실 등 꺼" |
| `cover` (커튼/블라인드) | open / close / toggle | "거실 커튼 닫아줘" |
| `switch` (콘센트/스위치) | turn_on / turn_off / toggle | "거실 콘센트 켜줘" |

요청 흐름: Telegram → cap_im_tg → LLM (gpt-5-mini) → ha_control(target, action, brightness_pct, color, kelvin) → cap_ha_control_core가 schema 검증 + entity resolve + service 매핑 → HA `POST /api/services/<domain>/<service>` → 200 응답 시 firmware가 합성한 한국어 메시지("화장실 조명 yellow 60%을(를) 켰습니다.")를 LLM이 **verbatim echo** → 사용자에게 도착.

### 3. 자연어 → 기기 매칭

3단계 cascade로 사용자가 말한 표현을 등록된 기기에 매칭:
1. entity_id 정확 일치 (예: `light.smart_bulb`)
2. friendly_name 정확 일치 (예: `화장실 조명`)
3. 정규화 일치 — 공백 제거 + 한국어 trailing particle (등/의/은/는/이/가/을/를/도) 제거 후 비교. "화장실 등" → "화장실등" → registry의 "화장실 조명" (정규화 후 "화장실조명")과 매칭 불가, 하지만 "화장실 등을"의 마지막 "을"이 제거돼 매칭됨.

LLM은 active devices 리스트를 tool description으로 받기 때문에 정확한 이름을 알고 있어서 일반적으로 stage 1/2에서 hit.

### 4. 동적 기기 발견 (boot-fetch)

부팅 직후 Wi-Fi가 올라오면 background task가 HA `/api/states`를 한 번 GET해서 light/cover/switch 도메인 기기를 필터링 → NVS `ha_ctl/entity_cache` blob에 저장. 다음 부팅부터는 정적 4개 + HA 발견 기기의 합집합이 active registry로 동작. 사용자가 HA에 새 기기를 등록하면 ESP-Claw 재부팅으로 자동 인식.

### 5. 거짓 성공 차단 (architecture 차원의 데모 guardrail)

v2의 가장 큰 문제 — 멀티라운드에서 LLM이 멋대로 "켰습니다"를 합성 — 가 architecture 차원에서 차단됨:
- firmware가 합성한 `message` 필드를 LLM이 verbatim echo (system prompt 강제)
- HA가 401을 반환하면 firmware가 `success:false, message:"HA 인증에 실패했습니다 (토큰 확인 필요)."`로 응답 → LLM이 그대로 사용자에게 전달
- LLM은 success/failure를 직접 판단하지 않음 — `success` 필드를 검사하고 메시지를 그대로 echo만 함
- 실측: invalid token 강제 후 "화장실 조명 켜줘" → Telegram에 정확히 "HA 인증에 실패했습니다" 도착, 거짓 "켰습니다" 0건.

### 6. 콘솔 디버그 (USB)

```
ha_control --call '<json>'             # LLM과 동일한 dispatch path 호출
ha_control --resolve <target>          # cascade lookup 디버그
ha_control --refresh-registry          # boot-fetch 수동 트리거
ha_control --set-url <url>             # HA URL을 NVS에 저장
ha_control --set-token <bearer_token>  # HA 토큰을 NVS에 저장
```

데모 준비 흐름: erase-flash → wizard로 Wi-Fi/LLM/Telegram만 입력 → 콘솔로 URL/token 입력 → 재부팅 → 동작.

---

## 다음 버전 (v4) 작업 예정

자동화 등록/해제 등 다음 단계 능력을 추가합니다. 직전 PR의 review에서 발견된 5개 follow-up 작업이 우선:

상세 plan: [`smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md`](../superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md)

| # | 우선순위 | 작업 |
|---|---|---|
| 1 | P1 | `s_cache_registry` refresh race 차단 (FreeRTOS mutex) |
| 2 | P2 | `#rrggbb` invalid hex 검증 (isxdigit) — 잘못된 hex가 silently red로 잡히는 문제 |
| 3 | P2 | `parse_registry` entity 개수 cap (64) — 악성/오설정 HA의 heap 고갈 방지 |
| 4 | P3 | boot-fetch 후 LLM description 재합성 — 동적 발견 기기가 다음 부팅까지 반영 안 되던 문제 |
| 5 | P3 | HTTPS 지원 + `--insecure` 플래그 — Bearer token이 LAN 평문 전송되던 문제 |

이외 자동화 / 추가 도메인 확장 계획:
- **자동화 등록/해제** (요청하신 항목): "화장실 조명을 매일 저녁 7시에 켜줘" 같은 시간 기반 자동화를 Telegram에서 생성/해제. 기존 `cap_scheduler` 인프라 위에 ha_control과 결합. cron 표현 자동 생성 + scheduler에 자동화 룰 등록 + 해제 인터페이스.
- **추가 HA 도메인:** climate (에어컨/난방), fan (선풍기), media_player (스피커/TV), scene (씬 호출)
- **HA secure NVS storage:** 토큰을 encrypted NVS 파티션에 저장
- **다중 기기 복합 명령:** "거실 다 꺼" 같은 부분 실패 처리
- **Setup wizard ha_url/ha_token 필드 통합:** 현재 console-only 입력을 wizard로 단일화 (NVS namespace 통합 필요)
- **`roll_chat_session` persistent history clear:** 세션 히스토리 영구 저장이 reboot으로도 안 지워지는 firmware-wide 이슈

---

## 이번에 구현/수정된 내역 (PR #1, 27 commits)

### 신규 component `cap_ha_control` (13 파일)

```
components/claw_capabilities/cap_ha_control/
├── CMakeLists.txt              # idf_component_register + EMBED_TXTFILES
├── Kconfig                     # CAP_HA_CONTROL_BOOT_FETCH_ENABLED 토글
├── idf_component.yml           # espressif/led_strip ^3.0.3 dependency
├── data/entities.default.json  # 정적 registry 4 entries (화장실 조명, 거실 커튼, 거실 콘센트, 보드 RGB)
├── include/
│   ├── cap_ha_control.h        # cap_ha_control_register_group()
│   └── cmd_cap_ha_control.h    # cmd_cap_ha_control_register()
└── src/
    ├── cap_ha_control_internal.h    # typedefs (cap_ha_entity_t / cap_ha_registry_t), NVS macros, 함수 prototypes
    ├── cap_ha_control.c             # descriptor + group_init + dynamic description compose
    ├── cap_ha_control_core.c        # schema validate + dispatch + 한국어 메시지 합성 + 색 테이블
    ├── cap_ha_control_resolve.c     # registry parse + 3-stage cascade + NVS cache + boot_fetch_task
    ├── cap_ha_control_http.c        # esp_http_client + Bearer + NVS url/token
    ├── cap_ha_control_board.c       # GPIO 48 WS2812 직접 제어 + brightness scaling
    └── cmd_cap_ha_control.c         # argtable3 콘솔 (--call/--resolve/--refresh-registry/--set-url/--set-token)
```

### 기존 component 수정 (4 파일)

- **`components/common/app_claw/`** (4 파일): `APP_CLAW_CAP_HA_CONTROL` Kconfig 토글 + CMakeLists 조건부 REQUIRES + idf_component.yml 조건부 path + app_capabilities.c 양쪽 dispatch table (`s_capability_group_entries` + `s_capability_group_infos`) 등록.
- **`components/common/app_claw/app_claw.c`** `APP_SYSTEM_PROMPT_COMMON`: verbatim echo 룰 + ha_control-only 룰 + lua_module_* skill activation 차단 룰 추가.
- **`components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c`**: 빈 `arguments` reject (cJSON 기반) + 응답 content text의 실패 marker 감지 시 `isError:true` 강제 — v2 거짓 성공의 1차 source 차단.
- **`components/claw_modules/claw_cap/src/claw_cap.c`**: `CLAW_CAP_TOOL_DESCRIPTION_MAX` 256 → 1024. 동적 active devices 리스트 + verbatim echo 룰이 잘려 LLM에 미도달하던 문제 해결.

### Configuration

- **`application/edge_agent/sdkconfig.defaults`**: `CONFIG_APP_CLAW_CAP_HA_CONTROL=y` + `CONFIG_CAP_HA_CONTROL_BOOT_FETCH_ENABLED=y` 핀.
- **`application/edge_agent/components/app_config/app_config.c`**: `APP_DEFAULT_LLM_VISIBLE_CAP_GROUPS` = `"cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system"` — LLM이 typed surface만 보고 raw `cap_lua` / `cap_mcp_client`는 enabled but invisible.

### 문서 (plan / spec / learn / v4 plan)

- `smarthome-docs/superpowers/specs/2026-05-08-cap-ha-control-typed-tool-design.md` — v3 design
- `smarthome-docs/superpowers/plans/2026-05-08-cap-ha-control-typed-tool.md` — 17 task implementation plan (plan-reviewer 2 라운드 13 issues 반영)
- `smarthome-docs/superpowers/plans/2026-05-11-cap-ha-control-v4-followups.md` — v4 follow-up plan (5 tasks)
- `docs/learn/20260508-cap-ha-control-v3.md` — on-board 검증 결과 + 발견된 firmware 버그 4개 + Telegram E2E user 검증
- `docs/learn/20260508-cap-ha-control-plan-review-revision.md` — plan-reviewer 2 라운드 학습 정리
- `docs/learn/20260507-*.md` — Wi-Fi/HA/RGB/CLI/MCP 디버깅 과정 학습 정리 (구현 전 단계)

### On-board 검증 중 발견 + 즉시 fix한 firmware 버그 4개

| Commit | 버그 | 영향 |
|---|---|---|
| `b8655f6` | `app_claw.c` system prompt seam 공백 누락 (MEMORY_FULL 빌드) + `err_root` NULL 체크 부재 (OOM 시 NULL deref 위험) | code review 자동 발견, ship 전 fix |
| `ed2280c` | stub `snprintf` NULL/zero-size 버퍼 guard 누락 | code review 자동 발견 |
| `32f527c` | `cap_ha_http_post_service` / `_get_states`의 `char auth_header[4128]`이 6KB boot-fetch task 스택 overflow → `Backtrace: \|<-CORRUPTED` + `rst:0xc` 무한 reboot | on-board에서 발견 (실 보드 없었으면 못 잡았을 critical bug). heap으로 이전. |
| `df783ea` | esp_http_client가 HTTP 401을 `ESP_ERR_NOT_SUPPORTED`로 반환 — `cap_ha_compose_failure_message`의 분기 순서가 http_err을 먼저 검사해 401 메시지가 "network err"로 가려졌음 | **v3 데모 guardrail 중 가장 중요한 fix.** invalid token → "인증 실패" verbatim 흐름이 이 fix로 완성. |

### 환경 발견사항

- **`memory/project_worktree_build_env.md`**: `.claude/worktrees/develop` worktree에서 첫 빌드 전 main worktree의 `application/edge_agent/components/gen_bmgr_codes/gen_*` + `sdkconfig` (FLASHSIZE_16MB) 를 복사해야 함. 둘 다 gitignored 생성 산출물이고 `bmgr_patch.py`는 *생성*하지 않고 *patch*만 함.
- **NVS `vis_cap_groups` stale 값**: 기존에 wizard로 설정된 보드는 NVS에 v2 시점 default가 박혀 있어 새 default가 적용 안 됨. 첫 부팅 시 web 설정 API (`POST http://<board_ip>/api/config`) 또는 erase-flash 필요.

---

## 검증 요약

| 검증 항목 | 결과 | 출처 |
|---|---|---|
| `idf.py build` clean | ✅ | 모든 commit에서 |
| 보드 boot — cap_ha_control registered, 4 static entities loaded | ✅ | 직접 monitor 캡처 |
| Boot-fetch HA `/api/states` → kept 2 entities NVS_OK | ✅ | monitor 로그 (status=200, resp_len=51868) |
| HA REST light.smart_bulb yellow 60% → 실제 램프 ON | ✅ | 실 보드 + 실 HA |
| 보드 RGB purple/red 50%/off → 실제 LED 변화 | ✅ | 직접 확인 |
| Schema validation reject (target/action/brightness/kelvin out-of-range) | ✅ | 콘솔 8 시나리오 |
| False-success block (invalid token → 401 → 인증 실패 verbatim) | ✅ | 토큰 강제 변경 후 확인 |
| Telegram NL E2E (실 client `/start` 후 HA + 보드 RGB) | ✅ | user-verified |
| Pre-landing review (5 findings → v4 plan) | ✅ | `/review` 실행 |

---

## PR & main 상태

- PR #1: https://github.com/JayMon0327/esp-claw-smarthome/pull/1 (MERGED 2026-05-11)
- Merge commit on main: `7f14e69`
- Total: 27 commits, 26 files, +4934 / -5 lines
- v4 follow-up plan ready to execute (Tasks 1–3 묶음으로 ~70분 CC effort)
