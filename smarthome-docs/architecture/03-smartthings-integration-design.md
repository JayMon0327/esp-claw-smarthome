# Samsung SmartThings 통합 — 미래 개발 설계 자료 (2026-05-14)

> **목적:** esp-claw 가 현재 Home Assistant 만 지원하는 상태에서, 향후 Samsung SmartThings 백엔드도 지원하기 위한 *design exploration*. 본 문서는 **plan 이 아니다** — 사용자의 질문 답변을 정식 기록한 design source. V6 plan 작성 시점이 오면 본 문서를 input 으로 사용.
>
> **현재 상태:** 미구현. esp-claw 의 단일 백엔드는 HA (`cap_ha_control` component). SmartThings 통합 (`cap_st_control` 가상) 은 V6/V7 후보. 관련 candidate items 는 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` 의 카테고리 I (I-20 ~ I-24) 참조.

- **자매 문서:**
  - `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md` — 현재 HA 통합 architecture (특히 §6 의 NVS-기반 portability 가 본 문서의 base)
  - `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md` — application-layer 구현과의 비교
- **모든 path 절대경로 (`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/...`).**

---

## 0. TL;DR

esp-claw 가 SmartThings 도 지원하려면:

1. **새 capability component** `cap_st_control` 작성 — `cap_ha_control` 패턴 mirror. 즉시 제어 (~500-700 LoC) + 자동화 list/execute (~200-300 LoC, 풀 CRUD 는 SmartThings Rules API 제약으로 어려움).
2. **인증 모델 결정**:
   - PAT (Personal Access Token, **영구**) — 사용자가 SmartThings developer console 에서 manual 생성. 단순. 개발자 / 얼리어답터용.
   - OAuth2 (access_token **24h** + refresh_token 30일+) — 사용자 친화적 portal flow. 제품화용. **`client_secret` 보호 위해 cloud relay 필수.**
3. **동적 backend detection**: mDNS `_home-assistant._tcp.local` (HA) + cloud probe `https://api.smartthings.com/v1/locations` (SmartThings). 보드 부팅 시 자동 시도 + portal 에서 사용자 선택.
4. **Capability gating**: NVS `app_config/backend` 키 (`"ha"` / `"smartthings"` / `"hybrid"`) 로 LLM-visible cap groups 동적 결정. LLM 입장에선 어느 백엔드인지 모름.
5. **핵심 architectural 사실 — 플래싱 ≠ 데이터 갱신.** OAuth refresh 처럼 24시간마다 token 새로 받아도 펌웨어 플래싱 *불필요*. 펌웨어가 NVS partition 에 자동 write. esp-claw 가 이미 현재 그렇게 동작 (HA URL, Bearer token, WiFi credentials 모두 NVS read/write — 플래싱 없이).

---

## 1. 사용자 질문 (원문 인용)

본 문서의 모든 답변은 다음 사용자 질문에 대한 정식 기록.

### Q1
> 삼성 스마트싱스 api에 맞게끔 개발할 수 있는지

### Q2
> 동적으로 로컬에 있는 삼성 스마트싱스와 ha를 검색 후, 사용자가 삼성 스마트싱스 스테이션을 사용하고 있다고 판단되면 삼성 api를 쓰도록 동적으로 초기 설정이 가능한지. -> 세팅된 esp-claw만을 사용자에게 판매한다고 가정

### Q3 (보조 질문 — PAT 만료에 대한 정정 요청)
> 스싱 pat의 경우 영구토큰이 아니라, 24시간마다 리프레쉬토큰을 이용해 발급받는 형태야. 발급받는 과정까지 esp보드에 플래싱 할 수 있어?

### Q4 (architectural 핵심 의문 — 플래싱 vs 데이터 갱신)
> 근데 그렇게 oauth + cloud relay를 통해 한다고 했는데, 플래싱 없으면 esp-claw는 그거 안되는거 아니야 ? 토큰을 esp-claw로 전달한다고 해서 esp-claw가 사용할 수가 있나 ? 플래싱 안하면 안되는거잖아

