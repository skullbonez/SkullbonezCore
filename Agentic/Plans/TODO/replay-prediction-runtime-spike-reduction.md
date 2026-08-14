# Replay Prediction Runtime Spike Reduction

Date: 2026-08-14
Status: Active - 0/5 phases complete
Impact area: Runtime/Prediction publication and presentation, Replay scrubber
composition, focused CPU tests, Automation diagnostics
Owner: Replay Prediction owns publication/cache storage; ReplayRuntime composes
typed scrubber actions without absorbing Prediction state
Priority: Address the measured runtime stalls before assigning hard timing gates

## Owner Direction

Address these three runtime spike classes from the bounded 120-second prediction
diagnostic:

1. `Frame/Replay/Prediction/PublishCompletedFrame/TrajectoryStore`
2. `Frame/Replay/Prediction/PrepareOverlay/BuildChildMarkerContext`
3. `Frame/Replay/ScrubberInput` while turning Predict off

The diagnostic-only final-report serialization under
`Frame/PostDraw/InteractionAutomation` is excluded. Target changes and their
prediction-restart/cache-seeding path are also excluded. Do not fold either
excluded path into this plan merely because it shares `Frame/Input` ancestry.

Implementation of this plan must use `Agentic/Skills/orchestrator/SKILL.md`.
Each runtime spike is an independently reviewable and revertible slice; do not
bundle all three behavior changes into one commit.

## Current Evidence

The final shortened diagnostic completed four 120-second prediction generations
in 62.649 seconds inside a 3,800-frame launch bound (3,700 interaction frames
ran). It reproduced all three in-scope classes:

| Runtime class | Whole-frame observation | Direct marker observation | Current cause |
|---|---:|---:|---|
| Completion trajectory publication | 144.19-153.47 ms | 119.67-126.65 ms | `PublishCompletedFrame` rebuilds the committed trajectory bank from all 14,401 frames with a zero-expiry budget. |
| Child-marker context | 33.72-49.86 ms | 20.08-35.02 ms | A reveal/frame-count key change reruns the frame-by-node scan from frame zero. |
| Predict-off scrubber input | 33.90-34.45 ms | 25.10-25.45 ms | `HandleReplayPredictionPressed` calls `ClearCache`, which synchronously destroys active trajectory records and their nested point-vector storage. |

Evidence artifacts are produced by
`tools/validate_replay_prediction_frame_spikes.bat` under
`TestOutput/diagnostics/replay_prediction_frame_spikes/`. Artifacts remain
untracked; this plan records the reproducible ranges and the tracked diagnostic
is the way to refresh them.

## Goal

Remove the three frame-thread bursts without changing prediction physics,
published trajectory/marker content, replay hashes, visual reveal order, or
post-gameplay allocation policy. Long work must become resumable under the
existing Prediction presentation budget, or become constant-time logical
publication/invalidation over already retained storage.

Timing remains informational until the owner ratifies thresholds from the
post-fix measurements. An implementation may not invent a generous threshold
and call the spike closed.

## Non-Goals

- Do not optimize, gate, or otherwise change Automation report serialization.
- Do not optimize target selection, prediction restart, private-engine seeding,
  or generation interruption.
- Do not change the 120-second horizon, 200-brick workload, fixed timestep,
  prediction physics, worker slice budget, or four-generation diagnostic shape.
- Do not hide work on an unbounded detached thread or permit presentation to
  read worker-owned frames before release/acquire publication.
- Do not add a second Replay/Prediction state owner, broad context/service bag,
  callback pack, forwarding facade, or downward Runtime dependency.
- Do not add or expand a replay growth privilege, reserve cap, or hot
  `PhysicsBodyRecord` field. Any unavoidable owner/cap change requires a new
  owner ruling and inventory update before implementation continues.

## Closure Invariants

- Physics and replay outputs remain byte-reproducible for identical inputs.
- A reader sees either the previous coherent trajectory bank or the complete
  replacement bank, never a mixed generation or partially re-keyed bank.
- Entry/rest/horizon markers and final trajectory fingerprints are independent
  of render cadence, worker completion frame, and presentation-budget expiry.
- Turning Predict off immediately hides the future, cancels/joins in-flight
  work safely, and invalidates reader-visible publication in the action frame.
  Physical capacity reclamation is not part of that hot transition.
- Repeated generations and Predict toggles produce no gameplay/reserve
  violation and no retained-owner growth after the established high-water mark.
- The shortened diagnostic remains full-only and non-blocking until the owner
  explicitly ratifies hard limits.

## Phase RP0 - Lock Attribution And Behavioral Oracles

- [x] Add nested markers around the Predict-off path: worker cancellation,
  trajectory-store invalidation, baseline reset, and other retained-state
  resets. Keep `Frame/Replay/ScrubberInput` as the parent marker.
- [x] Extend the diagnostic analysis only as needed to report the three named
  classes consistently across repeated runs; keep harness and target-restart
  findings labeled as excluded evidence.
