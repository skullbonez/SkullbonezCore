# Replay Visual Fidelity Mega Probe — Frame-Exact 200-Box Prediction Proof

Date: 2026-07-13
Status: Complete — 7/7 tasks complete; single-generation closure approved
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
differs by any compared bit from the approved working baseline or the durable
state reconstructed from that same generation. It always runs the full 200-box
scene, never a reduced substitute.

The proof has two independent oracles:

1. **Golden working-base equality.** Every observed presentation tick matches a
   committed digest manifest captured before refactoring.
2. **Durable single-generation equality.** The saved RVIS rows and RVPD state
   reconstruct the same packets in a non-presenting CPU verifier without
   advancing the live scene or starting prediction again.

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

Typed fields are compared first. The in-memory comparator then walks packet
floats bit-exactly and reports the first changed element. The bounded committed
golden stores ordered bit hashes, byte lengths, and counts for every renderer-
bound packet span; capture recomputes those facts from the spans themselves and
rejects stale tracer telemetry. Full packet dumps remain bounded failure
artifacts because retaining every cumulative stream would be unbounded in
practice. A hash without the typed rows, structural facts, seam assertion, and
one-micron controls is insufficient.

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

## Whole-Wall Completion Definition

For this fixture, `toppled` means more than half of the 200 authored wall bricks
are directly on the flat y=0 terrain and engine-sleeping throughout the final
prediction second. Ground contact uses each box's oriented vertical support
extent and the physics contact tolerance, so a sleeping brick perched on another
brick does not count. The approved base has 187 grounded sleepers. The gate also
requires all 200 bricks to have moved, all 199 downstream causal nodes to reveal,
and all 200 bricks to remain settled within the one-micron final-second bound.
The former 175-brick fall/rotation diagnostic is not the meaning of `toppled`.

## Tasks

- [x] **V0 — Freeze the working base and land the first complete gate.** Record
  the approved pre-refactor commit and all provenance. Build an end-to-end
  Profile fixed-step probe that selects `prediction_striker_ball`, freezes seed
  T, completes a bounded prediction horizon while live advance remains paused,
  presents the causal reveal once, records every compared `ReplayFrameIndex`, and writes a
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

- [x] **V2 — Prove the full causal cascade tick by tick.** Retain predicted
  packet and causal envelope for every T+k in the sole generation. Assert the
  striker path appears first,
  downstream activation is monotonic through the causal topology, revealed
  paths remain present, and marker transitions occur on exact ticks. Cover
  collision onset and the full cascade, not only the settled tail. Negative
  controls shift one activation tick, alter parent/depth, and remove a segment.
  Acceptance: each fails at its injected first divergence. Validation:
  `tools\validate_replay_visual_fidelity.bat`.

- [x] **V3 — Round-trip durable replay presentation.** Extend the
  `ReplayPresentationSample` visual seam, delta capture/hash, and artifact
  serialization with the minimum domain state required to reproduce the packet;
  never serialize DX12 resources or pointers. Any schema change follows the
  versioned-migration policy with integer bump, deterministic migration,
  upgraded fixtures, and legacy/current/future/writer tests. Save the 200-box
  replay, retain every per-tick visual row, and reconstruct its typed prediction
  state in a non-presenting CPU verifier. The verifier deserializes into temporary
  domain values and must reserialize byte-exactly; it must not launch
  `SKULLBONEZ_CORE.exe` or submit a second visual pass. Acceptance: the sole
  captured prediction matches the golden, saved RVIS rows match every captured
  row, and
  loaded RVPD state round-trips exactly. Validation:
  `tools\validate_replay_visual_fidelity.bat`,
  focused artifact tests, and `tools\migrate_data_formats.py --check` when
  applicable.

- [x] **V4 — Close timing, determinism, and false-pass holes.** Run exactly one
  prediction generation and compare its ordered per-tick manifest with the
  immutable approved working-base manifest. Later checks may only parse and
  reconstruct the saved artifact into temporary CPU values without launching
  the engine or presenting another visual pass; prediction entry points are
  forbidden there. The gate
  fails immediately if it detects a second engine launch or prediction
  generation. Pin fixed step, seed, reveal-frame mapping, camera
  input, worker completion, event cursor, scene/config input, and horizon.
  Reject render-frame/wall-clock comparison. Negative controls cover seed
  mismatch, missing tick, event mutation, non-fixed step, truncated horizon,
  record reordering, vertex-byte change, dropped geometry, reserve growth, and
  attempted duplicate generation. Acceptance: the sole engine run matches the
  frozen baseline, the offline artifact/state reconstruction matches that generation, and
  every false-pass control fails. Validation:
  `tools\validate_replay_visual_fidelity.bat`.

