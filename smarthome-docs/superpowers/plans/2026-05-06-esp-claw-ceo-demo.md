# ESP-Claw CEO Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP-Claw 펌웨어가 Telegram 명령을 받아 Anthropic Claude로 자연어 해석 → Pi HA REST API 호출 → Tapo 플러그 → 스탠드 램프 ON/OFF. 대표 5-10분 시연용. End-to-end 동작 + 5분 전 sanity 검증 + 백업 영상까지 준비.

**Architecture (v3, MCP 경로 확정):** ESP32-S3 보드 + ESP-Claw 펌웨어 (수정 없음, 내장 cap_mcp_client 활용). Telegram bot 입력 → Anthropic Claude Sonnet tool_use → cap_mcp_client → **Pi의 HA MCP server (hass-mcp 또는 동등)** → HA REST API → Tapo P100 → 램프. Day 0 Task 3 발견: ESP-Claw는 사용자 추가 tool을 C+rebuild로만 받음. MCP는 외장 server로 펌웨어 수정 회피하는 README 권장 패턴.

**Tech Stack:** ESP-IDF v5.5.4, ESP-Claw repo (espressif/esp-claw), Anthropic Claude Sonnet API, Telegram Bot API, Home Assistant REST API, macOS 호스트.

**Source of Truth:** `/Users/wm-mac-01/.gstack/projects/esp-claw/wm-mac-01-unknown-design-20260506-120130.md` (Status: APPROVED). 본 플랜과 디자인 문서가 충돌하면 디자인 문서가 우선.

**Working Directories:**
- ESP-Claw repo clone: `/Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/` (Day 0 Task 1에서 생성)
- ESP-IDF: `~/esp/esp-idf/` (Day 1에서 생성)
- 작업 노트: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md` 등 (자유롭게)

**중요한 비-TDD 주석:** 본 프로젝트는 ESP32 펌웨어 + 외부 통합 위주라 전통적 TDD가 어렵습니다. 각 Task의 "검증" 단계는 unit test가 아니라 **실제 보드에서 명령 실행 + 시리얼 로그/curl 응답으로 동작 확인**입니다. TDD 단계 표기를 그대로 유지하되 "test" = "실제 device/network 검증" 으로 읽으세요.

---

## Day 0: Discovery (Kill-criteria, 2-3시간, 구매 0원)

다섯 항목 모두 통과해야 Day 1로 진행. 한 항목이라도 막히면 STOP하고 사용자에게 보고.

### Task 1: ESP-Claw repo clone + 구조 파악

**Files:**
- Create: `/Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/` (clone target)
- Create: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md` (발견 사항 기록)

- [ ] **Step 1: clone ESP-Claw repo**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main
git clone https://github.com/espressif/esp-claw.git
```

Expected: clone 완료, `esp-claw/` 디렉토리 생성

- [ ] **Step 2: top-level 구조 확인**

```bash
cd esp-claw
ls -la
cat README.md | head -100
```

Expected: `application/`, `components/`, `docs/`, `README.md` 등이 보임

- [ ] **Step 3: notes-day0.md 만들고 발견사항 기록 시작**

```bash
mkdir -p /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers
cat > /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md <<EOF
# Day 0 Discovery Notes

## ESP-Claw repo
- Top-level dirs: <ls 결과>
- README 요약: <핵심 1-2줄>
EOF
```

- [ ] **Step 4: commit (esp-claw repo는 fork 또는 별도 origin 결정)**

본인 작업물을 esp-claw에 직접 push할 수는 없으므로:
- 옵션 A: GitHub에 본인 fork 만들고 origin 변경
- 옵션 B: 로컬에서만 작업, push 안 함 (시연 후 본인 backup repo로 push)
- 추천: 옵션 B (시연 후 정리)

지금은 commit 안 함. notes-day0.md만 main 워크스페이스에서 git status로 확인:

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main
git status 2>/dev/null || echo "main is not a git repo, that's fine for now"
```

---

### Task 2: 보드 정의 존재 확인 (Open Question #2) — 사용자 보유 보드 ESP32-S3-WROOM-1 (N16R8) 기준 갱신

**중요 변경**: 사용자는 ESP-BOX-3/CoreS3가 아닌 **ESP32-S3-WROOM-1 (N16R8) 모듈**을 보유. 이는 DevKitC-1 형태의 bare dev kit 또는 breadboard 셋업으로 추정. 메모리 16MB Flash + 8MB PSRAM은 ESP-Claw 최소 요구사항(8MB+8MB) 충족.

**Files:**
- Read: `/Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/application/edge_agent/boards/`

