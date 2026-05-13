# ESP-Claw 스마트홈 — 현재 아키텍처 분석 (2026-05-13, rev. HA 설정 + Portability)

> **목적:** v3-v5 사이클이 main 에 안착한 현재 시점에서 esp-claw 가 스마트홈 비서로 어떻게 동작하는지, 왜 ESP-IDF 펌웨어 플래싱을 거쳐야 했는지, 어떤 파일들을 수정했는지, 두 layer 의 "에이전트 학습", **그리고 HA 연결 설정 + 다른 네트워크/사용자 portability** 까지 한 문서에 정리.
>
> **이번 rev (2026-05-13) 추가:** §6 — HA 환경설정 + Portability (사용자 Q&A 7건 verbatim + 답변).

- **대상 독자:** 이 프로젝트를 인계받거나 v6 부터 참여할 엔지니어. 사전 지식: 일반 임베디드 + 스마트홈 + LLM API 사용 경험. ESP-IDF 경험은 무관.
- **자매 문서:** [`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`](./02-openclaw-vs-esp-claw-comparison.md) — Pi 위에서 도는 사전 구현 (openclaw-smarthome) 과의 layer-by-layer 비교.
- **모든 path 는 절대경로 (`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/...`) 로 표기.**

---

## 0. TL;DR

ESP-Claw 는 Espressif 가 만든 오픈소스 ESP32 firmware (LLM 에 capabilities 를 노출하는 보드 측 agent runtime). 우리는 이 위에 **`cap_ha_control`** capability 를 추가해서 보드 한 대가 OpenAI gpt-5-mini 를 호출하고 Home Assistant 의 기기를 자연어로 제어/자동화하도록 만들었다.

핵심 결정 5가지:

