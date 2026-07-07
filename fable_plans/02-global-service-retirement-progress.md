# Progress: Global Service Retirement (plan 02)

Source plan: `fable_plans/02-global-service-retirement-plan.md`
Status: phase 4 L2 WorkerPool, Window, EngineConfig, and UI profiler snapshot slices complete on 2026-07-07; remaining profiler diagnostics/Gfx singleton demotion/freezing pending.
Last updated: 2026-07-07 (takeover UI profiler snapshot slice)

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- This plan COORDINATES with `authoritative-plan-03-explicit-service-contexts`
  (or its Done successor + the overnight blockers file). Before starting any
  phase, read that CSV's current row statuses — do not redo done rows, do not
  unblock their blocked rows without reading the blocker reason.
- Comment quality gate applies to touched source files.

## Current facts (post-UI profiler snapshot, 2026-07-07)

- `Cfg()` no longer exists (0 call sites), and `EngineConfig::Instance()` has
  now been deleted. Runtime startup owns an `EngineConfig` value, loads and
  patches it, then threads references or snapshots from the composition root.
- `EngineConfig::Instance` exact call sites: 0 in production source and tests.
  The only remaining text is the runtime-boundary checker's synthetic regression
  self-test that rejects reintroduced singleton config access.
- Singleton-style source files from `::Instance()`/`GetInstance(` census:
  5 files total (`Core` 3, `Runtime` 2, `UI` 0, `Rendering` 0, `Physics` 0):
  `Core\Profiler.h`, `Core\Profiler.cpp`, `Core\LockOrderValidator.cpp`,
  `Runtime\RuntimeDiagnostics.cpp`, and `Runtime\RunUiTextPass.cpp`.
- `UITabProfiler.cpp` now has 0 `Gfx()`, 0 `IsGfxReady()`, and 0
  `Profiler::Instance()` hits. Its draw trace, marker tree, and worker-core
  chart consume the bounded `ProfilerTab::FrameSnapshot` filled by
  `RunUiTextPass`.
- Singleton accessors known from the current census: Profiler::Instance and
  LockOrderValidator::Instance (SVC-034 frozen diagnostics exception), plus
  `s_gfxBackend`/`Gfx()`/`SetGfxBackend` in `Rendering/IRenderBackend.cpp`.
- The checker `tools/check_runtime_boundaries.py` now carries
  `MAX_GLOBAL_SERVICE_ACCESS_CENSUS = 133`, no EngineConfig allowlist rows, and
  no `UITabProfiler.cpp` global-service allowlist rows.

## Verified facts (as of 2026-07-07 takeover census — re-verify before later phases)

- `Cfg()` no longer exists (0 call sites). Config access is now
  `EngineConfig::Instance()` — declared `Core/Config.h:256`, with
  `Config.h:40-42` now documenting startup ownership of the legacy singleton
  accessor and directing normal code to use a threaded reference or snapshot.
  The convenience accessor died; the GLOBAL remains only at bootstrap/definition
  boundaries.
- `EngineConfig::Instance` exact call sites: 2 total (`Core` 1, `Runtime` 1):
  the singleton definition in `Core\Config.cpp` and startup bootstrap in
  `Runtime\Init.cpp`.
- Exact `Gfx()` call sites: 25 total by the Phase 1 command (`Core` 10,
  `Rendering` 13, `UI` 2, `Runtime` 0, `Physics` 0). The broader checker
  budget also covers related renderer service globals such as
  `GfxRayTracing()` and readiness probes.
- Singleton-style source files from `::Instance()`/`GetInstance(` census:
  8 files total (`Core` 4, `Runtime` 3, `UI` 1, `Rendering` 0, `Physics` 0).
- Singleton accessors known from the current census: EngineConfig::Instance,
  Profiler::Instance,
  LockOrderValidator::Instance (SVC-034 frozen diagnostics exception), plus
  `s_gfxBackend`/`Gfx()`/`SetGfxBackend` in `Rendering/IRenderBackend.cpp`.
- The checker `tools/check_runtime_boundaries.py` already carries the
  global-service ratchet installed by overnight work: `GLOBAL_SERVICE_ACCESS_ALLOWLIST`
  entries including `EngineConfig::Instance()`, counted global-service
  patterns, generic `Class::Instance()` handling, reviewed renderer-service
  file classifications, frozen diagnostics singleton classification for
  `LockOrderValidator`, and `MAX_GLOBAL_SERVICE_ACCESS_CENSUS = 143`.
