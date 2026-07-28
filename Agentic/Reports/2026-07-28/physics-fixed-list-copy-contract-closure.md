# Physics Fixed List Copy Contract Closure

Date: 2026-07-28
Plan: `Agentic/Plans/DONE/physics-fixed-list-copy-contract.md`
Status: Complete — FC0-FC2 (3/3)

## Outcome

`PhysicsFixedList` and every reviewed aggregate owner are non-copyable and
non-movable. Prediction isolation no longer depends on an implicit owner
transfer: one production `ReplayPredictionReserve` adapter constructs and
seeds the private `PhysicsEngine` under the exact
`replay_prediction_working_set` allocation owner and Replay growth scope.
No public list clone, generic engine clone, context/service bag, or hidden
upward dependency was added.

## FC2 Proof

- Compile-time assertions cover `PhysicsFixedList`, `PhysicsEngine`,
  `PhysicsBodyStore`, `ColliderStore`, `BuoyancySystem`, `PhysicsWorld`, and all
  eight reviewed list-owning Physics stages.
- `ReplayPrediction` and its focused test call the same production adapter.
  The adapter owns lazy engine construction, reserve growth approval, Replay
  allocation phase, exact owner scope, growth scope, and the concrete Physics
  storage seed.
- The positive lifecycle test clones sphere, box, convex-hull, authored
  collider, buoyancy, body hot/cold, point-joint, and solver state; proves
  destination-owned shape backing; advances source and destination one fixed
  step with bit-exact hot state; destroys the source; and reads every cloned
  shape again.
- Child fatal probes prove that startup/no-scope, SceneLoad, Replay without an
  owner, and Replay with the wrong valid owner cannot call the low-level
  Physics seed.
- The test project compiles the existing production
  `ReplayPredictionReserve.cpp` translation unit; no test-only policy wrapper
  or synthetic reserve registration remains.
- The existing allocation-policy row for the one retained private engine
  construction moved with its call site. Its owner, Replay phase, one-engine
  cap, 256 MiB working-set cap, and deletion condition are unchanged.

## Validation Incident

The first strict two-generation Replay allocation run found an 8,583,200-byte
Replay allocation with owner `0`: the first adapter revision scoped storage
seeding but constructed the private `PhysicsEngine` immediately before the
adapter call. FC2 therefore changed the adapter to receive the retained
`unique_ptr` and own both lazy construction and seeding. The rerun was clean.
This was an FC2 implementation defect caught before closure, not a baseline
change.

## Validation Evidence

All final-source gates ran in the isolated worktree
`C:\SkullbonezCore-fc2-validation`.

| Gate | Result |
|---|---|
| Project/filter metadata | Pass: 785 project items and 785 filter items |
| Ownership inventories | Pass: every 12+ signature ruled; aggregates 86/86 ruled; extraction scars 1/1 ruled |
| Allocation-policy self-test and repository scan | Pass: 461 files, 36 direct-heap findings, 85 dynamic-STL-member findings, 617 STL-growth findings, zero allowlist errors |
| `tools\validate_tests.bat` | Pass: 422/422 cases and 2,410,618 assertions |
| `tools\validate_physics.bat` | Pass: lifecycle hash `0x953D97A226665242` repeated exactly; 44,401-line regression output byte-exact |
| `tools\validate_replay_allocation_policy.bat` | Pass: strict two-generation prediction probe clean |
| `tools\validate_replay_visual_fidelity.bat` | Pass: 2,401 ticks, 200 moved and 175 toppled wall bricks, one presented cascade, durable artifact, and every false-pass control |
| `tools\validate_perf.bat` | Pass: allocation guard clean; DX12 and Physics Bench absolute budgets pass with no comparison regressions |
| `tools\validate_full.bat` | Pass: preflight, all CPU/coverage lanes, Automation, Debug, DX12, and deterministic Physics |

The authoritative visual run used the owner-approved local
`SkullbonezData/engine.cfg` fixture with SHA-256
`541816EEC32F361CCFEB1AD9B6719F8DB0D70CD75D1F10B10DB253F577BAC83D`.
No Physics, Replay, visual, or performance baseline was refreshed.

## Review And Comments

The touched-source comment audit is 5/5 with zero deferred files:

- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h`
- `SkullbonezTests/TestPhysicsHandles.cpp`
- `SkullbonezTests/TestRuntimeContracts.cpp`

Independent end-of-plan review returned `ZERO BLOCKERS`. It confirmed the
canonical construction/seed scopes, complete clone and source-destruction
proof, fatal-phase coverage, unchanged allocation privilege, clear ownership
shape, and the 5/5 comment audit.

## Protected Owner Work

The uncommitted warm-start experiment in
`PersistentContactSolver.cpp`, `PersistentContactSolver.h`, and
`PhysicsNarrowphaseStage.cpp` remained outside the FC2 patch, validation
worktree, formatting, and staging. It still awaits owner evaluation; no
baseline reset was performed.

## Questions

None. FC2 required no unresolved owner ruling.
