# v5 완료 보고서 — post-push hotfix 가 같은 PR 에 묶일 때 명시적 단계 분리가 필요한 이유

> **컨텍스트:** v5 사이클 (PR #8) 머지 후 v3/v4 패턴 따라 별도 docs PR 로 완료 보고서 ship. PR #8 안에 post-push hotfix (`fa42827`) 가 포함되어 있어서 보고서가 그 흐름을 어떻게 정리할지가 결정 포인트였음.

## 무엇을 배웠나

### 1. post-push hotfix 가 같은 PR 에 묶이면 commit history 만으로는 review process 의 blind spot 이 안 보인다

PR #8 의 git history 만 보면:
```
fa42827 fix: normalize binary_sensor state values
ac004dd docs: drop stale v4/v5 refs
f6b3fcd docs(learn): cap_ha_automation v5
... (v5 핵심 5 commit)
```

모든 commit 이 같은 branch 의 정상 작업처럼 보임. 하지만 실제로는 `fa42827` 가 PR push 직후 사용자 텔레그램 실사용에서 잡힌 critical fix (LLM 자연어 어휘 mismatch — `to:"open"` 으로 등록된 자동화가 영원히 fire 안 함). v5 의 implementer → spec → code-quality → adversarial 4단 리뷰가 모두 통과했는데도 잡지 못한 케이스.

**원칙:** 보고서에서 hotfix 를 별도 섹션 (§ 2.3 ✨) 으로 분리하고 **발견 경로 + repro CLI + 시리얼 로그 evidence + main path 와의 차이** 를 다 기록. commit message 안에만 적어두면 사이클 retrospective 시 review process 의 blind spot 을 못 잡는다. 이 차이 (review 코드 자기 정합성 vs. LLM-shape 시나리오 검증) 는 보고서의 핵심 학습 § 4.1 로 승격.

### 2. 같은 PR 안의 단계 분리는 "review 가 무엇을 검증할 수 있고 없는지" 라는 메타 데이터

리뷰 사이클이 "plan 명세 일치 / 코드 자기 정합성" 만 검증한다는 한계는 v5 핫픽스로 처음 데이터화됨. v6 부터는 typed tool 변경의 review input 에 **prompt-shape 시나리오 한두 개** 를 같이 넣는 게 합리적 — LLM 가 실제로 어떤 argument 모양으로 호출할 가능성이 높은지. 보고서가 이 신호를 § 4.1 로 명시해놨기 때문에 향후 사이클에서 자연스럽게 호출 가능.

**원칙:** 보고서의 "학습" 섹션은 그 사이클의 retrospective 가치 — *다음 사이클이 무엇을 다르게 해야 하는지* 가 담겨야 함. 단순한 "무엇을 만들었나" 요약은 commit message 면 충분.

### 3. v3/v4/v5 보고서 형식 일관성이 retrospective 의 쉬운 비교를 가능하게 함

v4 보고서 (`smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`) 의 구조 (PR 현황 → 구현 항목 → 검증 결과 → 핵심 학습 → 한계 → 종료 → 참고) 를 그대로 따랐다. 동일한 슬롯에 같은 종류 정보가 있어서 v4 와 v5 비교 시 한눈에 변화량 파악 가능 — v4 가 13 commit + 2 post-flash 였고 v5 는 7 commit + 1 hotfix 등. 메모리/검증/학습 항목 수 자체도 비교 가능.

**원칙:** 완료 보고서는 *프로젝트의 retrospective 데이터셋* 이다. 형식 일관성이 분석 도구 (사람의 눈) 의 효율을 보장.

## 참고

- 이 보고서 PR: #9 (`docs/completion-report-cap-ha-control-v5`)
- 대상 cycle PR: #8 (머지 commit `8deedb6`)
- 보고서 본문: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- 이전 cycle 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`