- [x] Capture a fresh before-change run and record generation count, final
  prediction fingerprint facts, marker ranges, reserve growth counters, and
  interaction-frame identities.
- [ ] Add focused CPU fixtures for coherent bank switching, marker-scan resume,
  and trajectory-record reuse before changing production behavior.

Primary files:

- `tools/analyze_replay_prediction_spikes.py`
- `tools/test_analyze_replay_prediction_spikes.py`
- `SkullbonezData/interaction/replay_prediction_120s_frame_spike.json`
- `SkullbonezTests/TestReplayPredictionScheduling.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`

RP0 is observational. It must not alter prediction output, scrubber semantics,
or allocation policy.

## Phase RP1 - Replace Completion Rebuild With Budgeted Coherent Publication

Current flow swaps `buildFrames` into `simulation.frames`, clears the future
node cache, then calls
`RebuildReplayPredictionCommittedTreeAfterWorkerCompletion`. That function
passes `0.0` as the budget, deliberately forcing every future-node and
trajectory record through one frame-thread pass.

- [ ] Introduce a Prediction-owned committed-publication state with generation,
  source-frame count, topology/trajectory resume cursors, pending/visible bank,
  and a single coherent-ready transition. This state owns an invariant; it must
  not be a parameter bag.
- [ ] Change `PublishCompletedFrame` to arm the pending committed publication,
  not scan all completed frames synchronously.
- [ ] Continue publication from `PrepareReplayPredictionOverlay` under the
  existing presentation budget. Budget checks must occur inside the expensive
  frame/node/body loops at resumable boundaries.
- [ ] Keep the previous coherent/build trajectory bank visible while the
  replacement bank is incomplete. Switch the reader-visible bank/version only
  after future-node topology and trajectory records describe the same complete
  generation.
- [ ] Preserve deterministic record ordering, branch identity, publication
  versions, final fingerprints, and Automation/offline artifact output across
  different budget-expiry schedules.
- [ ] Delete the zero-budget completion path once no reader depends on it; do
  not retain it as a fallback that can reintroduce the stall.

Primary files:

- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h`
- `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`
- `SkullbonezTests/TestReplayPredictionScheduling.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`

RP1 acceptance:

- The four completion frames retain one coherent visible prediction throughout
  pending publication.
- Final trajectory/causal fingerprints match the pre-change oracle exactly.
- No single `PublishCompletedFrame/TrajectoryStore` call performs the complete
  14,401-frame scan; measured work follows the configured presentation budget
  plus bounded publication overhead.

## Phase RP2 - Make Child-Marker Discovery Incremental

`BuildReplayPredictionChildMarkerContext` currently resets a fixed context and
walks every visible frame for every future node. The exact cache key includes
frame count and reveal frame, so normal growth invalidates the result and starts
again from frame zero.

- [x] Replace exact-result caching with Prediction-owned incremental scan state:
  generation/topology identity, revealed-frame cursor, per-node activation,
  entry pose, last-motion frame, and catch-up cursor for newly published nodes.
- [x] Scan only the newly revealed/published suffix for stable nodes. A topology
  change may initialize new nodes, but it must not rescan unchanged nodes.
- [x] Put budget checks inside marker discovery and resume from the exact
  frame/node cursor. Do not wrap another unbounded frame-by-node loop in one
  outer budget check.
- [x] Keep retained-marker side effects idempotent. Entry markers publish once;
  rest/horizon markers appear only when the same authoritative reveal and
  completion conditions used today are satisfied.
- [ ] Compare incremental results with the existing full-scan oracle over
  growing prefixes, reveal jumps, topology additions, generation replacement,
  and completed 200-brick predictions before deleting the full scan.

Primary files:

- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`

RP2 acceptance:

- Marker output is byte/value-identical to the full-scan oracle for every
  tested prefix and reveal sequence.
- Stable nodes are processed once per new frame suffix rather than once per
  presentation frame.
- `BuildChildMarkerContext` no longer owns an unbudgeted frame-by-node burst;
  any retained replacement marker exposes the resumable work clearly.

RP2 evidence (Automation, four completed 120-second predictions):

- The grouped `BuildChildMarkerContext` marker fell from 0.0021-35.5981 ms to
  0.0006-0.9382 ms; its worst whole frame fell from 48.8738 ms to 15.6957 ms.
- The completed run retained all 776 trajectory records, published 5,596,532
  points, and held reserve growth flat at 1720 events from start to end.
- Focused doctests prove stable topology preserves suffix cursors, changed
  topology resets the affected node, and suffix accumulation produces the same
  entry/last-motion facts as a full scan. Retained marker effects publish only
  after every node reaches the requested coherent prefix.
- The immutable visual gate's 17 CPU controls passed and its authoritative
  engine run captured all 2,401 reveal ticks, but the gate then failed because
  the 4,200-frame interaction entered a second live-playback pass after the
  reveal. Harness changes are excluded from this plan, so the full-scan oracle
  checkbox and RP2 phase remain open pending an owner-approved harness repair.