- Sanctioned-global candidates per source plan: `Log()` (Common.h:116-119,
  documented as convenience accessor over EngineLog::Get()) and Profiler —
  frozen, not injected. Everything else is injection work.

## Phase 1 — census + ratchet (idempotent; do even if overnight ratchets exist)

- [x] N1. Fresh census, recorded HERE with date:
  `rg -c "\bGfx\(\)" SkullbonezSource | sort` (per-file),
  `rg -c "EngineConfig::Instance" SkullbonezSource | sort`,
  `rg -n "::Instance\(\)|GetInstance\(" SkullbonezSource --type-add 'src:*.{cpp,h,inl}' -tsrc -l`.
  Paste totals + per-top-directory counts (Runtime, Rendering, UI, Physics,
  Core) in a dated block below this list.
- [x] N2. Read the current checker state:
  `python tools/check_runtime_boundaries.py` (record which global-service
  rules already exist from the overnight run) and
  `git log --oneline -- tools/check_runtime_boundaries.py | head -10`.
  If a Gfx/Instance ratchet already exists, record its budget numbers and
  SKIP N3.
- [x] N3. (Only if missing) Add per-directory budget rules for `Gfx()`,
  `EngineConfig::Instance`, `::Instance()` with stored budgets = N1 numbers,
  self-tests included, imitating the `e101a40f` idiom. Gate: `validate_fast`
  + run the checker. Commit.

Phase 1 evidence (2026-07-07 takeover census):

- `rg -c "\bGfx\(\)" SkullbonezSource | sort`
  - `SkullbonezSource\Core\Profiler.cpp:10`
  - `SkullbonezSource\Rendering\IRenderBackend.cpp:8`
  - `SkullbonezSource\Rendering\IRenderBackend.h:5`
  - `SkullbonezSource\UI\UIBackdropBlur.cpp:1`
  - `SkullbonezSource\UI\UITabProfiler.cpp:1`
  - Total exact `Gfx()` hits: 25 (`Core` 10, `Rendering` 13, `UI` 2,
    `Runtime` 0, `Physics` 0).
- `rg -c "EngineConfig::Instance" SkullbonezSource | sort`
  - `SkullbonezSource\Core\Config.cpp:1`
  - `SkullbonezSource\Runtime\Init.cpp:1`
  - Total `EngineConfig::Instance` hits: 2 (`Core` 1, `Runtime` 1,
    `Rendering` 0, `UI` 0, `Physics` 0).
- `rg -n "::Instance\(\)|GetInstance\(" SkullbonezSource --type-add 'src:*.{cpp,h,inl}' -tsrc -l`
  - 8 source files: `Core\Profiler.h`, `Core\Profiler.cpp`,
    `Core\LockOrderValidator.cpp`, `Core\Config.cpp`, `Runtime\Init.cpp`,
    `Runtime\RuntimeDiagnostics.cpp`,
    `Runtime\RunUiTextPass.cpp`, `UI\UITabProfiler.cpp`.
  - Top-directory counts: `Core` 4, `Runtime` 3, `UI` 1, `Rendering` 0,
    `Physics` 0.
- `python tools\check_runtime_boundaries.py` passed:
  `Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)`.
- Existing checker ratchet state:
  - `GLOBAL_SERVICE_ACCESS_ALLOWLIST` stores the current per-file budgets,
    including `EngineConfig::Instance()` entries for `Core\Config.cpp` and
    `Runtime\Init.cpp`.
  - `GLOBAL_SERVICE_ACCESS_PATTERNS` covers `Cfg()`, `Gfx()`,
    `GfxRayTracing()`, `IsGfxReady()`, `IsGfxRayTracingReady()`,
    `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`,
    `TextureCollection::Instance()`, `CameraCollection::Instance()`,
    `Window::Instance()`, `SkyBox::Instance()`, `WorkerPool::Instance()`,
    and `Profiler::Instance()`.
  - `GENERIC_INSTANCE_ACCESS_PATTERN` count-guards other
    `Class::Instance()` calls unless the class is a named global-service
    singleton or a frozen diagnostics singleton.
  - `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` reviews each remaining
    renderer-global file.
  - `FROZEN_DIAGNOSTIC_SINGLETON_INSTANCE_CLASSES` contains
    `LockOrderValidator`.
  - `MAX_GLOBAL_SERVICE_ACCESS_CENSUS = 143`.
  - Recent checker commits:
    `96580241 guard: close replay prediction isolation`,
    `e04d7fec fix: classify lock-order validator singleton`,
    `fc15d9de refactor: split replay velocity edit unit`,
    `e101a40f guard: ratchet Run private members`,
    `c2aeb4fd tools: ratchet physics collection authority`.
