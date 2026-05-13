# OpenClaw vs ESP-Claw — 사전 구현과의 비교 분석 (2026-05-13)

> **목적:** esp-claw 진입 전에 사용자가 약 1.5달 동안 구축한 **OpenClaw 기반 스마트홈** (Raspberry Pi 5 위 application layer 구현) 과 esp-claw (펌웨어 embedded 구현) 의 layer-by-layer 비교. 같은 사용자 시나리오 (Telegram 자연어 → 자동화/제어) 를 두 다른 호스트에서 어떻게 풀었는지, trade-off 와 적합 시나리오를 정리.

- **자매 문서:** [`01-esp-claw-current-architecture.md`](./01-esp-claw-current-architecture.md)
- **OpenClaw repo:** `~/Desktop/openclaw-smarthome`
- **ESP-Claw repo:** 이 저장소 (`JayMon0327/esp-claw-smarthome`)

---

## 0. TL;DR

두 시스템 모두 "Telegram 자연어 → LLM tool call → Home Assistant → 한국어 응답" 같은 시나리오를 푼다. 그러나 **agent 가 어디서 동작하는가, agent 학습 자료가 어떤 형식인가, HA 와 어떻게 통신하는가** 가 근본적으로 다르다.

| 한 줄 요약 | OpenClaw | ESP-Claw |
|---|---|---|
| **Agent 호스트** | Raspberry Pi 5 (Linux) — Claude Code 같은 application-layer agent runtime | ESP32-S3 보드 (FreeRTOS) — firmware-embedded OpenAI REST client |
| **Agent 학습 형식** | markdown directory tree (`workspace/AGENTS.md`, `SOUL.md`, `TOOLS.md`, `smart-home/...`) | C 코드 + JSON Schema literal + 동적 보간 텍스트 (`cap_ha_control.c`, `cap_ha_compose_description`) |
| **HA 통신** | HA 공식 MCP Server (Streamable HTTP) via `.mcp.json` | HA Core REST API 직접 호출 (`/api/services/...`, `/api/states`, `/api/config/automation/config/...`) |
| **Update cycle** | git pull + Claude Code session restart (또는 `deploy/deploy.sh` rsync) | `idf.py build && idf.py app-flash` (USB) |
| **Always-on 조건** | Pi 5 전원 + Linux booted + Claude Code session active | 보드 USB 전원만 (외부 host 불필요) |

핵심 trade-off: **OpenClaw 가 풀 수 있는 정책 분기의 복잡도가 훨씬 크고 (markdown 으로 stage 분기 / safety 등급 / suggestion / candidate 등 자유 정의), ESP-Claw 는 self-contained 한 단일 보드 demo 의 단순함과 운영 부담 0** 이라는 점.

---

## 1. OpenClaw 아키텍처

### 1.1 3-Plane 분리 (product.md 의 핵심 결정)

```
┌──────────────────────────────────────────────────────────────────┐
│ 개발 Plane (Claude Code + Opus 계열)                              │
│   workspace 작성, 정책 markdown, skills 작성, deploy 스크립트     │
│   ← 고성능 모델 집중 사용 시점                                     │
└──────────────────────────────────────────────────────────────────┘
                          │ git push + deploy/deploy.sh (rsync over SSH)
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│ 운영 Plane (OpenClaw on Raspberry Pi 5)                          │
│   pi_01a_bot — 스마트홈 비서 (Telegram 수신, 자연어 해석,         │
│                           승인 흐름, cron, heartbeat)            │
│   pi_02a_bot — 분석 비서 (event_history.ndjson 분석)            │
│   ← 저비용 모델 (gpt-5-mini / haiku 4.5 등) cascade 사용         │
└──────────────────────────────────────────────────────────────────┘
                          │ MCP (Streamable HTTP)
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│ 실행 Plane (Home Assistant + HA MCP Server)                       │
│   기기 상태 원본 저장, 제어, 자동화 실행                           │
└──────────────────────────────────────────────────────────────────┘
```