- [x] **V5 — Make the probe a permanent repository gate.** Register scripts,
  interaction assets, manifest, and any CPU target in project filters,
  `tools\README.md`, `validate_select.bat`, and the mandatory CPU umbrella when
  applicable. Make `tools\validate_replay_scrub.bat` invoke this command rather
  than retain a weaker parallel oracle. Update `AGENTS.md` file-to-gate mapping
  so replay source, test, artifact, or presentation changes require it.
  Acceptance: one authoritative command exists and nested failures propagate
  nonzero. Validation: `tools\validate_replay_visual_fidelity.bat`, the changed
  validation script, and `tools\validate_all_cpu_tests.bat` if applicable.

- [x] **V6 — Adversarial closure and decomposition handoff.** Complete the
  touched-file comment audit and an independent review for omitted lanes,
  shared-builder false positives, self-updating baselines, vacuous comparison,
  render/simulation tick confusion, artifact omissions, and allocations.
  Demonstrate semantic and exact-byte negative controls in final evidence.
  Record commands, runtimes, manifest provenance, screenshots, and first-
  divergence output. Acceptance: no credible gap remains and decomposition M0
  is unblocked. Validation, in order: invoke
  `tools\validate_replay_visual_fidelity.bat` exactly once; verify the scrub
  alias without launching the engine via its synthetic propagation/static
  delegation controls; then run `tools\validate_full.bat` and required DX12
  stress if DX12/tooling changed. Calling both the mega command and its normal
  scrub alias is forbidden because that would generate prediction twice.

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

## V2 Closure Evidence — 2026-07-14

- The approved reveal remains one generation: its cursor advances exactly from
  0 through 2400 and any duplicate, rewind, skip, or post-Play prediction build
  fails automation immediately. The long gate now creates one real DX12 window
  without showing it, so mandatory per-task reruns no longer replay the scene on
  the operator desktop.
- The causal manifest binds target `ReplayBodyId` 1, all 199 downstream nodes,
  parent/depth relationships, exact first-activation frames, monotonic revealed
  record/point/segment counts, marker transitions, ghost counts, and the stable
  active-topology hash to the unchanged V0 visual baseline.
- After the final reveal screenshot, automation uses the real pause/play UI path
  to freeze the committed prediction and advance live physics once. All 2,401
  later solver body packets matched the corresponding predicted packet bit for
  bit across ordered identity, model row, position, orientation, and linear
  velocity. The source was solver frame 59 and the final matched frame was 2459.
- The owner-observed broken second prediction was reproduced while developing
  the proof: one pause-toggle click left prediction enabled and allowed a new
  worker generation. The final probe primes the toggle, supplies a release
  frame, clicks Play, and asserts that prediction is disabled with no worker
  building before accepting any live comparison row.
- Negative controls are non-vacuous and name their injected first divergence:
  activation shift at `topology[108].firstFrame`, parent/depth mutation at
  `topology[0].parentId`, and segment removal at
  `ticks[114].revealedSegmentCount`. The V0 exact-float mutation and incomplete
  horizon controls also still fail at their intended fields.
- Final `tools\validate_replay_visual_fidelity.bat` passed in about 143 seconds:
  15.52-second zero-warning Profile build, one hidden engine process through
  frame 6602, all 200 bricks moved, 2,401 exact visual ticks, 199 causal nodes,
  and 2,401 predicted/live ticks. The original V0 baseline was not changed; V2
  adds `replay_visual_fidelity_200_box_causal.json` bound to its SHA-256.
- `tools\validate_full.bat` also passed after closing inherited V1 formatting
  and project-filter omissions: CPU tests, zero-warning Profile/Debug builds,
  zero DX12 validation errors, screenshot comparisons, standalone physics, and
  the 44,401-line byte-exact varied physics baseline all passed.