- N3 skipped by condition: the global-service ratchet and reviewed renderer
  classifications already exist. No checker/source change was needed for this
  phase-1 documentation slice.

## Phase 2 — config snapshots (the EngineConfig::Instance burn-down)

- [x] C1. DISCOVERY: how config reaches Run today — `rg -n "m_config|config"
  SkullbonezSource/Runtime/Run.h | head -20`; record whether Run holds
  `EngineConfig*`/reference and how frame code receives it (RunReplayTools
  passes `*m_systems.config` — verify the member name). The injection pattern
  for every conversion below is "pass the config reference the caller already
  has", NOT "add a new Instance() call higher up".
- [x] C2. Convert per directory, smallest first, one commit each. For each
  call site: replace `EngineConfig::Instance().field` with a `const
  EngineConfig& config` parameter threaded from the nearest owner that
  already has one. Order: (a) UI/ tabs, (b) Runtime/Audio, (c)
  Runtime/Editor tools, (d) Physics/ (should already be zero — physics gate
  if not), (e) Rendering/, (f) Runtime/ frame+init last (init KEEPS
  startup-time Instance() — that is the sanctioned construction point until
  plan-04/composition-root work assigns ownership).
  Gate per commit: the touched area's validation-map row; ratchet budget
  decremented in the same commit.
- [x] C3. End state check: `rg -n "EngineConfig::Instance" SkullbonezSource`
  hits only Init/startup + Config.cpp itself. Update the checker rule from
  budget to directory allowlist. Gate: `validate_full`. Commit.

Phase 2 evidence (2026-07-07 takeover config cleanup):

- Discovery: `rg -n "m_config|config" SkullbonezSource\Runtime\Run.h`
  shows `Run` owns a borrowed `EngineConfig& m_config` at line 103 and its
  constructor receives `EngineConfig& config` at line 365. Startup loads and
  CLI-patches the config in `Runtime\Init.cpp` before constructing `Run`.
- No directory conversion was needed in this slice: `rg -n
  "EngineConfig::Instance" SkullbonezSource` found no UI, Runtime frame/audio,
  editor, Physics, or Rendering normal-path callers.
- The only remaining exact hits are the singleton definition
  `SkullbonezSource\Core\Config.cpp:533` and startup bootstrap
  `SkullbonezSource\Runtime\Init.cpp:3295`.
- `SkullbonezSource\Core\Config.h` had a stale comment teaching access through
  the static accessor from anywhere. That comment was changed to direct normal
  code to use the `EngineConfig` reference or snapshot threaded from the
  composition root. The diff is comment-only.
- The current checker already uses `GLOBAL_SERVICE_ACCESS_ALLOWLIST` per-file
  entries for the remaining `EngineConfig::Instance()` definition/startup
  bootstrap and a total global-service budget of
  `MAX_GLOBAL_SERVICE_ACCESS_CENSUS = 152`; no checker edit was needed.
- Evidence command passed after the comment cleanup:
  `python tools\check_runtime_boundaries.py` (0 errors).
- Repository validation was not run: this slice is comment/documentation-only.
- Superseded by phase 4 L2 EngineConfig demotion: the startup/bootstrap
  allowlist and singleton definition were removed, leaving 0
  `EngineConfig::Instance` production/test hits.

## Phase 3 — Gfx() burn-down (25 exact sites; coordinate with plan-05 RGRAPH rows)

- [x] G1. Classify the 25 exact hits: `rg -n "\bGfx\(\)"
  SkullbonezSource` → tag each line here as: (a) startup/teardown
  (Init.cpp SetGfxBackend region — keep,
  allowlist), (b) covered by an authoritative SVC/RGRAPH row (cite row id —
  work it THERE, not here, or verify already done), (c) orphan (no row —
  convert here). Expected: most are (b).

G1 classification evidence (2026-07-07 takeover Gfx census):

The raw takeover `rg` command reported 25 exact text hits. After the
2026-07-07 UI profiler snapshot slice, current exact text hits are 24 because
the `UITabProfiler.cpp` draw-trace row was retired. This includes comments,
declarations, string literals, and real calls; the runtime-boundary checker uses
stripped source and the `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` file
fence for behavior-bearing uses.

