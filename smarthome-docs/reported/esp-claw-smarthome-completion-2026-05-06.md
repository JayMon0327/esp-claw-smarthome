# ESP-Claw 스마트홈 통합 — 완료 보고서 (Phase 1: 사전 준비)

- **작성일**: 2026-05-06
- **작성자**: JayMon0327
- **저장소**: https://github.com/JayMon0327/esp-claw-smarthome
- **단계**: 사전 조사 → 플랜 → 교차 검증 → Day 0 발견 → 저장소 셋업 **완료**
- **다음 단계**: Day 1 (환경 셋업 + 하드웨어 주문) 시작 예정

---

## 0. 요약 (TL;DR)

ESP-Claw 펌웨어를 활용한 **CEO 시연용 스마트홈 통합 데모**의 Phase 1(사전 준비)을 완료했습니다.

**시연 시나리오**:
> Telegram에서 "거실 조명 켜줘" 입력 → ESP32-S3 보드의 ESP-Claw 펌웨어가 Anthropic Claude Sonnet으로 자연어 해석 → MCP 클라이언트로 Pi의 Home Assistant MCP 서버 호출 → HA REST API → Tapo P100 플러그 → 스탠드 램프 ON.

**Phase 1 산출물**:
1. ESP-Claw 사전 조사 + 3-모델 교차 검증 보고서
2. 5일 일정의 구현 플랜 (1433줄, Day 0~5)
3. Day 0 Discovery 노트 (보드/툴 등록/프로비저닝/Anthropic provider 분석)
4. GitHub 저장소 셋업 (espressif 히스토리 보존 + 사용자 docs 추가)

**핵심 결론**:
- ✅ ESP32-S3-WROOM-1 (N16R8) 보드로 빌드 가능 (sdkconfig 1줄 수정 필요)
- ✅ Anthropic Claude tool_use dispatch가 OpenAI와 full parity로 구현되어 있음
- ✅ Telegram cap은 기본 활성화, NVS에 bot token만 주입하면 동작
- ✅ HA 통합은 펌웨어 수정 없이 **MCP 서버(외부)** 경로로 가능 (B-lite 옵션)
- ⚠️ 사용자 추가 tool은 C 코드 + 재빌드만 가능 (자동 디스커버리 없음)

---

## 1. 완료된 작업

### 1.1 사전 조사 + 교차 검증
- **파일**: `smarthome-docs/reported/esp-claw-cross-verification-2026-05-06.md`
- ESP-Claw 아키텍처/지원 칩/보드/ESP-IDF 버전을 Codex(gpt-5.4) + Gemini + Claude 3-모델 교차 검증
- 사전 조사 정확도: **80~90%**, 5개 항목 정정 (지원 칩 확장, M5StickS3 정확한 명칭, ESP-IDF 권장/필수 구분 등)
- 핵심 결론은 모두 유효: Matter 허브 불가, M5Stack CoreS3 권장, esp-claw.com 웹 플래셔, 시나리오 A 채택

### 1.2 구현 플랜 작성
- **파일**: `smarthome-docs/superpowers/plans/2026-05-06-esp-claw-ceo-demo.md` (1433줄)
- 5일 일정 분해: Day 0 발견 → Day 1 환경 → Day 2 hello world → Day 3 HA 통합 → Day 4 변주 테스트 → Day 5 리허설

### 1.3 플랜 교차 검증
- **파일**: `smarthome-docs/learn/20260506-plan-cross-verify.md`
- 3-모델로 플랜 자체 검증, 위험 5건 + 단독 발견 5건 식별
- 가장 위험한 누락: **Day 0 kill-criteria에 Anthropic tool_use 검증 항목 누락** → Day 0 Task 5.5로 추가 완료

### 1.4 Day 0 Discovery (코드 분석 only, 빌드 0회)
- **파일**: `smarthome-docs/superpowers/notes-day0.md` (177줄)
- Task 1~5.5 완료, 모든 kill-criteria 통과 → Day 1 진행 GREEN 라이트
- 상세 발견사항: 아래 §2 참조

### 1.5 GitHub 저장소 셋업
- **파일**: `smarthome-docs/learn/20260506-github-push-setup.md`
- espressif 커밋 히스토리 보존 + `smarthome-docs/` 폴더로 사용자 작업물 분리
- `origin` = JayMon0327/esp-claw-smarthome, `upstream` = espressif/esp-claw

---

## 2. Day 0 핵심 발견사항

