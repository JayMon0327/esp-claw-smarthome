# 사업가 관점 비교 문서 작성 — esp-claw 가 시장에서 강한 이유 + V6 후보의 사업 가치

> **컨텍스트:** 사용자가 esp-claw 와 openclaw 를 *사업 아이템* 으로 비교하는 새 문서 요청. 3가지 사업가 관점 (기억 확장성 / 폼팩터 / 로딩 속도) 제시 + 추가 의견 요청. 검증 결과 모두 정확 + 7가지 추가 사업 dimension 발견 → 새 architecture 문서 (`04-...business-perspective.md`).

## 무엇을 만들었나

| 파일 | 내용 |
|---|---|
| `smarthome-docs/architecture/04-openclaw-vs-esp-claw-business-perspective.md` | 사업가 관점 10 dimension 비교 (사용자 3 + 추가 7) + 시나리오별 추천 + V6 후보의 사업 가치 |
| `docs/learn/20260514-business-perspective-doc.md` | 이 문서 |

## 무엇을 배웠나

### 1. *기술 비교* 와 *사업 비교* 는 다른 frame — 시리즈에 02 와 04 가 보완적으로

이전 02 번 문서는 *layer-by-layer 기술 비교* (host / runtime / agent 학습 / HA 통신 / state 저장 / update cycle). 본 04 번은 *사업 dimension 비교* (단가 / 시장 / 수익 / 유지보수 / 신뢰성). 같은 두 시스템을 보지만 *frame* 다름.

**원칙:** 의사결정 frame 이 다르면 문서도 분리. 같은 비교를 두 번 안 하지만, 사업 결정자와 기술 결정자는 다른 dimension 으로 본다는 사실을 architecture 시리즈가 반영.

### 2. 사용자 진단 3가지 검증 + nuance 보강 — 사용자가 *놓친 차원* 이 사업적으로 더 중요할 수 있음

사용자가 든 3가지 (기억 / 폼팩터 / 로딩) 가 모두 정확. 다만 사업가 관점에서는:
- **단가 + COGS** 가 mass-market 진입 결정 요인
- **수익 모델 (구독 SaaS)** 가 LTV 결정
- **신뢰성** 이 enterprise / 통신사 B2B 결정 요인

사용자가 *기술 user* 관점에서 본 3가지 + 우리가 *시장 운영자* 관점에서 본 7가지 = 10 dimension. **사용자 + 우리의 시각 보완성**이 좋은 비교 문서의 base.

**원칙:** 사용자가 *어느 frame 에서* 질문하는지 식별 → 그 frame 의 진단 검증 + *다른 frame* 에서 보강. 단순 검증만으로 끝내지 않음.

### 3. esp-claw 가 *7/10 dimension* 에서 우위 — openclaw 약점 보완 가능성도 V6 후보 안에 있음

10 dimension 중:
- esp-claw 우위 7: 폼팩터 / 응답 속도 / 단가 / 유지보수 / 신뢰성 / 시장진입 / 수익모델 / 멀티디바이스 (실제 8개)
- openclaw 우위 2: 기억 확장성 / 사용자 정의
- 사용자 정의는 V6 candidate **E-12 (storage markdown skill)** 가 해결 path
- 기억 확장성은 V6 candidate **E-13 (사용자 history) + E-14 (외부 sink hybrid)** 가 해결 path

→ **V6 후보 진행 시 esp-claw 가 *모든 dimension 우위* + SaaS 모델 완성 가능**. 이 시각이 V6 plan 의 *사업 가치 추적* base.

**원칙:** *기술 후보* 들을 *사업 가치 prism* 으로 통과시키면 우선순위가 명확해짐. 단순 "코드 가치" 보다 "시장 dimension 영향" 이 의사결정자에게 communicate 가치 큼.

### 4. *시나리오별 추천* 이 비교 문서의 산출물 — 단순 표보다 의사결정 지원

§5 의 6 시나리오별 추천 (일반 소비자 / 파워유저 / B2B 통신사 / B2B 부동산 / 컨설팅 / hospitality) 이 *사업가가 즉시 사용 가능한 정보*. "esp-claw vs openclaw" 의 결과가 단일 답이 아니라 *상황 의존* 임을 명시.

**원칙:** 비교 문서의 *산출물* 은 단순 표가 아니라 *상황별 의사결정 가이드*. "어느 게 더 좋은가?" 의 답은 항상 *어떤 시나리오에서?* 의 함수.

## V6 plan 작성 시 영향

본 문서가 V6 candidate consolidation (PR #12) 의 *사업 가치 가중치* 제공:

| V6 candidate | 사업 가치 (본 문서 기준) |
|---|---|
| E-12 storage markdown skill | openclaw 6번 dimension 따라잡기 |
| E-13 사용자 history NDJSON | openclaw 1번 dimension 따라잡기 |
| E-14 외부 sink hybrid | SaaS 수익 모델 완성 + 1번 dimension 따라잡기 |
| D-8 mDNS HA 발견 | 시장 진입 8번 dimension 강화 |
| D-9 esp-web-tools 호스팅 flasher | 시장 진입 8번 + 유지보수 5번 강화 |
| H-19 보드 호환성 reference | 시장 진입 8번 강화 (M5Stack 등 변형 채택 가능) |

V6 plan 작성자는 *기술 가치* + *사업 가치* 두 가지로 후보 우선순위 결정 가능.

## 참고

- 새 비교 문서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/04-openclaw-vs-esp-claw-business-perspective.md`
- 자매 기술 비교: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
- 현재 architecture: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md`
- V6 candidate consolidation: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` (PR #12)
- SmartThings design: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/03-smartthings-integration-design.md` (PR #13)
