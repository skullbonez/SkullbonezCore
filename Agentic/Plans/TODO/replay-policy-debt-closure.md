# Replay Policy Debt Closure

Status: Active — 3/4 tasks (RP0-RP3)
Owner: repository owner; registered 2026-07-21 from replay-boundary RB1 audit
Evidence: `../../Reports/2026-07-21/replay-boundary-containment-closure.md`
RB1 findings RB1-F1 through RB1-F3
Ledger: RP0-RP3
Depends on: `replay-boundary-containment` RB2 closure

## Objective

Close the three findings exposed by the replay boundary audit without
shrinking Replay or changing replay behavior: every live prediction/
presentation allocation must be attributed to an existing registered replay
owner, and `PhysicsSceneObjectId` must become Replay's durable cross-system
identity while serialized artifact scalars remain byte-compatible.

## Findings And Binding Decisions

1. A strict two-generation prediction/presentation probe recorded 41,606
   gameplay allocation violations and 41,603 reserve-policy violations in the
   RB1 audit. RP0 reproduced 40,353 and 40,350 respectively. Symbolization then
   exposed a prerequisite policy defect: `RuntimeAllocationTracker` stores its
   current phase in one process-global atomic while owner attribution is thread
   local. Replay worker scopes can therefore relabel concurrent DX12 manifest
   loading and render diagnostics as Replay, while render/main-thread scopes can
   relabel Replay work. The ordinary performance gate does not exercise this
   path. Correct the attribution race before deciding which remaining sites are
   genuine live Replay growth; do not suppress the guard.
2. `ReplayBodyId` and raw `replayBodyId` values remain in Replay, Physics, and
   Rendering even though the standing Scene Object Identity Policy names
   `PhysicsSceneObjectId` as the single durable cross-system identity. The
   legacy scalar is derived from `PhysicsSceneObjectId`; converge the C++ type
   boundary while preserving the durable artifact's existing integer bytes and
   schema version.
3. Replay already has exactly three registered byte-budget owners. Use those
   owners or preallocated/fixed storage; do not register a broad fourth owner,
   wrap an entire frame in a permissive owner scope, or relabel unrelated
   allocations. Owner scopes must be as narrow as the allocation they approve.
4. No replay, physics, screenshot, or behavioral baseline refresh is
   authorized. Divergence is a failed change.

## Tasks

- [x] RP0 — Attribution contract: reproduce the strict two-generation probe,
  symbolize or otherwise map every material owner-zero callsite, classify it
  as Replay prediction/trajectory, Runtime presentation/render, or unrelated
  steady work, and record the exact owner/preallocation remedy. Add a focused
  repeatable probe if the current interaction command is not deterministic.
  Investigation only; no behavior change. Validation: targeted probe.
- [x] RP1 — Allocation closure: route every live Replay-owned growth through
  one of the three existing registered owners or prepare fixed capacity before
  steady use; eliminate owner-zero render/steady allocations caused by replay
  presentation without blanket scopes. Correct the recorder/solver policy
  comments and `REPLAY_GROWTH_OWNER_POLICIES` evidence against the strict-run
  allocator high-water/counters. Fix the two ReplayRecorder `.cpp`/`.h`
  allowlist rows to name `replay_recorder_samples`, its aggregate 32 MiB cap,
  and recorder-owned storage. Extend focused coverage so the strict probe fails
  on any gameplay or reserve-policy violation. First make allocation phase
  state thread-local, add a multithreaded regression proving nested scopes do
  not overwrite another thread, and rerun the strict probe to obtain the
  authoritative post-fix callsite inventory. Validation:
  `tools\validate_fast.bat`,
  `python tools\check_allocation_policy.py --self-test`,
  `python tools\check_allocation_policy.py --repo .`,
  `tools\validate_perf.bat`, the focused strict probe,
  `tools\validate_full.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.
- [x] RP2 — Identity convergence: replace Replay-owned `ReplayBodyId` and raw
  cross-system `replayBodyId` surfaces with `PhysicsSceneObjectId`, including
  Physics authored refresh/body/collider APIs and storage plus Rendering
  instance/override values; retain dense model rows only as repairable hints.
  Delete definition-only `PhysicsReplaySolverSnapshotView` and
  `SceneWorld::TryQueueReplayRenderPoseOverride` unless a concrete non-Replay
  consumer is proven. Keep artifact encoding/decoding as the existing
  fixed-width scalar at the cold IO boundary and prove existing artifacts
  load/save byte-identically. Validation: focused identity/restore tests,
  `tools\validate_physics.bat`, `tools\validate_full.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.
