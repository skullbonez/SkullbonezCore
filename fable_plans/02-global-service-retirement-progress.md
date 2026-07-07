# Progress: Global Service Retirement (plan 02)

Source plan: `fable_plans/02-global-service-retirement-plan.md`
Status: not started — NOTE: the overnight authoritative run is actively moving
this ground; re-census before every phase.
Last updated: 2026-07-07 (early AM, mid-overnight-run)

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- This plan COORDINATES with `authoritative-plan-03-explicit-service-contexts`
  (or its Done successor + the overnight blockers file). Before starting any
  phase, read that CSV's current row statuses — do not redo done rows, do not
  unblock their blocked rows without reading the blocker reason.
- Comment quality gate applies to touched source files.

## Verified facts (as of 2026-07-07 early AM — VOLATILE, re-verify)

- `Cfg()` no longer exists (0 call sites). Config access is now
  `EngineConfig::Instance()` — declared `Core/Config.h:256`, documented at
  Config.h:40-41 ("Access via EngineConfig::Instance().fieldName ... anywhere").
  The convenience accessor died; the GLOBAL did not. The target moved from
  "delete Cfg()" to "retire EngineConfig::Instance() from normal paths".
- `Gfx()` call sites: 44 (down from the 579-era inventory).
- Singleton accessors known: EngineConfig::Instance (Config.cpp),
  WorkerPool::Instance, Window::Instance, Profiler::Instance,
  LockOrderValidator::Instance (SVC-034 classifies it as allowed diagnostics),
  TextureCollection singleton, RenderBackendDX12::s_instance/Get (plan-05
  RGRAPH rows own that one), `s_gfxBackend`/`Gfx()`/`SetGfxBackend`
  (Rendering/IRenderBackend.cpp).
- The checker `tools/check_runtime_boundaries.py` already carries overnight
  ratchets (commit `e101a40f "guard: ratchet Run private members"` is the
  pattern to imitate — read that commit's diff for the current rule+self-test
  idiom: `git show e101a40f -- tools/check_runtime_boundaries.py`).
- Sanctioned-global candidates per source plan: `Log()` (Common.h:116-119,
  documented as convenience accessor over EngineLog::Get()) and Profiler —
  frozen, not injected. Everything else is injection work.

## Phase 1 — census + ratchet (idempotent; do even if overnight ratchets exist)

- [ ] N1. Fresh census, recorded HERE with date:
  `rg -c "\bGfx\(\)" SkullbonezSource | sort` (per-file),
  `rg -c "EngineConfig::Instance" SkullbonezSource | sort`,
  `rg -n "::Instance\(\)|GetInstance\(" SkullbonezSource --type-add 'src:*.{cpp,h,inl}' -tsrc -l`.
  Paste totals + per-top-directory counts (Runtime, Rendering, UI, Physics,
  Core) in a dated block below this list.
- [ ] N2. Read the current checker state:
  `python tools/check_runtime_boundaries.py` (record which global-service
  rules already exist from the overnight run) and
  `git log --oneline -- tools/check_runtime_boundaries.py | head -10`.
  If a Gfx/Instance ratchet already exists, record its budget numbers and
  SKIP N3.
- [ ] N3. (Only if missing) Add per-directory budget rules for `Gfx()`,
  `EngineConfig::Instance`, `::Instance()` with stored budgets = N1 numbers,
  self-tests included, imitating the `e101a40f` idiom. Gate: `validate_fast`
  + run the checker. Commit.

## Phase 2 — config snapshots (the EngineConfig::Instance burn-down)

- [ ] C1. DISCOVERY: how config reaches Run today — `rg -n "m_config|config"
  SkullbonezSource/Runtime/Run.h | head -20`; record whether Run holds
  `EngineConfig*`/reference and how frame code receives it (RunReplayTools
  passes `*m_systems.config` — verify the member name). The injection pattern
  for every conversion below is "pass the config reference the caller already
  has", NOT "add a new Instance() call higher up".