---

## 2. Q1 답변 — SmartThings API 개발 가능성

**✅ 가능.** v3-v5 cycle 의 `cap_ha_control` 패턴 그대로 적용. 한 v6 사이클 (~500-1000 LoC + plan + 검증) 작업.

### SmartThings API 구조 (HA 와의 핵심 차이)

| 차원 | HA | SmartThings |
|---|---|---|
| **Endpoint** | Local (`http://192.168.1.94:8123`) | **Cloud only** (`https://api.smartthings.com/v1/`) — 외부 인터넷 필수 |
| **인증** | Long-Lived Access Token | PAT (영구) 또는 OAuth2 (24h + refresh) |
| **TLS** | HTTP 평문 가능 (LAN) | **HTTPS 필수** — `crt_bundle` 필요 |
| **기기 모델** | `domain.entity_id` (`light.bedroom`) + `service` (`light.turn_on`) | `deviceId` UUID + `capability.command` (`switch.on`, `colorTemperature.setColorTemperature`) |
| **상태 조회** | `GET /api/states/{entity_id}` 또는 `/api/states` (일괄) | `GET /v1/devices/{id}/status`, `GET /v1/devices` (list) |
| **제어** | `POST /api/services/{domain}/{action}` body 에 entity_id + data | `POST /v1/devices/{id}/commands` body 에 commands[] array |
| **자동화 모델** | `/api/config/automation/config/{id}` PUT — trigger/condition/action 직접 작성 풀 CRUD | **Routine** (UI-only, API 로 list/execute 만) 또는 **Rules API** (capability 제한 있음, 더 복잡) |

### 필요한 새 component (절대경로, 가상)

```
/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_st_control/
├── CMakeLists.txt
├── Kconfig
├── idf_component.yml
├── include/cap_st_control.h
├── data/                           ← 기본 device alias (optional)
└── src/
    ├── cap_st_control.c            ← typed tool descriptor (st_control + st_automation)
    ├── cap_st_control_core.c       ← 즉시 제어 execute (capability/command 변환)
    ├── cap_st_control_resolve.c    ← 한국어 친절어 ↔ deviceId UUID 매핑 + NVS cache
    ├── cap_st_control_http.c       ← HTTPS only + Bearer + cloud API
    ├── cap_st_oauth.c              ← OAuth2 flow + refresh task (선택)
    ├── cap_st_automation.c         ← Routine list/execute
    └── cmd_cap_st_control.c        ← console `st_control --set-token` 등
```

NVS namespace `st_ctl`:

| 키 | 크기 | 용도 |
|---|---|---|
| `st_token_type` | enum | `"pat"` or `"oauth"` |
| `st_access_token` | ~500B | Bearer token (PAT 면 영구, OAuth 면 24h) |
| `st_refresh_token` | ~500B | OAuth 만, 30일+ |
| `st_expires_at` | u32 | epoch timestamp, OAuth 만 |
| `st_location_id` | UUID | 사용자 location (다중 location 시 default) |
| `st_insecure` | flag | HTTPS cert 검증 skip (debug only) |

### Service mapping (cap_ha_control → cap_st_control)

| HA service | SmartThings command |
|---|---|
| `light.turn_on(brightness_pct, color, kelvin)` | `[{capability:"switch", command:"on"}, {capability:"switchLevel", command:"setLevel", arguments:[N]}, {capability:"colorTemperature", command:"setColorTemperature", arguments:[K]}]` |
| `light.turn_off` | `[{capability:"switch", command:"off"}]` |
| `cover.open_cover` | `[{capability:"windowShade", command:"open"}]` |
| `lock.lock` | `[{capability:"lock", command:"lock"}]` |

### 제약

