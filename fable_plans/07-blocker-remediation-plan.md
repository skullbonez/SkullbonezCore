# Blocker Remediation Plan (overnight run, 2026-07-07)

Date: 2026-07-07
Status: In progress
Input: 31 blocked rows + 1 open row from the authoritative overnight run
(127/160 done). Reasons read from the blocker commits (`git log --grep=block`).
Validation for this document: none (documentation-only)

## The shape of the blockers

The 31 blocks are not 31 problems. They are **four root causes** plus two
cheap strays. Every blocker commit says one of two things: "this row is a
facet of a bigger ownership knot" or "no gate can verify this move." The plan
below attacks the four roots; the facet rows then reopen as normal
overnight-safe work.

| Cluster | Rows | Root cause |
|---------|------|-----------|
| A. World-ownership knot | PHYS-004, 009, 012, 016, 018, 020, 021, 022, 025, 026, 027 (11) | PhysicsEngine is owned by GameModelCollection; `GetPhysicsEngine` has ~124 callers; scene creation/load/reset pass collection+engine together. Circular: each row's safe version depends on the others. |
| B. DX12 capability-split chain | RGRAPH-003, 004, 007, 010, 014, 022, 023, 024, 029 (9) | Everything chains to RGRAPH-003/004: narrow command-context and resource-factory capabilities don't exist yet, and the migration artifact gate (correctly) forbids bridge owners as a substitute. |
| C. Run behavioral routers | RUN-009, 010, 011, 015 (4) | The gates cannot verify full routing/ordering equivalence for TakeInput, DrainRuntimeCommands, LoadScene, UpdateLogic. Needs decomposition + tests that make equivalence checkable, not braver refactoring. |
| D. Diagnostics receiving path | SVC-022, 032, 033 (+034 decision) (4) | Plan-03's own non-goal: profiler/diagnostics reads can't move until a receiving diagnostics snapshot path exists. |
| E. Endgame deletions | SVC-001, 002 (2) | `Gfx()`/`s_gfxBackend` deletion waits on the caller census reaching the startup allowlist (44 sites remain; many are Cluster B rows). |
| Stray | PHYS-035 (in A above, but see A0), RUN-027 (open) | See below — both have independent paths. |

## Quick wins first (no design needed; overnight-safe)

- **A0. PHYS-035 (prediction mutation window) does NOT need the knot.**
  The machine blocked it citing PHYS-004/PHYS-020 per the row text, but
  `fable_plans/03-prediction-isolated-world-progress.md` sidesteps that
  dependency: prediction owns a PRIVATE PhysicsEngine copy (phases P1–P2) —
  no live-ownership moves required. Action: re-point the row at the fable-03
  progress checklist and run it as its own night. Gates are already specified
  there (byte-exact physics + live-hash proof).
- **D0. SVC-034 (LockOrderValidator classification)** is a decision, not
  work: adopt the frozen-globals contract from
  `fable_plans/02-global-service-retirement-progress.md` L2 (function-local
  static, no config reads, no ordering deps, documented header contract).
  One commit, `validate_fast`.
- **RUN-027 (.inl → TU)** is open, not blocked: run it per
  `fable_plans/04-...-progress.md` M3, one completed cluster at a time,
  paired with `validate_perf` for hot files.
- **Leverage buy: fable-01 phase 0 (doctest harness).** The RUN-012 commit
  proved the pattern that unblocks Cluster C — a "policy test" that locks a
  mapping table. Without the harness, every Cluster C slice re-litigates
  verifiability. Half a day; do it before touching C.

## Cluster A — world-ownership knot (human-awake design, then mechanical burn-down)

One design slice, then the 11 rows reopen in order.

1. **Design doc (half-day, human-awake):** name the owner (promote
   `PhysicsScene`, or a new `RuntimeWorld` that owns PhysicsEngine + scene
   entity storage), define the narrow physics command API that replaces
   `GetPhysicsEngine()`, and define the creation pipeline
   (scene entity → physics body/collider rows → presentation registration)
   that PHYS-016/017/025/026/027 keep tripping over. Deliverable: a CSV
   workqueue clustering the ~124 `GetPhysicsEngine` callers by consumer
   domain (replay, editor tools, scene setup, rendering, input) — the
   machine already counted them (commit `750c2e3d`).
2. **Burn-down (overnight-safe once the API exists):** migrate caller
   clusters to the command API; each cluster is a normal gated slice
   (`validate_physics` / area map). PHYS-009 closes when the census hits
   zero; PHYS-004/020 close by moving ownership after the last caller.
