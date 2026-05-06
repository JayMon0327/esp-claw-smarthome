# 2026-05-06 — ESP-Claw CEO 시연 플랜 교차 검증

## 작업 개요
ESP-Claw CEO 시연 구현 플랜(`plans/2026-05-06-esp-claw-ceo-demo.md`)을 Codex (gpt-5.4) + Gemini + Claude 3-모델 교차 검증으로 검토. 사전 cross-verification 보고서(`reported/esp-claw-cross-verification-2026-05-06.md`)와 정합성 확인 + 플랜 자체의 위험 포인트 식별.

## 결과 요약
- **사전 조사 결과와 충돌: 없음.** 플랜이 정정사항(Lua 기반, ESP-IDF v5.5.4 권장, 4-layer 아키텍처 등)을 잘 반영.
- **표기 불일치 1건**: 플랜 "ESP32-S3-BOX-3" vs 정확한 디렉토리명 `esp_box_3` / 제품명 `ESP-BOX-3`. grep 패턴은 매치되므로 발견은 가능.
- **플랜 자체 위험 5건 + 단독 발견 5건** 식별.

## 핵심 학습

### 1. tool_use dispatch는 provider별로 다르다 (Codex+Gemini 합의)
ESP-Claw가 Anthropic provider 지원 명시했더라도 `tool_use` 포맷 파싱 + 로컬 dispatch까지 동작하는지는 별도 검증 필요. OpenAI tool calling과 포맷 다름. **시연 플랜의 Day 0 kill-criteria에 누락하면 Day 3까지 진행 후 발화하는 가장 위험한 누락.**

### 2. HTTP retry는 에러 분류가 먼저
401/403/400은 retry해도 통과 안 됨. retry 대상은 timeout / connection error / 429 / 5xx. "모든 에러를 동일 retry"는 흔한 안티패턴.

### 3. 시연 정직성 = 사전 녹화본 disclosure
백업 영상으로 fallback 시 "기술 이슈로 사전 녹화본을 보여드립니다" 명시 필수. 표준 엔지니어링 절차이지만 발화 안 하면 정직성 훼손 가능.

### 4. ESP-Claw가 Lua 기반이라는 사실의 실전 영향
일반 ESP-IDF 패턴(C + REGISTER 매크로)으로 작성한 의사코드는 ESP-Claw repo 컨벤션과 충돌 가능성 높음. 사용자도 이미 인지("Day 0 grep 후 옵션 A/B 분기"). C 코드 의사코드는 Appendix 예시 이상으로 사용 금지.

### 5. "외부 통신" 표현 시 모든 외부 API 열거
Q&A 카드 "외부 통신은 Anthropic API만"은 Telegram도 외부 API라는 사실 누락. 시연 후 follow-up 질문에서 거짓말로 잡힐 수 있음. **외부 API를 정직하게 모두 나열.**

## 모델별 강점 관찰
- **Codex (gpt-5.4)**: 문서 라인 번호 정확히 인용하며 구조적 충돌 발견 (Task 11 점프 vs Day 3 커스텀 작업 충돌, Q&A 사실 오류). 가장 깊은 통찰.
- **Gemini**: 임베디드 일반론 강함 (mbedtls 인증서, brownout, rate limit). 단 일부 정정 사항(ESP-IDF v5.3/v5.4)은 추정 기반이라 출처 부족.
- **Claude (Lead)**: 문서 간 정합성 + 디테일 (caffeinate cleanup, working directory 경로 일치) 발견.

## 후속 액션
사용자가 플랜 패치 결정 시 위 5개 우선순위 정정안 적용:
1. Day 0에 Anthropic tool_use 검증 항목 추가
2. Task 15 핸들러에 4xx fail-fast 분기
3. Task 25 Q&A 카드 외부 통신 설명 정정
4. Day 0 Task 3에 provisioning/NVS 편집 방법 발견 추가
5. Task 11 Step 4에 stock flasher로 커스텀 tool 주입 가능성 경고

## 참고 자료
- 플랜: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/plans/2026-05-06-esp-claw-ceo-demo.md`
- 디자인 (source of truth): `/Users/wm-mac-01/.gstack/projects/esp-claw/wm-mac-01-unknown-design-20260506-120130.md`
- 사전 cross-verification 보고서: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/reported/esp-claw-cross-verification-2026-05-06.md`
