# Progress: Unified Error Handling Policy (plan 05)

Source plan: `fable_plans/05-unified-error-handling-policy-plan.md`
Status: phase 1 complete on 2026-07-07; conversions not started
Last updated: 2026-07-07

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Comment quality gate applies to every touched source file.

## Verified facts (do not re-derive)

- 2026-07-08 recount: 355 `throw` tokens across 48 files; 30 `catch` sites in
  13 files (drift from 47/28/11 is new files, not new throws in old code; the
  `MAX_SOURCE_THROW_TOKENS = 355` ratchet is unchanged and green). Per-file
  cluster counts below re-verified exact: RunFrame.cpp 61, ConvexHullShape.cpp
  41, RenderDeviceDX12.cpp 38, SpatialGrid.cpp 13, PhysicsWorld.cpp 6.
- Census rule: counts must use `git ls-files` + `grep`, never bare `rg`
  (`.gitignore` `Debug/` hides tracked `SkullbonezSource/Physics/Debug/` from
  `rg`; no throws live there today, but do not trust that stays true). Never
  raise the throw ratchet budget to get green — it only goes down.
- Throw census (2026-07-06): 355 sites across 47 files. Top clusters:
  RunFrame.cpp 60 (replay/scrub PROBE assertions, e.g. anchor
  `replay scrub probe mutated the live body`), ConvexHullShape.cpp 41 (data
  validation), RenderDeviceDX12.cpp 38 (API failure), RenderBackendDX12.cpp 22,
  RenderGraph.cpp 18, RunRender.cpp 14, SpatialGrid.cpp 13 (invariants),
  TextureCollection.cpp 12, TestScene.cpp 12, GameModelCollection.cpp 9
  (invariants), CameraCollection.cpp 8, Input.cpp 7, PhysicsWorld.cpp 6.
- Catch census: 28 sites in 11 files. The load-bearing ones:
  - `Init.cpp:3205` — TOP-LEVEL handler: logs
    `Log().WriteEventf("fatal_exception message=...")`, prints `FATAL:` to
    stderr, `Log().FlushAll()`, optional exit dialog. This is where all ~60
    RunFrame probe throws currently land (no local catch in RunFrame.cpp).
  - `Init.cpp:273` — std::terminate handler, logs `terminate_abort`.
  - `Init.cpp:1630` — suite JSON parse → `FailCommandLineParse(...)` (already
    Lane R shaped).
  - `RunInteractionAutomation.cpp:697` — script parse error →
    `FailAutomation( state, message )` (already Lane P shaped).
  - Others: ConvexHullShape.cpp (12), WorkerPool (4), TestSceneParser (1),
    ContactAudioService (1), RunLiveStyle.cpp:273 (logs style_error),
    Window (1), RunScene (1), SceneRuntimeLoad (1).
- `FailAutomation( state, message )` in RunInteractionAutomation.cpp is the
  existing mechanism that sets the interaction report `ok=false` +
  `failure=<message>` — the Lane P (probe) reporting target.
- `Core/Common.h` (~121 lines) is included everywhere and defines `Cfg()` at
  line ~111 — a new tiny `Core/FatalError.h` is the right home for `SB_FATAL`
  (do NOT grow Common.h; plan 04 is shrinking it).
- Log API pattern: `Log().WriteEventf( "event key=\"%s\"", value )` +
  `Log().FlushAll()` before dying (see Init.cpp:3207-3210).
- Profile builds define `SKULLBONEZ_PROFILE_ENABLED` in
  `SKULLBONEZ_CORE.vcxproj`; Debug defines both `_DEBUG` and
  `SKULLBONEZ_PROFILE_ENABLED`.
- `tools/check_runtime_boundaries.py` already includes the phase-1 throw
  ratchet (`MAX_SOURCE_THROW_TOKENS = 355`), `check_throw_site_count`, and
  synthetic clean/grown self-tests. This phase records and validates that
  existing guard instead of duplicating it.

## Phase 1 — policy, primitives, ratchet (no conversions yet)