**의도:** 단일 AI runtime 에 모든 책임 몰지 않음. 개발/운영/실행 책임 분리. 운영 단계에서는 저비용 모델 안정성 우선.

### 1.2 OpenClaw 디렉토리 구조

```
~/Desktop/openclaw-smarthome/
├── workspace/                       ★ Agent definition + runtime state (Pi 가 직접 사용하는 폴더)
│   ├── AGENTS.md                    역할 정의: 무엇을 하고 무엇을 안 하는지
│   ├── SOUL.md                      응답 스타일: "최종 응답 1개만" 등 엄격한 OUTPUT DISCIPLINE
│   ├── TOOLS.md                     도구 사용 규칙: MCP 호출, 파일 read/write, n8n webhook
│   ├── HEARTBEAT.md                 30분 cron 점검 절차 (저비용 모델 cascade 사용)
│   ├── memory.md                    영구 운영 기억 (markdown 직접 수정)
│   ├── event_history.ndjson         모든 MCP 호출 로그 (NDJSON append-only)
│   ├── automation_candidates.ndjson 자동화 후보 제안 history
│   ├── candidate_cooldown.json      candidate 제안 쿨다운 state
│   ├── last_suggestion.json         최근 선제 제안 (Stage 3 진입 trigger)
│   ├── occupancy_tracker.json       재실 감지 state
│   ├── state_snapshot.json          마지막 HA 상태 snapshot
│   ├── suggestion_cooldown.json
│   ├── demo_scenarios.md
│   │
│   ├── skills/                      ★ Claude Code-style skill collection
│   │   ├── smarthome-automation/    자동화 등록 워크플로우 (SKILL.md + 보조)
│   │   ├── smarthome-mvp/           MVP 시연 스킬
│   │   ├── smarthome-n8n/           n8n webhook 통합
│   │   ├── smarthome-status/        상태 조회
│   │   ├── smarthome-suggest/       선제 제안
│   │   ├── demo-scene1/             데모 시나리오 1
│   │   ├── demo-scene2/             데모 시나리오 2
│   │   └── demo-scene-stage4/       Stage 4 candidate 처리
│   │
│   └── smart-home/                  ★ 정책 + 메타데이터 (markdown / yaml / json)
│       ├── common/
│       │   ├── safety_rules.md      안전 등급 1-3 (lock.unlock 같은 위험 액션 분류)
│       │   ├── entity_aliases.yaml  한국어 표현 → domain 힌트 (light/switch/cover 등)
│       │   ├── room_map.yaml        방 별칭 → area_id 매핑
│       │   ├── output_rules.md      응답 구성 규칙 (멀티-기기/방별/도메인별 응답)
│       │   ├── entity_discovery.md  동적 entity 발견 절차
│       │   └── mcp_contract.md      HA MCP 호출 규약
│       ├── stage1-control/          Stage 1: 직접 제어 (service_policies + nl_examples)
│       │   ├── service_policies.md
│       │   ├── nl_examples.md
│       │   └── bedtime_routine.md   취침 자연어 ("나 잘게")
│       ├── stage2-query/            Stage 2: 상태 조회 (query_policies.md)
│       ├── stage3-suggestion/       Stage 3: 선제 제안 (agent_response_flow.md)
│       ├── stage4-candidate/        Stage 4: 자동화 후보 분석 + 제안 (agent_handlers.md)
│       ├── stage5-management/       Stage 5: 운영 관리
│       └── shared/
│           └── entity_inventory.json 모든 entity_id list (HA 에서 동기화)
│
├── config/openclaw.json             OpenClaw 중앙 설정 (placeholder → 배포 시 치환)
│
├── deploy/                          Pi 배포
│   ├── deploy.sh                    rsync over SSH (.env 의 PI_HOST, PI_USER, PI_PASS 사용)
│   ├── credentials/.env             secrets (PI 접속, HA token, n8n key, owner Telegram ID 등)
│   ├── post-deploy-check.sh
│   ├── setup-ha-webhooks-2026-04-06.sh
│   └── ha-automation-webhook-2026-04-06.yaml
│
├── n8n/                             n8n workflows (HA webhook + 보조 자동화)
├── n8n-workflows/                   n8n workflow JSON exports
│
├── scripts/
│   ├── demo/                        데모 시나리오 실행 스크립트
│   └── lint/                        prompt size 회귀 가드
│       └── check-prompt-size.sh     workspace 의 markdown 크기 체크 (배포 차단 X, 경고만)
│
├── skills-lock.json                 skills 버전 lock
├── product.md                       프로젝트 design 문서 (38 KB, 핵심 정책)
├── README.md                        TL;DR + 아키텍처 다이어그램
├── CLAUDE.md                        Claude Code project-level instruction (8.5 KB)
├── .mcp.json                        MCP server 정의 (n8n-mcp, home-assistant 등)
├── .agents/                         agent metadata
└── .claude/                         Claude Code session config
```