### 2.1 보드 (사용자 보유: ESP32-S3-WROOM-1 N16R8)
- **선정 보드 디렉토리**: `application/edge_agent/boards/espressif/esp32_S3_DevKitC_1`
- **필수 변경**: `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` → `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` (1줄)
- **선택 변경**: `CONFIG_APP_CLAW_LUA_MODULE_LCD=n` (빌드 의존성 정리, LCD 미보유)
- **일정 영향**: +0.3일

### 2.2 Tool 등록 메커니즘 (가장 중요)
- LLM-callable tool은 **C 코드의 정적 `claw_cap_descriptor_t` 배열**로만 등록 (자동 디스커버리 없음)
- 신규 tool 추가 = C 파일 작성 + Kconfig 옵션 추가 + 재빌드 (분류 B)
- **B-lite 우회 (채택)**: 외부 MCP 서버를 HA 측에 띄우고 ESP-Claw는 내장 `cap_mcp_client`만 사용 → 펌웨어 수정 0회

### 2.3 프로비저닝 / NVS
- 사용 가능 방법: **캡티브 포털 + AP 모드** (BLE provisioning 없음)
- 절차: 첫 부팅 → AP `esp-claw-XXXXXX` (open) → `http://192.168.4.1/` → 웹 폼에서 Wi-Fi + Anthropic key + Telegram bot token 일괄 입력 → POST `/api/config` → 재시작
- **데모 관련 NVS 키**: `wifi_ssid`, `wifi_password`, `llm_api_key`, `tg_bot_token`, `en_cap_groups`, `vis_cap_groups`

### 2.4 Telegram capability
- **Kconfig**: `CONFIG_APP_CLAW_CAP_IM_TG` (기본 `default y`, DevKitC-1 sdkconfig override 없음)
- 활성화 작업 불필요, NVS `tg_bot_token`만 주입
- HTTP long-poll 방식 (`api.telegram.org/bot<token>/getUpdates`, timeout 20s)
- 한국어 처리: cJSON UTF-8 직렬화로 정상 동작

### 2.5 Anthropic provider + tool_use dispatch
- **분류 (a) Full parity**: OpenAI provider와 동일한 `claw_llm_backend_vtable_t` 구현, 동일한 `claw_llm_response_t.tool_calls[]` 구조체 사용
- `parse_chat_response()` (915줄 backend의 `:458-553`)에서 `content[].type=="tool_use"` 블록을 정확히 인식
- Multi-round message replay까지 완전 구현 (Anthropic의 `tool_result` 블록 자동 재포장)
- **NVS 설정**: `llm_profile=anthropic`, `llm_model=claude-sonnet-...`, `llm_api_key=sk-ant-...`

---

## 3. 예정 항목 (Day 1~5 후속 진행)

### 3.1 Day 1 — 환경 셋업 + 하드웨어 주문 (2~3시간) **← 다음 작업**

| Task | 내용 | 산출물 |
|---|---|---|
| Task 6 | ESP-IDF v5.5.4 설치 (`~/esp/esp-idf/`) | `idf.py --version` 동작 확인 |
| Task 7 | 하드웨어 주문 (Tapo P100 + Pi 4 SD카드 + USB-C 케이블) | 주문 완료 / 도착 ETA 기록 |
| Task 8 | Telegram bot 생성 (BotFather) + token 보관 | `tg_bot_token` 안전 저장 |
| Task 9 | Anthropic API key 발급 + Pi HA Long-Lived Token 발급 | secrets 파일 정리 |

**예상 위험**:
- ESP-IDF 설치는 macOS에서 brew 의존성(cmake/ninja/dfu-util/python3) + 30분 이상 소요 가능
- 하드웨어 배송 지연 시 Day 2가 밀림 → Day 1.5에 시뮬레이터/QEMU 검토 fallback

### 3.2 Day 2 — 보드 Hello World (보드 도착 후, 2~3시간)

| Task | 내용 |
|---|---|
| Task 10 | USB 연결 + 시리얼 포트 식별 (`/dev/cu.usbmodem*`) |
| Task 11 | esp-claw.com 웹 플래셔로 stock 펌웨어 시도 (옵션 우선) |
| Task 12 | 로컬 빌드 플래시 (`idf.py -p ... flash monitor`) — N16R8 sdkconfig 적용 |
| Task 13 | 캡티브 포털로 Wi-Fi/API key/bot token 입력 → 첫 Telegram echo |

**Kill-criteria**: Telegram에서 보낸 메시지가 시리얼 로그에 inbound로 잡히고, Claude 응답이 다시 Telegram에 도착하면 Day 2 통과.

### 3.3 Day 3 — HA 통합 (MCP 경로) (1일)

