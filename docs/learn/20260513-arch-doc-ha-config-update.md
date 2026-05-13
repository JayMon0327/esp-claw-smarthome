# 아키텍처 문서에 HA 설정 + Portability §6 추가 + 경로 절대화

> **컨텍스트:** 사용자가 다른 집/네트워크에서 esp-claw 를 동작시킬 때 무엇을 바꿔야 하는지 질문 7건 (HA URL 고정 여부 / 다른 HA 연결 / 설정 파일 분리 / 동적 발견 / 플래싱 반복 필요 / 자동화 웹사이트 / 수정 항목 list). 기존 `01-esp-claw-current-architecture.md` 에 새 §6 으로 결합 + 모든 file path 를 절대경로화.

## 무엇을 만들었나

| 파일 | 변경 |
|---|---|
| `smarthome-docs/architecture/01-esp-claw-current-architecture.md` | rev "HA 설정 + Portability" — 새 §6 추가 (Q&A 7건 verbatim 인용 + 답변), 모든 path 절대경로 일괄 변환, NVS partition 구조 (24KB at 0x9000) 명시, `app_config` (25 키) + `ha_ctl` (3 키) NVS namespace 분리 명시 |
| `docs/learn/20260513-arch-doc-ha-config-update.md` | 이 문서 (사이클 학습) |

## 무엇을 배웠나

### 1. 사용자 질문 7건 verbatim 인용이 문서를 *대화형 reference* 로 만든다

요구사항은 "내 질문들은 그대로 남기도록" 이었음. 답변만 정리한 형식보다 질문 원문을 인용 + 답변 구조가 *retrospective 시점에 사용자가 다시 봤을 때 자기 사고 흐름을 reconstruct 가능*. 즉 향후 비슷한 portability 질문이 나오면 §6 의 Q1-Q7 을 reference 로 답 path 빠르게 찾기 가능.

**원칙:** 사용자 질문이 retrospective 가치가 있는 경우 (구조적 질문, 의도가 명확한 흐름) 원문 인용. 단순 한 줄 질문은 답만 정리.

### 2. 절대경로 일괄 변환이 외부 reader (특히 신규 인계자) 의 file 검색 cost 를 0 으로

`/Users/wm-mac-01/Desktop/esp-claw/esp-claw/` prefix 가 길지만, 문서 읽는 사람이 *프로젝트 디렉터리 구조 모르고도* IDE 에 path 그대로 paste → 파일 즉시 open 가능. 상대경로는 *문서가 어디서 읽히는가* 에 따라 결과 달라짐. 인계 문서는 절대경로 권장.

**원칙:** 인계 / 외부 reference 가치가 있는 architecture 문서는 절대경로. 같은 디렉터리 내 navigation 만 의도면 상대경로 OK.

### 3. NVS 설정 분리가 portability 의 핵심 — 두 namespace 의 명시적 분리가 retrospective 시점에 큰 가치

esp-claw 의 NVS namespace 분리 (`app_config` 25 키 vs `ha_ctl` 3 키) 가 *처음 봤을 땐 implementation detail* 처럼 보이지만, portability 시나리오 (다른 사용자 / 다른 HA / 다른 네트워크) 에서 핵심. *한 namespace 만 reset 가능* 이라는 게 실용 가치 — 예: HA 만 새 인스턴스로 변경하고 WiFi 는 유지 → `ha_ctl` 만 재입력. 문서가 이 분리를 명시 안 하면 사용자는 "전체 리셋" 만 알게 됨.

**원칙:** 시스템의 separation of concerns 가 *runtime portability* 와 직결되는 경우 architecture 문서에서 강조. implementation detail 이 아니라 user-facing decision point.

### 4. v6 portability 후보가 자연스럽게 도출됨 — Q&A 답변 자체가 다음 plan 의 input

§6 의 답변들이 다음 후보 3건 도출:
- mDNS 기반 HA 자동 발견 (Q4 의 Path A)
- esp-web-tools 호스팅 web flasher (Q6 의 Layer A)
- HA UI 의 esp-claw integration (Q5 의 방법 3)

**원칙:** 사용자 Q&A 가 자연스럽게 다음 plan 의 input 이 되도록 답변에서 *v6 후보로 명시*. retrospective 시점에 "이게 어디서 나온 후보지?" 추적 가능.

## 다음 사이클 (v6) 영향

- v6 plan 의 portability section 에 §6 의 Q4 (mDNS HA discovery) + Q6 (web flasher 호스팅) 포함 권장.
- 사용자 보드를 다른 환경 (예: 친구 집 demo, 사무실 demo) 으로 들고 갔을 때의 setup 흐름 검증이 V6 의 새로운 E2E 시나리오.

## 참고

- 자매 문서: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/smarthome-docs/architecture/02-openclaw-vs-esp-claw-comparison.md`
- NVS 관리 코드: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/components/app_config/app_config.c` (25 키) + `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` (3 키)
- partition layout: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/application/edge_agent/partitions_16MB.csv`
