# V6 후보 통합 목록 (Candidate Consolidation, 2026-05-13)

> **이 문서는 V6 plan 이 아니다.** V6 plan 작성자가 우선순위 결정 + 1-3건 선정 시 single-source 로 사용할 *후보 통합 list*. 다음 PR 에서 V6 plan 본격 작성 예정 (사용자 결정에 따라).

- **상태:** consolidation 단계 (plan 아님)
- **다음 단계:** V6 plan 본격 작성 (사용자 우선순위 결정 → brainstorming → v3-v5 plan 형식의 implementation plan markdown)
- **base 시점:** main HEAD `77d6671` (PR #11 머지 후, 2026-05-13)
- **source 4 layer:**
  1. v5 보고서 §5 — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
  2. 아키텍처 분석 §6 — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md`
  3. 아키텍처 비교 §6.4 — `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
  4. 본 문서 이전 conversation 의 Q&A (산발 7건의 유일한 출처 — 본 문서 작성으로 정식 기록)

---

## 0. 분류 + 우선순위 한눈

| 분류 | # | 후보 | 우선순위 표시 |
|---|---|---|---|
| **A. capability 확장 (자동화 기능)** | 1 | OR/NOT 복합 condition | 🟡 |
| | 2 | `for` 지속시간 condition/trigger | 🟡 |
| | 3 | numeric_state trigger + sensor.* 재도입 | 🟡 |
| | 4 | template / sun condition | 🟢 |
| | 5 | attribute change trigger | 🟢 |
| **B. on-device 자동화** | 6 | on-device automation (board:*) | 🟡 |
| **C. 안정성 / rigor** | 7 | builder rigor (OOM rollback + empty weekday) | 🔴 |
| **D. portability (다른 환경 이식)** | 8 | mDNS HA 자동 발견 | 🔴 |
| | 9 | esp-web-tools 호스팅 web flasher | 🟡 |
| | 10 | SoftAP provisioning portal UX 개선 | 🟡 |
| | 11 | HA UI 의 esp-claw integration | 🟢 |
| **E. 학습 layer (openclaw 패턴 차용)** | 12 | storage 파티션 markdown skill | 🟡 |
| | 13 | **사용자 제어 history 영구 저장 (storage NDJSON)** | 🔴 ★ |
| | 14 | **외부 sink (esp-claw + openclaw hybrid)** | 🟡 ★ |
| | 17 | **모델 cascade (저비용 모델 routine 작업)** | 🟢 ★ |
| **F. 안전 policy** | 18 | **safety policy layer (markdown-driven)** | 🟡 ★ |
| **G. 비전 + 멀티 보드 (보드 활용)** | 15 | **camera vision capability (S3-EYE 활용)** | 🟡 ★ |
| | 16 | **multi-board federation (CoreS3 hub + S3-EYE)** | 🟢 ★ |
| **H. 환경 reference (보드 호환성)** | 19 | **보드 호환성 reference (M5Stack / H2 / MXIC HPM)** | 🔴 ★ |

범례:
- 🔴 = 사용자 직접 진단 또는 production 사용 영향 큼
- 🟡 = 명백한 가치 + 실용성
- 🟢 = 가치는 있으나 niche / 후순위
- ★ = **이번 conversation Q&A 에서 도출 (정식 문서 첫 기록은 이 문서)**

---

## 1. 카테고리별 상세

### A. Capability 확장 — 자동화 기능 (5건)

#### A-1. OR/NOT 복합 condition (🟡)
- **source:** v5 보고서 §5, v5 learn §4.4 ("v6 후속")
- **현재 상태:** `cap_ha_automation` 의 condition 은 single object 만 (`build_ha_condition_array` 가 단일 object 처리). `do_create` 가 weekly trigger 의 auto-emit weekday condition 과 AND-merge 만 지원.
- **목표:** condition 객체에 `or` / `not` / `and` 형식 지원 (HA-native `condition.or` / `condition.not` 패턴). 예: `{or: [{kind:"time_range", ...}, {kind:"weekday", ...}]}`.
- **예상 LoC:** ~80-120 (builder 의 recursive 처리 + schema 확장)
- **위치:** `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c` (build_ha_condition_array) + `cap_ha_control.c` (input_schema_json)

#### A-2. `for` 지속시간 condition/trigger (🟡)
- **source:** v5 보고서 §5
- **목표:** state trigger 의 `for: "00:03:00"` (3분 이상 켜져 있을 때만), state condition 의 `for` 도. HA-native 지원.
- **예상 LoC:** ~40-60 (state trigger + state condition 양쪽 + schema 확장)

#### A-3. numeric_state trigger + sensor.* 재도입 (🟡)
- **source:** v5 보고서 §5
- **현재 상태:** v3 의 P2 review 에서 sensor 도메인이 NVS 폭발 방지로 boot-fetch 에서 제외됨 (`cap_ha_resolve` 의 filter 가 binary_sensor/light/switch/cover/lock 만 cache).
- **목표:** sensor 도메인 cache 별도 분리 (limit 더 작게) + numeric_state trigger (`above` / `below`) 지원.
- **예상 LoC:** ~100-150 (resolve cache 별도 분리 + builder + schema)

#### A-4. template condition / sun condition (🟢)
- **source:** v5 보고서 §5
- **목표:** Jinja2 template (HA 의 dynamic condition) 또는 sun (sunrise/sunset 기반) condition. template 은 LLM 이 직접 작성하면 위험 — sandbox 필요.
- **예상 LoC:** ~150-300 (sandbox 가 필요한 경우)

#### A-5. attribute change trigger (🟢)
- **source:** v5 보고서 §5
- **목표:** state 외 attribute 변경 (예: light 의 brightness 만 변화) 감지.
- **예상 LoC:** ~50-80

### B. On-device automation (1건)

#### B-6. board:* 자동화 (cap_scheduler subset) (🟡)
- **source:** v3-v5 deferred. 사용자 보드의 onboard RGB LED 같은 *보드 자체* peripheral 의 자동화. 현재는 v5 error 메시지에서 "보드 자체 자동화는 현재 지원되지 않습니다" 로 reject.
- **목표:** cap_scheduler 의 subset 으로 보드 측 trigger (interval / daily_time) + action (board:* GPIO 제어) 지원.
- **예상 LoC:** ~200-400 (cap_scheduler 의 새 action handler)

### C. 안정성 / rigor (1건)

#### C-7. Builder rigor (🔴)
- **source:** v5 review 후속 (learn `20260513-cap-ha-automation-v5.md` §4.4)
- **두 항목:**
  - **OOM rollback:** `cap_ha_automation.c` 의 do_create merge loop 에서 `cJSON_Duplicate(step, true)` 가 NULL 반환 시 silent skip → 명시적 failure + 이미 추가된 entries rollback.
  - **weekday all-invalid 명시 실패:** `weekdays:[7,8,99]` 같은 all-invalid 입력 시 filter 후 빈 array 가 그대로 HA 로 (HA 가 "never" 로 해석). filter 후 size==0 이면 ESP_ERR_INVALID_ARG + 한국어 에러 메시지.
- **예상 LoC:** ~20-30 (두 항목 합쳐서)
- **위치:** `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_automation.c`

### D. Portability — 다른 환경 이식 (4건)

#### D-8. mDNS 기반 HA 자동 발견 (🔴)
- **source:** 아키텍처 §6.4 (사용자 Q4 답변)
- **목표:** 부팅 시 NVS 의 `ha_url` 이 비어 있으면 mDNS query `_home-assistant._tcp.local` 시도 → 발견 시 SoftAP portal 에 prefill. 사용자가 token 만 입력하면 됨.
- **fallback:** subnet `GET /api/` probe (~150 LoC).
- **예상 LoC:** ~80 (mDNS only), ~150 (mDNS + subnet probe fallback)
- **위치:** 새 함수 `cap_ha_control_resolve_ha_url` in `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`

#### D-9. esp-web-tools 호스팅 web flasher (🟡)
- **source:** 아키텍처 §6.6 (사용자 Q6 답변)
- **목표:** 우리 fork 의 binary (HPM_DISABLED 적용 버전) 를 GitHub Releases 에 호스팅 + GitHub Pages 에 esp-web-tools UI 페이지 호스팅. 사용자가 다른 보드 셋업 시 한 페이지에서 처리.
- **예상 작업:** 코드 0, GitHub Actions workflow + Pages 설정 0.5일.
- **위치:** 새 repo `JayMon0327/esp-claw-smarthome-flasher` 또는 main repo 의 `docs/` 아래 GitHub Pages.

#### D-10. SoftAP provisioning portal UX 개선 (🟡)
- **source:** 아키텍처 §6.5 (방법 1 답변 끝 "사용자 친화적 UX 개선은 v6+ 후보")
- **현재 상태:** 부팅 시 `wifi_ssid` 빈 NVS 면 `esp-claw-XXXXXX` SoftAP 띄움. 캡티브 포털 노출. 단순한 form.
- **목표:** 한국어 UI + WiFi scan 결과 선택 + HA URL prefill (D-8 mDNS) + 입력 validation.
- **예상 LoC:** ~200-400 (HTTP server route + HTML/CSS/JS)
- **위치:** `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/http_server/`

#### D-11. HA UI 의 esp-claw integration (🟢)
- **source:** 아키텍처 §6.5 (방법 3)
- **목표:** HA 가 esp-claw 를 device 로 인식 (Matter / esphome 패턴). HA UI 에서 설정 가능.
- **예상 LoC:** ~500-1000 (esphome-style discovery + HA 의 config flow 와 통신)
- **risk:** 큰 작업, 외부 protocol 학습 필요.

### E. 학습 layer (openclaw 패턴 차용, 4건)

#### E-12. Storage 파티션 markdown skill (🟡)
- **source:** 아키텍처 비교 §6.4 (hybrid 가능성)
- **목표:** esp-claw 의 `storage` 파티션 (4MB FAT, `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/partitions_16MB.csv` 참조) 에 markdown 정책 파일들 두고 LLM 가 runtime 에 read. openclaw 의 `workspace/smart-home/` 패턴.
- **목적:** firmware 재플래시 없이 정책 추가/수정 가능.
- **예상 LoC:** ~150-300 (storage read capability 추가 + LLM context 주입)
- **risk:** prompt injection 표면 증가 — 정책 markdown 의 신뢰 boundary 결정 필요.

#### E-13. ★ 사용자 제어 history 영구 저장 (storage NDJSON) (🔴)
- **source:** 본 conversation Q&A (사용자가 "esp-claw 학습 못 하네?" 직접 진단)
- **현재 상태:** `ESP_LOGI` 시리얼만 — 영구 저장 없음. 보드 재부팅 시 모든 history 사라짐.
- **목표:** 매 tool call 결과를 `storage/event_history.ndjson` 에 append. 다음 LLM 호출 시 마지막 N건을 context 로 inject.
- **사용자 진단 가치:** openclaw 와의 학습 layer gap 의 핵심.
- **예상 LoC:** ~80-150 (FAT file append + context inject)
- **위치:** 새 capability `cap_history` 또는 `cap_files` 활용

#### E-14. ★ 외부 sink — esp-claw + openclaw hybrid (🟡)
- **source:** 본 conversation Q&A (사용자가 esp-claw 의 학습 부재 진단 후 도출)
- **목표:** esp-claw 가 매 tool call 후 외부 (HA webhook 또는 Pi 의 openclaw `event_history.ndjson` endpoint) 로 NDJSON 한 줄 POST. openclaw 의 분석 봇 (pi_02a_bot) 이 esp-claw + openclaw 둘 다 통합 분석.
- **장점:** 두 시스템의 강점 결합 (esp-claw 의 항상-on 임베디드 + openclaw 의 markdown 정책 + 분석).
- **예상 LoC:** ~50-100 (HTTP webhook POST capability)
- **dependency:** openclaw 의 ingest endpoint 추가 필요 (Pi 측 작업).

#### E-17. ★ 모델 cascade (🟢)
- **source:** 본 conversation (openclaw 비교 Q&A)
- **현재 상태:** esp-claw 가 단일 모델 (gpt-5-mini) 만 사용.
- **목표:** routine 작업 (boot-fetch, heartbeat, simple 자연어 해석) 은 haiku 4.5 같은 저비용 모델, 복잡한 분석은 gpt-5-mini 또는 gpt-4o cascade. openclaw 의 `HEARTBEAT.md` 패턴.
- **예상 LoC:** ~50-100 (LLM provider 의 model selection 분기)
- **cost 효과:** API 비용 30-50% 절감 가능 (routine 작업 비중에 따라)

### F. 안전 policy (1건)

#### F-18. ★ Safety policy layer (markdown-driven) (🟡)
- **source:** 본 conversation (openclaw 비교 Q&A)
- **현재 상태:** esp-claw 의 LLM 가 자체 판단 — safety_rules layer 없음. v5 normalize 가 어휘 layer 의 시작이지만 보안 layer 는 없음.
- **목표:** openclaw 의 `smart-home/common/safety_rules.md` 패턴 도입. 등급 1 (lock.unlock / alarm.disarm 같은 위험 service) 은 firmware-side gating + 사용자 명시 승인 required.
- **예상 LoC:** ~100-200 (markdown policy parser + firmware gating + 사용자 승인 path)
- **dependency:** E-12 (storage markdown skill) 또는 firmware 박힌 safety rule.

### G. 비전 + 멀티 보드 (보드 활용, 2건)

#### G-15. ★ Camera vision capability (🟡)
- **source:** 본 conversation (S3-EYE 보드 구매 Q&A)
- **현재 상태:** esp-claw 의 `lua_module_camera` 가 Lua 모듈로 카메라 driver 노출 — LLM-가시 typed tool 아님. `cap_llm_inspect` capability 가 LLM 의 이미지 검사 — 입력이 file 만 (camera direct stream 아님).
- **목표:** 두 가지 path:
  - **path 1:** Lua 스크립트 + `cap_lua` + `cap_llm_inspect` 결합 (~50 LoC). 자연어 "거실에 사람 있어?" → Lua → 캡처 → cap_llm_inspect → LLM vision 응답.
  - **path 2:** 새 typed capability `cap_camera_vision` (~300-500 LoC + v6 plan). LLM-가시.
- **위치:** path 1 = `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/main/skills/` 의 Lua skill. path 2 = 새 component `components/claw_capabilities/cap_camera_vision/`.

#### G-16. ★ Multi-board federation (🟢)
- **source:** 본 conversation (CoreS3 + S3-EYE 연동 Q&A)
- **목표:** 여러 esp-claw 보드가 (a) mDNS 으로 서로 발견 + (b) MCP federation 또는 HA event bus 통해 협력. 예: S3-EYE = camera sensor (detection 발화), CoreS3 = LCD dashboard + Telegram hub (수신 + 자동화 trigger).
- **예상 LoC:** ~200-400 (federation discovery + event 전달 + cap_mcp_server 의 새 tool)
- **dependency:** G-15 (vision capability) 가 source 보드 측에 있어야 함.

### H. 환경 reference — 보드 호환성 (1건)

#### H-19. ★ 보드 호환성 reference (🔴)
- **source:** day1 worktree 의 untracked learn 로그 2개 (`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/20260507-webflasher-flash-stuck.md` + `20260507-board-mismatch-n16r8.md`) — main 안 머지됨
- **목표:** 보드 호환성 분석 (M5Stack 변형 / ESP32-H2 / MXIC HPM 우회 등) 을 main 의 정식 문서로. v6 plan 의 base 자료. 새 보드 추가 시 reference.
- **예상 작업:** day1 의 untracked learn 로그 2개 + 본 conversation 의 M5Stack/S3-EYE Q&A 답변을 묶어서 `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/03-board-compatibility-reference.md` 작성. 코드 0, ~30-60분.

---

## 2. V6 plan 작성자 가이드

이 문서를 V6 plan 작성 시 사용하는 절차:

1. **읽기:** 본 문서 전체 + 4 source layer (v5 보고서 §5 + 아키텍처 §6 + 비교 §6.4 + 이 문서의 산발 7건).
2. **우선순위:** 🔴 (production-critical) → 🟡 (high value) → 🟢 (niche). ★ 표시 (사용자 직접 진단) 가중치 추가.
3. **V6 사이클 선정:** 1-3건 선정 권장. v3-v5 cycle 의 LoC 분포 참고 — v3 (typed tool 도입) ~400 LoC, v4 (자동화 CRUD) ~700 LoC, v5 (from auto-fill + condition + 핫픽스) ~300 LoC. V6 도 비슷한 size 가 한 사이클 적정.
4. **Plan 작성:** v5 plan (`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-v5.md`) 같은 형식. subagent-driven-development skill 의 입력 가능.
5. **빠진 후보:** 본 문서 외 추가 후보 도출 시 본 문서에 append 또는 새 candidate consolidation 문서로.

### 추천 V6 첫 사이클 조합 (~400-600 LoC)
- C-7 (builder rigor) + D-8 (mDNS HA 발견) + H-19 (보드 호환성 reference). 모두 🔴, 합 ~250 LoC + 30분 docs. 사용자 portability 시나리오 (다른 집 가서 데모) 즉시 가능.
- 또는 E-13 (사용자 제어 history) + E-14 (외부 sink) + C-7 (rigor). E-13/14 가 사용자 직접 진단한 "learning layer" gap 해결. ~250-380 LoC.

---

## 3. 참고

- 본 candidate list 도출 conversation 의 사이클 학습: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/20260513-v6-candidates-tracking.md` (본 PR 에서 함께 추가)
- V5 보고서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/reported/esp-claw-smarthome-completion-2026-05-13.md`
- 아키텍처 분석 (HA 설정 + portability §6): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/01-esp-claw-current-architecture.md`
- openclaw 비교 (hybrid §6.4): `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
- day1 worktree 의 보드 호환성 untracked learn: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/docs/learn/20260507-webflasher-flash-stuck.md`, `20260507-board-mismatch-n16r8.md` (H-19 후보의 source)