3. **Then the dependents reopen mechanically:** PHYS-012 (capacity policy →
   real owner), PHYS-018/026 (reset split → scene lifecycle owner),
   PHYS-021/022 (scratch/diagnostics splits — keep the "only after authority
   is stable" ordering, pair with `validate_perf`).
4. Sequencing note: fold RUN-009 (scene load, Cluster C) into this design
   doc — the blocker commits for PHYS-025/026/027 and RUN-009 describe the
   same scene-lifecycle surface from two sides.

## Cluster B — DX12 capability split (one design slice, then a chain)

1. **Design slice (human-awake):** author the narrow interfaces the blocks
   keep asking for — split graph materialization OUT of
   `IRenderCommandContext` (RGRAPH-003) and define capability-scoped factory
   views (RGRAPH-004). Interfaces first, no owner moves; gate
   `validate_dx12_renderer`.
2. **Chain order (overnight-safe after 1):** RGRAPH-014 (transient
   materializer onto the new interfaces) → RGRAPH-023 (PSO cache) →
   RGRAPH-024 (texture manager) → RGRAPH-022 (DXR owner) → RGRAPH-029
   (executor callback ownership). Barrier-adjacent rows keep the 3×
   consecutive `validate_dx12_renderer` rule.
3. **Endgame:** RGRAPH-007/010 (backend family split) last, when the owners
   above exist and `RenderBackendDX12` is mostly empty forwarding.

## Cluster C — Run routers (decomposition program, not a refactor)

Principle from the blocker commits: the problem is unverifiability. Make
equivalence checkable, then move code.

1. Prereq: fable-01 phase 0 harness (see quick wins).
2. **RUN-010 (TakeInput, ~1,600 lines):** decompose INTO COMMAND EVENTS one
   leaf at a time — each slice extracts one input concern into an explicit
   command handled by the existing RuntimeInteractionController/command
   queue, with a policy unit test locking the key→command mapping (the
   RUN-012 pattern) plus the interaction proofs. Dozens of small
   overnight-safe slices instead of one big unverifiable one.
3. **RUN-011 (DrainRuntimeCommands):** same treatment; the blocker commit
   already lists the command variants (scene load, cinematic, screenshot,
   save-defaults, create-scene, advance/quit, replay event logging) — one
   handler extraction per slice, one proof or policy test each.
4. **RUN-009 (LoadScene):** folded into the Cluster A design doc (scene
   lifecycle owner); implement after A1.
5. **RUN-015 (UpdateLogic/fixed-step loop):** LAST, human-awake, after
   TakeInput/Drain have shrunk Run's surface. Hard constraint from the
   hot-path gate: no callback hooks inside the fixed-step loop — the
   SimulationController must own the loop and call named phase functions.
   Gate: `validate_physics` byte-exact + `validate_perf` + full proofs.

## Cluster D — diagnostics receiving path

1. Build the small receiving path first: a diagnostics snapshot published
   through RuntimeViewModel/DiagnosticsRuntime (the blocker rows' named
   target). One slice, `validate_fast`/`validate_full` per area.
2. Then SVC-022 (perf CSV via injected capability), SVC-032/033 (UI profiler
   tab from snapshots) reopen as mechanical rows.
3. SVC-023 note (done ledger says platform-profiler-markers gate): any
   profiler marker change must run
   `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`.

## Cluster E — endgame deletions (do not schedule yet)

SVC-001/002 close themselves when the `Gfx()` census (44 today) reaches the
startup allowlist — driven by Cluster B rows and fable-02 phase 3. Track the
census number in the ratchet; when it equals the allowlist, one deletion
slice + `validate_full` + `validate_dx12_renderer`.

## Suggested calendar

| Order | Work | Mode | Unblocks |
|-------|------|------|----------|
| 1 | D0 (SVC-034), RUN-027 rolling, fable-01 phase 0 | overnight-safe | C prereq |
| 2 | A0: fable-03 prediction isolation night | overnight (own night) | PHYS-035 |
| 3 | A1 design doc (world owner + creation pipeline + RUN-009) | human-awake, half day | 11 A rows + RUN-009 |
| 4 | B1 design slice (RGRAPH-003/004 interfaces) | human-awake, half day | 9 B rows |
| 5 | D1 diagnostics path + SVC-022/032/033 | overnight | 3 rows |
| 6 | A2 caller burn-down nights + B2 chain nights | overnight | bulk |
| 7 | C2/C3 router decomposition program | mixed, many small slices | RUN-010/011 |
| 8 | RUN-015, then A3 dependents, then E endgames | human-awake finishes | rest |

Net: two half-day design sessions (A1, B1) are the real cost. Everything
else returns to the overnight machine as gated, verifiable slices.

## Execution log

- 2026-07-07: Started quick-win D0 / SVC-034. Classified
  `LockOrderValidator::Instance` as a frozen diagnostics singleton, documented
  the no-config/no-ordering contract in source, removed it from the
  global-service census, and lowered `MAX_GLOBAL_SERVICE_ACCESS_CENSUS` from
  157 to 152. Gates passed: checker self-test, checker scan, and
  `tools\validate_fast.bat` (37.164s, 0 warnings/errors).
- 2026-07-07: Completed fable-01 phase 0 harness bootstrap. Added vendored
  doctest v2.4.12 under `ThirdPtySource/doctest`, `SKULLBONEZ_TESTS` console
  project, smoke test, `tools\validate_tests.bat`, partial-project filter
  validation, solution wiring, and `validate_fast` test integration. Gates
  passed: `tools\validate_tests.bat` (5.192s) and `tools\validate_fast.bat`
  (34.166s), both with 0 warnings/errors. Takeover rerun:
  `tools\validate_fast.bat` passed in 31.346s before commit.
- 2026-07-07: Completed A0 implementation checkpoint for PHYS-035 through
  fable-03 phases 1-2. Replay prediction now seeds and steps a private
  replay-owned `PhysicsEngine`, deletes the live mutation/restore window, and
  adds `liveSolverHashStableAcrossPrediction` interaction proof coverage. Gates
  passed after rubber-duck follow-up: `tools\validate_physics.bat` (13.519s,
  byte-exact), `prediction_ragdoll_wall_200_predict` proof (4.537s, report
  `ok=1`, no gameplay allocation-guard violations), `tools\validate_perf.bat`
  (30.648s after explicit current-machine perf baseline refresh), and
  `tools\validate_full.bat` (39.288s, DX12 validation errors 0, screenshots
  matched, physics byte-exact). Follow-up phase 4 closed the guardrail,
  reference, and ledger work.
- 2026-07-07: Completed A0 / fable-03 phase 4 and marked PHYS-035 resolved.
  Added a runtime-boundary self-test and repo-scan rule forbidding prediction
  solver/body restore calls against the live engine, updated the runtime
  reference/source plan/progress checklist, and moved PHYS-035 to the resolved
  section of the blocker ledger. Remaining blockers: 29. Gates passed:
  `python tools\check_runtime_boundaries.py --self-test`,
  `python tools\check_runtime_boundaries.py` (15.179s, 0 errors), and
  `tools\validate_fast.bat` (33.065s, 0 warnings/errors).
- 2026-07-07: Started A0 / fable-03 prediction isolation and completed phase 1
  parameterization. Discovery recorded PhysicsEngine ownership, store reserve
  shape, PhysicsWorld snapshot coverage, zero physics singleton/global hits,
  explicit body-state helper access, and immutable collider stepping. Source
  slice threads `PhysicsBodyStore` into prediction body capture/apply/frame
  helpers and adds staged pure `StepPredictionEngineTick`. Gates passed:
  `tools\validate_physics.bat` (18.514s, byte-exact physics CSV) and
  `tools\validate_full.bat` (40.456s, DX12 validation errors 0, screenshots
  matched, physics byte-exact).
- 2026-07-07: Completed fable-04 phase 1 repo hygiene. `Agentic/Temp/` is
  ignored and empty in the tip tree, `tools/check_staged_file_sizes.py` blocks
  new oversized staged files outside approved data roots, and the 542 MiB pack
  shrink is recorded as a user-owned history-rewrite decision. Gate passed:
  `tools\validate_fast.bat` (32.100s, 0 warnings/errors).
- 2026-07-07: Completed fable-05 phase 1 error policy. Added Core
  `FatalError`/`SB_FATAL` and `SbResult`, wrote the Lane F/R/P policy into
  `AGENTS.md`, recorded the existing throw ratchet
  (`MAX_SOURCE_THROW_TOKENS = 355`), and updated project-filter rules for the
  new Core files. Gates passed: focused Profile build (4.293s),
  runtime-boundary self-test/check (0 errors), project filters (554 matched
  items), and `tools\validate_fast.bat` (35.331s, 0 warnings/errors).
- 2026-07-07: Completed fable-01 M1 Vector3 tests. Added
  `SkullbonezTests/TestVector3.cpp`, compiled `Vector3.cpp` into
  `SKULLBONEZ_TESTS`, and covered zero-vector `Normalise()` throwing, non-zero
  normalization, dot/cross basis identities, and magnitude consistency. Gate
  passed: `tools\validate_tests.bat` (4.049s, 5 doctest cases, 16 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 M2 Quaternion tests. Added
  `SkullbonezTests/TestQuaternion.cpp`, compiled `Quaternion.cpp` and
  `RotationMatrix.cpp` into `SKULLBONEZ_TESTS`, and covered normalization,
  zero reset, axis-angle sign/round-trip behavior, and repeated multiply drift.
  Discovery found no Slerp API on the current Quaternion surface, so that
  subcase is recorded as not applicable until an API exists. Gate passed:
  `tools\validate_tests.bat` (4.121s, 9 doctest cases, 35 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 M3 Matrix4 tests. Added
  `SkullbonezTests/TestMatrix4.cpp`, compiled `Matrix4.cpp` into
  `SKULLBONEZ_TESTS`, and covered identity inversion, TRS composition against
  manual column-major values, `Data()` storage aliasing, and inverse-original
  identity composition. Gate passed: `tools\validate_tests.bat` (3.834s,
  12 doctest cases, 86 assertions, 0 warnings/errors).
- 2026-07-07: Completed fable-01 M4 GeometricMath tests and closed the
  pure-math phase. Added `SkullbonezTests/TestGeometricMath.cpp`, compiled
  `GeometricMath.cpp` into `SKULLBONEZ_TESTS`, and covered public ray-plane,
  plane/height, sentinel, and reachable degenerate throw contracts. Discovery
  found the old ray-sphere/ray-box wording does not match the current public
  Maths API; those helpers live under runtime/physics surfaces. Gates passed:
  `tools\validate_tests.bat` (3.812s, 15 doctest cases, 105 assertions,
  0 warnings/errors) and `tools\validate_fast.bat` (33.651s, 0
  warnings/errors).
- 2026-07-07: Completed fable-01 S1/S2 physics primitive discovery and bounds
  tests. Confirmed `PhysicsBodyStore`/`ColliderStore` can be constructed and
  populated standalone via direct create/destroy APIs. Added
  `SkullbonezTests/TestBounds.cpp`, compiled `BoundingSphere.cpp`,
  `BoundingBox.cpp`, and the required `ConvexHullShape.cpp` dependency into
  `SKULLBONEZ_TESTS`, and covered broadphase sweep/overlap/touching contracts.
  Gate passed: `tools\validate_tests.bat` (3.748s, 19 doctest cases,
  115 assertions, 0 warnings/errors).
- 2026-07-07: Completed fable-01 S3 SpatialGrid tests. Added
  `SkullbonezTests/TestSpatialGrid.cpp`, compiled `SpatialGrid.cpp` into
  `SKULLBONEZ_TESTS`, and covered insert/query, pair deduplication,
  cell-boundary straddling, swept insertion, and `Clear()` emptiness. The
  initial stack-overflowing local fixture was replaced with static storage to
  match SpatialGrid's large fixed-buffer design. Gate passed:
  `tools\validate_tests.bat` (3.710s, 23 doctest cases, 128 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 S4 PhysicsHandles tests. Added
  `SkullbonezTests/TestPhysicsHandles.cpp`, compiled `PhysicsBodyStore.cpp`
  and `ColliderStore.cpp` into `SKULLBONEZ_TESTS`, and covered fresh handles,
  model-index inverse lookup, replay-id hint fallback, dense-row move on
  destruction, stale generation rejection, collider body lookup, and collider
  scene-object lookup. Added `SkullbonezTests/TestTerrainLinkStubs.cpp` as a
  loud test-only link stub for uncalled Terrain references from body-store
  integration helpers; if a focused handle test reaches Terrain, it throws.
  The first local-store run stack-overflowed, so final tests use static store
  fixtures and `Clear()` between cases to match fixed-capacity runtime storage.
  Gate passed: `tools\validate_tests.bat` (4.011s, 27 doctest cases,
  174 assertions, 0 warnings/errors).
- 2026-07-07: Completed fable-01 S5 ConvexHull tests and closed the physics
  primitives/stores phase. Added `SkullbonezTests/TestConvexHull.cpp`, loaded
  the smallest committed baked hull `SkullbonezData/hulls/pyramid.hull`, and
  covered baked identity, centered vertices, face spans, face normal lengths,
  face index ranges, edge topology, adjacent-face endpoint membership, volume,
  default mass, bounding radius, projected area, inertia half-extents, and
  box-approx inertia scaling. Gate passed: `tools\validate_tests.bat`
  (3.744s, 30 doctest cases, 305 assertions, 0 warnings/errors).
- 2026-07-07: Completed fable-01 E1 RuntimeReserveAllocator tests. Added
  `SkullbonezTests/TestReserveAllocator.cpp`, compiled
  `RuntimeReserveAllocator.cpp` into `SKULLBONEZ_TESTS`, and covered replay
  growth grants under cap, event byte accounting, replay-growth scope approval,
  over-cap denial, policy violation counting, growth-count limit denial, event
  reason fields, and `ResetCounters()` preserving owner registration while
  clearing diagnostics. The first gate failed on project-filter classification;
  `RuntimeReserveAllocator.cpp` now lives under
  `Source Files\Runtime\Allocation` in the test filters. Final gate passed:
  `tools\validate_tests.bat` (3.798s, 35 doctest cases, 364 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 E2 ReplayRecorder tests. Added
  `SkullbonezTests/TestReplayRecorder.cpp`, compiled `ReplayRecorder.cpp` into
  `SKULLBONEZ_TESTS`, and used synthetic solver samples through
  `CaptureFrameFromSolverSample()` to cover presentation ring wrap without live
  runtime owners. Coverage locks retention capacity, oldest-frame eviction,
  chronological copy order, event cursor retention, latest sample, normalized
  scrub lookup, stats, and `ResetTimeline()` preserving capacity. Added
  `SkullbonezTests/TestReplayRecorderLinkStubs.cpp` as loud test-only stubs for
  uncalled full-capture camera/world/model/physics owner hooks. The first gate
  failed at link before those stubs; final gate passed:
  `tools\validate_tests.bat` (4.335s, 38 doctest cases, 397 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 E3 SceneParser tests. Added
  `SkullbonezTests/TestSceneParserUnit.cpp`, compiled `TestScene.cpp` and
  `TestSceneParser.cpp` into `SKULLBONEZ_TESTS`, and covered the smallest
  committed scene `terrain_compare.scene.json` plus the current malformed JSON
  throwing contract. The test file uses a Unit suffix to avoid MSVC object-name
  collision with the production parser TU. Added
  `SkullbonezTests/TestSceneParserLinkStubs.cpp` as a loud test-only stub for
  uncalled `AssetSystem` asset-library lookup. The first gate failed on the
  basename collision and uncalled asset-system symbol; final gate passed:
  `tools\validate_tests.bat` (4.120s, 40 doctest cases, 417 assertions,
  0 warnings/errors).
- 2026-07-07: Completed fable-01 phase 4 determinism and closure. Added
  `SkullbonezTests/TestDeterminism.cpp` with real `PhysicsEngine` micro-world
  stepping and solver/body snapshot restore coverage, promoted the Terrain link
  stub into a flat-plane physics fixture, added Debug diagnostics link stubs,
  and closed the AGENTS regression-test review norm. Gates passed:
  `tools\validate_tests.bat` (5.203s, 42 doctest cases, 527 assertions,
  0 warnings/errors) and `tools\validate_physics.bat` (15.907s,
  `physics_regression_solver.csv` byte-exact).
- 2026-07-07: Completed fable-02 phase 1 global-service census/ratchet.
  Refreshed exact `Gfx()` and `EngineConfig::Instance` counts, singleton-style
  source-file counts, and checker state. Existing overnight guardrails already
  cover global-service access through `GLOBAL_SERVICE_ACCESS_PATTERNS`,
  reviewed renderer-service file classifications, the
  `LockOrderValidator` frozen diagnostics exception, and
  `MAX_GLOBAL_SERVICE_ACCESS_CENSUS = 152`, so no checker/source change was
  needed. Evidence command passed: `python tools\check_runtime_boundaries.py`
  (0 errors).
- 2026-07-07: Completed fable-02 phase 2 config cleanup. Verified `Run` owns a
  borrowed `EngineConfig& m_config` threaded from startup, confirmed exact
  `EngineConfig::Instance` hits are now only the singleton definition
  (`Core\Config.cpp`) and startup bootstrap (`Runtime\Init.cpp`), and changed
  the stale `Config.h` comment that still taught normal code to use the global
  accessor. No behavior changed; evidence command passed:
  `python tools\check_runtime_boundaries.py` (0 errors).