### 1.3 OpenClaw 의 핵심 파일 분석 (수정한 파일 = 다 만든 파일)

OpenClaw 는 esp-claw 처럼 "기존 fork 에 컴포넌트 add" 가 아님. **처음부터 직접 작성한 workspace + 정책 + skill 콜렉션**. 핵심 파일 4개만 짚는다.

#### 1.3.1 `workspace/AGENTS.md` (3.1 KB) — 역할 정의

```markdown
# 에이전트 정의: 스마트홈 비서 (pi_01a_bot)

## 역할
나는 스마트홈 운영 비서다. 사용자의 자연어 명령을 해석하고,
Home Assistant MCP Server를 통해 기기를 제어하며, 결과를 한국어로 응답한다.

## 책임
- Telegram/Control UI에서 사용자 메시지 수신 및 응답
- 자연어 해석 → HA MCP 도구 호출 → 결과 자연어 변환
- 위험 액션에 대한 승인 요청
- 모호한 명령에 대한 재질문
- 모든 실행 결과를 `event_history.ndjson`에 기록

## 절대 하지 않는 것
- 코드 작성, 파일 시스템 변경 (workspace 내 memory/log 제외)
- Flask API 사용
- 개발 작업 수행
- 사용자 승인 없이 위험 액션 실행
- MCP 도구 외 경로로 HA 제어

## 단계별 분기 routing (메시지 패턴 → 진입 reference)

| 메시지 패턴 | 진입 reference |
|-------------|----------------|
| 단순 제어 ("거실 불 켜줘") | smart-home/stage1-control/service_policies.md |
| 취침 자연어 ("나 잘게")    | smart-home/stage1-control/bedtime_routine.md |
| 상태 조회 ("거실 상태")    | smart-home/stage2-query/query_policies.md |
| 선제 제안 응답           | smart-home/stage3-suggestion/agent_response_flow.md |
| 자동화 후보 제안/응답     | smart-home/stage4-candidate/agent_handlers.md (4-A) |
| 자동화 등록 ("~하면 ~해줘") | skills/smarthome-automation/SKILL.md |
```

**핵심:** 메시지 패턴별로 다른 markdown 진입점을 routing 표로 명시. agent 가 매 메시지마다 이 표를 보고 어느 정책 markdown 을 read 할지 결정.

#### 1.3.2 `workspace/SOUL.md` — 응답 스타일 (강제 OUTPUT DISCIPLINE)

```markdown
# 응답 스타일

## ⛔ 출력 규율 (OUTPUT DISCIPLINE — 최상위 규칙, 모든 규칙보다 우선)

사용자에게 보이는 메시지는 **최종 결과만** 포함한다. 사고 과정·내부 처리·
파일 read·정책 근거·도구 이름·변수·경로 등을 **절대** 출력하지 않는다.

### 한 사용자 메시지 = 최종 응답 1개만
- 허용: 인사 / 확인 / 결과 보고 / 재질문 / 경고 중 **최종 메시지 1개**
- 금지: 중간 진행, 내부 상태, 판단 근거, 파일 확인, 도구 호출 설명 — **0개 출력**

### 절대 출력 금지 예시 (위반 시 UX 재앙)
❌ "last_suggestion.json 확인 중..."
❌ "AGENTS.md 3-B 규칙에 따라..."
❌ "entity_inventory.json에서 동적 발견 후..."
```