- **인터넷 필수.** 보드가 LAN-only 환경에서는 동작 안 함. HA 는 LAN-only 가능.
- **자동화 풀 CRUD 어려움.** SmartThings Routine 은 UI 에서 만든 것만 list/execute 가능. 자동화 *생성* 은 Rules API 필요 + 일부 capability 만. v5 의 ha_automation 같은 풀 CRUD 는 v6+ scope 에서 제외 권장.
- **Rate limit.** SmartThings cloud 가 분당 호출 제한 — boot-fetch 1회 + capability 호출마다 1회면 안전.

---

## 3. Q2 답변 — 동적 detection + 제품화 초기 설정

**✅ 가능. 3-layer 디자인.**

### 시나리오: 세팅된 esp-claw 만 판매, 사용자가 받아서 처음 켰을 때

#### Layer 1: WiFi provisioning (기존 esp-claw 패턴 그대로)
부팅 시 NVS `wifi_ssid` 비면 SoftAP `esp-claw-XXXXXX` 띄움 → 사용자 폰으로 `http://192.168.4.1/` 접속 → SSID/PW 입력 → STA 연결.

위치: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/http_server/`

#### Layer 2: 백엔드 자동 감지 (mDNS + cloud probe)
STA 연결 후 보드가 자동 시도:

1. **mDNS query** `_home-assistant._tcp.local` (HA 의 zeroconf integration enable 시) — 발견되면 *HA 모드 default*.
2. mDNS 실패 → portal 에 백엔드 선택 화면 노출:

```
┌──────────────────────────────────────────────┐
│ 어떤 스마트홈 hub 사용하시나요?               │
│                                              │
│ ◉ Home Assistant (라즈베리파이 + 자체 hub)    │
│ ○ Samsung SmartThings (cloud + Station)      │
│ ○ 둘 다 사용 (hybrid)                         │
│ ○ 모르겠음 → 자동 감지 시도                   │
│                                              │
│ [다음]                                        │
└──────────────────────────────────────────────┘
```

3. 사용자 선택에 따라 credentials 입력 화면 분기:
   - **HA**: URL + Long-Lived Token 입력
   - **SmartThings**: 인증 모델 선택 (PAT or OAuth) → PAT 입력 또는 OAuth flow 진입

#### Layer 3: Capability gating (LLM-visible cap groups)
부팅 시 NVS `app_config/backend` 키 (`"ha"` / `"smartthings"` / `"hybrid"`) 에 따라 `vis_cap_groups` 동적 결정:

```c
// 가상 코드 (위치: /Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/app_config/app_config.c)
const char *backend = nvs_get_str("app_config", "backend");
if (strcmp(backend, "ha") == 0) {
    set_vis_cap_groups("cap_skill,cap_ha_control,cap_im_tg,cap_time,cap_system");
} else if (strcmp(backend, "smartthings") == 0) {
    set_vis_cap_groups("cap_skill,cap_st_control,cap_im_tg,cap_time,cap_system");
} else if (strcmp(backend, "hybrid") == 0) {
    set_vis_cap_groups("cap_skill,cap_ha_control,cap_st_control,cap_im_tg,cap_time,cap_system");
}
```

**LLM 시각:** 어떤 백엔드인지 모름. enabled 된 `*_control` capability 만 본다. 같은 typed-tool 패턴이라 LLM 학습 부담 없음. 사용자 자연어 ("화장실 조명 켜줘") 는 두 백엔드에서 동일하게 작동.

---

## 4. Q3 답변 — PAT vs OAuth2 정정 + esp-claw 의 OAuth flow

### 정정: SmartThings 의 두 인증 모델

| 모델 | 만료 | 발급 방법 | refresh |
|---|---|---|---|
| **PAT** (Personal Access Token) | **영구** (사용자 명시 revoke 까지) | 사용자가 SmartThings developer console (`https://account.smartthings.com/tokens`) 에서 manual 생성 | 불필요 |
| **OAuth2 access_token** | **24시간** | SmartApp / OAuth-integrated app 이 사용자 인증 후 받음 | refresh_token (보통 30일+) 으로 새 access_token 발급 |