- [ ] RP3 — Closure: rerun the strict allocation probe, inbound Replay include
  proof, identity census, allocation inventory, comment audit, and one
  independent rubber-duck review. Final gates are cumulative:
  `tools\validate_full.bat`, `tools\validate_physics.bat`,
  `tools\validate_perf.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.

## Acceptance

- The strict two-generation path reports zero gameplay allocation violations
  and zero reserve-policy violations without suppressing guard coverage.
- Exactly three replay reserve owners remain, with unchanged or explicitly
  evidence-approved caps and complete counters.
- Replay, Physics, Rendering, and Runtime scene/presentation boundaries use
  `PhysicsSceneObjectId`; the two definition-only facades are gone unless a
  concrete retained consumer is documented; durable artifact bytes and schema
  are unchanged.
- All mapped gates pass without baseline refresh and independent review is
  clear.

## Validation Summary

### RP0 — strict attribution and symbolization

The reproducible Automation command was:

```bat
Automation\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/tornado_alley_showcase.scene.json --frames 220 --renderer dx12 --vsync off --cinematic off --shadows off --fixed-step --hide-top-text --automation-hidden-window --allocation-guard gameplay --dev-ui legacy --interaction-script TestOutput/agent_logs/gameplay_t3_tornado_prediction_probe.json --interaction-report TestOutput/agent_logs/rp0_strict_prediction_report.json --replay on --replay-seconds 2
```

It completed the interaction report with `ok=true`, `framesRun=180`, then
returned the expected strict-policy exit 9 in 3.08 s. The guard reported
317,385 allocations / 482,722,126 bytes, 40,353 gameplay violations, and
40,350 reserve-policy violations. All three registered Replay owners stayed
inside their caps with zero failed growth requests:

| Owner | High water | Approved growths | Failed growths |
|---|---:|---:|---:|
| `replay_recorder_samples` | 17,737,640 B | 933 | 0 |
| `replay_solver_snapshot` | 2,877,186 B | 2 | 0 |
| `replay_prediction_working_set` | 110,979,828 B | 8,491 | 0 |

Visual Studio LLVM symbolization of every material top-24 site produced these
owner/remedy groups. The top sites account for 40,095 of 40,350 policy
violations (99.4%); RP1's post-race-fix run owns the remaining long tail.

| Probe RVAs | Count | Symbol/source attribution | Binding classification and remedy |
|---|---:|---|---|
| `0x2ad659`, `0x2b2fde`, `0x2ad3b9`, `0x2ad3cf`, `0x2ad3ff`, `0x2a9c4b`, `0x2a9a61` | 35,557 | `nlohmann::json` map/parser/value/vector internals. The instantiated type uses `std::map`; `ShaderBytecodeManifest.cpp` is the sole engine translation unit using default `nlohmann::json` and parses `shader_manifest.json` at line 316. | DX12 backend initialization, not Replay. Keep the load under its BackendInit owner/phase. A thread-local allocation phase prevents a Replay worker from relabelling it; do not add a Replay owner scope. |
| `0x89a9b`, `0x2a8a9d`, `0xabfbc` | 2,404 | MSVC string copy, file-to-string construction, and `vector<char>` growth adjacent to `ShaderBytecodeManifest::ReadBytes`/manifest verification. | DX12 backend initialization. Same thread-local phase remedy; retain cold loader allocation policy. |
| `0xbfae4`, `0xbfab0`, `0xb2a3f`, `0xbeb9d`, `0x281643`, `0xba4f2`, `0xce6af` | 2,134 | `basic_stringbuf`/stream construction and `RenderGraph::DumpText` (`RenderGraph.cpp:414`). Of these, 1,676 were labelled Render and 458 Replay. `RuntimeRenderer.cpp:2470-2474` explicitly wraps the executed-frame-graph dump in Diagnostics. | Render diagnostics, not Replay. Preserve the Diagnostics scope and make phase state thread-local so concurrent Render/Replay scopes cannot overwrite it. Any site still labelled gameplay after that fix must be gated or moved to fixed/preallocated diagnostic storage; never give it a Replay owner. |

The one-generation control also returned strict exit 9 with a valid report in
3.08 s. Its dominant JSON counts were already 98.6-99.0% of the two-generation
counts (`16,271/16,506` map insertions and `9,633/9,775` parser strings), while
RenderGraph dump counts moved with frame timing rather than prediction count.
That rules out per-generation Replay growth as the source of these material
families and agrees with the source-level owner mapping.

The root attribution defect is source-proven: `RuntimeAllocationTracker.cpp`
declares process-global `std::atomic<int> s_currentPhase`, while
`RuntimeReserveAllocator.cpp` declares `thread_local s_currentOwner`. A scoped
Replay phase on one thread can therefore pair with owner zero on a different
thread. RP1 must first make the phase thread-local and test cross-thread scope
isolation; only the resulting strict inventory can authorize owner or capacity
changes.

Two discarded diagnostics did not contribute acceptance evidence. Profile
returned exit 1 in 1.83 s because interaction scripts require Automation. A
no-prediction control hit the already-audited 32 MiB recorder cap at frame 61;
a reduced-window retry stopped progressing and its exact PID was terminated.
Neither run produced a valid report, so no allocation conclusion rests on it.

### RP1 — allocation closure

Allocation phase attribution is now thread-local, matching reserve-owner
attribution. A concurrent nested-scope regression proves a Replay scope on one
thread cannot overwrite a Render scope on another. The allocation guard also
captures a bounded higher parent frame so future strict failures point at the
engine operation instead of only an STL allocator.

The post-fix inventory fell in three source-proven steps:

1. Thread-local phase attribution reduced 40,350 policy violations to 2,105;
   the removed JSON family was DX12 BackendInit and the phase-split stringstream
   family was RenderGraph evidence.
2. `WriteCinematicPostGraphEvidence` now labels its file/string work as
   Diagnostics, and the per-generation `ReplayPredictionAmortizedTask` is an
   in-place `std::optional` slot instead of a heap object. That left five
   160-byte steady-gameplay allocations.
3. Higher parent capture mapped all five to the automation-only prediction-
   horizon report formatter. Its narrow Diagnostics scope produced a clean
   strict exit 0 without a blanket Replay owner scope.

`tools\validate_replay_allocation_policy.bat` and
`SkullbonezData/interaction/replay_allocation_policy_two_generation.json` are
the repeatable regression gate. One hidden Automation process must complete two
prediction generations through frame 180 and expose both
`gameplay_violations=0` and `policy_violations=0`; any nonzero process result,
early report, or missing evidence is a failure.

The final strict evidence kept exactly three Replay owners and zero failed
growth requests. Ratified high-water evidence is 17,737,640 bytes for recorder,
2,877,186 for solver snapshots, and the maximum valid strict-run observation
110,979,828 for prediction. Policy comments, the three
`REPLAY_GROWTH_OWNER_POLICIES` measurements, and the allocation allowlist now
agree. The two ReplayRecorder rows name `replay_recorder_samples` and its
aggregate 32 MiB cap; the obsolete heap-task exception is gone.

| Final gate | Result | Time |
|---|---|---:|
| `tools\validate_fast.bat` | PASS; format/metadata/size, Profile/Debug builds, 336 tests / 68,633 assertions | 75.65 s |
| allocation policy self-test | PASS | 0.14 s |
| allocation policy repository scan | PASS; 412 files, zero allowlist errors | 9.4 s |
| `tools\validate_perf.bat` | PASS; allocation and performance comparisons clean | 108.5 s |
| `tools\validate_replay_allocation_policy.bat` | PASS; frame 180, zero gameplay and policy violations | 16.87 s |
| `tools\validate_full.bat` | PASS; CPU umbrella and five runtime lanes, byte-exact physics | 151.9 s |
| `tools\validate_replay_visual_fidelity.bat` | PASS; 2,401 ticks, one generation/presentation, all controls | 447.2 s |

The first `validate_fast` attempt correctly failed three stale headroom
assertions. They were replaced with the owner-approved invariant that every cap
retains at least 1.5x current measured high-water, then the entire final matrix
above passed. No baseline, golden, screenshot, artifact schema, or behavior
value changed.

Comment-style audit covered all 13 touched source/test/substantial-tool files:
13 checked, 0 deferred, no separate subsystem checklist required for this
touched-file pass, and no unresolved terminology.

### RP2 — scene-object identity convergence

Replay, Physics, Rendering, and runtime presentation now carry
`PhysicsSceneObjectId` directly. The duplicate Replay-owned `ReplayBodyId`
wrapper and raw `replayBodyId` fields are absent from the source/test census;
body, collider, handle-slot, authored-refresh, render-instance, pose-override,
camera-target, restore, prediction, and authoring surfaces all use the typed
scene identity. Dense `ModelRowHint` values remain lookup hints only.

The definition-only `PhysicsReplaySolverSnapshotView` and
`SceneWorld::TryQueueReplayRenderPoseOverride` facades had no callers and were
deleted. Replay artifact readers/writers still encode `id.value` in the same
fixed-width scalar fields with no schema or golden change. The byte-canonical
artifact round-trip test passed as part of the 336-test suite, and replay visual
fidelity accepted the durable artifact plus all byte-mutation negative controls.

The first three `validate_full` attempts stopped before completion: two exposed
the implementation/header formatting pipeline, and the third exposed an
Automation-only namespace qualification. Exactly the touched files were
formatted, the Automation target then built cleanly in 12.15 s, and the final
full gate passed. No baseline, golden, screenshot, artifact schema, or behavior
value was refreshed.

| Final gate | Result | Time |
|---|---|---:|
| Focused Profile builds + 336 doctests | PASS; 68,633 assertions, byte-canonical artifact round trip | 27.53 s rebuild + 1.35 s tests |
| `tools\validate_physics.bat` | PASS; handle mirror smoke and 44,401-line CSV byte-exact | 85.63 s |
| `tools\validate_full.bat` | PASS; CPU umbrella and five runtime lanes, DX12 images accepted, byte-exact physics | 153.8 s |
| `tools\validate_replay_visual_fidelity.bat` | PASS; 2,401 ticks, one generation/presentation, durable artifact and all controls | 447.0 s |

Comment-style audit covered all 48 touched source/test files: 48 checked, 0
deferred, every learning-header section present, and no unresolved legacy
identity terminology. The final source/test census reports zero occurrences of
`ReplayBodyId`, `replayBodyId`, `PhysicsReplaySolverSnapshotView`, and
`TryQueueReplayRenderPoseOverride`.

RP3 pending.