## Phase RP3 - Separate Predict-Off Invalidation From Capacity Reclamation

`HandleReplayPredictionPressed` turns Predict off and calls
`ReplayPrediction::ClearCache` inside `Frame/Replay/ScrubberInput`.
`ReplayTrajectoryStore::Clear` clears each record and then destroys the record
vector's active elements, releasing nested point-vector capacity synchronously.

- [x] Split logical invalidation from physical release. Predict-off must cancel
  the worker and make records invisible immediately, then reset active counts,
  cursors, and publication versions without freeing retained capacities.
- [x] Give `ReplayTrajectoryStore` an explicit active-record prefix or equivalent
  invariant-owning reuse mechanism. `FindRecord`, iteration, packet publication,
  and diagnostics must ignore inactive retained slots.
- [x] Reuse record slots and nested point-vector capacity on the next generation.
  Permit physical release only in an existing cold scene-reset/shutdown owner,
  not in pointer input or per-frame Prediction work.
- [ ] Use RP0 markers to adjudicate baseline/build-frame or other reset costs;
  apply the same logical-reset rule only where evidence shows material hot-path
  destruction. Do not broaden this into an unrelated memory rewrite.
- [ ] Prove Predict-off still hides future overlays in the action frame, leaves
  no worker borrow in flight, and cannot resurrect stale records when Predict is
  enabled again.

Primary files:

- `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- `SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp`
- `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`
- `SkullbonezTests/TestReplayVisualPacket.cpp`
- `SkullbonezTests/TestReplayPredictionScheduling.cpp`

RP3 acceptance:

- The Predict-off action performs no nested trajectory point-capacity release.
- A disable/enable/two-generation sequence has identical visible and final
  prediction output and no reserve growth after warm-up.
- `Frame/Replay/ScrubberInput` no longer contains the measured synchronous
  trajectory-store destruction burst.

Current RP0/RP3 evidence (Automation, four completed 120-second predictions):

- Before nested attribution: Predict-off grouped marker 26.0907-26.4603 ms.
  `InvalidateFrames` owned 15.3099-15.3103 ms and trajectory-store destruction
  owned 10.7711-11.1330 ms; worker cancellation was 0.0013 ms.
- After active-prefix/keyed-capacity reuse: Predict-off is 15.6154-16.2350 ms;
  trajectory-store invalidation is 0.0043-0.0047 ms, all 776 records publish,
  and reserve growth stays flat after warm-up (1720 at start and end).
- The remaining RP3 work is the independently measured 7,201-frame nested
  payload destruction. Completion publication remains 101.3209-116.7351 ms;
  child-marker context remains 0.0021-35.5981 ms.

## Phase RP4 - Closure, Threshold Ratification, And Documentation

- [ ] Run the shortened four-generation diagnostic repeatedly from one current
  Automation build and record before/after ranges for all three in-scope marker
  classes. Report excluded harness/restart rows separately.
- [ ] Run the focused CPU tests, replay visual-fidelity oracle, prediction
  determinism checks, replay allocation policy, dependency graph, and full gate.
- [ ] Perform the required touched-source comment audit and independent
  ownership review. Reopen any phase that leaves mixed-bank visibility,
  unbounded work, hidden capacity release, a new growth privilege, or App-owned
  Prediction state.
- [ ] Present post-fix distributions to the owner. Only the owner-selected
  marker/frame limits become hard failures in
  `validate_replay_prediction_frame_spikes.bat`; keep the gate informational if
  no limits are ratified.
- [ ] Update `Agentic/Reference/runtime-reference.md` and `tools/README.md` with
  the final publication, marker-scan, cache-reuse, and validation contracts.
- [ ] Delete this plan after all three runtime classes meet the ratified closure
  decision and update the master/session ledgers in the same commit. Git
  history remains the detailed execution archive.

## Validation Map

| Change | Required evidence |
|---|---|
| Publication state/bank selection | Focused doctests in `TestReplayPredictionScheduling.cpp` and `TestReplayVisualPacket.cpp`; `tools/validate_replay_visual_fidelity.bat`; prediction determinism check |
| Incremental child markers | Full-scan equivalence fixtures in `TestReplayVisualPacket.cpp`; visual-fidelity oracle; shortened spike diagnostic |
| Trajectory-store reuse/Predict-off | Focused store/toggle tests; `tools/validate_replay_allocation_policy.bat`; shortened spike diagnostic |
| Any Replay-facing source change | `tools/validate_dependency_graph.bat` and review for downward Replay includes or growth privilege changes |
| Final closure | `tools/validate_full.bat`, comment-style audit, independent ownership review, owner threshold ruling |

Do not refresh a visual, replay, physics, or fingerprint baseline to make a
behavioral difference pass. A changed oracle is a regression until the owner
explicitly approves the exact behavioral change.