**핵심:** Claude Code-style agent 가 chain-of-thought 를 그대로 사용자에게 보일 수 있는 문제를 markdown rule 로 막음. esp-claw 에는 동등한 layer 가 없음 (LLM 의 응답 그대로가 사용자에게 가는 architecture).

#### 1.3.3 `workspace/smart-home/common/safety_rules.md` — 안전 등급 3-tier

```markdown
# 안전 규칙

## 등급 1: 절대 자동실행 금지 (승인 필수)
- lock.unlock (도어락 해제)
- 보안 모드 해제 (alarm_control_panel.disarm)
- 대량 일괄 제어 (3개 이상 엔티티 동시)
- 외출 모드 중 출입 관련

## 등급 2: 확인 후 실행
- 간접 표현 ("어둡다", "춥다")
- 대상 불명확 ("불 켜줘" - 어느 방?)
- 야간 시간대 (23:00–06:00) 소음/빛 발생
- 고온 설정 (난방 28도 이상)

## 등급 3: 자동 실행 허용
- 명확한 단일 대상 + 직접 명령 ("거실 불 켜줘")
- 상태 조회
- lock.lock (잠금은 안전 방향이므로 OK)

## Cooldown 정책
- 같은 entity + 같은 action + 5초 이내 → 무시
- 같은 entity + 반대 action + 10초 이내 → 확인 재질문
```

**핵심:** safety 정책이 markdown 으로 *외부화*. agent 가 매 메시지마다 이 markdown 을 read + 따른다. esp-claw 의 LLM 은 이런 safety layer 없이 직접 tool call.

#### 1.3.4 `workspace/HEARTBEAT.md` — 30분 cron 점검

```markdown
# Heartbeat 점검 목록

모델: 저가형 모델 (우선순위: gpt-5.4 → gpt-5.3-spark → haiku4.5 → gpt5-mini)
주기: 30분

## 점검 항목
1. HA MCP Server 연결 확인 — 간단한 상태 조회 1회
2. 최근 실패 명령 확인 — event_history.ndjson 마지막 5건
3. 실패가 있으면 Telegram으로 간단 알림

## 금지 사항
- 기기 제어 액션 수행 금지
- 복잡한 분석이나 장문 생성 금지
- 대용량 파일 읽기 금지

## 응답 형식
문제 없음: HEARTBEAT_OK
문제 있음: "⚠️ [문제 요약]"
```

**핵심:** **모델 cascade** — 비싼 모델 (Opus/gpt-5.4) 는 개발 시점에 쓰고, 운영 30분 heartbeat 은 저비용 모델. esp-claw 는 단일 모델 (gpt-5-mini) 만 사용.

### 1.4 OpenClaw 의 agent 학습 방법

OpenClaw 에서 "에이전트 학습" 은 layer 분리 없는 단일 layer — **workspace markdown 시리즈를 agent runtime 이 read** 하는 게 학습의 전부.

```
사용자 메시지 도착
    │
    ▼
agent (Claude Code-style on Pi)
    │
    ├──▶ TOOLS.md (매 메시지마다 우선순위 규칙 적용)
    ├──▶ AGENTS.md (routing 표로 진입 reference 결정)
    │       │
    │       ▼
    │   stage1-control / stage2-query / stage3-suggestion / stage4-candidate / skill
    │       │
    │       ▼
    │   해당 markdown read + 정책 적용
    │
    ├──▶ smart-home/common/safety_rules.md (위험 분류)
    ├──▶ smart-home/common/entity_aliases.yaml (자연어 → domain 매핑)
    ├──▶ smart-home/shared/entity_inventory.json (실제 entity_id list)
    ├──▶ memory.md (영구 기억)
    │
    ▼
MCP 도구 호출 (home-assistant MCP server) 또는 n8n webhook
    │
    ▼
결과 + SOUL.md 의 OUTPUT DISCIPLINE 적용 → 사용자 응답 1개
    │
    ▼
event_history.ndjson 에 NDJSON append (응답 후)
```

