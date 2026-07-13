# Replay Visual Fidelity Mega Probe — Frame-Exact 200-Box Prediction Proof

Date: 2026-07-13
Status: Live — 2/7 tasks complete
Branch: `nightrunner-13th-july`
Impact area: replay prediction, replay presentation, trajectory/marker
submission, artifacts, automation, tests, and validation
Owner: replay subsystem

## Problem And Evidence

`SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json` is the
known-good product baseline. The striker reaches the wall and reveals its path
first; downstream bricks then activate and publish their paths in causal order
as the collapse cascades. This exact behavior must survive replay decomposition.

Current checks do not prove that contract:

- prediction determinism compares two prediction runs, so identically wrong
  runs can pass;
- submitted-geometry stability observes only a late steady window, so an early
  cascade divergence can settle and pass;
- the current exact hash covers replay ribbon vertices, not every replay-owned
  marker, ghost, causal line, reveal decision, or presentation value;
- the rolled-back fidelity probe compared solver state, not the complete visual
  packet shown to rendering;
- screenshots are useful downstream DX12 evidence but are too coarse and
  timing-sensitive to be the primary equality oracle.

## Goal

Create one mandatory command:

```bat
tools\validate_replay_visual_fidelity.bat
```

It fails at the first simulation tick where replay prediction or presentation
differs by any compared bit from the approved working baseline or the later
live future. It always runs the full 200-box scene, never a reduced substitute.

The proof has two independent oracles:

1. **Golden working-base equality.** Every observed presentation tick matches a
   committed digest manifest captured before refactoring.
2. **Predicted-versus-live equality.** A frozen predicted future from seed T
   matches retained live presentation at T+k after live simulation is released
   with no intervening mutations.

Both compare a canonical replay-owned CPU visual packet before DX12 upload.
Selected screenshots remain secondary shader/render evidence.

## Binding Visual Packet Contract

The packet is keyed by `ReplayFrameIndex`, never render-frame ordinal or
wall-clock time. It contains deterministic ordering and exact values for:

- seed/reveal frame, branch, target `ReplayBodyId`, build and publication state;
- every trajectory record key, lane, identity, parent, branch ordinal, depth,
  style, first activation frame, contact-derived flag, published count, point,
  and point frame index;
- future-node topology and causal activation order;
- entry, rest, and horizon marker inventories and exact poses;
- replay-owned prediction ghost and ragdoll draw requests;
- every replay ribbon, ordinary/priority line, marker, and ghost vertex sent
  toward rendering, including ordering, positions, adjacency, color, alpha,
  width, feathering, HDR scale, counts, and byte size;
- dropped-segment/budget state and replay reserve-growth counters, so truncated
  output cannot masquerade as equality;
- camera/world presentation inputs when they affect emitted output.

Typed fields are compared first and exact bytes second. A failure reports the
first tick, section, lane/body/record, element, field, expected/actual values,
and hashes. Hash-only diagnostics are insufficient.

## Golden Baseline Governance

- V0 records commit, scene/config/script/shader hashes, executable
  configuration, seed tick, horizon, and packet schema version.
- The committed manifest has one row per compared simulation tick plus
  structural counts. Full packet dumps are bounded failure artifacts.
- Reordering is behavior change unless explicitly canonicalized before hashing.
- The validation command never updates its own baseline.
- Baseline refresh requires explicit owner approval and an intentional visible-
  behavior explanation. Refactors and owner moves never justify refresh.
- Missing ticks, empty packets, reduced counts, early horizon, reserve failure,
  dropped geometry, or skipped comparisons are hard failures.

## Tasks

- [x] **V0 — Freeze the working base and land the first complete gate.** Record
  the approved pre-refactor commit and all provenance. Build an end-to-end
  Profile fixed-step probe that selects `prediction_striker_ball`, freezes seed
  T, completes a bounded prediction horizon, releases live advance with no
  further input, records every compared `ReplayFrameIndex`, and writes a
  bounded report. Commit the first golden per-tick manifest. Inject one point
  mutation and prove the command fails at that tick before removing it.
  Acceptance: the real command passes the base, fails the negative control, and
  rejects empty/incomplete horizons. Validation:
  `tools\validate_replay_visual_fidelity.bat` at the end of V0.

- [x] **V1 — Publish the canonical replay visual packet.** Introduce a typed,
  replay-owned, read-only packet at the presentation-to-render seam. It observes
  every replay ribbon, line, marker, ghost, topology/reveal value, count, drop,
  and reserve diagnostic without owning renderer authority or becoming a broad
  context bag. Production rendering consumes the same packet; a test-only
  parallel builder is forbidden. Add focused CPU tests for ordering and first-
  difference diagnostics. Acceptance: all replay visual submission has one
  packet owner and the V0 manifest remains exact. Validation:
  `tools\validate_replay_visual_fidelity.bat`, then the affected CPU test gate.