사용자가 묘사한 "24시간마다 리프레쉬토큰을 이용해 발급" = OAuth2.

### 표준 OAuth2 Authorization Code flow

```
1. 사용자 → esp-claw portal → "SmartThings 로 로그인" 버튼
2. 브라우저 redirect: https://api.smartthings.com/oauth/authorize
                       ?client_id=<esp-claw-app-id>
                       &redirect_uri=<callback>
                       &response_type=code
                       &scope=r:devices:* w:devices:* x:devices:*
3. 사용자가 SmartThings 페이지에서 인증 + 권한 승인
4. SmartThings → redirect_uri 로 authorization code (one-time, 10분 만료)
5. esp-claw 가 code + client_secret 으로 token endpoint POST:
   POST https://api.smartthings.com/oauth/token
     grant_type=authorization_code
     code=<...>
     client_id=<esp-claw-app-id>
     client_secret=<...>
   → 응답: { access_token (24h), refresh_token (30일+), expires_in: 86400 }
6. esp-claw 가 NVS 에 두 토큰 + expires_at 저장
7. 매 API 호출 시 Bearer access_token
8. expires_at - 1h 시점에 자동 refresh:
   POST /oauth/token
     grant_type=refresh_token
     refresh_token=<...>
     client_id=<...> client_secret=<...>
   → 새 access_token + (rotation 시) 새 refresh_token
```

### 핵심 architectural 결정 두 가지

#### 결정 1: `redirect_uri` — cloud relay 필요

SmartThings developer console 에 등록된 `redirect_uri` 와 일치하는 URL 만 callback 수신 가능. 사용자마다 보드 IP 가 다르니 *모든 IP 를 등록 불가능*. 두 선택지:

**A. Cloud Relay (제품화 권장)**
```
SmartThings → https://esp-claw-oauth.io/callback?code=<...>
              (우리가 호스팅하는 작은 cloud 서버 — Cloudflare Worker / Vercel function)
              │
              ▼
         보드에 code 전달:
         - (a) 사용자가 portal 에 6자리 short-code paste
         - (b) WebSocket / mDNS / device-pairing flow
              ▼
         보드 → token endpoint POST → access + refresh token
```
- 등록된 `redirect_uri = https://esp-claw-oauth.io/callback` 하나만. 모든 esp-claw 보드 공용.
- 인프라 비용: Cloudflare Worker 무료 tier (월 100,000 req) 또는 Vercel free tier 면 충분. 도메인 ~$10/년.

**B. 사용자 manual 등록 (개발자/얼리어답터용)**
- 사용자가 자기 SmartThings developer console 에서 OAuth app 만들고 + 자기 보드 IP 등록 + client_id/secret 받아서 esp-claw portal 에 입력.
- 사용자 친화성 0.

#### 결정 2: `client_secret` 보호 — 펌웨어에 박으면 안 됨

OAuth2 의 client_secret 은 공개되면 안 되는 비밀. esp-claw 펌웨어에 hardcode 시 누구나 binary 디스어셈블 해서 추출 → 임의의 esp-claw "행세" 가능 → SmartThings 측 abuse.

**해결책:** cloud relay 가 client_secret 보유 + token exchange / refresh 처리. 보드는 client_secret 모름.

```
보드 → cloud relay (code 또는 refresh_token 전달)
        │ + client_secret (relay 가 가짐)
        ▼
        SmartThings token endpoint
        │
        ▼
      access + refresh token
        │
        ▼
보드 ← cloud relay (token 만 보드로 전달)
```

→ **cloud relay 는 OAuth flow 의 *필수* 구성요소** (제품화 시).

### Refresh 도 relay 위임 (옵션 B 권장)

