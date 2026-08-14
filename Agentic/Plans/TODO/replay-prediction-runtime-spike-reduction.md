# Replay Prediction Runtime Spike Reduction

Date: 2026-08-14
Status: Active - 2/5 phases complete
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
- [x] Add focused CPU fixtures for coherent bank switching, marker-scan resume,
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

- [x] Introduce a Prediction-owned committed-publication state with generation,
  source-frame count, topology/trajectory resume cursors, pending/visible bank,
  and a single coherent-ready transition. This state owns an invariant; it must
  not be a parameter bag.
- [x] Change `PublishCompletedFrame` to arm the pending committed publication,
  not scan all completed frames synchronously.
- [x] Continue publication from `PrepareReplayPredictionOverlay` under the
  existing presentation budget. Budget checks must occur inside the expensive
  frame/node/body loops at resumable boundaries.
- [x] Keep the previous coherent/build trajectory bank visible while the
  replacement bank is incomplete. Switch the reader-visible bank/version only
  after future-node topology and trajectory records describe the same complete
  generation.
- [ ] Preserve deterministic record ordering, branch identity, publication
  versions, final fingerprints, and Automation/offline artifact output across
  different budget-expiry schedules.
- [x] Delete the zero-budget completion path once no reader depends on it; do
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

RP1 implementation evidence (Automation, four completed 120-second predictions):

- The 97.0515-116.7760 ms `PublishCompletedFrame/TrajectoryStore` marker is no
  longer observed. All four completion assertions pass with 14,401 active
  frames, 200 future nodes, 776 trajectory records, and flat reserve growth at
  1718 events.
- Committed duplication resumes between whole causal nodes under the existing
  overlay budget. The latest accepted run places `FutureNodeCache` slices at up
  to 3.9078 ms and `TrajectoryStore` slices at up to 5.1593 ms; no synchronous
  completion-publication marker is observed.
- A Prediction-owned visible snapshot captures the exact frame prefix and
  storage bank, topology, retained markers, trajectory facts, and trajectory
  publication token before a same-target build can mutate them. Fast
  completion, failed begin/worker paths, and Promote-and-Begin storage swaps
  retain that coherent snapshot. Hidden committed topology uses one replacement
  version and flips only after topology, child/all-body records, and marker scan
  all reach the same complete prefix.
- Focused production-path fixtures cover pending presentation, fast completion,
  failed begin, Promote-and-Begin, node-count marker invalidation, and a
  deterministic nonzero all-body resume prefix whose record versions match an
  uninterrupted build. The visual-fidelity engine again
  captures all 2,401 authoritative reveal ticks, then the excluded 4,200-frame
  harness enters its known second live-playback pass. The exact fingerprint
  checkbox therefore remains open and RP1 is not counted complete.

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
- [x] Use RP0 markers to adjudicate baseline/build-frame or other reset costs;
  apply the same logical-reset rule only where evidence shows material hot-path
  destruction. Do not broaden this into an unrelated memory rewrite.
- [x] Prove Predict-off still hides future overlays in the action frame, leaves
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
- After committed-frame active-prefix invalidation: two repeated diagnostics
  place Predict-off at 0.0043-0.0110 ms and its containing frames at
  8.9767-9.3338 ms. `InvalidateFrames` no longer destroys the 14,401 retained
  nested frame payloads; both banks keep their outer and per-frame capacities.
- The focused bank test passes 8/8 assertions. The disable/enable allocation
  probe's engine report passes two generations, keeps the restored prediction
  path visible, publishes 121 final frames, and holds trajectory reserve growth
  flat at 1459 events. Its wrapper still rejects the current 208-frame report
  because it hard-codes an obsolete 180/181 end-frame check; harness repair is
  excluded from this plan.
- Predict-off publication is now count-authoritative across presentation,
  scrubber timelines, archive serialization, topology/trajectory rebuilds,
  deterministic reveal, and validation probes. `CancelJob` joins the worker
  before the count is invalidated, and a later generation can become visible
  only by promoting a newly published build bank.
- The repeated diagnostic's final trajectory fingerprint and point count are
  not stable between executions, so RP1/RP4 must not treat that informational
  report as an immutable oracle or refresh a baseline from it. The existing
  visual-fidelity gate still captures all 2,401 authoritative reveal ticks and
  then fails when its 4,200-frame harness enters a second live-playback pass.
- At the end of this RP3 slice, completion publication remained
  97.0515-116.7760 ms and child-marker context measured 0.0004-0.7588 ms. RP1
  subsequently removed the completion marker as recorded above.

## Phase RP4 - Closure, Threshold Ratification, And Documentation

- [x] Run the shortened four-generation diagnostic repeatedly from one current
  Automation build and record before/after ranges for all three in-scope marker
  classes. Report excluded harness/restart rows separately.
- [x] Run the focused CPU tests, replay visual-fidelity oracle, prediction
  determinism checks, replay allocation policy, dependency graph, and full gate.
- [x] Perform the required touched-source comment audit and independent
  ownership review. Reopen any phase that leaves mixed-bank visibility,
  unbounded work, hidden capacity release, a new growth privilege, or App-owned
  Prediction state.
- [ ] Present post-fix distributions to the owner. Only the owner-selected
  marker/frame limits become hard failures in
  `validate_replay_prediction_frame_spikes.bat`; keep the gate informational if
  no limits are ratified.
- [x] Update `Agentic/Reference/runtime-reference.md` and `tools/README.md` with
  the final publication, marker-scan, cache-reuse, and validation contracts.
- [ ] Delete this plan after all three runtime classes meet the ratified closure
  decision and update the master/session ledgers in the same commit. Git
  history remains the detailed execution archive.

Current RP4 evidence:

- Four accepted four-generation diagnostics no longer observe completion
  publication. The final full-gate run places child-marker discovery at
  0.0006-0.6583 ms and Predict-off at 0.0045-0.0105 ms; p99 is 15.2249 ms and
  p99.9 is 15.8814 ms. The 119.8513 ms maximum belongs to excluded Automation
  report serialization, whose marker spans 3.6416-113.3068 ms. Target restart
  remains a separately labeled 3.7529-6.7798 ms exclusion.
- The complete Replay CPU family passes 84 cases and 1,734 assertions. Focused
  coherent-publication fixtures cover committed, pending, fast/failure,
  Promote-and-Begin, cross-target promotion, and marker-key cases. The all-body
  fixture executes the production builder with a deterministic nonzero prefix,
  resumes it, and proves unchanged prefix versions plus byte-equal final record
  facts against an uninterrupted schedule.
- The touched-source comment audit checked all eight source-bearing files with
  zero deferrals. Independent ownership review found no remaining concrete
  blocker after cross-target pending publication was bound to the promoted
  snapshot through its coherent flip.
- `tools/validate_full.bat` passes end to end after formatting the
  touched Prediction files and deleting an unused mutable `ActiveRecords`
  overload identified by compiled-symbol reachability. Dependency, ownership,
  complexity, glossary, all CPU lanes, and bounded engine probes pass.
- The visual-fidelity process passes its 17 CPU controls and captures all 2,401
  authoritative reveal ticks, then its excluded 4,200-frame interaction enters
  a second live-playback pass. The allocation-policy engine report likewise
  passes two generations and flat reserve growth, while its wrapper retains an
  obsolete 180/181 check against the current 208-frame report. Those harness
  repairs remain outside this plan, so RP1/RP2 immutable-oracle closure remains
  open rather than refreshing either baseline.

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
