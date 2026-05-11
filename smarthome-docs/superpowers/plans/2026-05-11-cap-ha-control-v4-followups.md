# cap_ha_control v4 follow-ups — Plan

> **Source:** `/review` of PR #1 (cap_ha_control v3 typed tool) on 2026-05-11. 5 findings recorded as v4 follow-up tasks per user decision to not block v3 land.
>
> **Status:** ready to execute. Each task has files, line refs, acceptance criteria, and verification.
>
> **Branch suggestion:** `feat/cap-ha-control-v4` off the v3 land commit.

**Goal:** close the 5 known v3 issues without expanding scope (no new HA domains, no wizard rework). Each task is independent — can ship as separate PRs if preferred.

**Build hygiene:** worktree at `.claude/worktrees/develop` still needs `gen_bmgr_codes/` + `sdkconfig` seeded from main on first build (see `memory/project_worktree_build_env.md`).

---

## Task 1 (P1) — Thread-safe registry refresh

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_internal.h` (if mutex helper exposed)

**Problem:** `cap_ha_resolve_refresh_from_ha()` at line 312–314 frees `s_cache_registry.items` and re-parses while concurrent callers iterate the same global via `lookup_in()` / `cap_ha_resolve_top_candidates()`. Window is ~50ms during boot_fetch_task's refresh, or whenever console `--refresh-registry` runs.

**Approach (preferred — FreeRTOS mutex):**

1. Add `static SemaphoreHandle_t s_registry_mutex = NULL;` at top of `cap_ha_control_resolve.c`.
2. In `cap_ha_resolve_init` (before `parse_registry` on static), create:
   ```c
   s_registry_mutex = xSemaphoreCreateMutex();
   if (!s_registry_mutex) return ESP_ERR_NO_MEM;
   ```
3. Wrap every `s_cache_registry` read in `cap_ha_resolve_target`, `cap_ha_resolve_top_candidates`, `cap_ha_resolve_active_friendly_names` with `xSemaphoreTake(s_registry_mutex, portMAX_DELAY)` / `xSemaphoreGive` around the iterate block. Copy out the entity to a local `cap_ha_entity_t` before releasing — the caller mustn't hold the lock while running HTTP.
4. In `cap_ha_resolve_refresh_from_ha` line 312–314 (the free + reassign + parse_registry block), take the same mutex around `free(s_cache_registry.items); s_cache_registry = ...; parse_registry(...)`.
5. `s_static_registry` is write-once at init — no mutex needed for reads after init. Document this invariant in a comment.

**Alternative (atomic pointer swap, lock-free):** maintain `s_cache_registry` as a pointer (`cap_ha_registry_t *s_cache_registry`); refresh allocates a new struct, atomic-swaps the pointer, then frees the old one after a grace period (RCU-style). More complex; mutex is fine for ESP-IDF's typical concurrency profile.

**Acceptance:**
- Stress test: console loop `while true; do ha_control --refresh-registry & ha_control --call '{"target":"화장실 조명","action":"toggle"}'; done` runs ≥ 60s without crash/heap-corruption.
- `idf.py build` clean; `idf.py size` shows minimal flash/RAM growth.

**Verification:** unit-ish on-board test — script that emits 10 parallel `event_router --emit-message` calls during a refresh, then checks `cap_ha_resolve_target` still returns valid entities.

---

## Task 2 (P2) — `#rrggbb` invalid-hex validation

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_core.c:44-51`

**Problem:** `strtol(..., 16)` returns 0 for non-hex chars, so `"#FFGG00"` silently parses as `(0xFF, 0x00, 0x00)` — red — instead of being rejected. The LLM can synthesize bad hex codes, and the user gets the wrong color with no indication.

**Approach:**
```c
if (color[0] == '#' && strlen(color) == 7) {
    for (size_t i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)color[i])) return ESP_ERR_INVALID_ARG;
    }
    /* ...existing strtol calls... */
}
```
Add `#include <ctype.h>` if not already pulled in.

**Acceptance:**
- `ha_control --call '{"target":"board:onboard_rgb","action":"turn_on","color":"#FFGG00"}'` returns `{"success":false,"message":"지원하지 않는 색상입니다 (color=#FFGG00).",...}`.
- Valid hex like `#A1B2C3` still works.

**Verification:** console call with bad hex returns `success:false` with the existing "지원하지 않는 색상입니다" message (already the rejection path in `cap_ha_control_board.c`).

---

## Task 3 (P2) — Entity count cap in `parse_registry`

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c:50` area

**Problem:** `cap_ha_entity_t *items = calloc((size_t)count, sizeof(*items));` where `count` comes from untrusted JSON. Each entity is ~152 bytes. A malformed or malicious HA could push 10K entries → 1.5 MB allocation. ESP32-S3 with PSRAM survives but fragments heap; smaller variants would OOM.

**Approach:**
```c
#define CAP_HA_MAX_REGISTRY_ENTRIES 64