| 옵션 | 동작 | trade-off |
|---|---|---|
| **A. 보드 직접 refresh** | 보드가 NVS refresh_token + (relay 로부터 받은) client_secret 으로 직접 POST. | client_secret 이 보드에 노출 — abuse risk. |
| **B. Refresh 도 relay 위임** | 보드가 relay 에 "refresh" 요청 + refresh_token 보냄. relay 가 client_secret 추가해서 SmartThings POST. 새 access_token 만 보드로 반환. | client_secret 보호 ✓. 모든 사용자 refresh 트래픽이 relay 거침 (월 ~30 req/board). |

---

## 5. Q4 답변 — 펌웨어 ≠ 데이터 (NVS partition 기반)

**핵심 정정:** 토큰을 받아서 사용하는 데 *플래싱 필요 없음*. 펌웨어와 데이터는 두 layer.

### Layer 분리

| Layer | 위치 | 변경 시 |
|---|---|---|
| **A. 펌웨어 (Code)** | `ota_0` 파티션 (0x20000, 4MB) — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/build/edge_agent.bin` | `idf.py flash` 필요 |
| **B. 데이터 (NVS)** | `nvs` 파티션 (0x9000, 24KB) — binary key-value store | **runtime read/write, 플래싱 불필요** |

펌웨어 안에는 *OAuth flow 처리 코드* (POST 요청 만들기, NVS read/write, Bearer header, FreeRTOS refresh timer) 가 박혀 있다. 그 코드는 *데이터를 NVS 에서 read* + *받은 token 을 NVS 에 write*. **데이터가 token 이든 IP 든 무엇이든, 펌웨어는 같은 코드 path 로 동작.**

### OAuth refresh 의 실제 흐름 (펌웨어 한 번만 flash, 토큰은 영구 자동 갱신)

```
[ 1회 플래싱 ] — esp-claw 제품 출하 시 (또는 v6 사이클 코드 변경 시)
   ↓
펌웨어에 박힌 코드:
- /Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_st_control/src/cap_st_control_http.c
- /Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_st_control/src/cap_st_oauth.c
   ├─ HTTPS POST helper
   ├─ NVS namespace "st_ctl" read/write
   ├─ OAuth token refresh 함수
   └─ FreeRTOS timer task (1시간마다 expires_at 체크)
   ↓
[ 보드 출하 — 사용자 손에 ]
   ↓
첫 부팅: NVS 비어있음 → SoftAP portal
   ↓
사용자: SmartThings OAuth 통해 access_token + refresh_token 받음 (cloud relay 통해 보드로 전달)
   ↓
펌웨어가 NVS 에 write (자동):
  nvs_open("st_ctl", NVS_READWRITE, &h);
  nvs_set_str(h, "st_access_token", "abc123...");
  nvs_set_str(h, "st_refresh_token", "xyz789...");
  nvs_set_u32(h, "st_expires_at", 1747234567);
  nvs_commit(h);
   ↓
[ 사용자 자연어 명령 도착 ]
   ↓
펌웨어가 NVS read (자동):
  nvs_get_str(h, "st_access_token", buf, &len);
  → Bearer <access_token> 으로 SmartThings API 호출
   ↓
[ 24시간 후 ]
   ↓
FreeRTOS timer task 가 1시간마다 체크 (자동):
  if (now > expires_at - 3600) {
      access_new = oauth_refresh_via_relay(refresh_token_old);
      nvs_set_str(h, "st_access_token", access_new);
      nvs_set_str(h, "st_refresh_token", refresh_new);
      nvs_commit(h);
  }
   ↓