- Touched-source comment audit: 11/11 checked, 0 deferred — `Init.cpp`,
  `InteractionAutomationController.cpp/.h`, `Window.cpp/.h`, and the
  mechanically formatted V1 files `RunEditorTracer.cpp`, `RuntimeRenderer.cpp`,
  `ReplayRuntime.cpp/.h`, `ReplayVisualPacket.h`, and `RunReplayTools.cpp`.

## V3 Closure Evidence — 2026-07-14

> **Superseded by the authoritative single-generation reclosure below.** This
> historical evidence launched a second engine process for reconstruction and
> therefore does not satisfy the owner's one-presentation requirement.

- The established `ReplayV2Artifact` file family now writes schema version 3.
  Its 80-byte body dictionary rows retain stable replay identity plus resolver
  hints, shape, mass, and fixed state; its 76-byte per-frame rows retain pose,
  linear/angular velocity, sleep/support/inhibition/contact flags, sleep-island
  visual id, contact count, maximum penetration, and normal impulse sum. No
  renderer resource, DX12 handle, pointer, or owner object enters the artifact.
- The reader accepts current v3, deterministically migrates previous v2
  pose-only rows, and rejects future v4. A focused mutation flips one saved
  linear-velocity bit and is rejected because every reconstructed v3 sample
  must reproduce its complete presentation-state hash before scrub can expose
  it. Writer/current, previous, future, and corrupt-visual tests all pass.
- The final 200-box artifact is 48,578,901 bytes with 2,460 samples covering
  frames 0 through 2459, 211 dictionary bodies, 2,460 solver hashes, 41 sparse
  checkpoints/cursors, and one event. All 2,401 approved live frames 59 through
  2459 reproduce the saved ordered packet hash and body count exactly.
- Prediction remains one 20-second generation. The recorder alone uses a
  21-second retention guard band because 2,400 intervals have 2,401 inclusive
  endpoints; without that extra recording second, the source endpoint would be
  evicted when the final packet arrived.
- `tools\validate_replay_visual_fidelity.bat` passed in 185.3 seconds. It used
  exactly one hidden Profile process to generate prediction, then one fresh
  hidden Debug process that only loaded/scrubbed the saved artifact. The gate
  reported 2,401 visual ticks, all 200 wall bricks moved, 199 causal nodes,
  2,401 predicted/live matches, 2,460 saved/loaded samples, and all semantic,
  topology, segment, exact-float, and incomplete-horizon controls rejected.
- `tools\validate_replay_v2_artifact.bat` passed in 76.4 seconds, including
  v3 write/load, deterministic v2 migration, future rejection, visual mutation
  rejection, saved restore/query lanes, and generated-topology restore.
  `python tools\migrate_data_formats.py --check` also passed all 39 authored
  files; the binary replay schema remains format-owned rather than an authored
  scene/asset schema.
- `tools\validate_full.bat` passed in 103.0 seconds: formatting and metadata,
  186 doctest cases with 4,111 assertions, every standalone CPU suite,
  zero-warning Profile/Debug builds, zero DX12 validation errors and matching
  screenshots, standalone physics, and the byte-exact 44,401-line varied
  physics baseline. This broad gate did not run prediction.
- Touched-source/tool comment audit: 9/9 checked, 0 deferred —
  `InteractionAutomationController.cpp`, `ReplayRecorder.cpp/.h`,
  `ReplayV2Artifact.cpp/.h`, `check_replay_v2_artifact.py`,
  `check_replay_visual_fidelity.py`, `replay_query.py`, and
  `validate_replay_visual_fidelity.bat`.

## V4 Closure Evidence — 2026-07-14

> **Superseded owner correction — 2026-07-13:** the gate below launched a
> second prediction process. The owner observed that second visual prediction
> was broken. That invalidates V3-V5 acceptance and all A/B evidence in this
> section, regardless of matching report hashes. V3-V5 are reopened. The
> replacement gate permits exactly one engine process and one prediction
> generation. A later prediction-disabled engine load is still a second visual
> pass and is forbidden; saved-state checks must be non-presenting CPU/artifact
> verification.

- The permanent gate now runs clean Profile processes A and B sequentially,
  hidden, and never concurrently. Each process retains V2's hard guard against
  a duplicate, rewind, skip, or post-Play generation. B cannot launch until A
  exits and independently passes the immutable V0/V2/V3 contract.