/* in parse_registry, after computing count: */
if (count > CAP_HA_MAX_REGISTRY_ENTRIES) {
    ESP_LOGW(TAG, "registry entry count %d exceeds cap %d; truncating",
             count, CAP_HA_MAX_REGISTRY_ENTRIES);
    count = CAP_HA_MAX_REGISTRY_ENTRIES;
}
```
The cap applies to BOTH static and cache parses (same function). 64 is generous for a typical home — most demos have ≤ 10 entities.

**Acceptance:**
- Synthesized 100-entry `entities.default.json` → boot log `registry entry count 100 exceeds cap 64; truncating` + `loaded 64 static entities`.
- Real HA `/api/states` continues to work (typical responses are ≤ 50 light/cover/switch entries).

---

## Task 4 (P3) — Description refresh after boot-fetch

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control.c`
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_resolve.c` (boot_fetch_task)

**Problem:** `compose_description()` runs once in `cap_ha_group_init` BEFORE boot-fetch enriches `s_cache_registry`. The dynamic "Active devices: ..." list shows only the 4 static entities — even after boot-fetch adds HA-discovered entities to the cache. v3 docs this: "Boot-fetch updates take effect on the next reboot." v4 closes the gap.

**Approach (two-stage):**

1. Export `compose_description()` from `cap_ha_control.c` (rename to `cap_ha_compose_description` and declare in `cap_ha_control_internal.h`).
2. After `cap_ha_resolve_refresh_from_ha()` succeeds in `boot_fetch_task`, call `cap_ha_compose_description()` to rebuild `s_ha_description` from the now-enriched registry.
3. **Critical question:** does `s_ha_descriptors[0].description` get re-read by claw_core after boot, or is the descriptor snapshot cached in the LLM tools context? Verify by reading `components/claw_modules/claw_cap/src/claw_cap.c` for how `claw_cap_add_capped_description` is invoked — if it's called per-LLM-request, the new description propagates; if it's cached, we need to invalidate.
4. If cached, add a `claw_cap_invalidate_tool_description(group_id, cap_id)` API on `claw_cap` so cap_ha_control can poke it. Out of scope for this v4 task if the API doesn't exist — punt to v5 with a TODO.

**Acceptance:**
- Boot board with empty NVS cache → first boot description shows 4 static entities, post-boot-fetch description shows ≥ 5 (static + HA-discovered).
- Subsequent LLM message uses the enriched description (verify via `cap_llm_inspect` if available, otherwise via Telegram message that names an HA-only entity).

**Risk:** if the descriptor is cached at register-time in claw_core, this task needs claw_cap API work first. Investigate before committing to the approach.

---

## Task 5 (P3) — HTTPS support (Bearer token cleartext)

**Files:**
- Modify: `components/claw_capabilities/cap_ha_control/src/cap_ha_control_http.c` (URL scheme handling)
- Modify: `components/claw_capabilities/cap_ha_control/CMakeLists.txt` (esp-tls REQUIRES if not transitively pulled)
- Update: spec / docs

**Problem:** `http://192.168.1.94:8123/api/services/...` + `Authorization: Bearer <token>` — token sent in plaintext on the LAN. On any shared/guest Wi-Fi, sniffable.

**Approach:**

1. Accept `https://` URLs in `cap_ha_http_set_url`. esp_http_client + `crt_bundle_attach` already in cfg — TLS infrastructure is wired but unused for `http://` URLs.
2. For HA setups using self-signed certs (common for local HA on Pi), document the option to:
   - Either disable cert verification (`.skip_cert_common_name_check = true` and remove `crt_bundle_attach`) with a console flag like `ha_control --insecure`,
   - Or import the user's HA cert into NVS as a custom CA bundle (`esp_http_client_config_t.cert_pem`).
3. v4 ships option A (insecure flag for self-signed) with a WARN log on every request: `WARNING: HA URL uses HTTPS without certificate verification`.
4. v5 adds option B (custom CA import via `ha_control --set-ca <pem>`).

**Acceptance:**
- `ha_control --set-url https://192.168.1.94:8123` + `ha_control --insecure on` → POST works against self-signed HA.
- `ha_control --set-url http://...` continues to work (no regression).
- Cleartext WARN logged.

**Pairs with:** v4 NVS secure storage (HA token currently stored in plaintext NVS — separate spec item, listed in v3 learn log).

---

## Out of scope (not v4)

These v3 learn-log items stay deferred beyond v4 follow-up:

- climate / fan / media_player / scene domain support
- HA secure NVS storage (encrypted NVS partition)
- Multi-entity composite commands with partial-failure handling
- Setup wizard ha_url / ha_token field integration with NVS namespace unification (`app_config` ↔ `ha_ctl`)
- `cap_ha_http_get_url_alloc(char **out)` / `_token_alloc` helpers for unbounded caller buffers
- `roll_chat_session` persistent history clear (firmware-wide, not cap_ha_control-specific)

---

## Execution suggestion

| Order | Task | Estimated effort |
|---|---|---|
| 1 | Task 2 (#rrggbb hex validate) | ~15 min CC, low risk |
| 2 | Task 3 (entity cap) | ~10 min CC, low risk |
| 3 | Task 1 (mutex) | ~45 min CC, requires test |
| 4 | Task 5 (HTTPS) | ~1 h CC, needs HA setup with TLS |
| 5 | Task 4 (description refresh) | ~1–2 h CC, depends on claw_cap API |

Tasks 1–3 can ship as a single small PR. Tasks 4–5 may warrant their own PRs given dependency analysis.