**학습 = 정책 markdown 수정.** 새 시나리오 추가하려면 새 markdown 작성 + AGENTS.md 의 routing 표에 한 줄 추가. 즉시 반영 (Claude Code session 다음 turn 부터). 빌드 / 플래시 / 재부팅 불필요.

**Update cycle:**
```bash
# 개발 환경 (Mac)
vim workspace/smart-home/stage1-control/nl_examples.md     # 정책 수정
git add . && git commit -m "..." && git push

# Pi 에 deploy
cd deploy/
./deploy.sh                          # rsync over SSH, 약 5-15초
# Pi 측 Claude Code session 이 알아서 새 파일 read (또는 session restart)
```

---

## 2. ESP-Claw 아키텍처 (요약 — 자세한 건 `01-esp-claw-current-architecture.md`)

### 2.1 2-Plane (개발 + 통합 운영-실행)

```
┌────────────────────────────────────────────────────────────┐
│ 개발 Plane (Claude Code on Mac)                            │
│   plan / 구현 / review / commit / PR                       │
└────────────────────────────────────────────────────────────┘
                  │ git push + idf.py build + flash (USB)
                  ▼
┌────────────────────────────────────────────────────────────┐
│ 운영-실행 Plane (ESP32-S3 보드 안 통합)                      │
│   claw_core worker — OpenAI gpt-5-mini REST client          │
│   cap_ha_control — HA Core REST 직접 호출                   │
│   cap_im_tg — Telegram bot                                  │
│   cap_mcp_server — 외부 호출 받는 MCP 서버 (mDNS)            │
│   ↑ 모든 게 firmware 안에 통합. 별도 host 없음.              │
└────────────────────────────────────────────────────────────┘
                  │ Wi-Fi STA
                  ▼
              Home Assistant (Pi 5, 192.168.1.94:8123)
```

### 2.2 ESP-Claw 의 agent 학습 (layer A + B)

자세한 건 자매 문서 §5. 핵심:
- **Layer A (보드 내부 LLM)**: capability descriptor + JSON Schema literal + `cap_ha_compose_description` 의 friendly_names 보간. 변경 → `idf.py app-flash` → 다음 메시지부터 적용.
- **Layer B (Claude Code 하네스, 개발 사이클)**: plan / learn / report markdown 시리즈 + subagent-driven-development + enforce-learn-log hook.

---

## 3. Layer-by-Layer 비교 표

