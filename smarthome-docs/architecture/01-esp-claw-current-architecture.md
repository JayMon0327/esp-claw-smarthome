# ESP-Claw 스마트홈 — 현재 아키텍처 분석 (2026-05-13)

> **목적:** v3-v5 사이클이 main 에 안착한 현재 시점에서 esp-claw 가 스마트홈 비서로 어떻게 동작하는지, 왜 ESP-IDF 펌웨어 플래싱을 거쳐야 했는지, 어떤 파일들을 수정했는지, 그리고 두 layer 의 "에이전트 학습" (보드 내부 LLM + 외부 Claude Code 하네스) 을 어떻게 시켰는지 한 문서에 정리.

- **대상 독자:** 이 프로젝트를 인계받거나 v6 부터 참여할 엔지니어. 사전 지식: 일반 임베디드 + 스마트홈 + LLM API 사용 경험. ESP-IDF 경험은 무관.
- **자매 문서:** [`02-openclaw-vs-esp-claw-comparison.md`](./02-openclaw-vs-esp-claw-comparison.md) — Pi 위에서 도는 사전 구현 (openclaw-smarthome) 과의 layer-by-layer 비교.

---

## 0. TL;DR

ESP-Claw 는 Espressif 가 만든 오픈소스 ESP32 firmware (LLM 에 capabilities 를 노출하는 보드 측 agent runtime). 우리는 이 위에 **`cap_ha_control`** capability 를 추가해서 보드 한 대가 OpenAI gpt-5-mini 를 호출하고 Home Assistant 의 기기를 자연어로 제어/자동화하도록 만들었다.

핵심 결정 4가지:

1. **공식 web flasher (https://esp-claw.com) 사용 실패 → ESP-IDF 빌드로 우회.** 원인: 보드의 Flash 칩 brand (MXIC) 가 esp-claw stock image 의 HPM (High Performance Mode) 가속 코드와 비호환. 8MB stock image 와 16MB N16R8 보드 mismatch 까지 겹쳐 부팅 초기 MSPI timing tuning 단계에서 무한 reboot. sdkconfig 3줄 수정 (`FLASHSIZE_16MB / FLASHFREQ_80M / HPM_DISABLED`) + ESP-IDF v5.5.4 빌드로 해결.

2. **보드-측 typed tool 로 HA 와 직접 통신.** `cap_ha_control` 은 HA Core REST API (`/api/services/<domain>/<action>`, `/api/states`, `/api/config/automation/config/<id>`) 를 직접 호출. HA Matter integration 이나 HA MCP server 같은 중간 layer 없음.

3. **LLM 가시 capability 는 두 개**: `ha_control` (기기 즉시 제어) + `ha_automation` (HA 자동화 등록/수정/제거/조회/발화/enable/disable). 둘 다 input_schema_json (JSON Schema as C string literal) + 한국어 description 으로 정의.

4. **에이전트 학습은 두 layer**:
   - **보드 내부 LLM** (firmware-embedded): capability descriptor + `cap_ha_compose_description` 의 runtime-friendly_names 보간 + entity registry NVS 캐시.
   - **외부 Claude Code 하네스** (개발 사이클): `smarthome-docs/superpowers/plans/`, `docs/learn/`, `smarthome-docs/reported/` 의 markdown 시리즈 + subagent-driven-development skill + completion-intent hook.

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

1. **공식 BOM mismatch.** esp-claw 공식 BOM (`docs/src/content/docs/en/tutorial/bom.mdx`) 은 `Select: 1-N8R8` (8MB Flash + 8MB OCT PSRAM) 만 권장. 사용자 보드는 **N16R8** (16MB Flash + 8MB OCT PSRAM) — esp-claw 의 web flasher stock image (boards/espressif/esp32_S3_DevKitC_1) 와 Flash size 불일치.

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
# application/edge_agent/boards/espressif/esp32_S3_DevKitC_1/sdkconfig.defaults.board
#   CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y  →  CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
#   CONFIG_ESPTOOLPY_FLASHFREQ_120M=y →  CONFIG_ESPTOOLPY_FLASHFREQ_80M=y    (MXIC 안전 freq)
#                                        CONFIG_SPI_FLASH_HPM_DISABLED=y      (HPM 강제 OFF)

# 3. 16MB partition table 정의 (`partitions_two_ota.csv` 16MB 변형)

# 4. 빌드 + 플래시
cd application/edge_agent
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem<CH343-port> flash monitor
```

이후 평소 개발 cycle:
```bash
idf.py build           # 변경분만 rebuild
idf.py -p $ESP_PORT app-flash   # app partition 만 (NVS/storage 보존 ← 중요!)
idf.py -p $ESP_PORT flash       # 전체 partition (NVS reset)
idf.py -p $ESP_PORT monitor     # 시리얼 로그 실시간
```

`app-flash` 와 `flash` 의 차이는 v5 핫픽스 사이클에서 학습 — `flash` 가 storage 파티션을 다시 써서 Wi-Fi credentials NVS 가 reset 되는 케이스가 있었다.

### 2.4 두 USB 포트 단서

사용자 보드는 **듀얼 USB**:
- **USB-JTAG/OTG** (PID 0x1001) — 내장. flash 후 WDT reset 시 USB descriptor 재등록 → macOS 가 새 `/dev/cu.usbmodem*` 발급
- **CH343 USB-UART** (PID 0x55d3) — 외부 칩. RTS/DTR 자동 reset 지원 → flash 시 더 안정적. 평소 사용 권장.

영상 화자 (Tech SMS) 의 "Micro USB 가 잘 됐다" 발언은 CH343 같은 외부 UART 칩의 안정성이 정체.

---

## 3. 디렉토리 구조 (수정/추가한 부분 중심)

esp-claw 는 Espressif 공식 fork. 우리 fork (`JayMon0327/esp-claw-smarthome`) 가 add 한 부분만 발췌. 전체 824 files / +118,452 / -8,186 중 핵심은 다음.

```
esp-claw/
├── application/edge_agent/                       ESP-IDF 프로젝트 루트
│   ├── main/main.c                               app_main 진입점
│   ├── sdkconfig                                 빌드 설정 (HPM_DISABLED 포함)
│   └── boards/espressif/esp32_S3_DevKitC_1/      보드별 sdkconfig.defaults.board
│       └── sdkconfig.defaults.board              ← FLASHSIZE_16MB / FREQ_80M / HPM_DISABLED 수정
│
├── components/claw_capabilities/cap_ha_control/  ★ 신규 capability (이 프로젝트 핵심)
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── idf_component.yml
│   ├── include/cap_ha_control.h                  공개 API + entity 구조체
│   ├── include/cmd_cap_ha_control.h              console 명령 등록
│   ├── data/entities.default.json                기본 static entity 4개 (보드 onboard_rgb 등)
│   └── src/
│       ├── cap_ha_control.c                      ★ 두 typed tool descriptor + group_init
│       ├── cap_ha_control_core.c                 ha_control execute (즉시 제어)
│       ├── cap_ha_automation.c                   ★ ha_automation execute (자동화 CRUD)
│       ├── cap_ha_control_resolve.c              entity friendly_name ↔ entity_id 매핑 + NVS 캐시
│       ├── cap_ha_control_http.c                 HA REST 호출 (Bearer / HTTPS / crt_bundle)
│       ├── cap_ha_control_board.c                board:* 가상 entity (onboard_rgb 등) 처리
│       ├── cap_ha_control_internal.h             내부 공유 헤더
│       └── cmd_cap_ha_control.c                  console 명령 `ha_control --automation=...`
│
├── components/claw_capabilities/                 다른 cap 들 — 우리가 수정 X (참고용)
│   ├── cap_im_tg/                                Telegram bot
│   ├── cap_mcp_client/                           외부 MCP 서버 호출
│   ├── cap_mcp_server/                           보드 자체 MCP 서버 (mDNS)
│   ├── cap_router_mgr/                           event router
│   ├── cap_scheduler/                            보드 측 자동화 (cron-like)
│   └── …
│
├── docs/learn/                                   ★ AI 사이클의 학습 로그 (시리즈)
│   ├── 20260507-webflasher-flash-stuck.md        web flasher 실패 원인 학습
│   ├── 20260507-board-mismatch-n16r8.md          N16R8 + MXIC 분석
│   ├── 20260508-cap-ha-control-v3.md             v3 typed tool 학습
│   ├── 20260511-cap-ha-control-v4.md             v4 자동화 CRUD 학습
│   ├── 20260512-cap-ha-state-value-normalize-hotfix.md  v5 핫픽스 학습
│   ├── 20260513-cap-ha-automation-v5.md          v5 메인 학습
│   └── 20260513-v5-completion-report.md          보고서 사이클 학습
│
├── smarthome-docs/                               ★ AI 사이클의 plan / report / 메타 (별도 디렉터리)
│   ├── architecture/                             ← (이 문서가 여기)
│   ├── superpowers/plans/                        구현 plan 시리즈 (subagent-driven 입력)
│   │   ├── 2026-05-06-esp-claw-ceo-demo.md
│   │   ├── 2026-05-08-cap-ha-control-typed-tool.md
│   │   ├── 2026-05-11-cap-ha-control-v4-followups.md
│   │   ├── 2026-05-12-cap-ha-automation-state-trigger.md
│   │   └── 2026-05-12-cap-ha-automation-v5.md
│   └── reported/                                 사이클별 완료 보고서
│       ├── esp-claw-smarthome-completion-2026-05-11.md   (v3 ship)
│       ├── esp-claw-smarthome-completion-2026-05-12.md   (v4 ship)
│       └── esp-claw-smarthome-completion-2026-05-13.md   (v5 ship)
│
└── .claude/                                      Claude Code agent harness
    ├── settings.json                             hooks (completion-intent / enforce-learn-log)
    ├── memory/                                   project-scoped auto-memory
    └── worktrees/                                작업별 isolated worktree (사이클마다 cleanup)
```

요점: **두 부류의 변경**
- 펌웨어 코드: `components/claw_capabilities/cap_ha_control/` 단일 컴포넌트에 거의 모두 응집 + `boards/.../sdkconfig.defaults.board` 3줄.
- 문서 + 메타: `docs/learn/`, `smarthome-docs/` 가 사이클별 plan / 학습 / 보고서 시리즈 누적.

---

## 4. 수정한 핵심 파일들 + 코드 예시

여기서는 v3-v5 사이클의 4개 hot file 만 깊게 본다. commit-by-commit 차이는 `smarthome-docs/reported/` 의 사이클별 보고서가 더 자세.

### 4.1 `cap_ha_control.c` — 두 typed tool descriptor

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
        .input_schema_json =
            "{\"type\":\"object\",\"properties\":{"
              "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"update\",\"remove\",\"list\","
                                                    "\"trigger_now\",\"enable\",\"disable\"]},"
              "\"automation_id\":{\"type\":\"string\"},"
              "\"alias\":{\"type\":\"string\"},"
              "\"trigger\":{\"type\":\"object\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"daily_time\",\"weekly\",\"interval\",\"state\"]},"
                "\"time\":{\"type\":\"string\",\"description\":\"daily_time/weekly: 'HH:MM' 24h KST\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\","
                              "\"minimum\":0,\"maximum\":6},\"description\":\"weekly: 0=Sunday\"},"
                "\"interval_ms\":{\"type\":\"integer\",\"minimum\":2000},"
                "\"entity\":{\"type\":\"string\",\"description\":\"state: friendly name or entity_id\"},"
                "\"to\":{\"type\":\"string\",\"description\":\"state: target state. binary_sensor/light/switch/"
                                                            "input_boolean use 'on'/'off' "
                                                            "(firmware auto-normalizes 'open'->'on', "
                                                            "'closed'->'off'). cover uses 'open'/'closed'. "
                                                            "lock uses 'locked'/'unlocked'.\"},"
                "\"from\":{\"type\":\"string\",\"description\":\"state: optional previous state. "
                                                              "If omitted, firmware auto-fills the "
                                                              "domain-pair opposite to force a HA transition.\"}"
              "}},"
              "\"condition\":{\"type\":\"object\",\"description\":\"Optional gate that must be true at "
                                                                "trigger time. Single condition object — "
                                                                "AND logic.\",\"properties\":{"
                "\"kind\":{\"type\":\"string\",\"enum\":[\"time_range\",\"weekday\",\"state\"]},"
                "\"after\":{\"type\":\"string\"},"
                "\"before\":{\"type\":\"string\"},"
                "\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\","
                              "\"minimum\":0,\"maximum\":6}},"
                "\"entity\":{\"type\":\"string\"},"
                "\"state\":{\"type\":\"string\"}"
              "}},"
              "\"target\":{\"type\":\"string\"},"
              "\"device_action\":{\"type\":\"string\",\"enum\":[\"turn_on\",\"turn_off\",\"toggle\","
                                                              "\"open\",\"close\"]}"
            "},\"required\":[\"action\"]}",
        .execute = cap_ha_automation_execute_wrapper,
    },
};

void cap_ha_compose_description(void)
{
    /* 부팅 시 + boot_fetch 성공 후 호출. friendly_names 를 runtime 보간. */
    cap_ha_resolve_active_friendly_names(s_ha_friendly_names, sizeof(s_ha_friendly_names));
    snprintf(s_ha_automation_description, sizeof(s_ha_automation_description),
             "Register/modify/remove automations on Home Assistant. "
             "Active devices (use these names verbatim in 'target' or in trigger.entity / "
             "condition.entity): %s. "
             "Trigger kinds: 'daily_time' (HH:MM), 'weekly' (HH:MM + weekdays[]), "
             "'interval' (interval_ms ≥ 2000), 'state' (entity + to; firmware auto-fills 'from' "
             "as the domain-pair opposite). "
             "IMPORTANT: binary_sensor (door/window/motion) uses 'on'/'off' values, "
             "NOT 'open'/'closed' (firmware auto-normalizes if you forget). "
             "Optional 'condition' gates the trigger: 'time_range' / 'weekday' / 'state'. "
             "Example: door sensor opens between 10:00–18:00 → light on. "
             "After this tool returns, respond to the user with the result 'message' field VERBATIM.",
             s_ha_friendly_names);
    s_ha_descriptors[1].description = s_ha_automation_description;
}
```

핵심 패턴: **JSON Schema 가 C 문자열 리터럴**. 길어보이지만 C 컴파일러가 인접 리터럴 자동 결합. `claw_cap` runtime 이 LLM API 호출 시 이 문자열을 그대로 `tools[].input_schema` 로 보냄.

### 4.2 `cap_ha_automation.c` — v5 의 핵심 builder

state-trigger 의 `from` auto-fill + condition typed payload + state-value 정규화 (핫픽스):

```c
/* For domains whose HA state vocabulary is exactly "on"/"off"
 * (binary_sensor / light / switch / input_boolean), translate the
 * door/window-flavored synonyms LLMs often pick from UI labels
 * ("Open"/"Closed" display) into the actual state values HA matches against. */
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

/* Return the natural opposite of `to_val` for `domain`, or NULL if no
 * sensible default exists. Used in state trigger to force HA transition. */
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

/* build_ha_trigger_array 의 state 분기 (v5 + 핫픽스 적용 후). */
cJSON *step = cJSON_CreateObject();
cJSON_AddStringToObject(step, "platform", "state");
cJSON_AddStringToObject(step, "entity_id", resolved_eid);

const char *dot = strchr(resolved_eid, '.');
char dom[20] = {0};
if (dot && (size_t)(dot - resolved_eid) < sizeof(dom)) {
    memcpy(dom, resolved_eid, dot - resolved_eid);
}

/* Normalize 1: to / from values for on-off domains. */
const char *to_val = normalize_state_value(dom, to_j->valuestring);
cJSON_AddStringToObject(step, "to", to_val);

if (cJSON_IsString(from_j) && from_j->valuestring[0]) {
    const char *from_val = normalize_state_value(dom, from_j->valuestring);
    cJSON_AddStringToObject(step, "from", from_val);
} else {
    /* Auto-fill 2: from is the domain-pair opposite of the (already normalized) to. */
    const char *auto_from = opposite_state(dom, to_val);
    if (auto_from) {
        cJSON_AddStringToObject(step, "from", auto_from);
        ESP_LOGI(TAG, "state trigger from auto-fill: %s -> %s (domain=%s)",
                 auto_from, to_val, dom);
    }
}
```

이 30줄이 v5 의 핵심 가치. LLM 이 `to:"open"` 보내도 `to:"on", from:"off"` 로 변환되어 자동화가 transition 강제로 fire — 어휘 mismatch 가 firmware 책임.

### 4.3 `cap_ha_control_resolve.c` — entity 검색

자연어 → entity_id 매핑. NVS 캐시 + 부팅 시 `/api/states` slow-path fetch + 친절어 fuzzy 매칭. v3 의 P2 review 가 `CAP_HA_MAX_REGISTRY_ENTRIES = 64` cap + WARN 추가, v4 가 binary_sensor + light 만 cache (sensor 도메인은 NVS 폭발 방지로 제외).

### 4.4 `cap_ha_control_http.c` — HA REST 클라이언트

Bearer 인증 + `crt_bundle_attach` (HTTPS) gating + `skip_cert_common_name_check` (`--insecure` 모드, demo 전용). 5 helper:

```c
esp_err_t cap_ha_http_get_states(char *out, size_t out_size);
esp_err_t cap_ha_http_post_service(const char *domain, const char *action, const char *body);
esp_err_t cap_ha_http_put_automation_config(const char *id, const char *body, int *http_status, ...);
esp_err_t cap_ha_http_get_automation_config(const char *id, int *http_status, char *out, ...);
esp_err_t cap_ha_http_delete_automation_config(const char *id, int *http_status, ...);
esp_err_t cap_ha_http_post_reload_automations(void);
```

URL 구성:
```c
snprintf(full_url, sizeof(full_url),
         "%s/api/config/automation/config/%s", base_url, id);
```

`base_url` 은 NVS 에서 `ha_url`, Bearer 는 `ha_token` 로 저장 (셸 `ha_control --set-url` / `--set-token`).

---

## 5. 에이전트 학습 — 두 layer

"에이전트 학습" 은 이 프로젝트에서 두 가지 다른 layer 를 의미한다. 둘 다 의도적으로 구분해야 혼동 없음.

### 5.1 Layer A — 보드 내부 LLM 의 학습 (firmware-embedded)

**Agent runtime**: `claw_core` worker task (FreeRTOS) + OpenAI gpt-5-mini REST client. message → tool descriptors[] 와 함께 LLM 에 보내고, LLM 이 tool call 결정 → firmware 가 해당 capability 의 `execute()` 호출 → 결과를 다음 message 로 LLM 에게 → 최종 응답.

**학습 = LLM 가시 정보 3종**:

1. **system prompt** — `application/edge_agent/main/skills/` 의 markdown 들 (Lua skill 들 + 공통 instruction). 우리는 v3-v5 에서 이 layer 는 거의 손대지 않았음. 기본 esp-claw 흐름 유지.

2. **tool descriptors** — `s_ha_descriptors[].input_schema_json` + `cap_ha_compose_description` 의 description 텍스트. 위 4.1 의 JSON Schema + 동적 보간 텍스트가 모두 LLM 의 학습 자료. v5 의 condition 객체 추가 + 핫픽스의 "binary_sensor 는 on/off, open/closed 아님" 명시도 모두 이 layer.

3. **runtime context** — entity friendly_names (`s_ha_friendly_names`). `cap_ha_resolve` 가 부팅 시 + 명시 refresh 시 `/api/states` fetch 해서 16개 entity 의 한국어 친절어 list 를 description 에 보간. LLM 은 "거실 조명" / "현관 도어센서" 같은 자연어를 그대로 tool argument 에 사용 → firmware 가 fuzzy 매칭으로 entity_id resolve.

**학습 사이클**: code 수정 → `idf.py build` → `idf.py -p $ESP_PORT app-flash` → 보드 재부팅 → 다음 사용자 메시지부터 새 descriptor 적용. 즉, **LLM 학습 = firmware flash**.

### 5.2 Layer B — 외부 Claude Code 하네스의 학습 (개발 사이클)

**Agent runtime**: Claude Code 본체 (이 conversation 의 환경). superpowers skill 시스템 + sub-agent dispatch.

**학습 = 디렉터리 시리즈 3종**:

1. **plans** (`smarthome-docs/superpowers/plans/`) — 사이클별 구현 plan markdown. subagent-driven-development skill 의 입력으로 사용 (implementer subagent 가 task 별로 dispatched, fresh context, plan 의 task description 그대로). v3-v5 plan 5개:
   ```
   2026-05-06-esp-claw-ceo-demo.md                       (전체 비전)
   2026-05-08-cap-ha-control-typed-tool.md               (v3)
   2026-05-11-cap-ha-control-v4-followups.md             (v4)
   2026-05-12-cap-ha-automation-state-trigger.md         (v4 후속 PR #6)
   2026-05-12-cap-ha-automation-v5.md                    (v5)
   ```

2. **learn logs** (`docs/learn/`) — 사이클의 깨달음. completion-intent hook + enforce-learn-log hook 이 매 작업 마무리마다 강제. 11개 (2026-05-08 ~ 2026-05-13). 각 로그 구조: 컨텍스트 → 무엇을 만들었나 (commit table) → 무엇을 배웠나 (원칙 1-4개) → 다음 후보 → 참고.

3. **completion reports** (`smarthome-docs/reported/`) — 사이클별 완료 보고서. 별도 PR 로 ship. v3-v5: 2026-05-11 / 2026-05-12 / 2026-05-13.

**보조 메커니즘**:
- `.claude/memory/` — auto-memory (이 프로젝트의 영구 메모리: 워크트리 build env init 같은 quirk).
- `.claude/worktrees/<name>/` — 작업별 isolated 워크트리 (subagent 가 main worktree 의 진행 중 다른 작업과 충돌 안 함).
- `enforce-learn-log` hook — `docs/learn/YYYYMMDD-<topic>.md` 가 없으면 completion 차단. learn 로그를 강제로 누적.
- subagent-driven-development skill — task 별로 fresh implementer + spec reviewer + code-quality reviewer 3단 dispatch. controller (메인 conversation) 가 review 결과 조정.

**학습 사이클**: 사용자 요청 → brainstorming (선택) → plan 작성 → subagent dispatch (구현 + review) → branch-wide adversarial review → user-driven E2E → completion report → main 머지 → worktree cleanup. 이 사이클이 매 PR 마다 동일 형태로 반복되어, **다음 사이클이 이전 사이클의 plan/learn/report 를 input 으로 가져온다** (예: v5 plan 의 source 섹션이 v4 의 사용자 발견 인용).

### 5.3 두 layer 의 분리가 핵심

- Layer A (firmware-embedded) 는 *프로덕션 사용자 경로*. 사용자 텔레그램 메시지를 처리.
- Layer B (Claude Code) 는 *개발 사이클*. PR 만들고 merge 하는 사람의 path.

두 layer 가 섞이면 안 됨. v5 핫픽스 사이클이 좋은 예시 — Layer A 의 LLM 이 `to:"open"` 으로 잘못 호출한 케이스를 Layer B 의 review 가 잡지 못한 것. Layer B 의 review 는 plan + 코드 자기 정합성만 검증; LLM 의 실제 어휘는 production E2E 까지 가야 발견됨. 이게 v5 learn log §4.1 의 핵심 원칙.

---

## 6. 운영 상태 (2026-05-13 기준)

- 보드: ESP32-S3 N16R8 (MXIC Flash), 사용자 거실, 전원만 연결되어 자동 부팅. WiFi STA + HA REST + Telegram + MCP server + LLM provider (gpt-5-mini) 모두 자동 활성화.
- 펌웨어 HEAD: `fa42827` (v5 + 핫픽스). 다음 변경시 `idf.py app-flash` 만.
- HA: 192.168.1.94:8123. 자동화 4개 (사용자 직접 만든 것 + v3-v5 firmware 가 만든 것). state-trigger 가 정상 fire 하는지 사용자 영역 E2E 검증 권장.
- 다음 사이클: v6 plan 미작성. 후보 7건 — v5 보고서 §5 참조.

---

## 7. 참고

- v3 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-11.md`
- v4 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-12.md`
- v5 보고서: `smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- 자매 문서 (openclaw 와 비교): [`02-openclaw-vs-esp-claw-comparison.md`](./02-openclaw-vs-esp-claw-comparison.md)
- esp-claw 상류 (Espressif): https://github.com/espressif/esp-claw
- 우리 fork: https://github.com/JayMon0327/esp-claw-smarthome
