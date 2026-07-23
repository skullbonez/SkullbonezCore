# Source Blemish Remediation

Date: 2026-07-23
Status: IN PROGRESS — drafted from the 2026-07-23 from-source architecture review of
`nightrunner-22nd-JUL-26`. Registered in `MASTER-PLAN.md` on 2026-07-23 as
plan 1 of the Architecture Follow-Up Campaign Round 3; starts after
`wide-signature-parameter-bag-remediation` closes. 2/6 phases complete.
Impact area: Physics store layout, physics step API, Runtime editor file
naming, profiler unit placement, development-tools TU size
Owner: physics + runtime
Priority: Medium — none of these are behavior bugs; all are carrying-cost and
data-layout blemishes that get more expensive to fix as call sites accumulate

## Problem And Evidence (measured 2026-07-23)

Five concrete blemishes from the architecture review, each verified against
source on `nightrunner-22nd-JUL-26`:

1. **Cold authoring data inside a hot dense store row.**
   `ColliderRecord` (`SkullbonezSource/Physics/ColliderStore.h:58`) carries
   `char contactMaterialName[32]` — a cold scene round-trip token — inside the
   dense record narrowphase and fluid-force code iterate every fixed tick.
   That is 32 string bytes of cache pollution per collider row in an engine
   whose physics is otherwise scrupulously data-oriented.
   `projectedSurfaceArea` and `dragCoefficient` are *hot* (fluid drag reads
   them per step) and stay put; `contactMaterialId` is the runtime-facing hash
   and stays put. Only the name bytes are cold.

2. **Diagnostics identity plumbed through the hot step API.**
   Both `PhysicsEngine::Step` overloads
   (`SkullbonezSource/Physics/PhysicsEngine.h:124` and `:130`) take
   `const char* const* diagnosticNames, int diagnosticNameCount` on every
   call. Diagnostic names are topology-lifetime facts (they change only when
   scene topology changes — see `SceneWorld::BuildDiagnosticNamesForReload`,
   `SkullbonezSource/Runtime/Scene/SceneWorld.h:175`), yet they ride the
   per-tick signature as a C-style array.

3. **`Run*` file-name residue on files that are not `class Run`.**
   Every `Run`-prefixed file below contains zero `Run::` member definitions
   (verified by grep on 2026-07-23): `Runtime/Editor/RunEditorTools.cpp`,
   `RunEditorGizmoTools.cpp`, `RunEditorHistory.cpp`,
   `RunEditorObjectPlacement.cpp`, `RunEditorOverlayTools.cpp`,
   `RunEditorPlacementAssets.cpp`, `RunEditorTracer.cpp`,
   `RunMousePickupTools.cpp`, `Runtime/Scene/RunScene.cpp`, and
   `Runtime/RunCameraState.cpp`. Several already include the correctly-named
   headers they implement (`RunEditorTools.cpp` includes `EditorTools.h`,
   `EditorOverlayTools.h`, `EditorPlacementAssets.h`). The `Run` prefix is
   extraction residue from the old god-object decomposition and now actively
   misleads: it implies `class Run` authority that no longer exists there.
   Note `RunEditorTracer.cpp` defines `RunEditorTracer` the *type* — the type
   name itself carries the same residue and is in scope for the rename
   decision.

4. **Profiler implementation placed in the wrong package.**
   `class Profiler` is declared in `SkullbonezSource/Core/Profiler.h:70` but
   implemented in `SkullbonezSource/Rendering/ProfilerImplementation.cpp`
   (2,087 lines), which mixes the Core-owned marker/history record-keeping
   with renderer GPU-timing and text-overlay presentation in one unit. The
   placement is a legal dodge of the Core→Rendering include rule, but the
   file location will misdirect every future reader, and the CPU-side marker
   logic cannot be touched without opening a Rendering unit.

5. **One development-tools monster TU.**
   `Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` is 3,077 lines in a single
   unit despite sibling policy headers
   (`ImGuiEditorLayoutPolicy.h`, `ImGuiEditorInputPolicy.h`,
   `ImGuiEditorCausalityProjection.h`) already naming its internal seams.

Out of scope per the owner instructions attached to this review: all Replay
TU sizes (`ReplayRecorder.cpp`, `ReplayV2Artifact.cpp`), all wide-parameter
seams (`ApplySceneLoadConsumerOutputs`, `ReplayRuntime::PrepareRenderFrame`),
and the engine/game content boundary (`TornadoGameplay` in `SceneWorld`).

## Goal

Hot physics rows contain only hot data; the physics step signature carries
only per-tick inputs; file names and unit placement say what the code is;
the one oversized development-tools TU is split along its existing policy
seams.

## Non-Goals