| File / lines | Classification | Owner |
|--------------|----------------|-------|
| `Core\Profiler.cpp:460,484,496,502,510,516,525,535,589,613` | (b) diagnostics/profiler bridge | Cluster D / SVC-022 receiving path and Core profiler GPU-timer cleanup |
| `UI\UITabProfiler.cpp:165` | Retired 2026-07-07 | SVC-032/SVC-033 resolved by `ProfilerTab::FrameSnapshot`; no current `UITabProfiler.cpp` `Gfx()` hit |
| `Rendering\IRenderBackend.h:77,89,96` | (b) backend accessor declaration and tracing RAII compatibility | Cluster E / SVC-001, SVC-002 endgame; plan-05 render capability cleanup supplies the explicit trace/render context first |
| `Rendering\IRenderBackend.cpp:39,41,44,54` | (a)/(b) backend accessor definition, guard strings, and raytracing facade | Cluster E / SVC-001, SVC-002 endgame; keep until all callers leave the facade |
| `UI\UIBackdropBlur.cpp:20`, `Rendering\IRenderBackend.h:8,62`, `Rendering\IRenderBackend.cpp:7,17,20,53` | Comment-only or migration-contract prose | No conversion; keep wording accurate as rows drain |

G1 result: 0 orphan exact `Gfx()` hits. G2 should not start by inventing local
conversions here; it should consume the Cluster D diagnostics path, plan-05
render capability rows, and SVC-001/SVC-002 endgame in that order.
- [x] G2. Convert the orphans using the capability interfaces plan-05
  established (`IRenderCommandContext`, `IRenderResourceFactory`,
  `IRenderDiagnostics` — passed through RenderFrameContext or explicit
  parameters; imitate a completed RHOST/SVC row's commit for the idiom:
  `git log --oneline --grep="SVC-" | head` to find one). Gate per commit:
  `validate_dx12_renderer`. Ratchet decrement per commit.

G2 evidence: G1 found 0 orphan exact `Gfx()` hits. The remaining real calls are
owned by Cluster D diagnostics, plan-05 render capability cleanup, and the
SVC-001/SVC-002 accessor endgame, so there was no local orphan conversion and
no renderer validation gate for this documentation-only classification slice.
- 2026-07-07 UI profiler snapshot slice: resolved SVC-032/SVC-033 by copying
  draw-call trace nodes, profiler markers, and worker-core samples into
  `InGameUIFrameData::profiler` inside `RunUiTextPass`, then making
  `UITabProfiler` consume `ProfilerTab::FrameSnapshot` from UI state. The
  checker global-service census dropped from 141 to 133. Gates passed so far:
  `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (20.306s, 0 errors),
  `tools\validate_fast.bat` (48.437s, 0 warnings/errors), and
  `tools\validate_full.bat` (45.392s, DX12 validation errors 0, screenshots
  matched, `physics_regression_solver.csv` byte-exact).
- [ ] G3. Delete `Gfx()` + `s_gfxBackend` public accessor once N1-count
  reaches the startup allowlist only (SVC-001/002/004 endgame — check their
  status first). Gate: `validate_full` + `validate_dx12_renderer`. Commit.

## Phase 4 — singleton lifetime hardening

- [x] L1. For each remaining `::Instance()` class after phases 2-3
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

- 2026-07-07 L1 discovery: the pre-L2 `rg -n "::Instance\(\)|GetInstance\("`
  census named `EngineConfig`, `WorkerPool`, `Window`, `Profiler`, and
  `LockOrderValidator`. After the WorkerPool, Window, and EngineConfig L2
  slices, the current source census no longer names `EngineConfig`,
  `WorkerPool`, or `Window`; `TextureCollection`, `CameraCollection`, and
  `SkyBox` also remain absent.

  | Class | Construction site / storage | Teardown and ordering risk |
  |-------|-----------------------------|----------------------------|
  | `EngineConfig` | Superseded by L2: `Runtime\Init.cpp:3295` now constructs `EngineConfig cfg` with automatic startup storage, then `Run` receives `EngineConfig&`. | Demoted from singleton. No process-static config or accessor remains; focused tests now build local deterministic config values instead of mutating global state. |
  | `WorkerPool` | Superseded by L2: `Runtime\Init.cpp:3330` now constructs `WorkerPool workerPool` with automatic startup storage, then `Run` receives `WorkerPool&`. | Demoted from singleton. Explicit `workerPool.Shutdown()` still runs at `Runtime\Init.cpp:3355`, and the destructor calls `Shutdown()` again after startup scope exit; no process-static worker-pool lifetime remains. |
  | `Window` | Superseded by L2: `Runtime\Init.cpp:3339-3340` now constructs `Window windowOwner` with automatic startup storage and passes `&windowOwner` to existing pointer-based startup helpers. | Demoted from singleton. `CleanupWindow()` still disarms input/backend state, releases the device context, restores fullscreen state, and unregisters the class; no `pInstance` cache or process-static Window remains. |
  | `Profiler` | `Core\Profiler.cpp:79-82`, function-local static reached by macros, runtime diagnostics CSV/sampling, and runtime text pass snapshot creation. | Medium risk: no explicit destructor work, but GPU timer methods still read renderer globals and diagnostics CSV still queries the singleton. `UITabProfiler` no longer reads Profiler/Gfx directly; final freeze/demotion depends on SVC-022 diagnostics receiving path and the Core GPU timer global reads. |
  | `LockOrderValidator` | `Core\LockOrderValidator.cpp:114-120`, function-local static with header/source frozen-diagnostics comments from SVC-034. | Accepted frozen diagnostics exception: no config reads, no renderer/worker ownership, and no singleton teardown dependency. |

- 2026-07-07 SVC-034: `LockOrderValidator::Instance` was classified under the
  L2 frozen diagnostics contract. The source now documents function-local
  storage, no config reads, and no singleton teardown ordering dependency; the
  global-service checker excludes this diagnostic singleton and carries
  self-tests for both per-file guardrails and the total census.
- 2026-07-07 L2 WorkerPool: deleted `WorkerPool::Instance()` from
  `Core\WorkerPool.h/.cpp`, changed runtime startup to construct a local
  `WorkerPool` owner before `Initialise()`, and lowered the checker
  global-service census from 152 to 150 by removing the old WorkerPool
  allowlist entries. Added a WorkerPool-specific synthetic self-test so the
  singleton accessor cannot return under a new call site. Gates passed:
  `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (0 errors),
  `tools\validate_fast.bat` (65.975s, 0 warnings/errors), and
  `tools\validate_full.bat` (42.469s, DX12 validation errors 0, screenshots
  matched, `physics_regression_solver.csv` byte-exact).