1. **공식 web flasher (https://esp-claw.com) 사용 실패 → ESP-IDF 빌드로 우회.** 원인: 보드의 Flash 칩 brand (MXIC) 가 esp-claw stock image 의 HPM (High Performance Mode) 가속 코드와 비호환. 8MB stock image 와 16MB N16R8 보드 mismatch 까지 겹쳐 부팅 초기 MSPI timing tuning 단계에서 무한 reboot. sdkconfig 3줄 수정 (`FLASHSIZE_16MB / FLASHFREQ_80M / HPM_DISABLED`) + ESP-IDF v5.5.4 빌드로 해결.

2. **보드-측 typed tool 로 HA 와 직접 통신.** `cap_ha_control` 은 HA Core REST API (`/api/services/<domain>/<action>`, `/api/states`, `/api/config/automation/config/<id>`) 를 직접 호출. HA Matter integration 이나 HA MCP server 같은 중간 layer 없음.

3. **LLM 가시 capability 는 두 개**: `ha_control` (기기 즉시 제어) + `ha_automation` (HA 자동화 등록/수정/제거/조회/발화/enable/disable). 둘 다 input_schema_json (JSON Schema as C string literal) + 한국어 description 으로 정의.

4. **에이전트 학습은 두 layer**:
   - **보드 내부 LLM** (firmware-embedded): capability descriptor + `cap_ha_compose_description` 의 runtime-friendly_names 보간 + entity registry NVS 캐시.
   - **외부 Claude Code 하네스** (개발 사이클): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/`, `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/`, `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/` 의 markdown 시리즈 + subagent-driven-development skill + completion-intent hook.

5. **모든 사용자 설정은 NVS partition 의 binary key-value store 에 저장 — 코드 박힌 IP/token 없음.** 다른 사용자/네트워크로의 portability 는 *플래싱이 아닌 NVS 재입력* 으로 달성. 자세한 건 §6.

---

## 1. 시스템 개요

### 1.1 하드웨어 stack

```
┌────────────────────────────────────────────────────────────────┐
│ ESP32-S3 N16R8 (사용자 보드, 16MB Flash MXIC + 8MB OCT PSRAM)   │
│                                                                │
│   ┌─────────────────┐   ┌──────────────────┐                  │
│   │ USB-JTAG/OTG    │   │ CH343 USB-UART   │ ← 듀얼 USB 포트   │
│   │ (PID 0x1001)    │   │ (PID 0x55d3)     │   CH343 가 flash  │
│   └─────────────────┘   └──────────────────┘   에 더 안정적    │
│                                                                │
│   ┌────────────────────────────────────────────────────────┐  │
│   │ ESP-IDF v5.5.4 부트로더 + FreeRTOS + esp-claw 펌웨어     │  │
│   │  Partition 0xb20000 + 4MB (storage)                    │  │
│   │  Partition 0x20000 + 4MB (app, ota_0)                  │  │
│   │  Partition 0x9000 + 24KB (NVS — 사용자 설정 저장)       │  │
│   └────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────┘
            │  Wi-Fi STA (2.4 GHz, NVS 저장 credentials)
            ▼
┌──────────────────────────────────────────────────────────────────┐
│ Home Assistant (Raspberry Pi 5, 192.168.1.94:8123)               │
│   /api/states  /api/services/*  /api/config/automation/config/*  │
└──────────────────────────────────────────────────────────────────┘
            │
            ▼
┌──────────────────────────────────────────────────────────────────┐
│ Zigbee / Wi-Fi 기기들 (도어센서, 조명, 콘센트, 온습도 센서 …)        │
└──────────────────────────────────────────────────────────────────┘
```

Partition 정의 (절대경로):
- `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/partitions_16MB.csv` (16MB Flash 용, 현재 사용)
- `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/partitions_8MB.csv` (8MB Flash 용, N8R8 보드)

16MB layout:
```
nvs,       data, nvs,     0x9000,   0x6000     ← 사용자 설정 (24KB)
otadata,   data, ota,     0xF000,   0x2000
phy_init,  data, phy,     0x11000,  0x1000
ota_0,     app,  ota_0,         ,   4M         ← 펌웨어 A slot
ota_1,     app,  ota_1,         ,   4M         ← 펌웨어 B slot (OTA)
emote,     data, spiffs,        ,   3M         ← SPIFFS
storage,   data, fat,           ,   4M         ← FAT (Lua scripts, skill files)
```

### 1.2 소프트웨어 layer (보드 안)

```
                  ┌──────────────────────────────────┐
   사용자 자연어 ─▶│  Telegram bot (cap_im_tg)        │
   "도어 열리면    │  /  Web IM HTTP / QQ / Feishu …  │
    화장실 조명"   └──────────────────────────────────┘
                                │ event
                                ▼
              ┌──────────────────────────────────────┐
              │ event_router (cap_router_mgr)        │
              │   rules: im_any_message_agent 등      │
              └──────────────────────────────────────┘
                                │ run_agent
                                ▼
              ┌──────────────────────────────────────┐
              │ claw_core agent worker (FreeRTOS)    │
              │   OpenAI gpt-5-mini via REST          │
              │   tool descriptors: 60+ capabilities  │
              └──────────────────────────────────────┘
                                │ tool call
                                ▼
              ┌──────────────────────────────────────┐
              │ cap_ha_control 두 typed tool          │
              │   ha_control     — 즉시 제어           │
              │   ha_automation  — 자동화 CRUD         │
              └──────────────────────────────────────┘
                                │ HTTP
                                ▼
              ┌──────────────────────────────────────┐
              │ cap_ha_control_http  (Bearer auth,    │
              │   crt_bundle gating, HTTPS-aware)     │
              └──────────────────────────────────────┘
                                │
                                ▼
                          Home Assistant REST
```

부팅 직후 `cap_ha_resolve` 가 `/api/states` 를 fetch 해서 friendly_name ↔ entity_id 매핑 16개 (4 static + 12 NVS cache) 를 `s_ha_friendly_names` 에 넣고, `cap_ha_compose_description` 이 LLM-가시 description 텍스트를 실시간 보간한다.

---

## 2. 펌웨어 플래싱 — 왜 ESP-IDF 가 필요했는가

### 2.1 처음 시도: esp-claw.com 공식 web flasher

공식 절차 (`https://esp-claw.com`): 브라우저에서 보드 선택 → "ESP32-S3-DevKitC-1" 옵션 클릭 → Wi-Fi credentials 입력 → 플래시 끝. ESP-IDF 설치 불필요.

**증상:** flash 자체는 성공 (`Auto-detected Flash size: 16MB`, `Wrote 1385816 bytes`), WDT reset 후 USB descriptor 재등록 (Type-C 보드 정상 동작), 그러나 펌웨어 부팅이 console 에 응답 안 함:

```
Waiting for device info…
Querying Wi-Fi status (1/3)…
Querying Wi-Fi status (2/3)…
Querying Wi-Fi status (3/3)…
is flash model hasn't been supported.
[console-error] The device has been lost.
```

### 2.2 진짜 원인 — Flash 칩 brand 비호환 (HPM)

두 번째 시도에서 별도 serial monitor 로 부팅 로그 캡처:

```
I (34) qio_mode: Enabling QIO for flash chip MXIC      ← Flash 칩 brand 확인
I (38) boot.esp32s3: Boot SPI Speed : 80MHz
I (46) boot.esp32s3: SPI Flash Size : 8MB              ← 스톡 image 는 8MB 가정
W (543) flash HPM: HPM mode is optional feature that depends on flash model.
W (550) flash HPM: High performance mode of this flash model hasn't been supported.
I (557) MSPI Timing: Enter flash timing tuning
ESP-ROM:esp32s3-20210327                                ← reboot
rst:0x10 (RTCWDT_RTC_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

진단 두 가지가 겹친 상황:

1. **공식 BOM mismatch.** esp-claw 공식 BOM (`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/src/content/docs/en/tutorial/bom.mdx`) 은 `Select: 1-N8R8` (8MB Flash + 8MB OCT PSRAM) 만 권장. 사용자 보드는 **N16R8** (16MB Flash + 8MB OCT PSRAM) — esp-claw 의 web flasher stock image (boards/espressif/esp32_S3_DevKitC_1) 와 Flash size 불일치.

2. **Flash 칩 brand HPM 비호환.** ESP-IDF 의 HPM (High Performance Mode) 가속 코드는 Flash 칩 brand 별 비트 시퀀스. 호환 목록은 GigaDevice / Winbond 위주. 사용자 보드의 16MB Flash 는 **MXIC (Macronix)** 브랜드 — HPM 미지원. 부팅 초기 MSPI timing tuning 단계에서 무한 hang → RTCWDT 리셋 → reboot loop.

stock web flasher image 의 HPM 가정은 변경 불가 (Espressif 가 빌드해서 배포한 release artifact 라). 두 가지 길:
- A. N8R8 보드 새로 구매 (영상 화자가 검증한 경로)
- **B. ESP-IDF 빌드 + sdkconfig 3줄 수정** (사용자가 선택한 경로)

### 2.3 ESP-IDF 우회 procedure (실제 사용한 절차)

```bash
# 1. ESP-IDF v5.5.4 설치 (한 번만)
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh   # 매 셸에서

# 2. sdkconfig 3줄 정정
# 절대경로:
# /Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/boards/espressif/esp32_S3_DevKitC_1/sdkconfig.defaults.board
#
#   CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y  →  CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
#   CONFIG_ESPTOOLPY_FLASHFREQ_120M=y →  CONFIG_ESPTOOLPY_FLASHFREQ_80M=y    (MXIC 안전 freq)
#                                        CONFIG_SPI_FLASH_HPM_DISABLED=y      (HPM 강제 OFF)

# 3. 16MB partition table 정의 (/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/partitions_16MB.csv 사용)

# 4. 빌드 + 플래시
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem<CH343-port> flash monitor
```

이후 평소 개발 cycle:
```bash
cd /Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent
idf.py build                              # 변경분만 rebuild
idf.py -p $ESP_PORT app-flash             # app partition 만 (NVS/storage 보존 ← 중요!)
idf.py -p $ESP_PORT flash                 # 전체 partition (NVS reset)
idf.py -p $ESP_PORT monitor               # 시리얼 로그 실시간
```

`app-flash` 와 `flash` 의 차이는 v5 핫픽스 사이클에서 학습 — `flash` 가 storage 파티션을 다시 써서 Wi-Fi credentials NVS 가 reset 되는 케이스가 있었다.

### 2.4 두 USB 포트 단서

사용자 보드는 **듀얼 USB**:
- **USB-JTAG/OTG** (PID 0x1001) — 내장. flash 후 WDT reset 시 USB descriptor 재등록 → macOS 가 새 `/dev/cu.usbmodem*` 발급
- **CH343 USB-UART** (PID 0x55d3) — 외부 칩. RTS/DTR 자동 reset 지원 → flash 시 더 안정적. 평소 사용 권장.

영상 화자 (Tech SMS) 의 "Micro USB 가 잘 됐다" 발언은 CH343 같은 외부 UART 칩의 안정성이 정체.

---

## 3. 디렉토리 구조 (절대경로)

esp-claw 는 Espressif 공식 fork. 우리 fork (`JayMon0327/esp-claw-smarthome`) 가 add 한 부분만 발췌. 전체 824 files / +118,452 / -8,186 중 핵심은 다음. **모든 path 는 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/` 기준.**

```
/Users/wm-mac-01/Desktop/esp-claw/esp-claw/
├── application/edge_agent/                       ← ESP-IDF 프로젝트 루트 (idf.py build 시점)
│   ├── CMakeLists.txt                            ← project entry
│   ├── main/
│   │   ├── CMakeLists.txt                        ← main_requires: app_claw, app_config, etc.
│   │   ├── main.c                                ← app_main 진입점
│   │   ├── Kconfig.projbuild
│   │   ├── idf_component.yml
│   │   ├── lua_scripts/                          ← 내장 Lua skill 들
│   │   └── skills/                               ← Lua-style markdown skills
│   ├── sdkconfig                                 ← 현재 빌드 설정 (gitignored, board 별 다름)
│   ├── partitions_16MB.csv                       ← Partition table (16MB Flash 보드용)
│   ├── partitions_8MB.csv                        ← Partition table (8MB N8R8 보드용)
│   ├── boards/
│   │   ├── espressif/esp32_S3_DevKitC_1/
│   │   │   └── sdkconfig.defaults.board          ← FLASHSIZE_16MB / FREQ_80M / HPM_DISABLED 수정
│   │   ├── espressif/esp32_S3_DevKitC_1_breadboard/
│   │   ├── espressif/esp_box_3/
│   │   ├── espressif/esp_SensairShuttle/
│   │   ├── espressif/esp_vocat_board_v1_2/
│   │   ├── espressif/esp32_p4_eye/
│   │   ├── espressif/esp32_p4_function_ev/
│   │   ├── m5stack/m5stack_cores3/               ← M5Stack CoreS3 보드 디렉토리
│   │   ├── m5stack/m5stack_sticks3/
│   │   ├── lilygo/lilygo_t_display_s3/
│   │   └── dfrobot/dfrobot_k10/
│   └── components/                               ← application-layer components
│       ├── app_config/                           ← ★ NVS 키-값 설정 store (25 키)
│       │   ├── app_config.c
│       │   └── include/app_config.h              ← app_config_t struct 정의
│       ├── cmd_wifi/                             ← `wifi` 콘솔 명령 (SSID/PW 설정)
│       └── http_server/                          ← SoftAP 의 provisioning portal
│
├── components/                                   ← Espressif esp-claw 본가 component
│   └── claw_capabilities/
│       ├── cap_ha_control/                       ← ★ 우리가 v3 부터 신규 작성한 컴포넌트
│       │   ├── CMakeLists.txt
│       │   ├── Kconfig
│       │   ├── idf_component.yml
│       │   ├── data/entities.default.json        ← 기본 static entity 4개
│       │   ├── include/cap_ha_control.h
│       │   ├── include/cmd_cap_ha_control.h
│       │   └── src/
│       │       ├── cap_ha_control.c              ← ★ 두 typed tool descriptor + group_init
│       │       ├── cap_ha_control_core.c         ← ha_control execute (즉시 제어)
│       │       ├── cap_ha_automation.c           ← ★ ha_automation execute (자동화 CRUD)
│       │       ├── cap_ha_control_resolve.c     ← entity friendly_name ↔ entity_id 매핑
│       │       ├── cap_ha_control_http.c        ← ★ HA REST 호출 + NVS `ha_ctl` 읽기
│       │       ├── cap_ha_control_board.c       ← board:* 가상 entity
│       │       ├── cap_ha_control_internal.h
│       │       └── cmd_cap_ha_control.c          ← console 명령 (--set-url / --set-token 등)
│       │
│       ├── cap_im_tg/                            ← Telegram bot (esp-claw 본가)
│       ├── cap_mcp_client/                       ← 외부 MCP 서버 호출
│       ├── cap_mcp_server/                       ← 보드 자체 MCP 서버 (mDNS)
│       ├── cap_router_mgr/                       ← event router
│       ├── cap_scheduler/                        ← 보드 측 자동화 (cron-like)
│       ├── cap_lua/                              ← Lua 스크립트 runtime
│       └── … (cap_im_qq/feishu/wechat/local, cap_files, cap_skill, etc.)
│
├── docs/learn/                                   ← ★ AI 사이클의 학습 로그 시리즈
│   ├── 20260507-webflasher-flash-stuck.md       ← web flasher 실패 원인 (day1 worktree only)
│   ├── 20260507-board-mismatch-n16r8.md         ← N16R8 + MXIC 분석 (day1 worktree only)
│   ├── 20260508-cap-ha-control-v3.md             ← v3 typed tool 학습
│   ├── 20260511-cap-ha-control-v4.md             ← v4 자동화 CRUD 학습
│   ├── 20260512-cap-ha-state-value-normalize-hotfix.md  ← v5 핫픽스
│   ├── 20260513-cap-ha-automation-v5.md          ← v5 메인 학습
│   ├── 20260513-v5-completion-report.md          ← 보고서 사이클 학습
│   └── 20260513-architecture-analysis-docs.md    ← 아키텍처 분석 docs 학습
│
├── smarthome-docs/                               ← ★ AI 사이클의 plan / report / 메타
│   ├── architecture/                             ← 이 디렉터리
│   │   ├── 01-esp-claw-current-architecture.md   ← (이 파일)
│   │   └── 02-openclaw-vs-esp-claw-comparison.md
│   ├── superpowers/plans/                        ← 구현 plan 시리즈
│   │   ├── 2026-05-06-esp-claw-ceo-demo.md
│   │   ├── 2026-05-08-cap-ha-control-typed-tool.md       ← v3
│   │   ├── 2026-05-11-cap-ha-control-v4-followups.md     ← v4
│   │   ├── 2026-05-12-cap-ha-automation-state-trigger.md ← v4 follow-up PR #6
│   │   └── 2026-05-12-cap-ha-automation-v5.md            ← v5
│   └── reported/                                 ← 사이클별 완료 보고서
│       ├── esp-claw-smarthome-completion-2026-05-11.md   ← v3 ship
│       ├── esp-claw-smarthome-completion-2026-05-12.md   ← v4 ship
│       └── esp-claw-smarthome-completion-2026-05-13.md   ← v5 ship
│
└── .claude/                                      ← Claude Code agent harness
    ├── settings.json                             ← hooks (completion-intent / enforce-learn-log)
    ├── memory/                                   ← project-scoped auto-memory
    └── worktrees/                                ← 작업별 isolated worktree (사이클마다 cleanup)
```

요점: **두 부류의 변경**
- 펌웨어 코드: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/` 단일 컴포넌트에 거의 모두 응집 + `boards/.../sdkconfig.defaults.board` 3줄.
- 문서 + 메타: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/`, `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/` 가 사이클별 plan / 학습 / 보고서 시리즈 누적.

---

## 4. 수정한 핵심 파일들 + 코드 예시

여기서는 v3-v5 사이클의 4개 hot file 만 깊게 본다. commit-by-commit 차이는 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/` 의 사이클별 보고서가 더 자세.

### 4.1 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control.c` — 두 typed tool descriptor

LLM 에게 노출되는 capability 가 어떻게 정의되는지. `s_ha_descriptors[]` 가 2-element 배열로 두 tool 등록. 핵심 발췌 (v5 머지 후):

```c
static char s_ha_description[1024];                  /* ha_control 용 */
static char s_ha_automation_description[1536];       /* ha_automation 용 (v5 1024→1536 bump) */
static char s_ha_friendly_names[256];                /* "거실 조명, 도어센서 …" 동적 보간 */

static claw_cap_descriptor_t s_ha_descriptors[] = {
    {
        .name = "ha_control",
        .description = NULL,                         /* set in cap_ha_compose_description */
        .input_schema_json =
            "{\"type\":\"object\",\"properties\":{"
              "\"target\":{\"type\":\"string\",\"description\":\"HA entity friendly name or entity_id\"},"
              "\"action\":{\"type\":\"string\",\"enum\":[\"turn_on\",\"turn_off\",\"toggle\",\"open\",\"close\"]},"
              "\"brightness_pct\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},"
              "\"color\":{\"type\":\"string\"},"
              "\"kelvin\":{\"type\":\"integer\",\"minimum\":2000,\"maximum\":6500}"
            "},\"required\":[\"target\",\"action\"]}",
        .execute = cap_ha_control_execute_wrapper,
    },
    {
        .name = "ha_automation",
        .description = NULL,
        .input_schema_json = /* ... (생략, condition 객체 + trigger 객체 + auto-normalize 명시) */,
        .execute = cap_ha_automation_execute_wrapper,
    },
};
```

핵심 패턴: **JSON Schema 가 C 문자열 리터럴**. 길어보이지만 C 컴파일러가 인접 리터럴 자동 결합. `claw_cap` runtime 이 LLM API 호출 시 이 문자열을 그대로 `tools[].input_schema` 로 보냄.

### 4.2 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` — v5 의 핵심 builder

state-trigger 의 `from` auto-fill + condition typed payload + state-value 정규화 (핫픽스):

```c
/* binary_sensor / light / switch / input_boolean 에서 LLM 의 door/window 어휘
 * ("open"/"closed") 를 HA-native ("on"/"off") 로 정규화. */
static const char *normalize_state_value(const char *domain, const char *value)
{
    if (!domain || !value) return value;
    if (strcmp(domain, "binary_sensor") == 0 ||
        strcmp(domain, "light") == 0 ||
        strcmp(domain, "switch") == 0 ||
        strcmp(domain, "input_boolean") == 0) {
        if (strcmp(value, "open")   == 0) return "on";
        if (strcmp(value, "opened") == 0) return "on";
        if (strcmp(value, "closed") == 0) return "off";
    }
    return value;
}

/* state trigger from 자동 채움 — domain-pair opposite. */
static const char *opposite_state(const char *domain, const char *to_val)
{
    if (!domain || !to_val) return NULL;
    if (strcmp(domain, "binary_sensor") == 0 ||
        strcmp(domain, "light") == 0 ||
        strcmp(domain, "switch") == 0 ||
        strcmp(domain, "input_boolean") == 0) {
        if (strcmp(to_val, "on")  == 0) return "off";
        if (strcmp(to_val, "off") == 0) return "on";
    } else if (strcmp(domain, "cover") == 0) {
        if (strcmp(to_val, "open")   == 0) return "closed";
        if (strcmp(to_val, "closed") == 0) return "open";
    } else if (strcmp(domain, "lock") == 0) {
        if (strcmp(to_val, "locked")   == 0) return "unlocked";
        if (strcmp(to_val, "unlocked") == 0) return "locked";
    }
    return NULL;
}
```

이 30줄이 v5 의 핵심 가치. LLM 이 `to:"open"` 보내도 `to:"on", from:"off"` 로 변환되어 자동화가 transition 강제로 fire — 어휘 mismatch 가 firmware 책임.

### 4.3 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` — entity 검색

자연어 → entity_id 매핑. NVS 캐시 (namespace `ha_ctl`) + 부팅 시 `/api/states` slow-path fetch + 친절어 fuzzy 매칭. v3 의 P2 review 가 `CAP_HA_MAX_REGISTRY_ENTRIES = 64` cap + WARN 추가, v4 가 binary_sensor + light 만 cache (sensor 도메인은 NVS 폭발 방지로 제외).

### 4.4 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` — HA REST 클라이언트

Bearer 인증 + `crt_bundle_attach` (HTTPS) gating + `skip_cert_common_name_check` (`--insecure` 모드, demo 전용). 5 helper:

```c
esp_err_t cap_ha_http_get_states(char *out, size_t out_size);
esp_err_t cap_ha_http_post_service(const char *domain, const char *action, const char *body);
esp_err_t cap_ha_http_put_automation_config(const char *id, const char *body, int *http_status, ...);
esp_err_t cap_ha_http_get_automation_config(const char *id, int *http_status, char *out, ...);
esp_err_t cap_ha_http_delete_automation_config(const char *id, int *http_status, ...);
esp_err_t cap_ha_http_post_reload_automations(void);
```

URL 구성 — `base_url` 은 NVS namespace `ha_ctl` 의 key `ha_url` 에서 read:
```c
snprintf(full_url, sizeof(full_url),
         "%s/api/config/automation/config/%s", base_url, id);
```

Bearer 인증 token 은 NVS namespace `ha_ctl` 의 key `ha_token` 에서 read.

---

## 5. 에이전트 학습 — 두 layer

"에이전트 학습" 은 이 프로젝트에서 두 가지 다른 layer 를 의미한다. 둘 다 의도적으로 구분해야 혼동 없음.

### 5.1 Layer A — 보드 내부 LLM 의 학습 (firmware-embedded)

**Agent runtime**: `claw_core` worker task (FreeRTOS) + OpenAI gpt-5-mini REST client.

**학습 = LLM 가시 정보 3종**:

1. **system prompt** — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/main/skills/` 의 markdown 들 (Lua skill 들 + 공통 instruction).

2. **tool descriptors** — `s_ha_descriptors[].input_schema_json` + `cap_ha_compose_description` 의 description 텍스트.

3. **runtime context** — entity friendly_names (`s_ha_friendly_names`). `cap_ha_resolve` 가 부팅 시 + 명시 refresh 시 `/api/states` fetch.

**학습 사이클**: code 수정 → `idf.py build` → `idf.py -p $ESP_PORT app-flash` → 보드 재부팅 → 다음 사용자 메시지부터 새 descriptor 적용.

### 5.2 Layer B — 외부 Claude Code 하네스의 학습 (개발 사이클)

**Agent runtime**: Claude Code 본체 (이 conversation 의 환경).

**학습 = 디렉터리 시리즈 3종 (절대경로)**:

1. **plans** — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/`
2. **learn logs** — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/`
3. **completion reports** — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/`

**보조 메커니즘**:
- `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/memory/` — auto-memory
- `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/.claude/worktrees/<name>/` — 작업별 isolated 워크트리
- `enforce-learn-log` hook — `docs/learn/YYYYMMDD-<topic>.md` 가 없으면 completion 차단

### 5.3 두 layer 의 분리가 핵심

- Layer A (firmware-embedded) 는 *프로덕션 사용자 경로*. 사용자 텔레그램 메시지를 처리.
- Layer B (Claude Code) 는 *개발 사이클*. PR 만들고 merge 하는 사람의 path.

---

## 6. 환경 설정 — HA 연결 + Portability (사용자 Q&A)

> **이 섹션은 사용자의 질문 7건을 verbatim 으로 유지하고, 답변과 함께 정리한 자료다.** 새 사용자 / 새 네트워크 / 새 HA 인스턴스로 esp-claw 를 이식할 때 reference.

### 6.0 핵심 발견 한 줄

**모든 사용자 설정은 NVS partition (0x9000, 24KB) 의 binary key-value store 에 저장. firmware 코드에 박힌 IP / token / SSID 는 없다.** 두 NVS namespace 로 분리:
- `app_config` — WiFi + LLM + IM 채널 (25 키), 관리 코드: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/app_config/app_config.c`
- `ha_ctl` — HA 연결 (3 키), 관리 코드: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c`

### 6.1 Q1 — HA API 호출 주소가 고정인가?

> **사용자 질문 (원문):** HA API실행할때 로컬 주소로 연동되어 있는 것으로 알고 있는데 127.0.0.1(자기자신이니까 당연히 이건 아니겠지만)와 같은 고정된 주소로 호출하고 있는거야? 아니면 특정 로컬 IP(192.168.1.94)로 지정되어있는거야 ?

**답: ❌ 코드에 박힌 IP 없음. NVS namespace `ha_ctl` 의 키 `ha_url` 에서 매 호출 시 read.**

`cap_ha_control_http.c` 가 매 HTTP 호출마다 NVS 를 read:

```c
// 부팅 후 매 HTTP 호출 시 (의사 코드)
nvs_open("ha_ctl", NVS_READONLY, &h);
nvs_get_str(h, "ha_url", buf, &len);    // 예: "http://192.168.1.94:8123"
snprintf(full_url, sizeof(full_url), "%s/api/states", buf);
```

현재 사용자 보드의 NVS 에 `http://192.168.1.94:8123` 가 저장되어 있어서 그 IP 로 가는 것. **firmware 자체엔 IP 박힌 곳 없음.** NVS 값을 바꾸면 즉시 다른 HA 로 변경.

### 6.2 Q2 — 다른 사람 HA 에도 연결 가능?

> **사용자 질문 (원문):** 상기 1번을 물어본 이유는 나중에 다른 사람의 HA에도 연결할 수 있는지를 판단하려고 그러는거야. 다른 네트워크로 가서 ESP-claw가 연결되었을때 해당 로컬네트워크에 있는 HA에 동적으로 연결이 가능한지 확인하려고.

**답: ✅ 가능. NVS 값 4개만 새로 입력하면 다른 환경에서 동작.**

필수 변경 항목:
1. `app_config/wifi_ssid` — 새 공유기 SSID
2. `app_config/wifi_password` — 새 공유기 PW
3. `ha_ctl/ha_url` — 새 HA URL
4. `ha_ctl/ha_token` — 새 HA Long-Lived Access Token

firmware 자체는 **재빌드 / 재플래시 불필요**. NVS 만 새로 입력하면 부팅 시 새 값으로 동작.

다만 *동적 자동 발견* (사용자가 NVS 입력 없이도 HA 알아서 찾기) 은 별개 — Q4 참조.

### 6.3 Q3 — 설정 파일 경로 + 분리 여부

> **사용자 질문 (원문):** 만약 상기 1번이 고정된 주소라서 매번 바꿔줘야 한다면, 바꿔줄때 esp-claw의 와이파이 정보 및 API키도 바꿔줘야 할거 같은데, 바꿀 수 있는 설정파일 경로는 어디있어 ? HA 관련 설정파일(HA 주소) 및 esp-claw 설정파일(와이파이 정보 및 API 키 정보 등) 설정파일이 분리되어있어 ?

**답: 설정은 *파일* 이 아닌 *NVS partition* (binary key-value store) 에 저장. 두 NVS namespace 로 분리되어 있음.**

**A. `app_config` namespace (25 키) — WiFi + LLM + IM 채널**

- 관리 코드 (절대경로): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/app_config/app_config.c`
- struct 정의: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/app_config/include/app_config.h` 의 `app_config_t`

| 그룹 | NVS 키 | 의미 |
|---|---|---|
| **WiFi** | `wifi_ssid` | 공유기 SSID |
| | `wifi_password` | 공유기 PW |
| **LLM** | `llm_api_key` | OpenAI/Anthropic 등 API key |
| | `llm_backend` | provider 이름 (`openai` / `anthropic`) |
| | `llm_profile` | 프로파일 이름 |
| | `llm_model` | 모델 이름 (`gpt-5-mini` 등) |
| | `llm_base_url` | LLM endpoint base URL (default = empty) |
| | `llm_auth_type` | 인증 방식 |
| | `llm_timeout_ms` | 호출 timeout (default 120000) |
| | `llm_max_tokens` | response 최대 토큰 (default 8192) |
| **Telegram** | `tg_bot_token` | Telegram bot token |
| **QQ** | `qq_app_id` / `qq_app_secret` | QQ bot 인증 |
| **Feishu** | `feishu_app_id` / `feishu_secret` | Feishu bot |
| **WeChat** | `wechat_token` / `wechat_base_url` / `wechat_cdn_url` / `wechat_acct_id` | WeChat bot |
| **검색** | `brave_key` / `tavily_key` | 웹 검색 API |
| **Capability gating** | `en_cap_groups` | enable 할 capability group list (CSV) |
| | `vis_cap_groups` | LLM-visible cap groups (default `cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system`) |
| | `en_lua_mods` | enable 할 Lua 모듈 list |
| **시간** | `time_timezone` | TZ 문자열 (default `CST-8`) |

**B. `ha_ctl` namespace (3 키) — HA 연결 정보**

- 관리 코드 (절대경로): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c`
- 콘솔 입력 코드: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`

| 키 | 크기 | 의미 |
|---|---|---|
| `ha_url` | 160B | HA Core URL (`http://192.168.1.94:8123` 또는 `https://homeassistant.local:8123`) |
| `ha_token` | 4096B | HA Long-Lived Access Token (Bearer) |
| `ha_insecure` | flag | HTTPS cert 검증 skip (demo 전용) |

**→ HA 설정과 esp-claw 일반 설정은 *NVS namespace 단위로 분리*. 코드 path 도 분리.** 한 namespace 만 reset 가능 (예: HA 만 새 인스턴스로 변경하고 WiFi 는 유지).

### 6.4 Q4 — 동적 발견 구현 가능성?

> **사용자 질문 (원문):** 상기 2번처럼 동적으로 연결을 하려고 한다면, 동적으로도 캐치할 수 있도록 구현이 가능하겠어? 구현가능성을 물어보는 거야.

**답: ✅ 가능. 두 path 중 선택 또는 fallback chain.**

**Path A: HA 의 mDNS 서비스 검색 (가장 깔끔)**

HA 가 `_home-assistant._tcp.local` 또는 `_homeassistant._tcp.local` 서비스를 mDNS 으로 advertise (HA 의 `configuration.yaml` 에 `zeroconf:` enable 시). esp-claw 의 mDNS client (`esp_mdns`, 이미 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_mcp_server/` 와 `cap_mcp_client/` 가 사용 중) 가 검색 가능:

```c
// 가상 코드 예시 (v6 후보 — cap_ha_control_resolve_ha_url)
esp_err_t resolve_ha_via_mdns(char *out_url, size_t cap) {
    mdns_result_t *r = NULL;
    mdns_query_ptr("_home-assistant", "_tcp", 2000, 1, &r);
    if (r) {
        snprintf(out_url, cap, "http://%s:%d", r->hostname, r->port);
        return ESP_OK;
    }
    return ESP_FAIL;
}
```

**Path B: 네트워크 스캔 + API probe (fallback)**

같은 서브넷의 모든 IP 에 `GET /api/` 요청 → `"API running."` 응답 받으면 HA 발견. 인증 없이 endpoint 존재만 확인 가능.

**인증 (token) 은 자동 발견 불가능**: 보안상 token 은 사용자가 명시 입력해야 함 (HA UI 의 "Long-Lived Access Tokens" 메뉴에서 생성). mDNS 가 URL 자동 발견까지는 OK, token 은 SoftAP provisioning portal 에서 입력.

**구현 비용 추정**: Path A (~80 LoC), Path B (~150 LoC). v6+ 후보로 적합.

### 6.5 Q5 — 설정 변경 시 매번 플래싱?

> **사용자 질문 (원문):** 상기 3번의 파일을 매번 바꿔준다면 바꿀때마다 esp-32-s3의 플래싱을 새로 해야하는거지? 만약 4번에서 동적 구현이 가능해서 동적구현을 성공했다고 해도, 매번 다른집에서 켤때마다 플래싱은 새로 해야하는거지 ?

**답: ❌ 플래싱 불필요.** NVS 는 runtime mutable storage. firmware 가 코드에 박은 게 없으므로 값 변경에 재컴파일 / 재플래시 안 필요.

변경 방법 4가지:

**방법 1: Provisioning Portal (SoftAP, 가장 사용자 친화적)**

NVS 의 `wifi_ssid` 가 비어 있으면 보드가 자동으로 `esp-claw-XXXXXX` SoftAP 로 띄움 + 캡티브 포털 노출. 사용자 절차:
1. 폰/노트북 WiFi 를 `esp-claw-XXXXXX` 에 접속 (PW 없음)
2. 브라우저 자동 redirect → `http://192.168.4.1/`
3. 폼에 WiFi SSID/PW + HA URL + HA token + LLM API key 입력
4. Submit → 보드가 NVS 저장 + STA 모드로 재부팅 → 동작

관련 코드 (절대경로): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/http_server/` (SoftAP HTTP server) + esp-claw 본가의 `wifi_manager` component.

**방법 2: USB 시리얼 콘솔 (개발자용, 가장 빠른)**

```bash
ha_control --set-url http://192.168.1.94:8123
ha_control --set-token <bearer-token>
wifi --set --ssid <SSID> --password <PW> --apply
tg --set-token <telegram-bot-token>
```

콘솔 명령 정의 (절대경로):
- HA: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cmd_cap_ha_control.c`
- WiFi: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/cmd_wifi/`
- Telegram: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_im_tg/`

**방법 3: HA UI 의 esp-claw 설정 (현재 없음 — v6 후보)**

HA 가 esp-claw 를 device 로 인식하면 HA UI 에서 설정 가능. esp-matter / esphome 패턴.

**방법 4: OTA + 새 NVS 이미지 (양산 배포 시)**

배치 배포 시 미리 NVS 이미지 만들어서 OTA 으로 flash.

**→ 다른 집에 보드 들고 가면 — 켜면 SoftAP 모드 (또는 시리얼 콘솔 접근) — WiFi/HA 설정만 새로 입력 → 끝. 플래싱 불필요.**

### 6.6 Q6 — 자동 설정 + 플래싱 웹사이트?

> **사용자 질문 (원문):** 상기 5번에서 플래싱을 매번 새로해야 한다면, 설정 정보 입력과 플래싱을 자동으로 해줄 수 있는 간단한 웹사이트라도 만들어서 배포해야겠지 ?

**답: 플래싱 자체는 매번 안 함 (Q5 답 참조). 다만 처음 보드 셋업 시 한 번 필요. 두 layer 로 분리:**

**Layer A: 플래싱 (한 번)**
- 현재: esp-claw.com 공식 web flasher — 다만 사용자 N16R8 + MXIC HPM 비호환이라 우리 fork 의 binary 가 필요.
- 미래 옵션 (~반나절 작업):
  - 우리 fork 의 binary 를 GitHub Releases (`https://github.com/JayMon0327/esp-claw-smarthome/releases`) 에 호스팅
  - `esp-web-tools` (https://esphome.github.io/esp-web-tools/) 같은 라이브러리로 web flasher UI 자체 호스팅 (예: `https://jaymon0327.github.io/esp-claw-smarthome-flasher/`)
  - GitHub Pages 한 페이지면 가능

**Layer B: 설정 (매번 새 환경마다)**
- 이미 esp-claw 가 SoftAP provisioning portal 제공. *추가 웹사이트 불필요*.
- 다만 사용자 친화적 UX 개선은 v6+ 후보 (current SoftAP UI 가 단순함).

**결론**: 자체 호스팅 web flasher 는 (a) 우리 fork 의 binary 배포 + (b) 사용자 처음 셋업 시 한 번 사용 — 가치 있음. 다만 설정 입력은 SoftAP 포털로 충분.

### 6.7 Q7 — 설정 파일의 값들 (수정 항목)

> **사용자 질문 (원문):** 상기 3번의 설정파일에는 어떤 값들이 존재하고 있는지 알려줘.(내가 수정해야할 것들이 뭐가 있는지 알기 위해서)

**답: NVS 28 키 중 새 환경 이동 시 *최소 수정 4개*:**

| 분류 | NVS namespace | 키 | 새 환경 수정? |
|---|---|---|---|
| **WiFi** | `app_config` | `wifi_ssid` | ✅ **필수** |
| **WiFi** | `app_config` | `wifi_password` | ✅ **필수** |
| **HA** | `ha_ctl` | `ha_url` | ✅ **필수** |
| **HA** | `ha_ctl` | `ha_token` | ✅ **필수** |
| **시간** | `app_config` | `time_timezone` | 한국이면 `KST-9` (default `CST-8` 중국) |
| **LLM** | `app_config` | `llm_api_key` | 보통 그대로 (OpenAI key 는 새 환경에서도 작동) |
| **LLM** | `app_config` | `llm_backend / profile / model / base_url / auth_type / timeout / max_tokens` | 일반적으로 그대로 |
| **Telegram** | `app_config` | `tg_bot_token` | 보통 그대로 (같은 bot 사용 시) |
| **QQ/Feishu/WeChat** | `app_config` | `qq_*` / `feishu_*` / `wechat_*` | 사용 안 하면 empty 그대로 |
| **검색** | `app_config` | `brave_key` / `tavily_key` | 사용 안 하면 empty |
| **Capability gating** | `app_config` | `en_cap_groups` / `vis_cap_groups` / `en_lua_mods` | 보통 default 그대로 |
| **HA 보안** | `ha_ctl` | `ha_insecure` | HTTPS cert 검증 skip — 일반적으로 false |

**즉 새 집/네트워크 이동 시 *최소 4개* (WiFi 2 + HA 2) 입력만 필요.** 다른 25 키는 그대로 유지.

### 6.8 Portability TL;DR

| 질문 | 답 |
|---|---|
| 1. 코드에 IP 박혔나? | ❌ NVS 에 저장 (`ha_ctl/ha_url`) |
| 2. 다른 HA 연결 가능? | ✅ NVS 값 4개 (WiFi 2 + HA 2) 만 새로 입력 |
| 3. 설정 파일 경로 / 분리? | NVS partition (파일 아님), `app_config` (25키) + `ha_ctl` (3키) 두 namespace 로 분리 |
| 4. 동적 발견 구현 가능? | ✅ mDNS (HA 의 `_home-assistant._tcp.local`) 또는 subnet scan — token 은 사용자 입력 필수 |
| 5. 설정 변경 시 플래싱? | ❌ 불필요 — SoftAP 포털 or 시리얼 콘솔로 runtime 변경 |
| 6. 자동 설정 + 플래싱 웹사이트? | 설정 UI 는 *이미 SoftAP 포털 제공*. flasher 는 자체 GitHub Pages + esp-web-tools 로 반나절에 가능 |
| 7. 수정 항목 list | 최소 4개 (`wifi_ssid` / `wifi_password` / `ha_url` / `ha_token`) + 선택 1개 (`time_timezone=KST-9`) |

---

## 7. 운영 상태 (2026-05-13 기준)

- 보드: ESP32-S3 N16R8 (MXIC Flash), 사용자 거실, 전원만 연결되어 자동 부팅. WiFi STA + HA REST + Telegram + MCP server + LLM provider (gpt-5-mini) 모두 자동 활성화.
- 펌웨어 HEAD: `fa42827` (v5 + 핫픽스). 다음 변경시 `idf.py app-flash` 만.
- HA: 192.168.1.94:8123. 자동화 4개 (사용자 직접 만든 것 + v3-v5 firmware 가 만든 것).
- 다음 사이클: v6 plan 미작성. 후보 7건 — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md` §5 참조. §6 의 portability 관련 후보 (mDNS HA 자동 발견, esp-web-tools flasher 호스팅, HA UI integration) 도 추가.

---

## 8. 참고 (모두 절대경로)

- v3 보고서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-11.md`
- v4 보고서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`
- v5 보고서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- 자매 문서 (openclaw 와 비교): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
- esp-claw 상류 (Espressif): https://github.com/espressif/esp-claw
- 우리 fork: https://github.com/JayMon0327/esp-claw-smarthome
