# V6 후보 추적 — 어디에 기록되어 있고 어디에 누락되어 있는가

> **컨텍스트:** PR #11 머지 직후 사용자가 "V6 portability 후보 3건이 다음 진행 계획에 포함되어 있는가?" 질문. 답을 위해 V6 후보 전체 surface 를 추적한 결과, **정식 문서 안착된 12건 + 산발된 7건** 으로 분리됨. V6 plan 파일 자체는 미작성 상태. 산발된 7건은 이전 conversation 이 유일한 기록 source 라 다음 세션에서 누락될 위험 → 옵션 A 선택으로 정식 candidate consolidation markdown (`smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md`) 작성 + 본 PR 으로 main 안착.

## 무엇을 배웠나

### 1. 사이클 누적 모델 (V3→V4→V5) 의 *boundary case* — Q&A 도중 도출된 후보는 정식 기록에서 누락된다

V3-V5 의 사이클은 `plan markdown → 구현 → learn log → completion report → 다음 사이클 plan 의 source` 흐름이 정형화. 그러나 *사이클 사이의 Q&A* 에서 도출된 후보는 어디에도 적합한 슬롯이 없음. 이번 세션의 후반 Q&A (esp-claw 학습 못 함 / S3-EYE 카메라 / 보드 호환성 / 모델 cascade 등) 가 정확히 그 경우 — 7건 모두 대화에만 존재.

**원칙:** 사이클 사이의 Q&A 도중 의식적 noting 없이 V6 후보 도출됨 + completion-intent hook 의 learn 로그 강제가 Q&A 답변에는 약하게 적용됨 (코드 변경 0). 이 boundary case 에서는 *명시적 candidate tracking 문서* 가 누락 방지 도구. 사이클 끝 시점이 아니라 *Q&A 끝 시점* 마다 한 번씩 — 또는 본 retrospective 처럼 후보가 누적된 후 일괄 consolidation.

### 2. 후보의 정식 기록 위치는 4 layer 로 분산 — V6 plan 작성자는 4곳 모두 확인 필요했지만, 이번 consolidation 으로 single source 등장

이전 (PR #11 머지 직후 시점) 의 후보 분산:

| 위치 | 안착된 후보 |
|---|---|
| `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md` §5 | v5 plan 후속 7건 |
| `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md` §6 | portability 4건 |
| `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md` §6.4 | hybrid 1건 |
| **(이전 conversation 만)** | **산발 7건** |

이번 PR 의 `2026-05-13-v6-candidates.md` 가 *19건 모두의 single source*. V6 plan 작성자는 이 한 파일을 read → 우선순위 결정. 4 layer 통합 cost 가 V6 진입 시점에 0.

**원칙:** consolidation 문서는 *4 source 의 합집합 + 우선순위 표기* 가 충분. 새 정보 추가는 안 됨 — 그건 다음 사이클의 plan 작성자의 일.

### 3. 산발 후보 7건의 retrospective 가치 — 잃지 말아야 할 데이터 source

각 산발 후보의 *도출 맥락* (어떤 사용자 질문에서 / 어떤 시나리오에서) 자체가 v6 plan 의 강도를 결정. 예: "사용자 제어 history 영구 저장" 은 *사용자 본인이 esp-claw 의 learning 부재를 직접 진단한 결과* — 우선순위 매우 높음. 이 맥락이 conversation 압축 시 사라지면 V6 plan 작성자가 *왜 이 후보가 필요한지* 재발견해야 함. cost 큼.

이번 consolidation 문서가 산발 7건마다 source ("본 conversation 의 어느 Q&A") + 사용자 직접 진단 여부 (★ 표시) 명시 — 우선순위 가중치의 근거 보존.

**원칙:** 사용자가 직접 진단한 후보 (vs. 우리가 제안한 후보) 는 *우선순위 가중치* 가 다름. 도출 맥락 + ★ 표시로 보존.

### 4. V6 plan 작성 자체가 별개 사이클 — 후보 통합 → 우선순위 → plan

V6 plan 작성은 19건 모두 통합 → 사용자가 우선순위 결정 → 1-3건 선정 → v3-v5 plan 형식으로 작성 흐름. 이게 한 사이클의 작업. 이번 retrospective + PR 는 *plan 작성의 전단계 작업* 정리.

**원칙:** plan 작성 시점에 후보 통합 + 우선순위 결정 + 실제 plan 작성을 한 사이클로 묶지 말 것 — 통합/우선순위는 사용자 input 비중이 크고, plan 작성은 우리 작업 비중이 큼. 분리해서 진행이 효율적. 본 PR 이 *통합 단계의 완료* — 다음 PR 이 *우선순위 결정 + plan 작성 단계*.

## 추천 V6 첫 사이클 조합

본 consolidation 문서 §2 의 추천 두 가지:
- **조합 A (portability 우선)**: C-7 (builder rigor) + D-8 (mDNS HA 발견) + H-19 (보드 호환성 reference). 모두 🔴 우선순위, 합 ~250 LoC + 30분 docs. 사용자 portability 시나리오 (다른 집 가서 데모) 즉시 가능.
- **조합 B (학습 layer 우선)**: E-13 (사용자 제어 history) + E-14 (외부 sink) + C-7 (rigor). E-13/14 가 사용자 직접 진단한 "learning layer" gap 해결. ~250-380 LoC.

사용자 결정 사항.

## 참고

- V6 candidate consolidation: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md`
- V5 보고서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- 아키텍처 (portability §6): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md`
- 아키텍처 비교 (hybrid §6.4): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