- 2026-07-07 L2 Window: deleted `Window::Instance()`, `Window::Destroy()`, and
  `Window::pInstance`, changed runtime startup to construct a local
  `Window windowOwner`, and lowered the checker global-service census from 150
  to 143 by removing the old Window/pInstance allowlist entries. Existing
  Window and `pInstance` synthetic checker self-tests cover the deleted access
  paths. Gates passed: `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (0 errors),
  `tools\validate_fast.bat` (48.314s, 0 warnings/errors), and
  `tools\validate_full.bat` (42.849s, DX12 validation errors 0, screenshots
  matched, `physics_regression_solver.csv` byte-exact).
- 2026-07-07 L2 EngineConfig: deleted `EngineConfig::Instance()` from
  `Core\Config.h/.cpp`, changed runtime startup to own a local
  `EngineConfig cfg`, and changed the determinism unit fixture to create local
  deterministic config values. Lowered the checker global-service census from
  143 to 141 by removing the old config allowlist entries; the existing generic
  `EngineConfig::Instance()` synthetic self-test now guards the deleted shape.
  Gates passed: `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (0 errors),
  `tools\validate_tests.bat` (8.059s, 42 doctest cases, 527 assertions,
  0 warnings/errors), `tools\validate_fast.bat` (66.672s, 0 warnings/errors),
  and `tools\validate_full.bat` (43.185s, DX12 validation errors 0,
  screenshots matched, `physics_regression_solver.csv` byte-exact).
- 2026-07-07 L2 UI profiler snapshot: added fixed-capacity
  `ProfilerTab::FrameSnapshot` data for marker rows, draw-trace rows, and
  worker-core samples; `RunUiTextPass` fills it from the explicit
  `IRenderDiagnostics` borrow and the one remaining runtime-owned
  `Profiler::Instance()` access; `UITabProfiler` now renders and hit-tests from
  the cached snapshot. Removed the `UITabProfiler.cpp` `Gfx()`,
  `IsGfxReady()`, `Profiler::Instance()`, and `IRenderBackend` compatibility
  rows from the checker and lowered the global-service census from 141 to 133.
  Gates passed: `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (20.306s, 0 errors),
  `tools\validate_fast.bat` (48.437s, 0 warnings/errors), and
  `tools\validate_full.bat` (45.392s, DX12 validation errors 0, screenshots
  matched, `physics_regression_solver.csv` byte-exact).

## Closure

- [ ] Z1. Ratchet budgets → allowlist-only bans; checker self-tests updated.
- [ ] Z2. Update `fable_plans/02-global-service-retirement-plan.md` status +
  this file with final counts (started 579-era → 0 outside allowlists).