- No Replay changes of any kind (owner instruction).
- No parameter-list reduction on scene-load or replay seams (owner
  instruction).
- No behavior change anywhere in this plan: physics baselines must remain
  byte-exact, DX12 baselines unchanged.
- No new context/service bags, forwarding headers, or compatibility aliases
  while moving code — every move lands in the owning unit directly.
- No comment-doctrine changes; header learning-comment structure stays as the
  comment style guide defines it.

## Phases

- [x] **B1 — Split cold collider authoring bytes out of the hot row.**
  Move `contactMaterialName[32]` from `ColliderRecord` into a parallel
  cold-side dense array owned by `ColliderStore` (same row indexing, replaced
  in the same topology commits; a plain `ColliderAuthoringRecord` row is
  enough). Keep `contactMaterialId`, `projectedSurfaceArea`,
  `dragCoefficient`, and every other field hot — record in the store header
  why each surviving field is hot. Update the scene save/round-trip readers
  to consume the cold array. Acceptance: `sizeof(ColliderRecord)` shrinks by
  the name bytes; no caller reads the name from the hot row; physics
  regression CSV is byte-exact against the committed baseline.

  Completed 2026-07-23. `ColliderRecord` is 7,228 bytes under the final MSVC
  class-layout report, exactly 32 bytes below the prior 7,260-byte source
  layout. `ColliderAuthoringRecord` owns the removed name bytes in a
  fixed-capacity parallel row. Create, authored update, clear, and swap-delete
  keep both rows in one topology transaction; focused collider compaction and
  scene snapshot/reparse tests pass 4 cases / 498 assertions. Scene save,
  editor history/inspector, automation fingerprints, and memory diagnostics
  now read/count the cold row. The 10/10 touched source files pass the comment
  audit with zero deferrals. `tools\validate_physics.bat` passes with lifecycle
  hash `0x953D97A226665242` and a byte-exact 44,401-line CSV;
  `tools\validate_perf.bat` passes with zero steady-gameplay allocation or
  reserve-policy violations and no DX12/physics regression. No baseline,
  golden, schema, config, or Replay source changed.