- [ ] **Step 1: boards/ 디렉토리 listing**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
ls application/edge_agent/boards/ 2>/dev/null || ls application/*/boards/ 2>/dev/null || find . -name "boards" -type d
```

Expected: 보드 정의 디렉토리들 (예: `esp32_s3_box_3/`, `m5stack_cores3/`, `breadboard_esp32s3/`, `esp32_s3_devkitc_1/`, `wroom`, `generic`)

- [ ] **Step 2: WROOM-1 / DevKitC / breadboard / generic 보드 정의 검색 (1순위)**

```bash
ls application/edge_agent/boards/ | grep -iE "wroom|devkit|breadboard|generic|s3_dev"
```

- [ ] **Step 3: 1순위 못 찾으면 fallback — 가장 단순한 보드 정의 찾기**

```bash
# breadboard, dev kit, generic 류는 보통 디스플레이/오디오 의존성이 적음
ls application/edge_agent/boards/ 
# 각 보드의 board.h를 빠르게 훑어 LCD/audio 의존성 없는 것 식별
for B in $(ls application/edge_agent/boards/); do
    echo "=== $B ==="
    grep -li "lcd\|display\|i2s\|es8311\|audio" application/edge_agent/boards/$B/ 2>/dev/null && echo "  HAS DISPLAY/AUDIO" || echo "  bare/generic candidate"
done
```

- [ ] **Step 4: 결정 — WROOM-1로 ESP-Claw 빌드 가능 보드 선정**

분류:
- **(a) WROOM-1/DevKitC 전용 board 정의 존재**: 그대로 사용. 일정 영향 없음
- **(b) breadboard / generic 보드 정의 존재**: 그대로 사용 가능. WROOM-1과 핀맵만 다를 가능성 — 추가 0.3일 매핑
- **(c) 디스플레이/오디오 의존 보드만 존재**: WROOM-1 전용 board.h를 본인이 만들어야 함. **일정 +1.5일** + 사용자에게 확인
- **(d) boards/ 자체가 없음**: 일정 재계획

- [ ] **Step 5: notes-day0.md에 결과 기록**

```bash
echo "
## 보드 지원 (사용자 보유: ESP32-S3-WROOM-1 N16R8)
- boards/ 전체 listing: <전부>
- WROOM-1/DevKitC/breadboard/generic 중 매칭: <존재/부재>
- 결정 보드 디렉토리: <확정>
- 분류: a/b/c/d
- 일정 영향: 0 / +0.3일 / +1.5일 / 재계획
- 결정 이유: <간단히>
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

- [ ] **Step 6: 검증 — 결정한 보드의 board.h / Kconfig 파일 + 디스플레이/오디오 의존성 점검**

```bash
BOARD=<결정한 보드 디렉토리명>
ls application/edge_agent/boards/$BOARD/
grep -li "lcd\|display\|i2s\|audio\|microphone" application/edge_agent/boards/$BOARD/ 2>/dev/null && \
  echo "WARN: display/audio 의존 코드 있음 — WROOM-1에서 비활성화 필요할 수 있음" || \
  echo "OK: bare 보드 호환"
```

Expected: `board.h`, `Kconfig` 또는 `CMakeLists.txt` 같은 파일이 보임

⚠️ STOP 조건: 분류 (c) 또는 (d)이면 사용자에게 보고. 일정 재계획.

---

### Task 3: Tool registration mechanism 발견 (Open Question #1, 가장 중요)

**Files:**
- Read: `/Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/` (전체)
- Update: `notes-day0.md`

- [ ] **Step 1: tool 관련 코드 위치 파악 (1차 grep)**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
grep -rli "tool" application/edge_agent/ | head -20
```

- [ ] **Step 2: tool 등록 함수/매크로 검색**

```bash
grep -rn "register_tool\|tool_register\|add_tool\|register_function\|mcp_tool" application/ components/ 2>/dev/null | head -30
```

- [ ] **Step 3: README와 docs/에서 "Adding tool", "Custom tool" 검색**

```bash
grep -rni "adding.*tool\|custom.*tool\|register.*tool\|tool.*registration" docs/ README.md 2>/dev/null
```

- [ ] **Step 4: example tool이 코드에 있는지 확인**

```bash
# RGB LED 같은 example tool은 보통 application/edge_agent/main/ 또는 application/edge_agent/tools/ 같은 곳에
find application -name "*.c" -path "*tool*" 2>/dev/null
find application -name "*.lua" 2>/dev/null  # Lua-based tools 가능성
find application -name "*.json" -path "*tool*" 2>/dev/null  # JSON config-based 가능성
```

- [ ] **Step 5: 옵션 분류**

발견 결과를 다음 셋 중 하나로 분류:
- **옵션 A (config-only)**: JSON/Lua/YAML 파일에 tool schema만 추가하면 끝. C 코드 수정 없음. 일정 변경 없음
- **옵션 B (C+rebuild)**: tool.c 같은 C 파일에 함수 추가 + register 매크로 호출 + firmware rebuild 필요. 일정 +1.5일
- **옵션 C (예상 외 패턴)**: 둘 다 아닌 경우 (예: WASM, 외부 서버 등). 사용자에게 보고 후 일정 재계획

- [ ] **Step 6: provisioning / NVS 편집 방법 발견 (cross-verify 보강)**

설정 입력 방법이 무엇인지 사전 확인. 이게 결정되어야 Task 9, 13, 22, 24가 구체화됨:

```bash
# 옵션별 검색
grep -rni "ble.*provision\|wifi.*provision\|prov_mgr" application/ components/ 2>/dev/null | head -10
grep -rni "captive.*portal\|http.*config" application/ components/ 2>/dev/null | head -10
grep -rni "uart.*cli\|console.*command\|esp_console" application/ components/ 2>/dev/null | head -10
grep -rni "nvs_set_str.*token\|nvs.*api_key" application/ components/ 2>/dev/null | head -10
```

분류:
- **(a) BLE provisioning**: ESP BLE Provisioning 앱으로 입력 (가장 흔함)
- **(b) Captive portal**: 보드 AP 모드 → 폰에서 form 입력
- **(c) UART CLI**: monitor에서 직접 명령으로 NVS 쓰기
- **(d) menuconfig + rebuild**: 컴파일 시간 입력 (보안 약함)

Task 22의 "토큰 한 글자 바꾸기" 시뮬레이션 절차도 이 결과 따라 결정됨.

- [ ] **Step 7: notes-day0.md에 결과 기록**

```bash
echo "
## Tool Registration
- 발견 위치: <파일 경로>
- 옵션 분류: A / B / C
- example tool 코드 한 줄 인용: \`<예시>\`
- 일정 영향: <0일 / +1.5일 / 재계획>

## Provisioning / NVS 편집 방법
- 분류: a/b/c/d
- 구체 절차: <앱 이름 / URL / 명령>
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

⚠️ STOP 조건: 옵션 C이면 사용자에게 보고. 진행 안 함.

---

### Task 4: Telegram capability 빌드 가능 확인 (Open Question #3)

**Files:**
- Read: `/Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/`

- [ ] **Step 1: telegram 관련 코드 검색**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
grep -rli telegram application/ components/ 2>/dev/null | head -10
```

- [ ] **Step 2: telegram이 활성화 가능한 component인지 Kconfig 확인**

```bash
grep -rn "TELEGRAM\|telegram" application/edge_agent/main/Kconfig* 2>/dev/null
grep -rn "TELEGRAM\|telegram" application/edge_agent/sdkconfig* 2>/dev/null
find . -name "Kconfig*" | xargs grep -l "telegram\|TELEGRAM" 2>/dev/null
```

- [ ] **Step 3: telegram component 폴더 구조 파악**

```bash
TELEGRAM_DIR=$(find . -type d -iname "*telegram*" | head -1)
[ -n "$TELEGRAM_DIR" ] && ls "$TELEGRAM_DIR/" || echo "no telegram dir"
```

- [ ] **Step 4: 결과 기록**

```bash
echo "
## Telegram Capability
- 코드 위치: <경로>
- Kconfig 활성화 옵션: <CONFIG_TELEGRAM_BOT_ENABLE 등>
- 활성화 방법: <menuconfig 키 또는 sdkconfig.defaults 수정>
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

⚠️ STOP 조건: telegram 코드가 없거나 stub이면 사용자에게 보고. 입력 모달리티 재검토 필요.

---

### Task 5: 사무실 Wi-Fi → Anthropic API egress 검증 (Open Question #4)

**Files:** (네트워크 검증, 코드 수정 없음)

- [ ] **Step 1: 회의실/사무실 Wi-Fi에서 Mac 터미널로 curl**

```bash
curl -i -X POST https://api.anthropic.com/v1/messages \
  -H "x-api-key: dummy-test-key" \
  -H "content-type: application/json" \
  -H "anthropic-version: 2023-06-01" \
  -d '{"model":"claude-sonnet-4-6","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}' \
  --max-time 10
```

- [ ] **Step 2: 응답 분류**

- HTTP 401 (Authentication error) = egress OK ✅ (인증만 안 된 상태)
- HTTP 200 (성공) = egress OK ✅ (실제 키로 통과)
- HTTP 403/blocked HTML 페이지 = 차단됨, 모바일 테더링 시연 필요
- timeout / connection refused = 차단됨, 모바일 테더링 시연 필요

- [ ] **Step 3: Telegram API도 같은 방식 검증**

```bash
curl -i https://api.telegram.org/ --max-time 10
```

Expected: HTTP 200 또는 404 (둘 다 reachable)

- [ ] **Step 4: 결과 기록 + 시연 환경 결정**

```bash
echo "
## 네트워크 Egress
- api.anthropic.com:443 사무실: <OK / blocked>
- api.telegram.org:443 사무실: <OK / blocked>
- 시연 환경 결정: <사무실 Wi-Fi / 모바일 테더링>
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

⚠️ STOP 조건 없음. blocked이면 모바일 테더링으로 시연 환경만 변경.

---

### Task 5.5: Anthropic provider + tool_use 동작 검증 (cross-verify 핵심 보강)

⚠️ **3 모델 cross-verify에서 가장 큰 위험으로 합의된 항목.** ESP-Claw가 README에 Anthropic 지원을 명시했어도 tool_use 포맷 파싱→로컬 dispatch까지 동작하지 않으면 Day 3-4까지 와서 발견됨. 시연 일정 회복 불가.

**Files:**
- Read: ESP-Claw repo 전체
- Update: `notes-day0.md`

- [ ] **Step 1: Anthropic provider 코드 위치 확인**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
grep -rli "anthropic\|claude" application/ components/ 2>/dev/null | head -10
```

- [ ] **Step 2: tool_use 응답 파싱 로직 존재 확인**

```bash
grep -rn "tool_use\|tool_call\|function_call" application/ components/ 2>/dev/null | head -20
```

- [ ] **Step 3: Anthropic 코드와 tool dispatch가 연결되어 있는지 확인**

Anthropic provider 파일에서 응답 처리 함수(JSON 파싱 + tool dispatch 호출)를 찾기:

```bash
ANTHROPIC_FILE=$(grep -rli "anthropic" application/ components/ | head -1)
[ -n "$ANTHROPIC_FILE" ] && grep -n "tool_use\|content_block\|stop_reason" "$ANTHROPIC_FILE" || echo "Anthropic provider 코드 없음"
```

- [ ] **Step 4: 분류**

- **(a) Anthropic + tool_use 파싱 양쪽 모두 존재**: 정상 일정. 그대로 진행
- **(b) Anthropic provider는 있으나 tool_use 파싱은 OpenAI provider에만 있음**: ESP-Claw가 LLM provider별 tool 핸들링 분기 안 했을 가능성. system prompt만으로 tool 강제하는 fallback 또는 OpenAI provider로 전환 필요. **일정 +1일**
- **(c) Anthropic provider 자체가 stub**: OpenAI provider로 전환 필수 (이 경우 Anthropic API 키 대신 OpenAI 키 발급 + LLM 모델 변경 필요). **일정 +0.5일 + 비용 변경 + 한국어 품질 재검증**

- [ ] **Step 5: notes-day0.md 기록**

```bash
echo "
## Anthropic + tool_use 검증
- Anthropic provider 코드: <경로 또는 없음>
- tool_use 파싱: <provider 분기 또는 공통>
- 분류: (a) / (b) / (c)
- LLM provider 결정: Anthropic Claude Sonnet / OpenAI GPT-4o
- 일정 영향: 0 / +0.5 / +1 / 재계획
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

⚠️ STOP 조건: (c)이고 OpenAI 사용 불가(예: 회사 정책)인 경우 사용자에게 보고. 시연 모드 재검토.

---

### Task 5.6: Wi-Fi client isolation + 보드↔Pi 도달성 사전 점검

⚠️ Mac↔Pi는 본인 검증 완료. 보드↔Pi는 보드 입장의 라우팅이 다를 수 있음. 일부 회사 Wi-Fi는 client isolation을 켜서 같은 SSID 디바이스끼리 통신 차단함.

- [ ] **Step 1: 사무실 Wi-Fi의 client isolation 여부 확인**

네트워크 담당자에게 1줄 문의 또는 다음 방법으로 추정:

```bash
# Mac에서 Mac과 Pi가 같은 서브넷인지
ifconfig | grep "inet " | grep -v 127.0.0.1
# 출력의 Mac IP와 Pi IP가 같은 /24 인지 비교
```

같은 서브넷인데 직접 ping 안 되면 client isolation 가능성. ping 되면 OK.

- [ ] **Step 2: 모바일 테더링 백업 시나리오 준비**

만약 사무실 Wi-Fi가 client isolation 켜져 있다면:
- 본인 폰 + 라즈베리파이도 같이 모바일 테더링에 붙임 (Pi 담당자 협조 필요)
- 또는 보드 + 노트북만 테더링, Pi는 사무실 Wi-Fi 유지하되 라우팅 가능 라우터 셋업 (복잡)
- 가장 간단한 길: **데모용 휴대용 라우터/AP 1개 준비 (Pi + 보드 + 노트북 모두 그쪽 Wi-Fi)** — 1만원 라우터로 OK

- [ ] **Step 3: notes-day0.md에 결과 기록**

```bash
echo "
## 네트워크 토폴로지
- 사무실 Wi-Fi client isolation: ON / OFF / 미확인
- 시연 시 보드/노트북/Pi 같은 SSID 사용 가능: 예 / 아니오
- 백업 라우터 필요: 예 / 아니오
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

---

### Day 0 Gate: 사용자 보고

- [ ] **모든 Day 0 결과를 사용자에게 보고**

notes-day0.md 전체 내용을 보여주고 다음 결정:
- ✅ 옵션 A + 모든 검증 통과 → Day 1 진입, 일정 그대로 4-5일
- ✅ 옵션 B + 모든 검증 통과 → Day 1 진입, 일정 +1.5일 (5-6일)
- ❌ 옵션 C 또는 STOP 조건 hit → 일정 재계획 또는 백업 시연(영상만) 모드

---

## Day 1: 환경 셋업 + 하드웨어 주문 (2-3시간)

### Task 6: ESP-IDF v5.5.4 설치

**Files:**
- Create: `~/esp/esp-idf/` (clone)
- Modify: `~/.zshrc` (export.sh 추가)

- [ ] **Step 1: 의존성 설치**

```bash
brew install cmake ninja dfu-util ccache
brew install python@3.10
```

Expected: 설치 완료 (이미 있으면 "already installed")

- [ ] **Step 2: ESP-IDF clone**

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git
```

Expected: clone 완료, `~/esp/esp-idf/` 생성

- [ ] **Step 3: ESP-IDF tools install**

```bash
cd ~/esp/esp-idf
./install.sh esp32s3
```

Expected: 5-10분 소요. "All done!" 메시지

- [ ] **Step 4: shell export 영구화**

```bash
# 기존에 export.sh source 라인이 없는지 확인
grep "esp-idf/export.sh" ~/.zshrc || echo '. ~/esp/esp-idf/export.sh' >> ~/.zshrc
```

- [ ] **Step 5: 검증**

```bash
. ~/esp/esp-idf/export.sh
idf.py --version
```

Expected: `ESP-IDF v5.5.4` 또는 그 근접 버전

- [ ] **Step 6: menuconfig으로 보드 + Telegram + LLM provider 확정 후 빌드 (cross-verify 보강)**

⚠️ `idf.py set-target esp32s3` + `idf.py build`만으로는 ESP-Claw 같은 application 빌드가 거의 항상 실패합니다 (보드 미선택, Telegram 비활성, provider 미선택). 첫 빌드 전 menuconfig 필수.

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/application/edge_agent
idf.py set-target esp32s3
idf.py menuconfig
```

UI에서 다음 셋 모두 설정 후 ESC ESC ESC ENTER로 저장:
- ESP-Claw → Board Selection → Day 0에서 결정한 보드
- ESP-Claw → IM/Telegram → Enable
- ESP-Claw → LLM Provider → Anthropic (또는 Day 0 Task 5.5 결과로 OpenAI)

```bash
idf.py build
```

Expected: 성공 빌드. 첫 빌드는 5-15분.

⚠️ 빌드 실패 시: 누락된 component 확인. `idf.py reconfigure` 후 재빌드.

---

### Task 7: 하드웨어 주문

**Files:** (구매, 코드 수정 없음)

- [ ] **Step 1: 보드 주문 — 사용자가 이미 ESP32-S3-WROOM-1 (N16R8) 보유 → SKIP 가능**

사용자 확인: ESP32-S3-WROOM-1 (N16R8) 모듈 보유 중. 메모리 충족(16MB+8MB). 추가 주문 불필요.

만약 Day 0 Task 2 결과가 (c) 또는 (d) (WROOM-1 호환 board 정의 없음)이면 fallback:
- 1순위: ESP-BOX-3 (Espressif 공식, repo `esp_box_3`)
- 2순위: M5Stack CoreS3

당일배송 옵션 선택. 도착 확인 후 Day 2 진행.

- [ ] **Step 2: USB-C 데이터 케이블 + 안정 USB 어댑터 확보 (cross-verify Gemini 지적)**

⚠️ 충전 전용 케이블은 동작 안 함. 데이터 전송 가능 여부 확인.
⚠️ ESP32-S3는 와이파이 송신 시 순간 전류가 큽니다. 노트북 USB 직결도 OK이지만 약한 USB 허브를 거치면 brownout 패닉 가능. 시연용에는 노트북 USB-C 직결 또는 5V/3A 이상 정격 USB 어댑터 권장.

- [ ] **Step 3: 도착 대기 동안 Day 1 나머지 Task 6, 8, 9 진행**

---

### Task 8: Telegram bot 생성 + token 보관

**Files:**
- Create: `~/.gstack/projects/esp-claw/secrets.env` (gitignore 대상)

- [ ] **Step 1: Telegram에서 BotFather와 대화**

Telegram 앱 → @BotFather → `/newbot` → bot 이름 입력 → 사용자명 입력 → token 받음

- [ ] **Step 2: token 안전 보관**

```bash
mkdir -p ~/.gstack/projects/esp-claw
cat > ~/.gstack/projects/esp-claw/secrets.env <<'EOF'
TELEGRAM_BOT_TOKEN="여기에 token 붙여넣기"
ANTHROPIC_API_KEY=""
HA_PI_IP="라즈베리파이 IP:8123"
HA_LONG_LIVED_TOKEN=""
HA_ENTITY_ID="switch.living_room_lamp"
EOF
chmod 600 ~/.gstack/projects/esp-claw/secrets.env
```

- [ ] **Step 3: bot 동작 테스트**

```bash
source ~/.gstack/projects/esp-claw/secrets.env
curl -s "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/getMe" | python3 -m json.tool
```

Expected: bot 정보가 JSON으로 출력 (`"ok": true`)

---

### Task 9: Anthropic API key 발급 + Pi HA 정보 secrets에 저장

**Files:**
- Modify: `~/.gstack/projects/esp-claw/secrets.env`

- [ ] **Step 1: console.anthropic.com에서 API key 발급**

Account → API Keys → Create Key. 결제 카드 등록 ($5 크레딧 충전 권장)

- [ ] **Step 2: secrets.env에 키 채우기**

```bash
# secrets.env 편집해서 ANTHROPIC_API_KEY="sk-ant-..." 추가
```

- [ ] **Step 3: 라즈베리파이 담당자로부터 받은 정보 채우기**

`HA_PI_IP`, `HA_LONG_LIVED_TOKEN`, `HA_ENTITY_ID` 모두 secrets.env에

- [ ] **Step 4: Pi HA 도달 재검증 (본인이 이미 검증했지만 secrets.env 값 정확성 재확인)**

```bash
source ~/.gstack/projects/esp-claw/secrets.env
curl -sf -H "Authorization: Bearer $HA_LONG_LIVED_TOKEN" "http://${HA_PI_IP}/api/" | python3 -m json.tool
```

Expected: `{"message": "API running."}`

⚠️ 실패 시 토큰 만료 또는 IP 변경 가능성. Pi 담당자 재확인.

---

## Day 2: 보드 Hello World (보드 도착 후, 2-3시간)

### Task 10: 보드 USB 연결 + 시리얼 포트 식별

**Files:** (없음, 환경 검증)

- [ ] **Step 1: 보드 USB 연결 전 포트 목록 캡처**

```bash
ls /dev/cu.* > /tmp/before.txt
```

- [ ] **Step 2: USB 연결 후 새 포트 확인**

```bash
# 보드를 USB-C 케이블로 Mac에 연결
sleep 2
ls /dev/cu.* > /tmp/after.txt
diff /tmp/before.txt /tmp/after.txt
```

Expected: `+/dev/cu.usbmodem...` 같은 새 포트가 보임. 그 경로를 기억.

- [ ] **Step 3: 환경변수에 포트 저장**

```bash
echo "ESP_PORT=/dev/cu.usbmodem<번호>" >> ~/.gstack/projects/esp-claw/secrets.env
source ~/.gstack/projects/esp-claw/secrets.env
echo "$ESP_PORT"
```

⚠️ 포트 안 뜨면: USB-C 케이블 데이터 전송 가능 여부 재확인. 보드 RST/BOOT 버튼 눌러보기.

---

### Task 11: ESP-Claw 공식 펌웨어 web flasher 시도 (옵션 우선)

**Files:** (브라우저 작업)

- [ ] **Step 1: esp-claw.com 또는 비슷한 web flasher URL 확인**

```bash
# Day 0 README/docs 검토에서 web flasher URL 확인
grep -rni "flasher\|esp-claw.com\|web.*install" /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/README.md /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/docs/ 2>/dev/null | head -5
```

- [ ] **Step 2: Chrome으로 web flasher 접속 + 보드 선택**

브라우저(Chrome 또는 Edge)에서 web flasher URL 열기 → "Connect" → /dev/cu.usbmodem... 선택 → 보드 모델 선택 → Flash

- [ ] **Step 3: 플래시 완료 확인**

플래시 후 보드 자동 재부팅. 보드의 디스플레이/LED에서 부팅 신호 확인.

⚠️ web flasher 보드 미지원이면: Task 12로 (로컬 빌드 플래시).

- [ ] **Step 4: web flasher 성공이면 Task 13으로 점프 — 단 단서 확인 (cross-verify 보강)**

⚠️ stock 펌웨어로 시작해도 **Day 3에 커스텀 tool / system prompt 작업이 필요**합니다. Day 0 Task 3 결과에 따라:
- **옵션 A (config-only)**: stock 펌웨어가 외부 설정 파일을 읽는 구조이면 stock으로 끝까지 가능. Day 3에 source build 불필요
- **옵션 B (C+rebuild)**: stock 펌웨어 + 외부 tool 주입은 거의 불가. Task 12 (로컬 빌드)로 돌아와야 함. 일정 영향 없음 (어차피 Day 3에 rebuild 필요했음)

Day 0에서 옵션 분류한 결과를 미리 확인하고 진행 결정.

---

### Task 12: 로컬 빌드 플래시 (web flasher fallback)

**Files:**
- Modify: `application/edge_agent/sdkconfig.defaults` (보드 선택)

- [ ] **Step 1: menuconfig으로 보드 선택**

```bash
. ~/esp/esp-idf/export.sh
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/application/edge_agent
idf.py menuconfig
```

UI에서: ESP-Claw 메뉴 → Board Selection → Day 0에서 결정한 보드. ESC ESC ESC ENTER로 저장.

- [ ] **Step 2: 빌드**

```bash
idf.py build
```

Expected: 5-10분 소요. "Project build complete"

- [ ] **Step 3: 플래시 + monitor 동시 실행**

```bash
idf.py -p $ESP_PORT -b 460800 flash monitor
```

Expected: 플래시 완료 후 자동 monitor 진입. 시리얼 로그가 흐르기 시작.

- [ ] **Step 4: monitor 종료 (Ctrl+])**

`Ctrl + ]` 로 monitor 빠져나오기.

⚠️ 플래시 실패 (port busy / permission denied): 다른 시리얼 프로그램 종료. `lsof $ESP_PORT` 확인.

---

### Task 13: 보드 Wi-Fi provisioning + Telegram/Anthropic 설정

**Files:** (보드 첫 부팅 시 BLE/캡티브 포털 또는 menuconfig)

- [ ] **Step 1: ESP-Claw provisioning 방식 확인**

Day 0 README에서 "Provisioning" 또는 "First boot" 섹션 검색:

```bash
grep -ni "provision\|first.*boot\|ble.*config\|captive" /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/README.md /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/docs/*.md 2>/dev/null | head -10
```

- [ ] **Step 2: provisioning 방식 별로 분기**

- BLE provisioning: ESP BLE Provisioning 앱(iOS/Android) 또는 esp-claw 전용 앱으로 SSID/비밀번호 + bot token + API key 입력
- Captive portal: 보드가 AP 모드로 들어가면 폰/Mac에서 그 SSID 접속 → 브라우저에서 192.168.4.1 → form 입력
- Kconfig (build-time): menuconfig에서 직접 입력 후 rebuild + reflash. 보안상 권장 안 함

- [ ] **Step 3: secrets.env 값 입력**

본인의 Wi-Fi SSID/PW (사무실 또는 모바일 테더링), `TELEGRAM_BOT_TOKEN`, `ANTHROPIC_API_KEY`. 

- [ ] **Step 4: 보드 재부팅 + monitor에서 연결 로그 확인**

```bash
idf.py -p $ESP_PORT monitor
```

Expected 로그:
- `Wi-Fi connected, IP: 192.168.x.x`
- `Telegram bot ready` 또는 비슷한 메시지
- `Anthropic API configured` 또는 비슷한 메시지

- [ ] **Step 5: Telegram bot에 "안녕" 메시지 전송 → 한국어 응답 받음**

본인 Telegram 앱에서 bot 찾아서 (bot username으로 검색) "안녕" 입력. 5-10초 내에 응답이 오면 Wi-Fi + LLM 경로 검증 완료.

⚠️ 응답 없음: monitor 로그에서 에러 확인. 흔한 원인: API key 틀림, Wi-Fi 약함, NAT 차단.

- [ ] **Step 6: commit (있다면)**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
git status
# 본인이 만든 변경(보드 선택 등) 있으면 commit
git add -p   # 의도한 파일만 stage
git commit -m "config: select board <보드명>, enable telegram"
```

---

## Day 3: HA 통합 (MCP 경로 확정 — 2026-05-06 PIVOT)

### 🔥 PIVOT NOTE — Day 0 Task 3 결과로 경로 변경

**원래 가정**: Day 3에 Lua 또는 C로 `ha_call_service` tool을 ESP-Claw 펌웨어에 작성.

**Day 0 발견**: ESP-Claw는 사용자 추가 tool을 받는 두 가지 길만 제공:
1. **C capability + Kconfig + rebuild** (option B classic, +1.5일)
2. **외부 MCP server를 cap_mcp_client로 호출** (B-lite, 펌웨어 수정 0줄)

**사용자 결정**: B-lite (MCP). Pi 담당자 협조 OK.

### 새로운 Day 3 Task 14-16 (MCP 경로)

기존 Task 14-16 (의사 C/Lua 코드 작성)는 **fallback only**로 격하. MCP server가 Pi에 설치 안 되거나 동작 안 할 때만 사용.

**신규 Day 3 흐름**:
- (a) Pi 담당자가 Pi에 HA MCP server 설치 (예: hass-mcp from PyPI 또는 community 프로젝트). 본인 작업 아님
- (b) MCP server URL/spec 본인에게 전달 (예: `http://<PI_IP>:8124/mcp` 또는 stdio over SSH 등 — server 종류에 따라)
- (c) ESP-Claw NVS에 MCP server endpoint 추가. 정확한 NVS 키는 Day 0 추가 분석 필요 (`grep -rn mcp_server\|mcp_url components/claw_capabilities/cap_mcp_client/` 로 발견 가능)
- (d) ESP-Claw `en_cap_groups`에 `cap_mcp_client` 활성화 (default-on 여부 Day 0 확인 가능)
- (e) system prompt에 "사용 가능한 도구는 MCP server의 turn_on/turn_off뿐. 그 외 명령은 거절" allowlist 명시
- (f) Telegram에 "거실 조명 켜줘" → LLM이 mcp_call_tool → Pi MCP server → HA REST → 램프 ON

**구체 file:line + 명령어는 Day 0 Task 3.5 (Pi MCP server 가용성) 결과 받은 후 채울 것**. 미리 구체 코드 작성하면 Pi 측 server 종류에 따라 무용 가능.

### 기존 Day 3 Task 14-16 (Classic B fallback, 아래 유지) — REJECTED unless MCP path fails

⚠️ Day 0의 결과(옵션 A vs B)에 따라 분기. 아래는 옵션 A 기준. 옵션 B는 Appendix 참조.

⚠️ 2026-05-06 cross-verify pivot: 옵션 A는 ESP-Claw에 존재하지 않음 (Day 0 Task 3 확인). 아래 의사 코드는 Classic B (cap_ha_call_service C 신설) 시 참고용으로만 유지.

### Task 14: ha_call_service tool schema 정의

**Files:**
- Create or Modify: Day 0에서 발견한 tool config 위치 (예: `application/edge_agent/main/tools.json` 또는 `tools.lua`)

- [ ] **Step 1: 기존 example tool 한 개 통째로 복사 (skeleton 확보)**

Day 0에서 발견한 example tool을 그대로 복사해서 새 tool로 변형 시작. 위치는 Day 0 notes 참조.

- [ ] **Step 2: ha_call_service tool schema 작성 (JSON 기준 예시)**

```json
{
  "name": "ha_call_service",
  "description": "Call a Home Assistant service to control a smart device. Only use for ON/OFF commands on the allowed entity.",
  "input_schema": {
    "type": "object",
    "properties": {
      "domain": {
        "type": "string",
        "enum": ["switch"],
        "description": "HA domain. Only 'switch' is allowed."
      },
      "service": {
        "type": "string",
        "enum": ["turn_on", "turn_off"],
        "description": "Service to call. Only turn_on or turn_off."
      },
      "entity_id": {
        "type": "string",
        "enum": ["switch.living_room_lamp"],
        "description": "Entity to control. Only switch.living_room_lamp is allowed."
      }
    },
    "required": ["domain", "service", "entity_id"]
  }
}
```

⚠️ ESP-Claw schema format이 다르면 동일 의미로 변환. 핵심은 `enum` 으로 allowlist 적용.

- [ ] **Step 3: 파일 저장**

Day 0 발견 위치에 저장. 빌드/리로드 방식은 Day 0 결과 따름.

- [ ] **Step 4: ESP-Claw 리로드 (옵션 A: rebuild 없이 재시작 가능, 옵션 B: rebuild 필요)**

옵션 A:
```bash
# 보드 RST 또는 monitor에서 'r' 또는 ESP-Claw 자체 reload 명령
idf.py -p $ESP_PORT monitor
```

옵션 B: Appendix 참조

- [ ] **Step 5: monitor 로그에서 tool 등록 확인**

```
Tool registered: ha_call_service
```

같은 로그가 나오는지 확인.

⚠️ 로그 없음: tool config 파싱 실패. 로그에서 JSON 파싱 에러 검색.

- [ ] **Step 6: commit**

```bash
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw
git add -p
git commit -m "feat: add ha_call_service tool schema with allowlist"
```

---

### Task 15: HA REST API HTTP client 구현 + retry 로직

**Files:**
- Create or Modify: tool 핸들러 위치 (Day 0에서 결정. 옵션 A는 Lua/JS, 옵션 B는 C)

⚠️ 아래는 옵션 B (C) 기준 의사 코드. 옵션 A는 같은 의도로 Lua/JS로 변환.

- [ ] **Step 1: HTTP client 헤더 + struct 셋업**

```c
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "nvs.h"

#define HA_HOST_KEY "ha_pi_ip"
#define HA_TOKEN_KEY "ha_token"
#define MAX_RETRIES 3
#define RETRY_BACKOFF_MS 200
```

- [ ] **Step 2: NVS에서 토큰 + IP 읽기**

```c
static esp_err_t ha_load_config(char *host, size_t host_len, char *token, size_t token_len) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("ha_cfg", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, HA_HOST_KEY, host, &host_len);
    if (err == ESP_OK) err = nvs_get_str(h, HA_TOKEN_KEY, token, &token_len);
    nvs_close(h);
    return err;
}
```

- [ ] **Step 3: ha_call_service tool handler 구현 (server-side allowlist + fail-fast retry, cross-verify 보강)**

```c
typedef struct {
    char *domain;
    char *service;
    char *entity_id;
} ha_call_args_t;

// Server-side allowlist (cross-verify Codex 권장: LLM hallucination 시 C 진입에서 차단)
static bool ha_args_allowed(const ha_call_args_t *args) {
    if (strcmp(args->domain, "switch") != 0) return false;
    if (strcmp(args->service, "turn_on") != 0 && strcmp(args->service, "turn_off") != 0) return false;
    if (strcmp(args->entity_id, "switch.living_room_lamp") != 0) return false;
    return true;
}

esp_err_t ha_call_service_handler(const ha_call_args_t *args, char *response_out, size_t out_len) {
    // 1. server-side allowlist
    if (!ha_args_allowed(args)) {
        snprintf(response_out, out_len, "ERROR: 허용되지 않은 명령. switch.living_room_lamp의 turn_on/turn_off만 가능합니다.");
        return ESP_FAIL;
    }

    // 2. config 로드
    char host[64], token[256];
    if (ha_load_config(host, sizeof(host), token, sizeof(token)) != ESP_OK) {
        snprintf(response_out, out_len, "ERROR: HA config not provisioned");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://%s/api/services/%s/%s", host, args->domain, args->service);
    char auth_header[320];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", args->entity_id);

    // 3. retry는 transient 에러에만 (cross-verify 합의: 4xx 인증/요청 오류는 fail-fast)
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        esp_http_client_config_t cfg = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 3000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_http_client_set_header(client, "Authorization", auth_header);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK && status >= 200 && status < 300) {
            snprintf(response_out, out_len, "OK: %s.%s called on %s", args->domain, args->service, args->entity_id);
            return ESP_OK;
        }

        // Fail-fast: 4xx 클라이언트 에러는 retry 무의미
        if (err == ESP_OK && status >= 400 && status < 500) {
            ESP_LOGE("ha", "HTTP %d (client error) — fail fast", status);
            snprintf(response_out, out_len, "ERROR: 인증/요청 오류 (HTTP %d). 토큰 또는 entity_id 확인 필요.", status);
            return ESP_FAIL;
        }

        ESP_LOGW("ha", "Attempt %d transient failure: err=%d status=%d", attempt+1, err, status);
        vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
    }

    snprintf(response_out, out_len, "ERROR: HA call failed after %d retries (transient). Only ON/OFF supported on switch.living_room_lamp.", MAX_RETRIES);
    return ESP_FAIL;
}
```

⚠️ 옵션 A의 경우 같은 로직을 Lua로 (서버-사이드 allowlist + fail-fast 동일 적용):

```lua
local http = require("http")
local nvs = require("nvs")

local ALLOWED_DOMAIN = "switch"
local ALLOWED_SERVICES = { turn_on = true, turn_off = true }
local ALLOWED_ENTITIES = { ["switch.living_room_lamp"] = true }

function ha_call_service(args)
    -- server-side allowlist
    if args.domain ~= ALLOWED_DOMAIN
       or not ALLOWED_SERVICES[args.service]
       or not ALLOWED_ENTITIES[args.entity_id] then
        return "ERROR: 허용되지 않은 명령. switch.living_room_lamp의 turn_on/turn_off만 가능합니다."
    end

    local host = nvs.get("ha_cfg", "ha_pi_ip")
    local token = nvs.get("ha_cfg", "ha_token")
    local url = string.format("http://%s/api/services/%s/%s", host, args.domain, args.service)
    local body = string.format('{"entity_id":"%s"}', args.entity_id)

    for attempt = 1, 3 do
        local ok, status = http.post(url, {
            headers = { ["Authorization"] = "Bearer " .. token, ["Content-Type"] = "application/json" },
            body = body,
            timeout_ms = 3000
        })
        if ok and status >= 200 and status < 300 then
            return "OK"
        end
        -- fail-fast: 4xx 클라이언트 오류는 retry 무의미
        if ok and status >= 400 and status < 500 then
            return string.format("ERROR: 인증/요청 오류 (HTTP %d). 토큰/entity 확인.", status)
        end
        os.sleep(0.2)
    end
    return "ERROR: HA call failed after 3 retries (transient). Only ON/OFF supported."
end
```

- [ ] **Step 4: 빌드 + 플래시**

옵션 B만:
```bash
. ~/esp/esp-idf/export.sh
cd /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/application/edge_agent
idf.py build
idf.py -p $ESP_PORT flash monitor
```

옵션 A: 보드 RST 또는 reload 명령으로 끝.

- [ ] **Step 5: HA token + IP를 NVS에 입력 (시리얼 콘솔 또는 menuconfig 또는 캡티브 포털)**

ESP-Claw가 NVS 입력 인터페이스를 어떻게 노출하는지에 따라 다름. Day 0에서 발견한 방식 사용.

- [ ] **Step 6: monitor에서 tool 핸들러 호출 시뮬레이션**

ESP-Claw가 직접 tool dispatch 테스트 명령 제공할 수 있음 (예: monitor에서 `tool ha_call_service '{"domain":"switch","service":"turn_on","entity_id":"switch.living_room_lamp"}'`). 없으면 Telegram에서 직접.

- [ ] **Step 7: 검증 — Telegram에 "거실 조명 켜줘" 입력 → 램프 ON**

Pi HA 정말 도달하는지 확인. 5초 내 동작.

- [ ] **Step 8: commit**

```bash
git add -p
git commit -m "feat: implement ha_call_service tool handler with retry"
```

---

### Task 16: System prompt 작성 + 적용

**Files:**
- Create or Modify: ESP-Claw system prompt 위치 (Day 0에서 발견. 보통 `application/edge_agent/main/prompts/` 또는 sdkconfig)

- [ ] **Step 1: system prompt 한국어 본문 준비**

```text
당신은 스마트 홈 디바이스 컨트롤러입니다. 사용자의 한국어 자연어 명령을 해석하여
Home Assistant의 ha_call_service 도구를 호출합니다.

## 절대 규칙
1. 사용 가능한 명령은 ha_call_service tool 호출뿐입니다.
2. 허용된 entity_id는 ["switch.living_room_lamp"]만 입니다.
3. 허용된 service는 ["turn_on", "turn_off"]만 입니다.
4. 허용된 domain은 ["switch"]만 입니다.
5. 위 범위를 벗어난 모든 명령은 정확히 다음 문구로 거절하세요:
   "이 기기는 ON/OFF만 가능합니다. 거실 조명만 제어할 수 있어요."
6. tool을 호출하지 않고 행동을 묘사하는 응답은 절대 금지합니다.
   예: "켰습니다" "조절했습니다" 같이 tool 호출 없이 행동 묘사하면 거짓말입니다.
7. ON 동의어 ("켜", "켜줘", "불 켜", "환하게"): switch.turn_on
8. OFF 동의어 ("꺼", "꺼줘", "불 꺼", "어둡게"): switch.turn_off
9. 그 외 명령(밝기 조절, 다른 방, 시간 조건, 색상 변경 등): 위 5번 규칙에 따라 거절
```

- [ ] **Step 2: ESP-Claw에 system prompt 등록 위치 확인**

Day 0 README/docs에서 "system prompt" 또는 "instruction" 검색:

```bash
grep -rni "system.*prompt\|instruction" /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/README.md /Users/wm-mac-01/Desktop/esp-claw/main/esp-claw/docs/ 2>/dev/null
```

- [ ] **Step 3: 발견된 위치(Lua config / sdkconfig / NVS / 별도 파일)에 prompt 입력**

ESP-Claw 컨벤션 따름.

- [ ] **Step 4: 보드 재시작 후 monitor에서 prompt 적용 확인**

```
System prompt loaded: <첫 50자>...
```

같은 로그.

- [ ] **Step 5: commit**

```bash
git add -p
git commit -m "feat: add Korean system prompt with entity allowlist"
```

---

## Day 4: 통합 검증 + 5가지 변주 테스트 (1일)

각 변주는 Telegram에서 입력 → 보드 monitor 로그 확인 → 기대 동작 검증.

### Task 17: Variant 1 — "거실 조명 켜줘" → 램프 ON ✅

- [ ] **Step 1: monitor 켜둔 상태에서 Telegram에 "거실 조명 켜줘" 입력**

```bash
idf.py -p $ESP_PORT monitor
# 다른 터미널에서 본인 Telegram 앱으로 입력
```

- [ ] **Step 2: 기대 동작 확인**
- monitor: `tool_use: ha_call_service domain=switch service=turn_on entity_id=switch.living_room_lamp`
- monitor: `HA POST 200 OK`
- 물리: 램프 ON
- Telegram: "OK" 또는 한국어 확인 응답

- [ ] **Step 3: 실패 시 디버깅**
- entity_id wrong: system prompt 강화
- HA 응답 != 200: token/network/HA 측 점검
- silent text: system prompt rule 6 강화

- [ ] **Step 4: PASS 시 다음 variant**

---

### Task 18: Variant 2 — "환하게 해줘" → switch.turn_on (동의어) ✅

- [ ] **Step 1: Telegram에 "환하게 해줘" 입력**

- [ ] **Step 2: 기대 동작**
- system prompt rule 7에 따라 ON 동의어로 해석 → switch.turn_on
- 램프 ON

- [ ] **Step 3: 만약 LLM이 "이건 밝기 조절이라 안 돼요" 식으로 거절하면**:
   system prompt rule 7을 더 명시적으로 변경 ("환하게 = ON, 어둡게 = OFF, 밝기 % 명시는 거절")

---

### Task 19: Variant 3 — "환하게 50%로" → 거절 ❌

- [ ] **Step 1: Telegram에 "환하게 50%로" 입력**

- [ ] **Step 2: 기대 동작**
- LLM이 % 키워드 인식하여 "이 기기는 ON/OFF만 가능합니다..." 거절 응답
- tool 호출 안 함
- 램프 상태 변화 없음

- [ ] **Step 3: 만약 LLM이 무리해서 turn_on 호출하면**:
   system prompt에 "%, 숫자, 정도 표현은 항상 거절" 규칙 추가

---

### Task 20: Variant 4 — "안방 조명 켜" → 거절 ❌

- [ ] **Step 1: Telegram에 "안방 조명 켜" 입력**

- [ ] **Step 2: 기대 동작**
- entity_id "안방"은 allowlist에 없음 → 거절
- "거실 조명만 제어할 수 있어요" 응답

- [ ] **Step 3: 만약 LLM이 switch.living_room_lamp로 mismatched dispatch하면**:
   system prompt rule 2를 더 명시적으로

---

### Task 21: Variant 5 — "5분 뒤에 꺼" → 거절 ❌

- [ ] **Step 1: Telegram에 "5분 뒤에 꺼" 입력**

- [ ] **Step 2: 기대 동작**
- 시간 조건 미지원 → 거절
- 즉시 turn_off 호출 안 함

- [ ] **Step 3: 모든 5가지 PASS 시 commit**

```bash
git add -p
git commit -m "test: 5 Korean variants pass (2 ON-synonyms, 3 rejection)"
```

⚠️ 한 variant라도 FAIL이면 system prompt 수정 → 5가지 모두 재테스트.

---

### Task 22: HA 측 의도 외 상태 시뮬레이션 (mid-failure 검증)

목적: 실제 시연 시 발생 가능한 mid-failure 케이스 검증.

- [ ] **Step 1: HA 토큰 일시 오염 → 401 응답 시뮬레이션**

NVS에서 토큰 한 글자 바꾸고 보드 재시작. Telegram에 "거실 조명 켜줘".

기대: 3회 retry → "ERROR: HA call failed after 3 retries..." 응답.
램프 상태 변화 없음 + Telegram에 한국어 친절한 에러 안내.

- [ ] **Step 2: 토큰 복구**

NVS에 정상 토큰 다시 입력.

- [ ] **Step 3: Pi HA 일시 정지 → connection refused 시뮬레이션 (Pi 담당자에 부탁 가능 시)**

가능하면 Pi HA 30초간 중지 → Telegram 명령 → retry 3회 후 안내 응답.
불가능하면 skip.

- [ ] **Step 4: commit (테스트 결과 노트)**

```bash
echo "
## Day 4 변주 테스트 결과
- Variant 1-5: PASS (모두 의도 동작)
- Mid-failure 401: PASS (3 retry + 안내)
- Mid-failure connection refused: <PASS/SKIP>
" >> /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/notes-day0.md
```

---

## Day 5: 리허설 + Sanity Script + 백업 영상

### Task 23: 시연 환경 리허설 3회

- [ ] **Step 1: 시연 회의실 (또는 동등 환경)에서 1회차 dry-run**

Sanity script (Task 24) 5단계 실행 → "거실 조명 켜줘" → ON → "꺼줘" → OFF → 한 번 더 ON/OFF.

소요 시간 측정. 5분 이내 셋업 + 5분 시연 가능한지.

- [ ] **Step 2: 2회차 — 일부러 실패 시뮬레이션**

NAT 끊김(Wi-Fi off 1초 후 on) → 보드 재연결 시간 측정. 시연 중 발생 시 fallback 멘트 연습.

- [ ] **Step 3: 3회차 — 본인이 대표 역할로 변주 5개 던져보기**

ON, "환하게", "5분 뒤에 꺼", "안방 켜", "어둡게" — 응답 어떤지 자기 모니터링.

- [ ] **Step 4: 발견한 문제 fix + 재테스트**

---

### Task 24: 시연 당일 Sanity Script 정리 + 본인 손에 익히기

**Files:**
- Create: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/sanity-script.sh`

- [ ] **Step 1: sanity script bash로 만들기 (caffeinate cleanup trap 포함, cross-verify Lead 보강)**

```bash
#!/usr/bin/env bash
set -e
source ~/.gstack/projects/esp-claw/secrets.env

# caffeinate cleanup trap — 시연 종료 또는 Ctrl+C 시 자동 kill
CAFFEINATE_PID=""
cleanup() {
    if [ -n "$CAFFEINATE_PID" ] && kill -0 "$CAFFEINATE_PID" 2>/dev/null; then
        kill "$CAFFEINATE_PID" 2>/dev/null
        echo "[cleanup] caffeinate stopped (PID $CAFFEINATE_PID)"
    fi
}
trap cleanup EXIT INT TERM

echo "[1/5] caffeinate (Mac sleep block — 시연 종료 시 자동 해제)"
caffeinate -d -i -m -s &
CAFFEINATE_PID=$!
echo "  caffeinate PID: $CAFFEINATE_PID"

echo "[2/5] Pi HA reachability"
RESULT=$(curl -sf -H "Authorization: Bearer $HA_LONG_LIVED_TOKEN" "http://${HA_PI_IP}/api/" 2>&1)
echo "  $RESULT" | grep -q "API running" && echo "  PASS" || { echo "  FAIL — Pi 담당자 즉시 연락"; exit 1; }

echo "[3/5] 보드 monitor — '준비 완료' 로그 확인"
echo "  다른 터미널에서: idf.py -p $ESP_PORT monitor"
echo "  로그에서 'Wi-Fi connected' + 'Telegram bot ready' + 'System prompt loaded' 확인"
read -p "  세 로그 모두 보였으면 Enter, 아니면 Ctrl+C: "

echo "[4/5] Telegram + LLM 통합 ping (Telegram dry-run으로 LLM 통과 검증, cross-verify Codex 의견 채택)"
echo "  본인 Telegram 앱에서 bot에게 'ping' 또는 짧은 한국어 입력"
echo "  bot이 한국어로 의미있는 답을 5초 내 보내면 Telegram + Anthropic API 양쪽 OK"
read -p "  응답 도착했으면 Enter, 아니면 Ctrl+C: "

echo "[5/5] 실제 명령 dry-run"
echo "  Telegram에 '거실 조명 켜줘' 입력"
read -p "  램프 ON 확인했으면 Enter, 아니면 Ctrl+C: "
echo "  Telegram에 '꺼줘' 입력"
read -p "  램프 OFF 확인했으면 Enter, 아니면 Ctrl+C: "

echo "All sanity checks PASSED. 시연 준비 완료."
echo "(시연 종료 시 이 스크립트 종료하면 caffeinate 자동 해제)"
```

- [ ] **Step 2: 실행 권한 부여 + 한 번 실행**

```bash
chmod +x /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/sanity-script.sh
/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/sanity-script.sh
```

- [ ] **Step 3: 시연 5분 전 dry-run 가이드**

5분 전에는 [4/5] + [5/5]만 한 번 더 반복하면 충분. sanity script 인자로 `--quick` 옵션 추가도 가능 (생략).

- [ ] **Step 4: commit**

```bash
git add /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/sanity-script.sh
git commit -m "feat: add 5-step sanity script for demo day"
```

---

### Task 25: 백업 영상 녹화 + 후속 답변 카드

**Files:**
- Create: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/backup-demo.mp4`
- Create: `/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/qa-cards.md`

- [ ] **Step 1: 성공 시연 1회를 QuickTime 화면녹화 + 폰 카메라로 동시 녹화**

QuickTime: Mac 화면(monitor 로그 + Telegram 채팅) 녹화.
폰 카메라: 책상 위 램프 ON/OFF 영상 녹화.

길이 1-2분.

- [ ] **Step 2: 영상 저장**

`/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/backup-demo.mp4`

- [ ] **Step 3: 답변 카드 작성 (cross-verify 정정 반영)**

```markdown
# 후속 질문 답변 카드 (3장만, 외워서)

## 비용
- 보드 ~10만원, 플러그 1.5만원 (1회성)
- LLM API 월 1-2만원 (개인 사용량 기준)
- 대량 배포 시 LLM 비용이 변수 — 1만 명 동시 사용 시 월 수백만원 수준 예상

## 보안
- 외부 통신: Anthropic API + Telegram Bot API (둘 다 HTTPS)
- 디바이스 제어는 100% 사내망 (HA가 모든 디바이스 인증 처리)
- 솔직히: 현 PoC에서 보드↔Pi 구간은 평문 HTTP로 토큰 전송. 프로덕션 시 HTTPS + 인증서 권장
- LLM hallucination 방어: ESP-Claw에 server-side allowlist 추가로 LLM이 잘못 호출해도 디바이스에 도달 안 함

## 확장성
- HA에 새 기기(매터, Zigbee, Z-Wave) 추가 시 ESP-Claw 코드 변경 없음
- system prompt + server-side allowlist 1줄만 갱신하면 자연어로 즉시 제어 가능
- 단점: LLM hallucination → entity allowlist + retry 정책 + server-side 검증 3단으로 차단

## 백업 영상 모드 disclosure (필요 시)
- "환경 이슈로 사전 녹화본 보여드리겠습니다"
- 실패를 숨기지 않음. 신뢰 유지가 시연 본질보다 중요
```

- [ ] **Step 4: 답변 카드 인쇄 또는 폰에 저장**

시연 들어가기 전에 한 번 읽기.

- [ ] **Step 5: commit**

```bash
git add /Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/qa-cards.md
git commit -m "docs: add Q&A cards for demo follow-up"
```

⚠️ 영상 파일은 git에 push 안 함 (.gitignore에 *.mp4 추가).

---

## Demo Day

### Task 26: 시연 30분 전 + 5분 전 sanity 실행

- [ ] **Step 1: 30분 전 회의실 입장**

```bash
/Users/wm-mac-01/Desktop/esp-claw/main/docs/superpowers/sanity-script.sh
```

5단계 모두 PASS 확인.

- [ ] **Step 2: 5분 전 dry-run**

[4/5] + [5/5]만 반복.

- [ ] **Step 3: 시연**

대표 입장 → 본인이 Telegram에 "거실 조명 켜줘" → 램프 ON → "꺼줘" → 램프 OFF → 한 번 더 → 후속 질문 받음 → 답변 카드대로 답.

⚠️ Sanity 실패 시: 디자인 문서 "Sanity 실패 시 의사결정 트리" 따름. 백업 영상 모드는 최후의 보루.

---

## Appendix A: Day 3 옵션 B (C+rebuild) 변형

옵션 A 대비 추가 단계:

1. tool handler를 C 함수로 작성 (Task 15 Step 1-3 그대로 사용)
2. ESP-Claw component CMakeLists.txt에 새 파일 등록
3. tool 등록 매크로 호출:
```c
ESP_CLAW_REGISTER_TOOL(ha_call_service, ha_call_service_handler, ha_call_service_schema_json);
```
4. `idf.py build` 후 `idf.py flash` (NVS는 보존됨)
5. 매 변경 시 rebuild 5-10분 소요 → iteration 느려짐
6. 디자인 문서 buffer 정책에 따라 +1.5일 흡수

---

## Appendix B: 자주 막히는 곳 + 디버그 팁

- **`idf.py: command not found`** → `. ~/esp/esp-idf/export.sh` 안 됨. .zshrc 확인.
- **`fatal: serial port busy`** → 다른 monitor 띄워둔 상태. `pkill idf_monitor.py`.
- **`HTTP 401` from HA** → 토큰 만료 또는 잘못 입력. Pi HA `/profile/security`에서 새로 발급.
- **Telegram 응답 없음, monitor에는 메시지 도착 로그 있음** → Anthropic API 키 잘못. monitor에서 401/403 검색.
- **monitor 로그에서 한글 깨짐** → `idf.py monitor` 기본 인코딩이 UTF-8이지만 일부 터미널에서 깨짐. iTerm2 + UTF-8 추천.
- **램프 안 켜지는데 monitor에서 200 OK** → HA 측 entity_id 다른 가능성. HA UI에서 entity 이름 재확인.

---

## 자체 리뷰 결과 (cross-verify 통합 후 v2)

**Spec coverage**: 디자인 문서 모든 섹션 매핑됨 (Given, Architecture, Open Questions, Day 0-5, Sanity, Q&A 카드).

**Placeholder scan**: "Day 0 결과로 결정" 같은 표현은 placeholder가 아니라 의도된 dependency (Day 0 본문에 구체적인 grep 명령 + 분류 기준 명시). "TBD/TODO/FIXME" 0건.

**Type consistency**: tool 이름 `ha_call_service` 일관, entity_id `switch.living_room_lamp` 일관, 토큰 변수 `HA_LONG_LIVED_TOKEN` 일관.

**Cross-verify 패치 적용 (v2)**:
- ✅ Day 0 Task 5.5 (Anthropic + tool_use 검증) 신설 — 가장 큰 위험 차단
- ✅ Day 0 Task 5.6 (Wi-Fi client isolation + 보드↔Pi 도달성) 신설
- ✅ Day 0 Task 3 Step 6 (provisioning/NVS 편집 방법 발견) 신설
- ✅ Task 6 Step 6 (menuconfig 후 빌드 강조) 정정
- ✅ Task 7 Step 2 (안정 USB 어댑터 / brownout 주의) 보강
- ✅ Task 11 Step 4 (stock flasher → Day 3 커스텀 작업 단서) 보강
- ✅ Task 15 Step 3 (server-side allowlist + 4xx fail-fast) 보강 — C/Lua 양쪽 모두
- ✅ Task 24 Step 1 (sanity script caffeinate cleanup trap + Telegram=LLM 통합 ping) 보강
- ✅ Task 25 Step 3 (Q&A 카드 외부 통신 정정 + 백업 영상 disclosure 멘트) 정정
- ✅ Task 7 Step 1 (ESP-BOX-3 / esp_box_3 정확한 명칭) 정정

**남은 예상 리스크**: ESP-Claw 실제 API가 본 plan의 의사 코드와 다를 가능성 — 이 plan은 가장 흔한 esp-idf 패턴 기준으로 작성. Day 0 결과에 따라 Task 14-16 코드는 본인이 가공해야 함. v2에서는 옵션 A/B/C 분기를 명시적으로 다루어 risk 가시화함.

**보류 항목 (선택적, 시간 여유 시)**:
- system prompt를 "영어 규칙 + 한국어 예시" 패턴으로 재작성 (Anthropic 권장 — Codex 단독 의견)
- HTTPS Root CA — Anthropic API 호출 시 ESP-Claw가 mbedtls Root CA를 어떻게 처리하는지 Day 2 monitor 로그에서 추가 확인 (Gemini 단독 의견)
