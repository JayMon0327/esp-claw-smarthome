# cap_ha_automation — c04c845 follow-up 정제 (NVS eid 캐시 + explicit fail)

> **컨텍스트:** v4 ship 직후 (`feat/cap-ha-control-v4` HEAD = 95a35cb) c04c845 가 도입한 resolve_entity_id_by_config_id 의 3가지 정제: (a) NVS 캐싱, (b) do_service silent fallback 제거, (c) cache write-through/invalidate 로 헬퍼 책임 분리.

## 무엇을 만들었나

| Task | Commit (head) | 핵심 변경 |
|---|---|---|
| 1 | feat: NVS cache | `ha_ctl/eid_cache` (JSON blob, 32 cap, FIFO drop) + `eid_cache_{lookup,put,invalidate}` static helpers. `resolve_entity_id_by_config_id` 가 cache → HA states GET → write-through 순서. |
| 2 | feat: do_create write-through | POST + reload 성공 후 매핑 cache. eventual consistency miss 시 fallback 사용하되 cache 오염 방지 위해 NVS 에는 안 적음. |
| 3 | feat: do_remove invalidate | DELETE 성공 후 cache 키 drop. |
| 4 | fix: do_service explicit-fail | esp_claw_<ts> 패턴은 resolver miss 시 verbatim fallback 금지 — c04c845 silent no-op 버그의 정확한 원인. 사용자 slug 는 verbatim 폴백 유지. |

## 무엇을 배웠나

### 1. c04c845 의 fallback 한 줄이 가장 위험한 코드였다

c04c845 가 `entity_id silent no-op` 버그를 잡으려고 resolver 를 도입했는데, 같은 함수 안에 `if (... != ESP_OK) snprintf("automation.%s", id);` 한 줄이 들어가면서 **resolver miss = 원래 버그 그대로 재발** 이 됐다. fallback 의 의도는 "사용자가 이미 slug 화된 entity_id 를 넘긴 케이스" 였지만 esp_claw_<ts> 패턴까지 똑같이 처리해 결국 의도와 정반대 효과. 패턴 분기 (`strncmp(id, "esp_claw_", 9) == 0`) 한 줄로 두 케이스 분리.

**원칙:** "안전한 fallback" 처럼 보이는 코드도 입력 분류 없이 일률 적용하면 원래 fix 의 의미를 무효화할 수 있다. 입력 origin 별로 fallback policy 다르게.

### 2. NVS cache 는 단일 JSON blob 이 multi-key 보다 쉽다

ESP-IDF NVS key 는 15 byte 제한이라 `esp_claw_<10digit>` 같은 자연스러운 키는 불가. 처음엔 hash 화나 prefix 단축을 고려했지만 단일 blob (`eid_cache` key 하나, JSON object value) 가 lookup/put/invalidate 모두 동기 read-parse-mutate-write 라 더 단순. 32 항목 cap 으로 blob 크기는 항상 수 KB 이하 — read 비용 무시 가능.

**원칙:** ESP NVS key 제약을 우회하기 위해 blob 화는 흔한 패턴. lookup 빈도가 매우 높으면 RAM 미러를 두지만 demo 수준에선 disk hit 충분.

### 3. write-through 와 cache invalidate 를 mutate 경로마다 책임 분리

`resolve_entity_id_by_config_id` slow-path 가 자동으로 write-through 하므로 `do_create` 의 명시적 `eid_cache_put` 은 사실상 redundant 지만 의도 명시 + cache 적중 케이스 일관성 위해 유지. `do_remove` 는 mutation 경로라 invalidate 필수. mutation 경로마다 cache 책임이 명확하면 stale read 추적이 쉽다.

## 관련

- 직접 영향 받은 commit: `c04c845 fix(cap_ha_automation): HA modern schema + entity_id resolution`
- 관련 plan: `smarthome-docs/superpowers/plans/2026-05-12-cap-ha-automation-c04c845-followups.md`
- 다음 후속: state-trigger 지원 (별도 plan / 다른 worktree 에서 병렬 진행)