- [x] **B2 — Register diagnostic names at topology time, not per step.**
  Remove `diagnosticNames`/`diagnosticNameCount` from both
  `PhysicsEngine::Step` overloads. Add one cold PhysicsEngine command (e.g.
  `SetDiagnosticNames(std::span<const char* const>)` or names carried on the
  existing authored-refresh view) invoked from the same scene-load/topology
  paths that currently build the name array; the diagnostics sink stores the
  registered identity. `SceneWorld::StepPhysics` and every other step caller
  drop the name plumbing; `BuildDiagnosticNamesForReload` moves to (or is
  called from) the registration edge. Debug-only diagnostics behavior must be
  identical: same names, same rows, same CSV bytes. Acceptance: `Step`
  signatures carry only per-tick inputs; deterministic physics CSV and
  SkullScope trace output are unchanged.

  Completed 2026-07-23. `PhysicsDiagnosticsSink` now owns a fixed-capacity
  pointer table registered through `PhysicsEngine::SetDiagnosticNames` after
  authored refreshes and scene create/delete/clear/replay-trim topology
  commits. Both `PhysicsEngine::Step` overloads and every production/test
  caller have dropped the C-style name pointer/count pair; fixed steps consume
  the registered table without allocating or rebuilding it. The 15/15 touched
  source files pass the comment audit with zero deferrals. A Debug solution
  build passes after the final call-site cleanup in 4.82 s.

  The before/after SkullScope witness used
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene
  SkullbonezData\scenes\physics_bench_varied.scene.json --fixed-step --frames
  120 --physics-diag
  TestOutput\orchestrator_b2_{before|after}.physicsdiag.ndjson --vsync off
  --shadows off`. Both traces are 9,481,773 bytes and both SQLite caches are
  4,644,864 bytes. Their SHA-256 is identically
  `641BDD98CB7229A82433D0AE74FA20D90C4448A9147D89C5451569A5427B7C83`;
  all 20 registered name rows match. Queries were
  `tools\physics_query.bat TestOutput\orchestrator_b2_before.physicsdiag.ndjson
  summary` (8,775 characters/bytes exposed),
  `... events --limit 12` (324), `... frame 0` (11,093),
  `tools\physics_query.bat TestOutput\orchestrator_b2_after.physicsdiag.ndjson
  summary` (8,773 redirected, zero exposed), and `... frame 0` (11,091
  redirected, zero exposed). The bounded derived comparison exposed 587
  characters/bytes, for 20,779 total GPT-read characters/bytes; no query
  output was truncated and neither raw trace was ingested.

  `tools\validate_full.bat` passes in 175.41 s: mandatory CPU coverage and
  runtime lanes pass, builds report zero warnings/errors, DX12 reports zero
  validation errors with accepted captures, and the 44,401-line physics CSV
  remains byte-exact. The mechanical `ReplayPrediction.cpp` caller update
  triggered the cumulative replay gate; one
  `tools\validate_replay_visual_fidelity.bat` invocation passes in 431.73 s
  with one engine process, one prediction generation, the full 2,401-tick
  fidelity oracle, and all negative controls. No baseline, golden, schema, or
  config changed.

- [ ] **B3 — Retire the `Run*` residue names.**
  For each file listed in Problem item 3: confirm again at execution time it
  defines no `Run::` member, then rename to match its real content
  (`RunEditorTools.cpp` → `EditorTools.cpp`, `RunMousePickupTools.cpp` →
  `MousePickupTools.cpp`, `RunScene.cpp` → a name matching the owner it
  implements, `RunCameraState.cpp` stays only if `RunCameraState` remains the
  type name, etc.). Decide type renames in the same pass where the type name
  itself is residue (`RunEditorTracer`, `RunCameraState`, `RunDebugState`,
  `RunSceneState` — rename or record in this plan why the `Run` noun is
  still honest for that type, e.g. it genuinely is Run-owned launch/timer
  state). Update `SKULLBONEZ_CORE.vcxproj`, filters, and all includes in the
  same commit; no forwarding headers, no aliasing. Acceptance: no
  `Run`-prefixed file without `Run::` members remains, builds are clean at
  `/W4`, and the commit body lists every rename pair.

- [ ] **B4 — Put the profiler implementation where it lives.**
  Split `Rendering/ProfilerImplementation.cpp`: the Core marker/history/
  hierarchy implementation moves to `Core/Profiler.cpp`; the renderer-side
  GPU-timing bracket, counter publication, and overlay presentation stay in
  Rendering under an honest unit name (e.g.
  `RenderProfilerPresentation.cpp` beside the existing
  `ProfilerOverlayPresenter.h` / `RenderGpuTimingOwner.h`). The split must
  not add any Core→Rendering include (dependency proof below stays empty)
  and must not change marker identity, nesting, or Tracy zones. Acceptance:
  `Core/Profiler.h`'s implementation is findable in Core; Rendering retains
  only renderer-owned profiler behavior; the platform-profiler-markers run
  passes.

- [ ] **B5 — Split `ImGuiEditorOwner.cpp` along its policy seams
  (owner-optional).** Development-tools builds only. Use the existing
  `ImGuiEditor*Policy`/projection headers as the split map; each new TU keeps
  the same owner type — this is a mechanical TU split, not an ownership
  change, and per the god-object closure rule it must not be reported as a
  decomposition. Skip this phase entirely if the owner prefers one large
  dev-tool TU; record the decision here either way. Acceptance: no TU in
  `Runtime/DevelopmentTools/` exceeds ~1,500 lines, or a recorded owner
  decision to keep it.

- [ ] **B6 — Final review and gates.** Re-run the dependency-direction proofs
  from `AGENTS.md` (all must return no rows), run the mapped validation set
  below, and paste command output in the closing commit body.

## Dependencies And Decisions

- B1 and B2 are independent of each other and of B3–B5; land them as separate
  commits so the physics diffs stay byte-level reviewable.
- B3 renames and any Runtime file moves from the
  `runtime-package-decomposition` plan both rewrite `.vcxproj`/filters —
  sequence the two plans (this plan's B3 first is simplest) rather than
  interleaving.
- B4 must be reviewed against the Core dependency proof:
  `rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core`
  must return no rows after the move.
- Type-rename decisions in B3 are recorded inline in the phase when made.

## Acceptance

Hot `ColliderRecord` carries no cold string bytes; `PhysicsEngine::Step` has
no diagnostics-name parameters; no misleading `Run*` file names remain;
`Core/Profiler.h` is implemented in Core; the B5 decision is recorded. All
gates green with output pasted.

## Validation

| Phase | Required gate |
|-------|---------------|
| B1 | `tools\validate_physics.bat` (byte-exact CSV) then `tools\validate_perf.bat` (hot-path data layout) |
| B2 | `tools\validate_full.bat` (Runtime callers change) — deterministic CSV must be byte-exact; also regenerate one `--physics-diag` trace and confirm identical diagnostic naming |
| B3 | `tools\validate_full.bat` (`Run*`/`Runtime/*` mapping row) |
| B4 | `tools\validate_full.bat` plus `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` (profiling marker rule) |
| B5 | `tools\validate_full.bat` (development-tools build must compile and run) |

No baseline of any kind may be refreshed by this plan; a baseline diff is a
defect in the change, not a cue to update the artifact.