- [x] P1.1 Create `SkullbonezSource/Core/FatalError.h` (+ .cpp), no includes
  beyond <cstdio>/<cstdarg> and Log.h:
  ```cpp
  // Concept: SB_FATAL is the single Lane F mechanism — a programmer
  // invariant that must never be false in shipping logic. Debug/Profile:
  // break into the debugger. Release: log owner + message + diagnostics,
  // flush, fail fast. Never throws; never returns.
  [[noreturn]] void SbFatal( const char* owner, const char* format, ... );
  #if defined( _DEBUG ) || defined( SB_PROFILE_BUILD )
  #define SB_FATAL( owner, ... ) \
      do { __debugbreak(); SbFatal( owner, __VA_ARGS__ ); } while ( 0 )
  #else
  #define SB_FATAL( owner, ... ) SbFatal( owner, __VA_ARGS__ )
  #endif
  ```
  Implementation: format message → `Log().WriteEventf("fatal owner=... msg=...")`
  → `Log().FlushAll()` → `fprintf(stderr, "FATAL: ...")` → `std::abort()`.
  DISCOVERY sub-item first: find the Profile-config define name
  (`rg -n "SB_PROFILE|PROFILE" SKULLBONEZ_CORE.vcxproj` — record the actual
  preprocessor symbol; if none exists, gate on `_DEBUG` only and note it).
  Evidence: Profile build 0/0. Add both files to the vcxproj AND the
  .vcxproj.filters (project-filter validation exists — run
  `tools\validate_project_filters.bat` if present, else validate_fast).

  Evidence: `SkullbonezSource/Core/FatalError.h` and `.cpp` added and wired
  into `SKULLBONEZ_CORE.vcxproj` and `.filters`; the implementation uses
  `EngineLog::Get()` directly so it does not grow `Common.h`. The public
  header stays Common-free; the `.cpp` adds `<cstdlib>` and conditional
  `<intrin.h>` only for `std::abort()` and Profile/Debug `__debugbreak()`.
  Profile define discovery found `SKULLBONEZ_PROFILE_ENABLED`. Focused Profile
  build passed in 4.293s with 0 warnings/errors.

- [x] P1.2 Create `SkullbonezSource/Core/SbResult.h`:
  ```cpp
  // Concept: Lane R — recoverable failure triggered by external input.
  // The operation fails; the app does not. No exceptions.
  struct SbError
  {
      const char* owner = "";
      char message[192] = {};
  };
  // Minimal expected-like carrier; add SbExpected<T> only when a caller
  // needs a value payload (do not build a framework speculatively).
  struct SbResult
  {
      bool ok = true;
      SbError error;
      static SbResult Success() { return {}; }
      static SbResult Failure( const char* owner, const char* format, ... );
  };
  ```
  Evidence: Profile build 0/0.

  Evidence: `SkullbonezSource/Core/SbResult.h` added as a header-only Lane R
  carrier with bounded inline error text and no heap ownership. Focused Profile
  build passed in 4.293s with 0 warnings/errors.

- [x] P1.3 Ratchet: add a `throw`-census rule to
  `tools/check_runtime_boundaries.py` — count `\bthrow\b` in
  SkullbonezSource (excluding comments/strings as best the script's existing
  scanning supports), stored budget = the count the rule measures on HEAD
  (355 ± comment noise; store whatever the rule itself measures). Failing
  condition: count exceeds budget. Include a self-test. Follow the existing
  rule+self-test pattern in the script. Evidence: checker green on HEAD;
  self-test red on synthetic throw. Gate: `validate_fast` + run the checker.

  Evidence: existing checker rule is present as `MAX_SOURCE_THROW_TOKENS = 355`
  plus `check_throw_site_count`; self-tests include a budget-matched throw
  surface and a grown throw surface that expects `source throw-site count
  exceeds ratchet`. Final validation evidence is recorded in
  `Agentic/Reports/2026-07-07/fable-05-phase-1-error-policy.md`.

- [x] P1.4 Write the three-lane policy into `AGENTS.md` (short table:
  Lane F = SB_FATAL, Lane R = SbResult, Lane P = FailAutomation/probe report;
  "new `throw` is a review failure"). Documentation-only. Commit phase 1.

  Evidence: `AGENTS.md` now has an Error Handling Policy section with Lane F,
  Lane R, and Lane P mechanisms, plus the review rule banning new `throw`
  sites.

## Phase 2 — probes (RunFrame.cpp, ~60 sites, lowest risk)

- [ ] P2.1 Locate the probe entry points: `rg -n "probe" SkullbonezSource/Runtime/RunFrame.cpp`
  — record which functions contain the throw clusters (scrub probe, restore
  probe, etc.) and who calls them (frame loop under automation flags only?).
  Record: are probes reachable outside `--interaction-script`/stress runs?
