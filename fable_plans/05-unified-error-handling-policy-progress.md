# Progress: Unified Error Handling Policy (plan 05)

Source plan: `fable_plans/05-unified-error-handling-policy-plan.md`
Status: phase 1 complete on 2026-07-07; P2.1-P2.3 replay probe conversion and evidence complete on 2026-07-08; SpatialGrid and PhysicsWorld hot-path conversions complete; remaining hot-path conversions pending
Last updated: 2026-07-08

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Comment quality gate applies to every touched source file.

## Verified facts (do not re-derive)

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
- `tools/check_runtime_boundaries.py` includes the throw ratchet,
  `check_throw_site_count`, and synthetic clean/grown self-tests. Phase 1
  recorded the pre-conversion budget at 355; P2.2/P2.3 lowered it to 294 after
  replacing the Debug replay probe throws. P3 SpatialGrid conversion lowered
  the budget to 281, and P3 PhysicsWorld lowered it to 275.

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

- [x] P2.1 Locate the probe entry points: `rg -n "probe" SkullbonezSource/Runtime/RunFrame.cpp`
  — record which functions contain the throw clusters (scrub probe, restore
  probe, etc.) and who calls them (frame loop under automation flags only?).
  Record: are probes reachable outside `--interaction-script`/stress runs?

  Evidence (2026-07-08): CodeGraph was used first for the RunFrame probe
  surface and `FailAutomation` report path. The RunFrame probe throw clusters
  are Debug-only CLI probe surfaces, not ordinary gameplay and not direct
  `--interaction-script` actions. Per-frame probes are called from
  `Run::ExecuteFrame` only after replay capture (`RunFrame.cpp:1112-1114`) and
  are enabled by Init wiring from command-line flags:
  `--replay-scrub-probe`, `--replay-restore-probe`, and
  `--replay-save-probe` (`Init.cpp:3080-3088`). File probes are invoked
  directly from Init debug-probe options (`Init.cpp:3157-3177`). Non-Debug
  guards reject those CLI options in `Init.cpp:2560-2688`.

  Throw clusters counted in `RunFrame.cpp`:
  `TickReplayScrubProbe:1121` has 11 throws;
  `TickReplayRestoreProbe:1270` has 3;
  `TickReplaySaveProbe:1319` has 22;
  `VerifyLoadedReplayPresentationProbe:1629` has 13;
  `VerifyReplaySolverCheckpointFileProbe:1772` has 5;
  `VerifyReplaySolverTargetFileProbe:2755` has 1;
  `VerifyReplaySolverFailureFileProbe:2794` has 2;
  `VerifyReplaySolverBranchFileProbe:2819` has 3. The non-probe
  `Run::Execute requires a render backend` throw at `RunFrame.cpp:746` is
  outside P2 and belongs to a fatal-invariant lane. `FailAutomation` remains in
  `RunInteractionAutomation.cpp:612`; interaction automation currently throws
  after setting `state.failed` at `RunInteractionAutomation.cpp:1098` and
  `:1916`, so the P2.2 conversion should separate CLI probe result reporting
  from interaction-report failures instead of assuming every RunFrame probe is
  already under an automation state.
- [x] P2.2 Convert each probe function: return `SbResult` (or bool + SbError
  out-param if signatures forbid) instead of throwing. P2.1 discovery corrected
  the reporting boundary: these RunFrame probes are Debug-only CLI diagnostics,
  not direct `--interaction-script` actions, so failures are recorded on
  `RunReplayProbeState::failure` and surfaced by `RunApp` as
  `replay_probe_failed`, stderr text, optional dialog, and process exit 1
  instead of `fatal_exception`. Keep messages byte-identical to the old throw
  strings (they are assertion documentation).

  Evidence (2026-07-08): `TickReplayScrubProbe`,
  `TickReplayRestoreProbe`, `TickReplaySaveProbe`,
  `VerifyLoadedReplayPresentationProbe`,
  `VerifyReplaySolverCheckpointFileProbe`,
  `VerifyReplaySolverTargetFileProbe`,
  `VerifyReplaySolverFailureFileProbe`, and
  `VerifyReplaySolverBranchFileProbe` now return `SbResult`. Per-frame probe
  failures are stored through `Run::RecordReplayProbeFailure()` so `Execute()`
  can unwind normally; Init file probes return immediately through the same
  CLI reporting helper. Old failure strings are preserved, including formatted
  `... failed: %s` cases. The non-P2 throws called out by P2.1 remain:
  `Run::Execute requires a render backend`, replay load failure, and
  `SetReplaySaveProbe` missing-path validation.