- [ ] **V2 — Prove the full causal cascade tick by tick.** Retain predicted
  packets for T+k and compare them with packets reconstructed from later live
  samples for the same frame index. Assert the striker path appears first,
  downstream activation is monotonic through the causal topology, revealed
  paths remain present, and marker transitions occur on exact ticks. Cover
  collision onset and the full cascade, not only the settled tail. Negative
  controls shift one activation tick, alter parent/depth, and remove a segment.
  Acceptance: each fails at its injected first divergence. Validation:
  `tools\validate_replay_visual_fidelity.bat`.

- [ ] **V3 — Round-trip durable replay presentation.** Extend the
  `ReplayPresentationSample` visual seam, delta capture/hash, and artifact
  serialization with the minimum domain state required to reproduce the packet;
  never serialize DX12 resources or pointers. Any schema change follows the
  versioned-migration policy with integer bump, deterministic migration,
  upgraded fixtures, and legacy/current/future/writer tests. Save the 200-box
  replay, load it in a fresh process, scrub the horizon, and compare the same
  manifest. Acceptance: live, predicted, saved, loaded, and scrubbed packets
  agree exactly. Validation: `tools\validate_replay_visual_fidelity.bat`,
  focused artifact tests, and `tools\migrate_data_formats.py --check` when
  applicable.

- [ ] **V4 — Close timing, determinism, and false-pass holes.** Run at least
  two clean processes and compare their ordered per-tick manifests. Pin fixed
  step, seed, reveal-frame mapping, camera input, worker completion, event
  cursor, scene/config input, and horizon. Reject render-frame/wall-clock
  comparison. Negative controls cover seed mismatch, missing tick, event
  mutation, non-fixed step, truncated horizon, record reordering, vertex-byte
  change, dropped geometry, and reserve growth. Acceptance: repeat runs are
  identical and every false-pass control fails. Validation:
  `tools\validate_replay_visual_fidelity.bat`.

- [ ] **V5 — Make the probe a permanent repository gate.** Register scripts,
  interaction assets, manifest, and any CPU target in project filters,
  `tools\README.md`, `validate_select.bat`, and the mandatory CPU umbrella when
  applicable. Make `tools\validate_replay_scrub.bat` invoke this command rather
  than retain a weaker parallel oracle. Update `AGENTS.md` file-to-gate mapping
  so replay source, test, artifact, or presentation changes require it.
  Acceptance: one authoritative command exists and nested failures propagate
  nonzero. Validation: `tools\validate_replay_visual_fidelity.bat`, the changed
  validation script, and `tools\validate_all_cpu_tests.bat` if applicable.

- [ ] **V6 — Adversarial closure and decomposition handoff.** Complete the
  touched-file comment audit and an independent review for omitted lanes,
  shared-builder false positives, self-updating baselines, vacuous comparison,
  render/simulation tick confusion, artifact omissions, and allocations.
  Demonstrate semantic and exact-byte negative controls in final evidence.
  Record commands, runtimes, manifest provenance, screenshots, and first-
  divergence output. Acceptance: no credible gap remains and decomposition M0
  is unblocked. Validation, in order:
  `tools\validate_replay_visual_fidelity.bat`,
  `tools\validate_replay_scrub.bat`, `tools\validate_full.bat`, and required
  DX12 stress if DX12/tooling changed.

## V0 Closure Evidence — 2026-07-14

- Approved working base: commit `6a6ab4c65`, Profile x64, DX12, fixed step,
  `prediction_striker_ball`, 20-second horizon, fixed presentation start scene
  frame 900, and `ReplayFrameIndex` rows 0 through 2400 inclusive. The committed
  manifest records scene, script, config, and shader-tree SHA-256 provenance.
- The first draft exposed two false-determinism holes before baseline approval:
  it allowed the wall-clock reveal to advance before rewinding its cursor, and
  the shared replay visualizer deadline could omit the retained striker trail
  and target marker after prediction work consumed the budget. The final probe
  arms and holds reveal frame zero before target selection, starts once at the
  fixed presentation frame, records zero restarts, and pins only the completed
  automation reveal against the retained-refresh wall-clock skip. Production
  builders and submission buffers remain the compared path.
- Two clean processes matched all 2,401 rows, including ordered ordinary and
  priority line hashes, ordered ordinary and priority ribbon hashes, canonical
  marker diagnostics, expanded vertex hashes, byte/count fields, and the
  trajectory structure. All 200 authored wall bricks moved within the horizon.