| 항목 | OpenClaw | ESP-Claw |
|---|---|---|
| **Agent 호스트** | Raspberry Pi 5 (Linux, always-on) | ESP32-S3 보드 (FreeRTOS, USB 전원 만) |
| **Agent runtime** | Claude Code 같은 agent harness (또는 동등 application) | firmware 안 `claw_core` worker + OpenAI REST client |
| **Agent 학습 형식** | markdown / yaml / json directory tree | C 코드 + JSON Schema literal + 동적 보간 |
| **학습 변경 cycle** | git push + deploy.sh rsync (5-15초) | `idf.py build` + `idf.py app-flash` (1-2분) |
| **사용자 진입점** | Telegram bot (Pi 측, single bot per user) | Telegram / Web IM / QQ / Feishu / WeChat (보드 안 multi-channel) |
| **LLM provider** | gpt-5.4 / gpt-5.3-spark / haiku 4.5 / gpt-5-mini (cascade) | gpt-5-mini (단일) |
| **모델 cost 최적화** | heartbeat 은 저가형, 자연어 해석은 중급 (명시적 cascade) | 단일 모델 (cost 균일) |
| **HA 통신** | HA 공식 MCP Server (Streamable HTTP) via `.mcp.json` | HA Core REST API 직접 (`/api/services/...`, `/api/states`, `/api/config/automation/config/...`) |
| **자동화 등록 path** | `skills/smarthome-automation/SKILL.md` 워크플로우 + (필요 시) n8n webhook | `cap_ha_automation.c` 의 typed tool → HA REST PUT `/api/config/automation/config/<id>` |
| **State 저장** | workspace 내 markdown / json / ndjson (file system) | NVS 파티션 (binary key-value) + storage 파티션 (FAT) |
| **영구 메모리** | `memory.md` (markdown 직접 수정 가능) | (없음 — entity 캐시만 NVS) |
| **Safety policy layer** | `smart-home/common/safety_rules.md` (등급 1-3 명시) | (없음 — LLM 가 자체 판단) |
| **Output style 강제** | `SOUL.md` 의 OUTPUT DISCIPLINE (chain-of-thought 차단) | (별도 layer 없음 — LLM 의 응답 그대로) |
| **Stage 분기** | stage1-5 markdown directory + AGENTS.md routing 표 | (없음 — single LLM call + multiple tools) |
| **Heartbeat / cron** | `HEARTBEAT.md` 30분 cron + 저가 모델 cascade | (없음 — 보드 부팅 시 boot-fetch 1회) |
| **Logging** | `event_history.ndjson` (NDJSON append) | ESP_LOGI (시리얼) — 영구 저장 없음 |
| **Suggestion / Candidate** | Stage 3 / Stage 4 markdown + state json | (없음 — v6 후보) |
| **Update without restart** | markdown 수정 → 다음 메시지부터 반영 (선택적 restart) | firmware flash 후 보드 재부팅 |
| **Always-on 의존성** | Pi 5 + Linux + Claude Code session + network | 보드 + USB 전원 + Wi-Fi |
| **단일 장애점** | Pi 다운 시 전체 정지 | 보드 다운 시 전체 정지 (단 Pi 의존성 없음) |
| **개발 cost (1회성)** | Pi 5 + 셋업 (Linux + Claude Code 환경) | ESP32-S3 보드 (~1만 원) + ESP-IDF 설치 |
| **운영 cost (월간)** | Pi 전기 + Claude API (저가형 cascade) + n8n | 보드 전기 + OpenAI API (gpt-5-mini) |

---

## 4. 두 접근의 핵심 차이 — 4 dimension

### 4.1 "Prompt 학습"의 표현 형식

- **OpenClaw**: markdown / yaml / json. 사람이 직접 읽고 수정. agent 가 read-time 에 동적 적용.
- **ESP-Claw**: C 코드 + JSON Schema literal. 사람이 코드로 수정. 컴파일 후 firmware 안에 박힘. 동적 부분은 `snprintf` 의 `%s` 자리에 runtime 보간 정도.

**비유:** OpenClaw 는 "agent 가 매 메시지마다 정책 reference 를 봄"; ESP-Claw 는 "agent 학습 자료를 firmware 에 컴파일해 박음".

### 4.2 Policy layer 의 두께

OpenClaw 가 압도적으로 두꺼움.
- safety_rules.md (안전 등급 3-tier + cooldown)
- output_rules.md (멀티-기기 응답 구성)
- SOUL.md (output discipline)
- stage1-5 분기 정책

ESP-Claw 는 두꺼운 정책 layer 없이 LLM (gpt-5-mini) 의 자체 판단에 의존. **그래서 v5 핫픽스 같은 어휘 mismatch 가 발생 가능** — OpenClaw 에선 entity_aliases.yaml 의 한국어 매핑이 이 문제를 reduce.

### 4.3 HA 통신 layer

- **OpenClaw**: HA 공식 MCP Server. `.mcp.json` 에 정의된 HA MCP endpoint + n8n-mcp (n8n workflow 와의 연결). MCP 가 추상화 layer.
- **ESP-Claw**: HA Core REST 직접 호출 (`cap_ha_control_http.c`). 중간 layer 없음. 모든 path 와 body 구조 firmware 가 직접 알고 있음 (예: PUT `/api/config/automation/config/<id>` + `{triggers, conditions, actions}` JSON).

**ESP-Claw 가 REST 직접 호출하는 이유:** ESP32-S3 의 MCP client 가 LLM 의 tool calling 과 별도 layer 라 추가 복잡도 + 메모리 비용. 그리고 HA Core REST 가 안정적이고 잘 documented 되어 있음. 다만 HA 가 schema 를 바꾸면 (예: v4 의 plural keys 변경) 우리 firmware 가 따라가야 함.