| Task | 내용 |
|---|---|
| Task 14 | `ha_call_service` tool schema 정의 (MCP 서버 측) |
| Task 15 | HA REST API HTTP 클라이언트 + retry 로직 (4xx fail-fast, 5xx/timeout/429 재시도) |
| Task 16 | System prompt 작성 (의도 → switch.turn_on/turn_off 매핑, 거절 패턴) |

**아키텍처 PIVOT 확정**: ESP-Claw 펌웨어 수정 없이 **외부 MCP 서버(Pi 측)** 경유. `cap_mcp_client`로 호출 → MCP 서버가 HA REST API로 변환 → Tapo 플러그 제어.

**예상 위험**:
- Pi 측 hass-mcp 서버 셋업 0.5일 별도
- HA Long-Lived Token TTL/스코프 점검 필수
- mid-failure 시뮬레이션(HA 다운 상태) 시 retry 폭주 방지

### 3.4 Day 4 — 통합 검증 + 5가지 변주 테스트 (1일)

| Variant | 입력 | 기대 동작 |
|---|---|---|
| 1 | "거실 조명 켜줘" | switch.turn_on ✅ |
| 2 | "환하게 해줘" (동의어) | switch.turn_on ✅ |
| 3 | "환하게 50%로" | **거절** (디머 미지원, 사실대로 응답) ❌ |
| 4 | "안방 조명 켜" | **거절** (entity 미존재) ❌ |
| 5 | "5분 뒤에 꺼" | **거절** (스케줄러 미구현) ❌ |
| Mid-fail | HA 다운 상태에서 1번 실행 | retry 후 사용자에게 실패 보고 |

**합격 기준**: ✅ 케이스 응답 시간 < 8초, ❌ 케이스 모델이 거짓말하지 않음.

### 3.5 Day 5 — 리허설 + Sanity Script + 백업 영상 (반나절)

| Task | 내용 |
|---|---|
| Task 23 | 시연 환경 리허설 3회 (다른 시간대, 다른 Wi-Fi 조건) |
| Task 24 | Sanity Script 정리 (시연 30분 전 / 5분 전 체크리스트) |
| Task 25 | 백업 영상 녹화 + Q&A 답변 카드 작성 |
| Task 26 | 시연 당일 sanity 실행 |

**시연 정직성 원칙**:
- 백업 영상 fallback 시 "기술 이슈로 사전 녹화본을 보여드립니다" 명시
- Q&A 카드 "외부 통신": Anthropic API + Telegram + Pi HA 모두 정직하게 나열

---

## 4. 리스크 및 대응

| # | 리스크 | 대응 | 단계 |
|---|---|---|---|
| 1 | ESP32-S3-WROOM-1 보드 정의가 16MB Flash 미반영 | sdkconfig 1줄 + 파티션 테이블 검토 (+0.3일 반영됨) | Day 2 |
| 2 | esp-claw.com 웹 플래셔에 커스텀 tool 주입 불가능 | B-lite (외부 MCP) 채택, 펌웨어 수정 회피 | Day 3 |
| 3 | HA REST 4xx에 retry 폭주 | retry는 timeout / connection error / 429 / 5xx만 (Task 15) | Day 3 |
| 4 | Anthropic API rate limit (429) | exponential backoff + jitter 1-2-4초 | Day 3 |
| 5 | 시연 당일 Wi-Fi/네트워크 장애 | 사전 녹화 영상 + 정직한 disclosure | Day 5 |

---

## 5. 산출물 파일 인덱스

```
smarthome-docs/
├── learn/
│   ├── 20260506-plan-cross-verify.md       # 플랜 3-모델 교차 검증 학습 로그
│   └── 20260506-github-push-setup.md       # GitHub 저장소 셋업 학습 로그
├── reported/
│   ├── esp-claw-cross-verification-2026-05-06.md     # 사전 조사 교차 검증 보고서
│   └── esp-claw-smarthome-completion-2026-05-06.md   # 본 문서 (완료 보고서)
└── superpowers/
    ├── notes-day0.md                       # Day 0 Discovery 발견사항
    └── plans/
        └── 2026-05-06-esp-claw-ceo-demo.md # 5일 구현 플랜 (1433줄)
```

---

## 6. 다음 액션

1. **Day 1 시작**: ESP-IDF v5.5.4 설치 → Telegram bot/Anthropic key 발급 → 하드웨어 주문 완료 보고
2. **저장소 운용**:
   - 작업 시작 시 `git checkout -b day1-bringup` 권장
   - upstream 동기화: `git fetch upstream && git merge upstream/master`
3. **체크인 주기**: Day 단위 진행 후 본 보고서 업데이트(또는 Day별 후속 보고서 추가)

---

**Phase 1 종료. Day 1 시작 대기 상태.**
