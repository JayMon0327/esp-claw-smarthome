# 아키텍처 분석 문서 작성 — 두 시스템 비교의 표현 방식

> **컨텍스트:** v6 진입 전에 esp-claw 와 사전 구현인 openclaw-smarthome (Pi 위 application layer) 의 architecture 분석 + 비교 문서 2개를 `smarthome-docs/architecture/` 하위에 작성. 사용자 의도: 두 시스템의 layer-by-layer 차이 + 적합 시나리오를 retrospective 자료로 정리.

## 무엇을 만들었나

| 파일 | 내용 |
|---|---|
| `smarthome-docs/architecture/01-esp-claw-current-architecture.md` | esp-claw 현재 상태 분석 — 시스템 개요 / 펌웨어 플래싱 history (web flasher 실패 → MXIC HPM 비호환 → ESP-IDF 우회) / 디렉토리 구조 / 4개 핵심 파일 코드 예시 / 에이전트 학습 두 layer (보드 내부 LLM + 외부 Claude Code 하네스) |
| `smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md` | OpenClaw (Pi 위 application layer) 의 3-Plane 분리 / 디렉토리 / 핵심 markdown 4개 (AGENTS / SOUL / TOOLS / HEARTBEAT) / agent 학습 방법 + esp-claw 와의 layer-by-layer 표 / 같은 시나리오 path 비교 / trade-off |
| `docs/learn/20260513-architecture-analysis-docs.md` | 이 문서 (사이클 학습) |

## 무엇을 배웠나

### 1. 두 시스템 비교는 같은 시나리오의 path 추적이 가장 명료한 도구

차이 표 (호스트 / agent runtime / HA 통신 / state 저장 등 항목별) 만으로는 *그래서 무엇이 다른가* 가 안 와닿음. 같은 시나리오 ("도어 열림 → 화장실 조명 켜기 자동화 등록") 를 두 시스템에서 어떻게 step-by-step 처리하는지 매핑하니 차이가 즉시 보임 — entity resolve / safety check / LLM 어휘 보호 / HA 호출 / 응답 layer / logging 의 각 단계에서 두 시스템이 어디에 어떤 책임을 두는지.

**원칙:** 시스템 비교 문서의 핵심 도구는 (a) layer-by-layer 표 (b) 같은 시나리오의 두 path step-by-step. 둘 다 있어야 빠른 reference + 깊은 이해 모두 가능.

### 2. "에이전트 학습" 은 두 layer 로 분리해서 표현해야 혼동 없다

사용자가 "agent 학습 (하네스) 어떻게 시켰는지" 라고 한 단어로 물었지만, esp-claw 에선 두 다른 layer:
- **Layer A — 보드 내부 LLM 학습**: capability descriptor (`s_ha_descriptors[]` + JSON Schema literal) + `cap_ha_compose_description` 의 friendly_names 보간. **변경 = firmware flash**.
- **Layer B — 외부 Claude Code 하네스 학습**: plans / learn / reports markdown 시리즈 + subagent-driven-development + completion-intent hook. **변경 = git push**.

두 layer 가 혼재되면 "왜 어떤 변경은 즉시 반영되고 어떤 건 flash 가 필요한가" 같은 헷갈림이 생김. 문서 § 5 에서 두 layer 명시 분리 + 분리의 이유 (production user path vs 개발 사이클) 까지 같이 적어 retrospective 시 한 줄로 답 가능.

**원칙:** 다의어 의도 (예: "agent 학습") 는 문서에서 layer 분리를 명시. 단어를 그대로 받아쓰는 게 아니라 retrospective 가치 가 있는 layer 분리를 제시하는 게 문서 작성자의 일.

### 3. 사이클 시리즈 (plans / learn / reports) 자체가 "외부 agent 학습" 임을 명시하는 효과

v3-v5 사이클에서 우리가 작성한 plans / learn / reports 시리즈는 *Claude Code 하네스의 학습 자료* 라는 명시적 frame 을 처음 본 문서. 이전에는 plan 은 plan, learn 은 learn 으로 분산. 이번 문서에서 "Layer B = plans + learn + reports + subagent + hook" 라고 묶으니 사이클의 메타-구조가 retrospective 가능한 단위로 보임.

**원칙:** 사이클이 누적되면 그 사이클의 *메타-구조* 도 문서화 가치. 다음 사이클 (v6) 의 plan 작성자가 "이 시리즈 위에 내가 무엇을 더하는가" 를 결정할 base 가 됨.

### 4. webflasher 실패의 진짜 원인 (Flash 칩 brand HPM 비호환) 은 v6 plan 에 영향 없음 — 보드 변경 시점에 다시 봄

esp-claw 가 ESP-IDF 빌드로 진행한 이유 = 사용자 보드 (N16R8, MXIC Flash) + esp-claw stock image (N8R8, GigaDevice/Winbond 가정) mismatch + HPM 가속 코드 비호환. 이 진단은 day1 의 학습 로그 두 개에 정리 (`20260507-webflasher-flash-stuck.md`, `20260507-board-mismatch-n16r8.md`). 다만 *현재 보드를 계속 사용한다는 가정* 에서는 v6 의 의사결정에 영향 없음. 다음 보드 변경 (예: N8R8 새 구매, 또는 다른 ESP32 variant) 시점에 이 학습을 다시 reference 해야 함.

**원칙:** 환경-specific quirk (보드 brand 비호환 같은) 는 문서에 영구 기록하되, retrospective 시 *현재 결정에 영향 있나* 판단 후 새 사이클의 input 으로 포함 여부 결정.

## 다음 사이클 (v6) 으로 전달할 것

- 두 시스템 비교에서 도출한 **hybrid 가능성** (§6.4): esp-claw 의 storage 파티션에 markdown 정책 파일 + Lua-style runtime read. v6 후보 8번 — 별도 plan 작성 가치 있음.
- esp-claw 에 safety_rules.md 같은 정책 layer 가 *없다* 는 점을 v6 의 보안 강화 (etc. 도어락 unlock) 작업 시 명시적으로 다룰 것. v5 normalize 가 어휘 layer 의 시작이라면 v6 의 safety_rules layer 는 보안 layer.
- OpenClaw 가 사용하는 모델 cascade 패턴이 esp-claw 에도 적용 가능한지 — v6 plan 의 cost 섹션에서 검토.

## 참고

- 자매 문서 둘 다: `smarthome-docs/architecture/01-...`, `02-...`
- v5 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- webflasher 진단 day1 학습: `docs/learn/20260507-webflasher-flash-stuck.md`, `20260507-board-mismatch-n16r8.md` (※ 메인 worktree 의 untracked 파일들 — 현재 main 에 머지 안 됨)
- OpenClaw repo: `~/Desktop/openclaw-smarthome`
