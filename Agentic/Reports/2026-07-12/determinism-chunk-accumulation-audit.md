# Determinism Chunk-Accumulation Audit

Date: 2026-07-12
Scope: every tracked `ParallelFor*`, `ParallelForChunks*`,
`ParallelCollectOrdered`, `MakeChunks`, and `BuildChunkRangesNoAlloc` production
call site
Owner: physics/validation

## Verdict

No thread-count-sensitive floating-point accumulation exists in the audited
worker-pool call sites. Physics, tornado, and replay paths write independent
per-item/per-pair slots. Serial stages consume those slots in stable body or
pair order. Rendering has two chunk reductions: shadow counts use exact integer
addition, and object bounds use component-wise min/max over finite authored
bounds, which is grouping-independent. Validation therefore does not need a
pinned worker count.

## Inventory

| File:line / call | Classification | Evidence |
|---|---|---|
| `Core/WorkerPool.cpp:402` self-test `ParallelFor` | Per-item independent | One integer square per output index; parent verifies index order. |
| `Core/WorkerPool.cpp:421` self-test `ParallelCollectOrdered` | Per-chunk output, stable concatenation | Each chunk emits ascending indices; merge visits chunk index order. |
| `Physics/PhysicsWorld.cpp:3598` apply forces | Per-item independent | One body row, sleep byte, and remaining-time row per body index; mutual gravity is precomputed input. |
| `Physics/PhysicsWorld.cpp:3678` object narrowphase | Per-item/per-pair staging, serial stable commit | Islands own disjoint pair ranges; events occupy candidate-pair slots and commit strictly by pair index. |
| `Physics/PhysicsWorld.cpp:3730` terrain detection | Per-item staging, serial stable commit | One candidate row per body; contact/manifold effects occur in the later body-order commit. |
| `Physics/PhysicsWorld.cpp:3778` integration | Per-item independent | One body/collider row and corresponding remaining-time row per index. |
| `Physics/TornadoGameplay.cpp:354` tornado field | Per-item independent | Each worker mutates only the indexed body and indexed capture/cooldown values. |
| `Rendering/GameModelRenderer.cpp:256` shadow caster `BuildChunkRangesNoAlloc` | Integer range partition, downstream stable merge | Worker count changes chunk boundaries only; exact counts merge by chunk index into serial prefixes before disjoint fill. |
| `Rendering/GameModelRenderer.cpp:267` shadow caster counts | Per-chunk exact integer reduction | Chunk-local category counts merge serially with integer addition; grouping cannot round. |
| `Rendering/GameModelRenderer.cpp:289` shadow batch fill | Per-chunk disjoint output | Serial prefix sums assign non-overlapping output ranges before workers fill them. |
| `Rendering/GameModelRenderer.cpp:818` shadow bounds `BuildChunkRangesNoAlloc` | Integer range partition, downstream stable merge | Worker count changes grouping only; chunk results merge in chunk order with component-wise min/max. |
| `Rendering/GameModelRenderer.cpp:826` shadow bounds | Per-chunk numeric reduction, thread-count-neutral | Chunk bounds merge serially by component-wise min/max. Finite authored bounds make grouping irrelevant; no sums/averages occur. |
| `Runtime/Replay/RunReplayTools.cpp:3586` prediction backup capture | Per-item independent | Reads one authoritative body and writes one backup slot. |
| `Runtime/Replay/RunReplayTools.cpp:3762` prediction sample capture | Per-item independent | Reads one predicted body and writes one frame-body slot. |

`WorkerPool::ParallelFor` and `ParallelForChunks` are forwarding wrappers over
their no-allocation forms; `MakeChunks` and `BuildChunkRangesNoAlloc` only
partition integer index ranges. No additional accumulation occurs inside those
wrappers.

## Binding Decision

Worker count remains an execution/performance input, not a physics-result
input. Do not add per-chunk floating-point sums, averages, impulses, energies,
or time integration without per-item staging plus a stable serial reduction.
If such a reduction cannot be restructured, an owner decision must pin the
validation worker count and regenerate every affected baseline in the same
commit.
