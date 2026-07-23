# Physics Standalone-World Unification Closure

Date: 2026-07-22
Branch: `nightrunner-22nd-JUL-26`
Result: Complete — PU0-PU4, 5/5

## Outcome

`PhysicsEngine` is the only shipping simulation owner. The duplicate
`PhysicsStandaloneWorld`, its private stepping/contact/island implementation,
parallel collection views, smoke result, and `PhysicsApi.cpp` are deleted.
`PhysicsApi.h` now contains only live descriptor and query value contracts.

The validation entry flag remains `--physics-standalone-smoke` for operator and
script compatibility, but its report identifies a PhysicsEngine lifecycle
smoke. Two fresh engines run the production solver with an explicit flat
terrain and inline zero-worker pool. Both must reach the exact state and hash
`0x953D97A226665242`.

## Final Ownership Surface

- `PhysicsEngine` owns body/collider creation, update, destruction, activation,
  fixed-step sequencing, immutable reads, and conservative ray/AABB queries.
- `PhysicsWorld` remains the cohesive solver implementation. Point-joint rows
  carry stable typed handles; compaction moves the complete row, clear advances
  the generation, and body cascade deletion retires connected handles.
- Ray and AABB queries read the engine's canonical stores. Broadphase results
  use a fixed `MAX_SCENE_OBJECTS` scratch list and cannot grow the heap in a
  runtime query.
- No compatibility owner, callback pack, service bag, mutable store escape,
  downward Replay include, or second physics `Step()` survives.
- Allocation metadata deletes the obsolete implementation/header exceptions;
  the cold startup probe allowance names its two sequential PhysicsEngine
  owners. No runtime growth privilege or Replay reserve inventory changed.

## Independent Review And Remediation

The required read-only rubber-duck review found one real false-pass risk: the
first smoke revision checked only three fields of the compacted point-joint row
and hashed descriptor inputs after row destruction. It also found a stale
coverage-routing sentence. PU4 now captures the live survivor row before
destruction, verifies and hashes its handle, endpoint handles, both anchors,
slack, stiffness, damping, group, and flags, and rejects handles retired by
cascade deletion and explicit clear. The coverage ruling now consistently
routes this system smoke through `validate_physics`.

The focused follow-up review found no remaining blocking or non-blocking issue.
It confirmed one simulation owner, sound stable-handle mechanics, bounded query
scratch, full live-row smoke evidence, and consistent coverage routing.

| Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---:|---:|---:|---:|---|---|
| `physics-standalone-world-unification-duck-01` | `/root/pu4_rubber_duck` | Initial closure review | 963 | 1,671 | n/a | ~9 min | Blocking false-pass + stale ruling | Fixed in main agent |
| `physics-standalone-world-unification-duck-02` | `/root/pu4_rubber_duck` | Follow-up after fix | 557 | 485 | n/a | <1 min | No findings | None |

## Comment Audit Checklist

Checklist path: this report. Checked: 14 retained source files. Deferred: 0.
Unchecked: none. The deleted `PhysicsApi.cpp` has no retained comment surface.

- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsHandles.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/Ragdoll.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`

## Final Validation

The desktop shell could not open a separate visible console, so commands ran
in the app shell and were mirrored under `TestOutput`.

| Command | Time | Result |
|---|---:|---|
| Focused Debug build + remediated smoke | 9.91 s | PASS; both runs hash `0x953D97A226665242`, full joint row and stale handles accepted |
| `tools\validate_fast.bat` | 29.04 s | PASS; 741/741 production project/filter items, zero build warnings/errors |
| `tools\validate_coverage.bat` | 18.64 s | PASS; all ratified subsystem floors, 18,736/26,278 whole-product lines |
| `tools\validate_physics.bat` | 23.25 s | PASS; exact lifecycle hash and 44,401-line byte-exact CSV |
| dependency-direction + Replay-boundary proofs | <1 s | PASS; all four commands returned no rows |
| independent rubber-duck review + follow-up | ~9 min + <1 min | PASS after one false-pass fix; final verdict has no findings |
| `tools\validate_full.bat` | 148.16 s | PASS; 345/345 doctests, 68,702 assertions, all CPU/coverage lanes, five runtime processes, zero DX12 errors, accepted images, byte-exact physics |

No physics baseline, replay golden, screenshot, config, authored data, or
allocation reserve inventory changed.

## Closure

PU0-PU4 are complete. The active TODO plan is removed under MASTER inventory
rule 4; this report and commits `b13b0683`, `c179bb8c`, `f3571fbb`, and the PU4
closure commit are the durable record. The binding queue advances to
`run-execute-frame-phase-decomposition` RX0.