- The cross-process determinism projection compares all 2,401 scene/reveal
  mappings and raw visual submission fields, the full causal/live proof, pinned
  scene/script/config/shader inputs, authored seed 62929, 20-second horizon,
  quiescent worker/restart state, and zero trajectory-reserve growth. It also
  compares 2,460 exact presentation headers including fixed-step world flags
  and camera float bits, ordered body packet hashes, branches, events, all 41
  event cursors, every solver hash, and the complete artifact SHA-256.
- Clean-run artifacts A and B are byte-identical at
  `E7CDCABA666F822B064CAF5D9469FA8D7D095712F47F600AA16E357DCAC4ACBF`.
  Both runs independently report all 200 wall bricks moved, 199 causal nodes,
  2,401 predicted/live matches, and 2,460 saved/loadable samples.
- The first real A/B comparison correctly exposed an over-broad candidate:
  internal trajectory-record publication fingerprints can vary with worker
  completion order even when every renderer-facing record and byte is exact.
  V2 already classified that ordering as diagnostic rather than visual. V4
  therefore excludes only that internal fingerprint, retains its record/point
  counts, and detects actual renderer record reordering through ordered raw
  submission hashes and counts.
- Nine V4 in-memory controls all fail at their injected first fields: seed
  mismatch, missing tick, event mutation, non-fixed step, truncated horizon,
  visual record reordering, vertex-byte change, dropped geometry, and reserve
  growth. The five existing exact-float, incomplete-horizon, causal activation,
  topology, and segment controls continue to fail as well.
- Final `tools\validate_replay_visual_fidelity.bat` passed in 337.8 seconds:
  hidden A, then hidden B, exact cross-process comparison, one fresh hidden
  load/scrub-only process, and all fourteen controls. No process performed a
  second prediction generation and no prediction processes overlapped.
- `tools\validate_fast.bat` passed in 53.0 seconds with formatting, project
  filters, staged-size policy, zero-warning Profile/Debug builds, and the main
  test suite. Touched-tool comment audit: 3/3 checked, 0 deferred —
  `check_replay_visual_fidelity.py`, `replay_query.py`, and
  `validate_replay_visual_fidelity.bat`.

## V5 Closure Evidence — 2026-07-14

> **Superseded by the authoritative single-generation reclosure below.** The
> historical scrub invocation inherited the invalid multi-process visual
> workflow. Only the one-engine gate and static/synthetic alias checks below are
> current closure evidence.

- `tools\validate_replay_visual_fidelity.bat` is now registered in
  `tools\README.md` and `validate_select.bat` as the single authoritative replay
  presentation gate. `tools\validate_replay_scrub.bat` no longer owns a weaker
  trajectory-fingerprint/steady-window oracle; it delegates exclusively to the
  authoritative command and launches no additional process of its own.
- The historical scrub wrapper includes a no-engine
  `--prove-failure-propagation` control. It returned the synthetic nested exit
  code 37 unchanged, proving a child failure cannot be converted into a pass.
- The 200-box scene and both interaction scripts are registered in
  `SKULLBONEZ_CORE.vcxproj` and its filters. Project-filter validation passed
  with 668 project items, 668 filter items, and zero errors. The committed
  manifests and validation scripts remain governed through the documented
  validation registry; this task introduced no standalone CPU target, so the
  mandatory CPU umbrella required no new entry.
- `AGENTS.md` now makes validation rows cumulative and requires the mega gate
  for replay source, replay-facing presentation/submission, replay tests,
  artifact/interaction/manifest inputs, and the owning validation tools.
- `tools\validate_fast.bat` passed in about 49 seconds: formatting, project
  filters, staged-size policy, 186/186 main tests with 4,111/4,111 assertions,
  and zero-warning Profile/Debug ready builds.
- One invocation of the changed `tools\validate_replay_scrub.bat` exercised the
  authoritative command once and passed in about 300 seconds. Hidden A generated
  once and exited; hidden B then generated once and exited; the later hidden
  Debug process only loaded/scrubbed A's artifact. Both clean runs reported
  2,401 visual ticks, all 200 wall bricks moved, 199 causal nodes, 2,401
  predicted/live matches, and 2,460 saved/loaded samples. Cross-process equality
  covered all ticks, saved frames, and 41 event cursors, and all fourteen
  false-pass controls named their intended first divergence.
- Touched-tool comment audit: 2/2 checked, 0 deferred —
  `validate_replay_scrub.bat` and `validate_select.bat`.

## V3-V6 Authoritative Single-Generation Reclosure — 2026-07-14