[ 30일, 1년, ... 후 ] 사용자 개입 0, 플래싱 0.
```

→ **펌웨어 한 번만 flash (제품 출하 시점) → 그 뒤 OAuth refresh, 다른 집 이동, HA→SmartThings 백엔드 변경 등 모두 NVS write 만.**

### 비유

- **펌웨어** = 자동차 본체 (engine, ECU, 운전 로직)
- **NVS 데이터** = 운전자가 차에 넣는 *연료 + 네비 목적지 주소*
- **플래싱** = 자동차 자체 교체 (engine swap)

자동차 한 번 잘 만들고 출고하면, 연료 갈아 넣는 거 / 목적지 새로 입력하는 거 / 운전자 바뀌는 거 — *차 자체는 그대로*. OAuth refresh = "운전자가 한 달에 한 번 주유소 들름". esp-claw 의 refresh task 가 자동 주유.

### 검증 — 이미 사용자 보드에서 동작 중인 동일 패턴

NVS 에 runtime write 되는 데이터 (모두 플래싱 없이):

| 데이터 | NVS 위치 | 누가 write | 플래싱 필요? |
|---|---|---|---|
| WiFi SSID/PW | `app_config/wifi_ssid` / `wifi_password` | SoftAP portal 또는 `wifi --set` 콘솔 | ❌ |
| HA URL | `ha_ctl/ha_url` | portal 또는 `ha_control --set-url` | ❌ |
| HA Long-Lived Token | `ha_ctl/ha_token` | portal 또는 `ha_control --set-token` | ❌ |
| Entity 캐시 (12개) | `ha_ctl/entity_cache` | 부팅 시 `/api/states` fetch 후 cap_ha_resolve 가 자동 write | ❌ |
| Telegram bot token | `app_config/tg_bot_token` | portal 또는 `tg --set-token` | ❌ |
| OpenAI API key | `app_config/llm_api_key` | portal | ❌ |
| OAuth access_token (v6+) | `st_ctl/st_access_token` | OAuth flow + refresh task (가상) | ❌ |
| OAuth refresh_token (v6+) | `st_ctl/st_refresh_token` | OAuth flow + refresh task (가상) | ❌ |

8 종류 데이터, 모두 플래싱 없이 runtime write. 사용자 보드가 이미 현재 그렇게 동작 중 (HA URL, Bearer token, WiFi credentials 등). OAuth 토큰도 같은 패턴.

### 플래싱이 *실제로* 필요한 경우

1. **펌웨어 코드 자체 변경 시** — 예: cap_st_control component 첫 추가, OAuth refresh 로직 작성, v6 사이클의 코드 변경. **한 번만**.
2. **부트로더 / 파티션 테이블 변경** (드물게).
3. **NVS 가 완전 corrupt 됐을 때** — `esptool erase_flash` 후 재플래시. 거의 안 일어남.

플래싱이 *절대 필요 없는* 경우:
- 토큰 만료 / 갱신 (OAuth refresh 포함)
- 다른 집 / 네트워크 이동
- HA URL / SmartThings 계정 변경
- WiFi 변경
- 사용자가 새 device 를 hub 에 추가

---

## 6. 사용자 경험 — 제품화 시나리오

**사용자가 esp-claw 제품을 받아서 처음 켤 때:**

1. 보드 부팅 → SoftAP `esp-claw-XXXXXX` 모드 (NVS 의 `wifi_ssid` 비어있음 → 자동 SoftAP)
2. 사용자 폰 WiFi 를 SoftAP 에 접속 → 자동 redirect to `http://192.168.4.1/`
3. portal 1단계: WiFi SSID/PW 입력 → 보드 STA 연결
4. portal 2단계: 백엔드 선택 (HA / SmartThings / 자동 감지 / 둘 다)
5. SmartThings 선택 시 인증 모델:
   - **PAT 모드** (개발자/얼리어답터): SmartThings 페이지에서 PAT 생성 후 paste
   - **OAuth 모드** (제품화 권장):
     ```
     ┌──────────────────────────────────────────────┐
     │ SmartThings 계정으로 연결                     │
     │                                              │
     │ [ SmartThings 로 로그인 ]   ← 버튼 클릭       │
     │                                              │
     │ ↓ 자동으로 SmartThings 로그인 페이지 열림     │
     │ ↓ 사용자 인증 + 권한 승인                     │
     │ ↓ "이제 esp-claw 로 돌아가세요" 안내           │
     │                                              │
     │ 6자리 코드 입력:  [ ____ ]                    │
     │                  (SmartThings 페이지에서       │
     │                   받은 코드)                   │
     │                                              │
     │ [완료]                                        │
     └──────────────────────────────────────────────┘
     ```
