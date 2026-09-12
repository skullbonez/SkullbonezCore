# PR 169 adversarial review — 2026-09-12

Scope: `origin/main...codex/unified-ui`, including the subsequent editor velocity change at `2e0f407a9e85eb2edb08bc7dbc815ccadc6984c9`. This is a separate critique pass in the same Codex session, not an independent reviewer process. The complete changed-file inventory is `TestOutput/pr169-review-files.tsv`.

## Findings

1. **Fixed — Modified path loss in large scenes.** `Runtime/App/ReplayPredictionPresentation.cpp` reserved a renderer range per body per branch. The renderer supports 4,096 ranges but a scene supports up to 8,192 bodies. Original consumed the range slots before Modified could cover the scene. Native reproduction with 3,000 moving bodies drew Original for all 3,000 but Modified for only 1,096. Each branch now shares one range and retains its existing half of the 24,000-record budget. No renderer or allocation capacity increases. A native regression reads actual compact records/ranges, checks both branches against every fixture body, checks selected/published/submitted target identity, and checks ghost coverage. The corrected build covers all 3,000 bodies in both branches.

2. **Fixed — stale editor-entry regression and missing gate coverage.** `validate_velocity_divergence.py` still required Edit to be blocked during an experiment. The later user requirement makes Edit cancel the experiment and restore authored frame zero. The divergence test now checks Delete protection outside Edit; `validate_editor_velocity.py` checks cancellation, frame-zero restoration, unsaved placement, undo/redo and save/reload. Both it and the new capacity regression are in the UI gate. The full catalog regression also used the old quick-object grid offset; its clicks now account for the two added velocity rows. All 39 catalog choices, 24 quick-object buttons and 13 hold variants pass in both layouts. The current runtime reference now describes widget-only activation and the Original/Modified acceptance labels.

3. **Fixed — pointer regression during camera return.** The native pointer test projected handles using the primary camera destination while the rendered camera was still tweening after experiment cancellation. It now consumes the recorded rendered camera. The corrected run captures an active tween with primary X=500 and rendered X=511.518, successfully edits the visible handle, interrupts production, and completes Solver Lab/cancellation checks.

4. **Unresolved approval — intentional 200-box oracle transition.** The immutable replay/causal oracle differs because `SeedModelAsleep` now retires a body's queued joint-construction wake. This is necessary for placed and loaded sleeping ragdolls to remain asleep. A controlled isolated build that removes only that retirement passes the entire existing oracle and all negative controls. The corrected sleep behavior retains all 2,401 prediction frames and all 200 moved/affected bodies, but changes first causal body 91 to 70, sustained toppled count 192 to 185 and final settled count 200 to 198. Existing before/after editor-velocity reports agree exactly, and the durable artifact round-trip passes. No canonical baseline has been replaced. Candidate files and exact hashes are under `TestOutput/pr169-baseline-review/`. This remains a branch merge-readiness limitation, not an unexplained prediction-completion failure.

## Reviewed responsibilities

- UI: shared draw clipping and popup ordering, panel transitions and pointer exclusion, startup viewport restoration, F5/F6 containment, causal seek/drag ownership, held transport speed, editor and replay routing.

- Velocity experiments: widget-only activation, delayed allocation, seed identity checks, asynchronous release and re-grab, pending original production, complete Modified publication, ghost identity, repeated edits, acceptance/cancellation, scene replacement, and Solver Lab capacity/import lifetime.

- Editor: authored body restoration at frame zero, fixed-body rejection, velocity history and persistence, unsaved placement retention, input capture, terrain/placement mode exclusion, and cancellation of an existing comparison.

- Terrain/DX12: first-stroke preparation, worker cancellation before collision mutation, mesh upload ownership, BLAS scratch and vertex-buffer lifetime, scene replacement, flat defaults, imported maps, immutable content-based height-map saving and atomic scene replacement.

- Ragdolls/physics: pose anchors and joint placement, authored sleep versus queued wakes, later impact/topology wake, mixed-body sleep display, speculative distance/rotational bounds, contact admission and friction/resting policy, and preserved deterministic test evidence. This review does not claim all dense piles settle completely.

- Validation/governance: immutable oracle and negative controls, artifact round-trip, compiler-backed source checks, dependency direction, renderer-neutral values, existing reserve capacities and ownership, mandatory commit hooks, and the current hosted matrix.

## Ownership questions

1. Aggregate ownership: pending velocity values bind target identity, the latest requested vectors and release state across asynchronous Original production. Divergence values retain the choice/playback state. History snapshots bind identity and complete before/after values. These are retained or presentation values, not borrowed-owner wrappers destructured by a sole apply function.