- `tools\validate_replay_visual_fidelity.bat` launches exactly one engine
  process, requests exactly one prediction generation, and presents exactly one
  contiguous 2,401-tick cascade. The launcher guard reported
  `engine_processes=1`, `prediction_starts=1`, `presented_cascades=1`, and
  `nested_scrub_runs=0`. After presentation the same process enters an
  irreversible offline-verification mode; the RVPD reconstruction is CPU-only
  and has neither prediction-generation nor Present authority.
- The immutable result contains 2,401 RVIS ticks, 200 moved wall bricks, 200
  settled wall bricks, 187 toppled wall bricks, and all 199 downstream causal
  nodes. `Toppled` is not a displacement proxy: a brick must be directly on the
  y=0 terrain using its oriented support extent and physics contact tolerance,
  and solver-sleeping in every one of the final 121 samples. The permanent gate
  requires at least 101; the approved manifest binds the exact working-base
  result of 187.
- Schema 4 stores all 2,401 RVIS rows plus typed RVPD prediction state. The
  presentation fingerprint binds every active ordered trajectory record,
  causal topology row, marker, ghost, and exact renderer-bound span. The
  completed prediction worker bank begins at child ordinal 240, is never drawn,
  and is therefore excluded only from the visual fingerprint; the complete
  ordered store, including that inactive bank and its versions, remains bound
  by semantic telemetry. A focused mutation test proves inactive worker changes
  affect semantic telemetry but not visual bytes, while an active mutation
  changes both. The focused packet suite passed 15/15 cases and 67/67
  assertions.
- The final corrected mega gate passed after formatting and the broad gate in
  about six minutes. It rejected intentional divergence at the exact vertex,
  incomplete horizon, causal activation, topology, revealed segment, semantic
  packet, artifact byte, and RVPD byte. Ten deterministic controls also rejected
  seed mismatch, missing tick, event mutation, non-fixed step, truncated
  horizon, record reordering, vertex-byte change, dropped geometry, reserve
  growth, and attempted duplicate generation.
- `tools\validate_replay_scrub.bat --prove-failure-propagation` returned the
  expected synthetic code 37. Static inspection again proved the alias contains
  one engine command, one Predict action, and no nested scrub run; the normal
  alias was deliberately not launched because doing so would generate the
  visible prediction again.
- `tools\validate_replay_v2_artifact.bat` passed schema-4 writer/current,
  deterministic schema-3 migration, future-version rejection, corrupt visual
  state rejection, restore/query, and generated-topology lanes with zero-warning
  Debug/Profile builds. `python tools\migrate_data_formats.py --check` passed
  all 39 authored files.
- `tools\validate_full.bat` passed the mandatory CPU suites, zero-warning
  Profile and Debug builds, DX12 screenshot comparison with zero InfoQueue
  errors, standalone physics, and the byte-exact 44,401-line varied physics
  baseline. The final mega gate then passed again from that exact formatted
  decomposition starting state.
- Independent adversarial review found no blocking issue. It verified the one
  engine/one generation capability boundary, renderer-visible worker-bank
  selection, exact span coverage, non-vacuous controls, and grounded/sleeping
  topple definition. The touched source/tool comment audit is 21/21 checked,
  0 deferred; the inventory is the V3-V6 file set recorded in the closure
  report.
- Selected secondary screenshots are retained under
  `TestOutput/validation/replay_visual_fidelity/` at reveal-building, mid, late,
  and complete checkpoints. They supplement but do not replace the exact CPU
  packet oracle.

## Dependencies And Decisions

- This plan runs first on `nightrunner-13th-july` from the working replay base.
- `replay-monolith-decomposition.md` is unblocked: V0-V6 are complete and the
  final mega probe passes from the exact decomposition starting state.
- The mega probe is a permanent product invariant, not migration scaffolding.
- Live capture and non-presenting RVPD reconstruction use the production
  presentation builder with one frozen prediction; the golden manifest
  independently detects a shared-builder regression.
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
- Golden-base, causal-envelope, and durable save/load equality all pass without
  advancing the live scene.
- Every replay-owned visual lane and exact CPU submission byte is covered.
- Diagnostics identify the first differing tick and field/byte.
- Negative controls prove semantic, temporal, structural, and byte detection.
- The command is permanent, documented, replay-gated, and mandatory for every
  replay decomposition task.