6. 보드가 6자리 코드를 cloud relay 로 보내 → relay 가 SmartThings 와 token exchange → access + refresh token 보드로 반환 → NVS 저장
7. 24시간마다 보드가 relay 통해 자동 refresh — **사용자 개입 0, 플래싱 0**

---

## 7. 구현 비용 + 분할 권장

| 작업 | LoC | 비고 |
|---|---|---|
| cap_st_control component (즉시 제어, PAT) | ~500-700 | cap_ha_control 패턴 mirror |
| cap_st_automation (Routine list + execute, 생성 제외) | ~200-300 | Rules API 풀 지원은 v7+ |
| OAuth2 flow + refresh task | ~300 | esp-claw 측 |
| Cloud relay (Cloudflare Worker, TypeScript/JS) | ~150 | 별도 repo / 호스팅 |
| 백엔드 자동 감지 (mDNS HA + cloud probe SmartThings) | ~150 | v6 candidate D-8 확장 |
| SoftAP portal 의 백엔드 선택 UI | ~300 | v6 candidate D-10 확장 |
| Capability gating dynamic (NVS `backend` 키 기반) | ~50 | app_config 확장 |
| **합계 esp-claw 코드** | **~1500-1800** | + relay 150 LoC + 도메인 $10/년 |

**한 v6 사이클로는 부담. 2 사이클 분리 권장:**

### v6 (~700 LoC, 즉시 가능)
- cap_st_control 즉시 제어 only
- PAT-only 인증 (영구 토큰, 사용자가 SmartThings developer console 에서 manual 생성 후 portal 에 paste)
- 백엔드 capability gating (NVS `backend` 키)
- **대상**: 개발자 / 얼리어답터. 제품화 X.

### v7 (~800 LoC + relay, 제품화)
- OAuth2 flow + refresh task
- Cloud relay 호스팅 (Cloudflare Worker / Vercel)
- SoftAP portal UI 의 OAuth 분기 + 6자리 코드 입력
- 백엔드 자동 감지 (mDNS + cloud probe)
- **대상**: 제품화 — 사용자 manual generation 없이 OAuth flow.

---

## 8. V6 candidate consolidation 에 반영 (PR #12 또는 후속)

`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` 의 카테고리 I (백엔드 멀티화) 5건이 본 문서를 design source 로 함:

- **I-20.** `cap_st_control` 즉시 제어 (PAT) — ~500-700 LoC, 🟡, v6 후보
- **I-21.** `cap_st_automation` (Routine list + execute) — ~200-300 LoC, 🟢
- **I-22.** OAuth2 + cloud relay — esp-claw 측 ~300 LoC + relay ~150 LoC, 🟡, v7 후보
- **I-23.** 백엔드 자동 감지 + portal 분기 UI — ~450 LoC, 🟡
- **I-24.** Capability gating dynamic (NVS `backend`) — ~50 LoC, 🔴 (제품화 dependency)

---

## 9. 참고 (모두 절대경로)

- esp-claw 현재 architecture: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md` (§6 의 NVS portability 가 본 문서의 base)
- openclaw 와 비교: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
- V6 candidate consolidation: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-13-v6-candidates.md` (PR #12, 머지 시점에 카테고리 I 포함됨)
- SmartThings API docs: `https://developer.smartthings.com/docs/api/public`
- SmartThings OAuth2 docs: `https://developer.smartthings.com/docs/oauth2/intro`
- SmartThings developer console: `https://account.smartthings.com/tokens` (PAT 생성) / `https://developer.smartthings.com/workspace` (OAuth app 등록)