- Negative control: mutation of `ticks[1200].ordinaryVertexHash` was rejected at
  that exact field. Incomplete control: a 2,400-row horizon was rejected before
  comparison. The gate never updates its baseline.
- V0 gate: `tools\validate_replay_visual_fidelity.bat` passed. The final run
  started at 00:06:22 and completed at 00:08:11 local time (about 109 seconds
  total); the Profile build reported 12.51 seconds, zero warnings, and zero
  errors, with the runtime/capture/comparison portion taking about 96 seconds.
- Secondary screenshots were inspected at held/building, mid-cascade, late,
  and full-horizon checkpoints under
  `TestOutput/validation/replay_visual_fidelity/`. They remain evidence, not the
  equality oracle.
- Touched-source comment audit: 6/6 checked, 0 deferred —
  `Core/MainMemoryStats.h`, `Runtime/Editor/RunEditorTracer.cpp`,
  `Runtime/InteractionAutomationController.cpp`,
  `Runtime/InteractionAutomationController.h`,
  `Runtime/Replay/ReplayRuntime.h`, and
  `Runtime/Replay/RunReplayTools.cpp`. Learning headers were retained and the
  new reveal, exact-buffer, scheduling, and test-only ownership invariants are
  documented beside the affected code.

## V1 Closure Evidence — 2026-07-14

- `ReplayVisualPacket` is the replay-owned frame-local seam published once
  before render-graph execution. Prediction ghosts and debug line/ribbon
  submission both consume that same packet; automation reads it after render.
  The former direct tracer submission getter and direct ghost-request accessor
  were removed, so production and validation cannot select different sources.
- The packet borrows ordered trajectory records and published points, causal
  future nodes, retained entry/rest/horizon markers, prediction ghost requests,
  camera inputs, trajectory drop/budget diagnostics, replay reserve-growth
  evidence, ordinary/priority line and ribbon streams, and expanded ribbon
  vertices. Comparison walks typed semantic fields first and bit-exact ordered
  float streams second.
- Focused packet tests passed 5/5 cases and 26/26 assertions, including semantic
  precedence, causal-node reordering, exact ghost components, lane ordering,
  and truncated buffers. `tools\validate_tests.bat` then passed 186/186 cases
  and 4,111/4,111 assertions with 74/74 project/filter items.
- The mandatory single-process `tools\validate_replay_visual_fidelity.bat`
  run started at 00:40:18 and wrote its final report at 00:42:08 local time
  (about 110 seconds). The Profile build reported zero warnings and zero errors;
  all 2,401 reveal ticks matched, all 200 bricks moved, and the exact mutation
  plus incomplete-horizon controls failed as intended. No second scene process
  was launched.
- Touched-source comment audit: 9/9 checked, 0 deferred —
  `Runtime/Editor/RunEditorTracer.cpp`,
  `Runtime/InteractionAutomationController.cpp`,
  `Runtime/Render/RuntimeRenderPasses.cpp`,
  `Runtime/Render/RuntimeRenderer.cpp`, `Runtime/Replay/ReplayRuntime.cpp`,
  `Runtime/Replay/ReplayRuntime.h`, `Runtime/Replay/ReplayVisualPacket.h`,
  `Runtime/Tools/RuntimeTools.h`, and
  `SkullbonezTests/TestReplayVisualPacket.cpp`. Existing learning headers were
  retained; packet lifetime, single-publication order, and typed diagnostic
  vocabulary are documented beside the new seam.

## Dependencies And Decisions

- This plan runs first on `nightrunner-13th-july` from the working replay base.
- `replay-monolith-decomposition.md` is blocked until V0-V6 are complete and
  the final mega probe passes from the exact decomposition starting state.
- The mega probe is a permanent product invariant, not migration scaffolding.
- Prediction and live use the production presentation builder but different
  source state; the golden manifest independently detects a shared-builder
  regression.
- World-space CPU presentation bytes are primary. DX12 screenshots and
  InfoQueue checks prove the downstream path but do not replace exact packets.
- Runtime allocation rules remain in force; artifact IO remains a cold lane.

## Validation Contract

Every implementation task V0-V6 ends with
`tools\validate_replay_visual_fidelity.bat`. V0 creates and runs it before V0
may be checked. No task lands without the command existing and passing. Any
failure reopens the current task; no task refreshes the manifest to obtain a
pass.

## Definition Of Done

- Every retained simulation tick from seed through the approved horizon is
  compared for the full 200-box cascade.
- Golden-base, predicted/live, and save/load/scrub equality all pass.
- Every replay-owned visual lane and exact CPU submission byte is covered.
- Diagnostics identify the first differing tick and field/byte.
- Negative controls prove semantic, temporal, structural, and byte detection.
- The command is permanent, documented, replay-gated, and mandatory for every
  replay decomposition task.