2. Capability slices: App composes full Physics, Prediction and Planning owners; lower consumers receive their existing value views. New diagnostic fields serialize compact render records without granting mutation. No new callback bag or alternative input owner was added.

3. Incomplete extractions: compiler source checks found no member-prefixed locals, pure parameter aliases or immediate parameter-struct unpacking in the checked changes.

4. Rename evasion: the changed interfaces do not replace a deleted context with an equivalent renamed parameter bundle. The shared-range fix removes redundant range reservations rather than moving them into another wrapper.

5. False claims: the runtime reference incorrectly described widget activation and has been corrected. Reviewed source sequencing matches the worker join before terrain mutation, delayed comparison allocation, and release-controlled Modified scheduling. An outdated Editor history header owner label was corrected.

The new changes do not introduce the combined extrusion signal of three sibling operand structs, a wide apply function and arbitration comments. Existing editor pointer request/result values predate this branch; velocity and terrain extend its retained editor state instead of adding another such family.

## Evidence

- Final full UI gate: PASS, 707.000 seconds, `TestOutput/pr169-terminal-ui.log`. All 1,044 CPU tests pass with one existing skip; every native case, DX12 check and ready-build restoration passes.
- Final corrected-source physics gate: PASS, 36.360 seconds, `TestOutput/pr169-terminal-physics.log`. Staged fingerprint `7427217724bb5a9aae30f47d316358dabf62fa58458751d4047726c90daf2ae6`; physics golden unchanged.
- Final corrected-source replay gate: FAIL only at the known unapproved causal transition, 304.687 seconds, `TestOutput/pr169-terminal-replay.log`. This failure remains visible rather than replacing its oracle.
- The final corrected-source report matches both reviewed candidate baselines and passes all negative controls and artifact round-trip checks: `TestOutput/pr169-baseline-review/final-validation.log`. This does not authorize installing the candidates.
- Renderer: PASS, 44.875 seconds. One-minute graphics stress: PASS, 70.812 seconds including cleanup; 60.175 seconds sampled and the exact owned process was stopped after graceful timeout. Logs: `TestOutput/pr169-terminal-renderer.log` and `TestOutput/pr169-terminal-stress.log`.
- Native terrain, sleeping-ragdoll and dense 200-box checks: PASS, 17.437 / 25.766 / 101.875 seconds respectively, in `TestOutput/pr169-native-results.json`.

- `TestOutput/pr169-capacity-before.log`: failed with Modified coverage 1,096 / 3,000, using the old path implementation plus the new read-only observation fields.

- `TestOutput/pr169-capacity-after.log`: both branches cover 3,000 / 3,000.

- `TestOutput/pr169-source-design.log`: compiler-backed check passes both changed C++ translation units, six contexts, no findings.

- `TestOutput/pr169-old-sleep-oracle.log`: full old-oracle pass after removing only pending construction-wake retirement in an isolated diagnostic copy. Root behavior was not reverted.

- `TestOutput/pr169-old-sleep-proof/`: controlled old-behavior report, replay artifact and observer executable. This is not claimed to be the historical golden's exact original producer.

- `TestOutput/editor-velocity-oracle-comparison.json`: no comparable visual or causal difference before/after editor velocity authoring.

- `TestOutput/pr169-status-latest.json`: all 13 hosted checks pass at `2e0f407a9`.

- Final local gate logs and timings: `TestOutput/pr169-final-*.log` and `TestOutput/pr169-native-results.json`.

Native input validation is serial. A clean serial run disproved the initial competing-process explanation for the pointer failure; the recorded rendered-camera mismatch above identifies the cause. Early capacity-fixture attempts exceeded the spatial-grid bucket limit or the 64 KiB live Skarness pipe. The final fixture keeps its bodies within grid limits and reads large packet evidence from the recorded stream while subscribing only to small frame-clock notifications.

An initial terminal replay run unexpectedly passed the old oracle because the isolated Automation build retained the diagnostic old-sleep object. Copying the correct source back had restored its older timestamp. That run is excluded from final-source evidence; its log is `TestOutput/pr169-stale-build-replay.log`. The corrected run explicitly recompiles `PhysicsSleepController.Wake.cpp`, with source bytes checked against the root. The isolated physics stamp was removed and its gate rerun as well. Root native UI, terrain, ragdoll and dense-comparison results did not use that diagnostic executable.

The unrelated untracked `SkullbonezData/scenes/asdasd.scene.json` remains untouched. Validation uses an isolated copy with independent Git metadata to exclude that user-owned file from the staged physics check.