### 4.4 Update / deploy cycle

- **OpenClaw**: `deploy/deploy.sh` 의 rsync over SSH. 5-15초. Pi 측 Claude Code 의 다음 turn 부터 새 markdown read. 변경의 80% 가 markdown 이라 빠른 iteration.
- **ESP-Claw**: `idf.py build` (1-2분, 변경 양에 따라) + `idf.py app-flash` (30-60초) + 보드 재부팅 (5초). 변경의 100% 가 C 코드라 build cycle 필수. 다만 `app-flash` 가 storage/NVS 보존해서 entity 캐시는 그대로 (재학습 불필요).

---

## 5. 같은 시나리오 두 시스템에서의 path 비교

**시나리오:** 사용자가 "현관문 도어센서가 열렸을 때 화장실 조명 켜주는 자동화 등록해줘" 라고 Telegram 에 보냄.

### 5.1 OpenClaw path

```
1. Telegram → pi_01a_bot (workspace 의 Claude Code-style agent)
2. agent: TOOLS.md → AGENTS.md routing 표 매칭
3. "자동화 등록" 패턴 → skills/smarthome-automation/SKILL.md 진입
4. SKILL.md 의 워크플로우:
   - entity_inventory.json 에서 "현관 도어센서" → entity_id resolve
   - entity_aliases.yaml 에서 "조명" → light domain hint
   - safety_rules.md 등급 분류 (등급 3 → 자동 실행 가능)
5. HA MCP server tool 호출 (또는 n8n webhook):
   → POST /api/services/automation/create-by-spec (또는 HA UI YAML 작성)
6. event_history.ndjson 에 NDJSON append
7. SOUL.md 의 OUTPUT DISCIPLINE 적용 → 사용자에게 "자동화 등록됐어요" 응답
```

### 5.2 ESP-Claw path

```
1. Telegram → cap_im_tg (firmware) → event_router → claw_core worker
2. OpenAI gpt-5-mini 에 tool descriptors[] 와 함께 호출
3. LLM 이 ha_automation tool call 결정:
   {action:"create", target:"화장실 조명", device_action:"turn_on",
    trigger:{kind:"state", entity:"현관문 도어센서", to:"open"}}
4. cap_ha_automation.c 의 do_create:
   - cap_ha_resolve_target("현관문 도어센서") → binary_sensor.smart_door_sensor_mun
   - normalize_state_value("binary_sensor", "open") → "on"          ← v5 핫픽스
   - opposite_state("binary_sensor", "on") → "off"                   ← v5 Task 1
   - build_ha_trigger_array → {platform:"state", entity_id:..., to:"on", from:"off"}
   - HA REST PUT /api/config/automation/config/esp_claw_<id>
   - HA reload
5. 응답: "'화장실 조명' turn_on 자동화를 등록했습니다 (ID: ..., config_id: esp_claw_<id>)"
6. LLM 이 응답 message 받아서 verbatim 으로 사용자에게 전달
```

### 5.3 차이 정리

| 단계 | OpenClaw | ESP-Claw |
|---|---|---|
| Entity resolve | entity_inventory.json + entity_aliases.yaml (markdown read) | cap_ha_resolve_target (C 함수 + NVS 캐시) |
| Safety check | safety_rules.md 등급 분류 (markdown read) | (없음) |
| LLM 어휘 보호 | entity_aliases.yaml 한국어 매핑 + SKILL.md | normalize_state_value (firmware, v5 핫픽스) |
| HA 호출 | HA MCP server tool | HA Core REST 직접 |
| 응답 layer | SOUL.md 적용 후 사용자 | LLM 의 응답 verbatim |
| Logging | event_history.ndjson append | ESP_LOGI (시리얼만, 영구 저장 X) |

---

## 6. Trade-off 분석 — 어느 상황에 어느 쪽?