- [ ] P2.2 Convert each probe function: return `SbResult` (or bool + SbError
  out-param if signatures forbid) instead of throwing; the probe driver calls
  `FailAutomation( state, error.message )` so failures land in the
  interaction report as `ok=false` + failure text instead of a
  `fatal_exception` crash. Keep messages byte-identical to the old throw
  strings (they are assertion documentation).
- [ ] P2.3 Evidence: run the tracked proofs
  (`memory_overlay_f6_toggle`, `replay_branch_restore_live_edge`,
  `prediction_ragdoll_wall_200_predict`) — all `ok=1`; then force one probe
  PER CONVERTED FUNCTION-CLUSTER to fail locally (temporary sabotage) and
  confirm each report shows `ok=false` with the message, no crash; revert the
  sabotage. One sabotage total is NOT enough: the failure mode this guards is
  a converted probe whose caller forgets to propagate/early-exit, silently
  turning "probe failure ends the run" into "probe failure is a log line" —
  and every green-path proof still passes in that broken state. Verify the
  early-exit semantics per cluster, not per phase. Gate: `validate_fast`.
  Ratchet budget drops by ~60 — update the stored number in the same commit.
  Commit.

## Phase 3 — hot-path invariants (physics/stores)

- [ ] P3.1 Convert SpatialGrid.cpp (13), PhysicsWorld.cpp (6),
  GameModelCollection.cpp (9, anchors like
  `Failed to resolve newly authored physics body record`) throws → `SB_FATAL(
  "<subsystem>", ... )` with the same message text. One file per commit.
- [ ] P3.2 Gate per commit: `tools\validate_physics.bat` byte-exact (the
  mechanism swap must not reorder any math — SB_FATAL call sites must stay
  exactly where the throws were). Ratchet budget updated per commit.
- [ ] P3.3 Audit before converting GameModelCollection: 3 of the 28 catch
  sites may be swallowing these (check ConvexHullShape's 12 catches and any
  caller catch of collection append — `rg -n "catch" SkullbonezSource/Scene
  SkullbonezSource/Runtime/Scene`). Any throw that IS caught-and-recovered
  today is Lane R, not Lane F — convert those to SbResult instead. Record the
  classification inline here per site before editing.

## Phase 4 — loaders and editor surface (Lane R)

- [ ] P4.1 TestScene.cpp (12) + TestSceneParser.cpp (2) + Terrain.cpp (5) +
  TextureCollection.cpp (12) + AssetSystem.cpp (3): scene/asset load errors
  → SbResult propagated to the load boundary; scene-load failure surfaces as
  logged `scene_load_failed reason=...` + the existing failed-load UX (find
  it: `rg -n "FailCommandLineParse|errorMessage" SkullbonezSource/Runtime/Init.cpp`
  — reuse, don't invent). ConvexHullShape.cpp (41) is bake/load validation:
  its throws convert to SbResult, its 12 internal catches collapse as their
  matching throws disappear.
- [ ] P4.2 Editor placement paths (GameModelCollection append callers): a
  failed append becomes a UI-visible no-op (log event + skip), never a crash.
  Find callers: `rg -n "AppendGameModelAndPhysicsRows|AppendModel" SkullbonezSource/Runtime`.
- [ ] P4.3 Gate: `tools\validate_full.bat` (scene/asset load is broad scope).
  Ratchet update. Commit per subsystem.

## Phase 5 — DX12 layer (last, most delicate)

- [ ] P5.1 Classify RenderDeviceDX12.cpp (38) / RenderBackendDX12*.cpp (~40
  total) throws: init-time (device creation, feature checks) → SbResult up to
  the boot boundary producing the clean "device unsupported" exit path in
  Init.cpp; steady-state (resource creation mid-run) → SB_FATAL with HRESULT
  in the message (matches the zero-DX12-error policy — a mid-run resource
  failure is not recoverable).
- [ ] P5.2 Gate: `tools\validate_dx12_renderer.bat` three consecutive runs +
  `dx12_validation.txt` = 0. Commit per file family.

## Closure

- [ ] Z1. Ratchet budget = 0 for banned scopes; whatever remains (WorkerPool
  internals? third-party-shaped code?) gets an explicit allowlist entry with
  an owner comment.
- [ ] Z2. Update `fable_plans/05-unified-error-handling-policy-plan.md`
  status + this file; add the lane table pointer to
  `Agentic/Reference/comment-style-guide.md` if reviewers need it.
