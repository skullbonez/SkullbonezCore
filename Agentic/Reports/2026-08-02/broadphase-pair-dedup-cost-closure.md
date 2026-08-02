# Broadphase Pair Dedup Cost Closure

Date: 2026-08-02
Result: BD0-BD4 complete; remaining live portfolio 0/15 (0%)

## Outcome

The triangular `pairSeen` store, its N-squared sizing constants, per-pass clear,
Debug same-state driver, pair-stream writer, geometry counter, two oracle scratch
lists, three reserve owners, and two temporary reachability rulings are deleted.
No forwarding API, compatibility alias, renamed counter, hidden service bag, or
replacement pair-identity store survives.

Production first-seen ownership remains the BD3 scene-reserved, per-body
eligibility-projected membership index. `PhysicsBroadphaseStage` retains only
the bounded Debug `sleepPrunedPairs` diagnostic needed by pipeline tracing; the
Profile/Release candidate path, canonical fast-sweep handling, fixed/joint
pruning, allocation bounds, and capacity-fatal behavior are unchanged.

No Physics, SkullScope, Replay, visual, schema, config, golden, or performance
baseline changed.

## Deleted Live Surface

- `SpatialGrid` no longer defines `MAX_PAIR_IDENTITIES`, `PAIR_WORDS`, the dense
  list, dense reset/mark helpers, the legacy filtered driver, dense capacity
  accessors, SceneLoad reserve row, cold reset, or dynamic-memory contribution.
- `PhysicsBroadphaseStage` no longer owns same-state scratch lists, driver/env
  state, stream file state, binary encoders, stream record writers, finalizer,
  destructor, or their memory/reservation terms.
- `SolverBroadphaseStage` no longer exposes or increments a thread-local geometry
  invocation counter. The geometry predicate and sleep-pruned boundary remain.
- `PhysicsFixedList` no longer carries the retired pair-dedup capacity reason.
- Focused reserve/memory tests now census the post-deletion owners; the three
  counter-only assertions are removed while pair and sleep output assertions
  remain.
- The two exact pair-stream writer `repair-plan` rulings are removed rather than
  retargeted to a nonexistent symbol.

## Debug Memory Removed

BD4 changes no Profile/Release dynamic storage. It removes Debug-only dense
storage plus two `4N`-pair scratch lists: exactly
`ceil((N(N-1)/2)/64) * 8 + 64N` bytes at body capacity `N`.

| Bodies | Debug bytes removed |
|---:|---:|
| 37 | 2,456 |
| 200 | 15,288 |
| 520 | 50,152 |
| 1,000 | 126,440 |
| 2,000 | 377,880 |
| 4,000 | 1,255,752 |
| 5,000 | 1,882,192 |
| 8,192 | 4,718,080 |

At the three-body focused capacity, grid backing is now 134,468 bytes in both
Debug and Profile, and broadphase-stage dynamic storage falls exactly 200 bytes
in Debug. The production membership replacement remains `24N + 10,244` bytes.

## Permanent Evidence Boundary

All 23 tracked BD0-BD3 evidence files remain unchanged: four phase reports,
eleven files under `broadphase-pair-dedup-oracles/`, and eight files under
`broadphase-pair-dedup-perf/`. BD3 is the final byte-exact oracle proof on the
immediately preceding source; BD4 deliberately cannot regenerate pair streams
after deleting their writer.

`tools/check_broadphase_pair_stream_oracle.py` remains as the read-only decoder
for the archived little-endian v2 format. Its historical field vocabulary is
format truth, not a live production spelling, and its permanent `Related:` paths
point to the BD0 report and oracle README rather than the deleted TODO plan.

## Ownership And Comment Audit

The touched-source comment audit inspected 9/9 source-bearing files with zero
deferrals:

1. `SkullbonezSource/Physics/PhysicsFixedList.h`
2. `SkullbonezSource/Physics/SolverBroadphaseStage.h`
3. `SkullbonezSource/Physics/SpatialGrid.h`
4. `SkullbonezSource/Physics/SpatialGrid.cpp`
5. `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
6. `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
7. `SkullbonezTests/TestSpatialGrid.cpp`
8. `SkullbonezTests/TestPhysicsHandles.cpp`
9. `tools/check_broadphase_pair_stream_oracle.py`

Learning headers and local comments describe only post-BD4 owners, sequencing,
diagnostic lifetime, fixed-capacity allocation, and failure semantics. Historical
decoder comments are explicitly artifact-facing. Repository-relative `Related:`
paths resolve. This was a touched-file audit, so no subsystem checklist was
required.

## Independent Review

Independent read-only rubber-duck review returned **ACCEPT** after one
comment-only correction: `TestSpatialGrid.cpp` now attributes the planted
membership-capacity fatal to the separate `TestRuntimeContracts.cpp` harness.
The reviewer found no code, ordering, allocation, exhaustion, decoder, or
ownership defect. All seven governance inventories pass: build configuration,
reachability, authority-free aggregates, extraction scars, wide signatures,
function complexity, and glossary vocabulary.

## Final Validation

| Gate | Result |
|---|---|
| Debug/Profile focused builds and tests | PASS — SpatialGrid 27/27 in each configuration; capacity census and broadphase memory accounting pass |
| Archived v2 decoder verification | PASS — all four 360-pass artifacts retain their recorded SHA-256 values |
| `tools\validate_tests.bat` | PASS — Profile project filters, build, and full doctest suite |
| `tools\validate_physics.bat` | PASS — exact Physics baselines unchanged; an initial transient `LNK1236` COFF link failure cleared on immediate Debug rebuild and the complete rerun passed |
| `tools\validate_physics_deep.bat` | PASS — all deep scene comparisons unchanged |
| `tools\validate_perf.bat` | PASS — no baseline refresh |
| `tools\validate_fast.bat` | PASS — 408.8 seconds; format, metadata, dependencies, ownership inventories, builds, and tests |
| `tools\validate_full.bat` | PASS — 649.6 seconds; complete default PR validation |
| `git diff --check` | PASS |

## Next

Continue the Fresh-Read Coverage And Convention campaign with Render Graph
Transition Coverage RG0. Broadphase Pair Dedup Cost is complete and leaves the
active/future ledger under rule 4.