- [x] P2.3 Evidence: run the tracked proofs
  (`memory_overlay_f6_toggle`, `replay_branch_restore_live_edge`,
  `prediction_ragdoll_wall_200_predict`) — all `ok=1`; then force one probe
  to fail locally and confirm the chosen machine-readable surface shows the old
  message, no crash; revert any sabotage. Gate: `validate_fast`. Ratchet budget
  drops by ~60 — update the stored number in the same commit. Commit.

  Evidence (2026-07-08):
  - `memory_overlay_f6_toggle`: `Profile\SKULLBONEZ_CORE.exe --renderer dx12
    --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script SkullbonezData\interaction\memory_overlay_f6_toggle.json
    --interaction-report TestOutput\interaction\memory_overlay_f6_toggle_report.json
    --frames 90 --vsync off --allocation-guard gameplay
    --platform-profiler-markers` exited 0 in 10.095s; report `ok=true`.
  - `replay_branch_restore_live_edge`: `Profile\SKULLBONEZ_CORE.exe --renderer
    dx12 --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json
    --interaction-script SkullbonezData\interaction\replay_branch_restore_live_edge.json
    --interaction-report TestOutput\interaction\replay_branch_restore_live_edge_report.json
    --frames 140 --replay on --replay-seconds 2 --fixed-step --vsync off
    --platform-profiler-markers` exited 0 in 3.028s; report `ok=true`.
  - `prediction_ragdoll_wall_200_predict`: `Profile\SKULLBONEZ_CORE.exe
    --renderer dx12 --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json
    --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_predict.json
    --interaction-report TestOutput\interaction\prediction_ragdoll_wall_200_predict_report.json
    --frames 220 --replay on --replay-seconds 2 --fixed-step --vsync off`
    exited 0 in 5.054s; report `ok=true`, `predictionPathVisible=true`, and
    `liveSolverHashStableAcrossPrediction=true`. A prior run with the extra
    `--allocation-guard gameplay` option also produced report `ok=true` but
    exited 9 from unrelated existing render-phase allocation-guard violations,
    so the accepted P2.3 proof uses the checklist's `ok=1` criterion.
  - Forced local failure: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene
    SkullbonezData\scenes\physics_roll.scene.json --frames 1 --vsync off
    --shadows off --replay-restore-file-probe
    TestOutput\interaction\missing_replay_probe_artifact.bin` intentionally
    used a missing artifact and returned process exit 1 in 4.718s with
    `[replay] Probe failed: replay restore file probe failed to load v2 solver checkpoints`
    and no `fatal_exception`.
  - Ratchet: `MAX_SOURCE_THROW_TOKENS` lowered from 355 to 294. `python
    tools\check_runtime_boundaries.py --self-test` passed, and `python
    tools\check_runtime_boundaries.py --max-errors 20` passed with 0 errors.
  - Touched-file comment audit inspected 7 source-bearing files with 0
    deferred: `Core/SbResult.h`, `Runtime/Init.cpp`, `Runtime/Run.cpp`,
    `Runtime/Run.h`, `Runtime/RunFrame.cpp`,
    `Runtime/RunReplayProbeState.h`, and `tools/check_runtime_boundaries.py`.
  - Commit gates: `tools\validate_fast.bat` passed in 67.232s; conservative
    runtime gate `tools\validate_full.bat` passed in 45.705s with DX12
    validation errors 0, screenshots matching committed baselines, and
    `physics_regression_solver.csv` byte-exact.

## Phase 3 — hot-path invariants (physics/stores)

- [x] P3.1a Convert SpatialGrid.cpp (13) throws to `SB_FATAL(
  "Physics/SpatialGrid", ... )` with the same message text. One file per
  commit.

  Evidence (2026-07-08): `SkullbonezSource/Physics/SpatialGrid.cpp` now has
  13 `SB_FATAL` call sites and no live `throw` or `<stdexcept>` use. The call
  sites stayed in the same guard branches as the old exceptions, and the file
  glossary/invariants now identify these capacity/index failures as Lane F
  broadphase contract failures.

- [x] P3.2a Gate SpatialGrid commit: `tools\validate_physics.bat` byte-exact
  (the mechanism swap must not reorder any math). Ratchet budget updated in
  the same commit.

  Evidence (2026-07-08): `MAX_SOURCE_THROW_TOKENS` lowered from 294 to 281.
  `python tools\check_runtime_boundaries.py --self-test` passed; `python
  tools\check_runtime_boundaries.py --max-errors 20` passed with 0 errors;
  focused `SKULLBONEZ_TESTS.vcxproj` project-filter validation passed with
  48/48 items and 0 errors; `tools\validate_fast.bat` passed in
  00:00:38.1202317; `tools\validate_physics.bat` passed in 00:00:15.2232463
  with Profile/Debug binaries ready and `VALIDATE_PHYSICS: ALL PASSED`.
  Touched-file comment audit inspected `SpatialGrid.cpp`,
  `TestDiagnosticsLinkStubs.cpp`, and `tools/check_runtime_boundaries.py`
  with 0 deferred.

- [x] P3.1b Convert PhysicsWorld.cpp (6) throws to `SB_FATAL(
  "Physics/PhysicsWorld", ... )` with the same message text. One file per
  commit.
- [x] P3.2b Gate PhysicsWorld commit: `tools\validate_physics.bat` byte-exact.
  Ratchet budget updated in the same commit.

  Evidence (2026-07-08): `SkullbonezSource/Physics/PhysicsWorld.cpp` now has
  six `SB_FATAL` call sites and no live `throw` or `<stdexcept>` use. The sites
  are fixed-capacity Lane F invariants in persistent-contact side effects,
  resting-wake scratch, collision-cell keys, and object narrowphase island
  staging. `MAX_SOURCE_THROW_TOKENS` lowered from 281 to 275. Hooke, a read-only
  explorer subagent, independently classified all six sites as Lane F with no
  recoverable/caught-path risk. `python tools\check_runtime_boundaries.py
  --self-test` passed in 00:00:00.2918558; `python
  tools\check_runtime_boundaries.py --max-errors 20` passed in
  00:00:17.4556095 with 0 errors; `tools\validate_format.bat` passed on the
  final source; `tools\validate_fast.bat` passed in 00:00:45.6318823; and
  `tools\validate_physics.bat` passed in 00:00:26.2402259 with
  `VALIDATE_PHYSICS: ALL PASSED`. Touched-file comment audit inspected
  `PhysicsWorld.cpp` and `tools/check_runtime_boundaries.py` with 0 deferred.
- [ ] P3.1c Convert GameModelCollection.cpp (8 current throw sites, anchors like
  `Failed to resolve newly authored physics body record`) after P3.3
  classification decides which sites are Lane F and which are Lane R. One file
  or narrow site-family per commit.
- [ ] P3.2c Gate GameModelCollection commit: `tools\validate_physics.bat`
  byte-exact, plus broader validation if the classification changes editor or
  scene-load recovery behavior. Ratchet budget updated in the same commit.
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