### 6.1 OpenClaw 가 강한 시나리오
- **복잡한 stage 정책**: Suggestion / Candidate / Stage 4 분석 같은 다단계 자동화 분석. markdown 으로 정책 추가 자유로움.
- **모델 cascade**: 비용 최적화 (heartbeat 은 haiku, 자연어 해석은 gpt-5-mini, 복잡한 분석은 gpt-4o 등).
- **이미 Pi 가 있는 환경**: HA / HomeBridge / Plex 같은 다른 home server 와 동거 가능.
- **빠른 정책 iteration**: 정책 변경 = markdown 수정 + push + 5-15초 deploy.
- **영구 로그 + 분석**: event_history.ndjson, pi_02a_bot 같은 분석 봇.

### 6.2 ESP-Claw 가 강한 시나리오
- **Self-contained 단일 보드**: Pi / 별도 host 없음. USB 전원만으로 동작. 시연 / 배포 단순.
- **임베디드 특수 기능**: 보드의 RGB LED, 센서, 직접 GPIO 제어 같은 hardware-near 기능. Pi 보다 보드가 가까이 둘 수 있음.
- **항상-on**: 부팅 5초, 재부팅 안정성, 시스템 update 부담 0.
- **Multi-channel out of the box**: Telegram + Web IM + QQ + Feishu + WeChat 가 esp-claw 의 capability 들로 다 들어가 있음.
- **MCP server 호스팅**: mDNS 통해 보드 자체가 MCP 서버. 외부 도구 (예: Mac 의 Claude Code) 가 보드를 controllable resource 로 사용.

### 6.3 둘 다 어려운 시나리오
- **두꺼운 안전 정책 + 임베디드 통합**: ESP-Claw 에 safety_rules.md 같은 layer 를 추가하려면 markdown 파일을 storage 파티션에 두고 runtime 에 read 하는 새 capability 필요. 가능하지만 v6 작업.
- **5개 이상 사용자 + 멀티 세션**: 두 시스템 모두 single-user 디자인. 멀티 사용자는 별도 channel mapping 필요.

### 6.4 Hybrid 가능성 (v6 후보)

esp-claw 와 openclaw 를 결합할 수 있나? 가능한 한 path:
- **esp-claw (보드) + openclaw (Pi)**: 보드는 Telegram + 보드 자체 기능. Pi 의 복잡한 정책 / 분석은 openclaw 가 처리. 보드 ↔ Pi 는 MCP / REST 로 연결.
- **esp-claw 의 markdown skill**: ESP-IDF 의 FAT filesystem 파티션 (storage) 에 markdown 정책 파일 두고 runtime 에 read. Lua 스킬 같은 패턴 이미 있음 (`application/edge_agent/main/lua_scripts/`).

이 hybrid 는 v6 + 이후의 디자인 결정 사항.

---

## 7. 결론

같은 사용자 의도 (자연어 → HA 자동화) 를 두 시스템이 다르게 푼다.

- **OpenClaw** = "agent 학습 자료가 markdown directory 라는 가정 하에 정책 layer 를 두껍게 쌓는다". Application-layer 의 자유도 / 디버그 용이성 우선.
- **ESP-Claw** = "agent 학습 자료가 firmware C 코드라는 가정 하에 핵심만 강하게 한다". Always-on / 단일 보드 / 임베디드 통합 우선.

둘 다 잘 작동. esp-claw 는 demo 시점에 사용자에게 *"보드 하나만 있으면 끝나는 스마트홈 비서"* 라는 메시지가 강함. openclaw 는 *"정책을 더 깊이 표현할 수 있는 운영 가능한 플랫폼"* 임팩트가 강함.

V6 의 디자인 결정 시점에 이 비교를 한 번 더 본 후 다음 capability 의 학습 자료 형식 (markdown vs C 코드 vs hybrid) 을 선택하는 게 합리적.

---

## 8. 참고

- OpenClaw repo: `~/Desktop/openclaw-smarthome`
- OpenClaw 의 핵심 design 문서: `~/Desktop/openclaw-smarthome/product.md` (38 KB)
- ESP-Claw 자매 문서: `01-esp-claw-current-architecture.md`
- ESP-Claw 의 사이클별 보고서: `smarthome-docs/reported/`
