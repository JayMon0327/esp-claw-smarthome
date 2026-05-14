# SmartThings 통합 design 자료 정식 기록 — Q&A 4건 + 펌웨어/데이터 분리 명시

> **컨텍스트:** 사용자가 esp-claw 를 향후 SmartThings 백엔드로 확장 가능한지 + 동적 detection / OAuth2 / 플래싱 의문 4가지 질문. 답변이 일관성 있고 design source 가치가 있어 새 architecture 문서 (`03-smartthings-integration-design.md`) 로 정식 기록. V6/V7 candidate (카테고리 I 의 I-20 ~ I-24) 의 design source 가 됨.

## 무엇을 만들었나

| 파일 | 내용 |
|---|---|
| `smarthome-docs/architecture/03-smartthings-integration-design.md` | SmartThings 통합 design exploration — Q&A 4건 verbatim + API 구조 비교 + 동적 detection + OAuth2 cloud relay 디자인 + 펌웨어/데이터 분리 + v6/v7 분할 권장 |
| `docs/learn/20260514-smartthings-design-doc.md` | 이 문서 (사이클 학습) |

## 무엇을 배웠나

### 1. *plan 이 아닌 design exploration* 문서가 새로운 슬롯 — 시리즈 03번 자리

이전 시리즈:
- 01-esp-claw-current-architecture.md = *현재 상태 분석*
- 02-openclaw-vs-esp-claw-comparison.md = *비교 자료*

본 문서 (03) = *미래 구현의 design exploration*. plan 시리즈 (`smarthome-docs/superpowers/plans/`) 와 다름 — plan 은 *구현 task 분해*, design exploration 은 *architectural 결정의 근거 + trade-off*. 두 시리즈가 V6+ 진입 시 보완적으로 동작:

```
03-smartthings-integration-design.md (design exploration, 본 문서)
    ↓
2026-05-13-v6-candidates.md (candidate consolidation, PR #12)
    ↓
2026-05-XX-cap-st-control-v6.md (구체 plan, 다음 PR)
```

**원칙:** architecture 시리즈에 *현재 / 비교 / 미래 design* 세 가지 slot. 미래 design 은 plan 보다 한 단계 위 — *plan 작성자의 decision base*. plan 자체는 task 분해 + 검증 procedure 가 중심.

### 2. 사용자 질문 4건 verbatim 인용이 design 문서를 *대화형 reference* 로

PR #11 의 §6 (HA 설정) 와 같은 패턴 — 사용자 질문 원문 + 답변 구조. 본 문서 §1 에 Q1-Q4 verbatim 인용. retrospective 시점에:
- 사용자가 어떤 의도에서 질문했는지 reconstruct 가능
- 답변의 *근거* 가 명확 (특정 사용자 시나리오에 대응)
- V6 plan 작성자가 이 문서를 read 하면 *왜 SmartThings 가 V6 candidate 에 포함됐는지* 즉시 파악

**원칙:** 사용자 의도가 design 결정의 핵심 근거인 경우 질문 verbatim 인용. 단순 정보 제공이면 답만 정리해도 OK.

### 3. *펌웨어 ≠ 데이터* 의 architectural 핵심을 명시 — Q4 가 가장 중요

사용자 질문 Q4 ("토큰을 받아도 플래싱 안 하면 못 쓰는 거 아닌가?") 가 design 문서의 §5 로 — esp-claw 의 핵심 architectural decision (코드와 데이터 분리, NVS partition 기반 runtime mutability) 을 정식 기록.

이 명시가 가치 있는 이유: SmartThings 만 아니라 *모든 향후 백엔드 추가 (Aqara, Hubitat, Tuya 등) 의 base assumption*. 새로 백엔드 추가할 때마다 같은 문제 (인증 / refresh / portability) 가 반복되고, 답은 항상 같음 = "NVS 에 저장, 플래싱 X". §5 의 비유 (자동차 + 연료/주소) + 비교 표 (8 종류 데이터 NVS 현황) 가 재사용 자료.

**원칙:** architectural 핵심 명제 (분리 / invariant / contract) 는 *반복되는 의문* 의 정식 답이 됨. 새 백엔드 / capability / portability 시나리오마다 같은 답이 나오면 → architecture 문서의 separate section 으로 승격.

### 4. V6 ↔ V7 분할 권장이 design 문서의 자연스러운 산출물

본 문서 §7 의 분할:
- v6 = PAT-only + 즉시 제어 (~700 LoC, 개발자/얼리어답터 대상)
- v7 = OAuth + cloud relay + portal UI (~800 LoC + relay, 제품화 대상)

이 분할은 V6 candidate consolidation (PR #12) 의 LoC 추정 (~1500-1800 한 사이클 부담) 에서 자연스럽게 도출. design 문서가 *V6 plan 작성자에게 "이 후보는 한 사이클로 무리, 두 사이클로 분할" 신호*.

**원칙:** design exploration 의 §"구현 비용 + 분할 권장" 섹션이 후속 plan 작성자에게 *사이클 size 의 cue*. 한 candidate 가 너무 크면 (>1000 LoC) design 문서에서 분할 권장 명시.

## V6 candidate consolidation 영향

`smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` (PR #12) 의 카테고리 I 의 5건 (I-20 ~ I-24) 이 본 design 문서를 source 로 함. PR #12 머지 시점에 본 문서가 함께 main 안착하면 V6 plan 작성자가 한 곳에서 *후보 list + design 근거* 모두 read 가능.

## 참고

- 새 design 문서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/03-smartthings-integration-design.md`
- 자매 architecture 문서: `01-esp-claw-current-architecture.md`, `02-openclaw-vs-esp-claw-comparison.md`
- V6 candidate consolidation: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` (PR #12, 카테고리 I 의 5건이 본 문서가 source)
- SmartThings API: https://developer.smartthings.com/docs/api/public