- [ ] C2. Convert per directory, smallest first, one commit each. For each
  call site: replace `EngineConfig::Instance().field` with a `const
  EngineConfig& config` parameter threaded from the nearest owner that
  already has one. Order: (a) UI/ tabs, (b) Runtime/Audio, (c)
  Runtime/Editor tools, (d) Physics/ (should already be zero — physics gate
  if not), (e) Rendering/, (f) Runtime/ frame+init last (init KEEPS
  startup-time Instance() — that is the sanctioned construction point until
  plan-04/composition-root work assigns ownership).
  Gate per commit: the touched area's validation-map row; ratchet budget
  decremented in the same commit.
- [ ] C3. End state check: `rg -n "EngineConfig::Instance" SkullbonezSource`
  hits only Init/startup + Config.cpp itself. Update the checker rule from
  budget to directory allowlist. Gate: `validate_full`. Commit.

## Phase 3 — Gfx() burn-down (44 sites; coordinate with plan-05 RGRAPH rows)

- [ ] G1. Classify the 44: `rg -n "\bGfx\(\)" SkullbonezSource` → tag each
  line here as: (a) startup/teardown (Init.cpp SetGfxBackend region — keep,
  allowlist), (b) covered by an authoritative SVC/RGRAPH row (cite row id —
  work it THERE, not here, or verify already done), (c) orphan (no row —
  convert here). Expected: most are (b).
- [ ] G2. Convert the orphans using the capability interfaces plan-05
  established (`IRenderCommandContext`, `IRenderResourceFactory`,
  `IRenderDiagnostics` — passed through RenderFrameContext or explicit
  parameters; imitate a completed RHOST/SVC row's commit for the idiom:
  `git log --oneline --grep="SVC-" | head` to find one). Gate per commit:
  `validate_dx12_renderer`. Ratchet decrement per commit.
- [ ] G3. Delete `Gfx()` + `s_gfxBackend` public accessor once N1-count
  reaches the startup allowlist only (SVC-001/002/004 endgame — check their
  status first). Gate: `validate_full` + `validate_dx12_renderer`. Commit.

## Phase 4 — singleton lifetime hardening

- [ ] L1. For each remaining `::Instance()` class after phases 2-3
  (WorkerPool, Window, Profiler, TextureCollection, EngineConfig,
  LockOrderValidator): record its construction site + destruction order risk
  (static local? namespace static? member of Run?). One line each here.
- [ ] L2. Demote or freeze each: (a) demote = composition root member +
  delete static accessor (WorkerPool and Window are SVC-007/008/011/012 —
  check status; TextureCollection likely demotable to AssetSystem); (b)
  freeze = documented diagnostics exception with trivially-safe lifetime
  (Profiler, LockOrderValidator, EngineLog): function-local static, no
  cross-singleton destructor dependency, header comment stating the frozen
  contract (no config reads, no ordering deps). One commit per class. Gates:
  `validate_full` for lifecycle changes; `platform-profiler-markers` launch
  for Profiler.
- [ ] L3. Acceptance: delete the "Singleton lifecycle: use-after-destroy,
  double-init crash" row from the AGENTS.md danger-zone table — with a commit
  note explaining why it is now structurally impossible. This is the
  source-plan's definition-of-done tripwire; do NOT delete the row if any
  ordering-dependent singleton remains.

Phase 4 execution notes:

- 2026-07-07 SVC-034: `LockOrderValidator::Instance` was classified under the
  L2 frozen diagnostics contract. The source now documents function-local
  storage, no config reads, and no singleton teardown ordering dependency; the
  global-service checker excludes this diagnostic singleton and carries
  self-tests for both per-file guardrails and the total census.

## Closure

- [ ] Z1. Ratchet budgets → allowlist-only bans; checker self-tests updated.
- [ ] Z2. Update `fable_plans/02-global-service-retirement-plan.md` status +
  this file with final counts (started 579-era → 0 outside allowlists).
