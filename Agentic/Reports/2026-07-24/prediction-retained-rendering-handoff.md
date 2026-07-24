# Prediction Retained Rendering Handoff

Date: 2026-07-24
Branch: `nightrunner-23rd-JUL-26`
Status: Merge-ready; final owner-visible confirmation remains

## Outcome

This branch retains replay prediction trajectory commands incrementally instead
of rebuilding the complete CPU-side ribbon stream each frame. The deterministic
visual-fidelity path remains the frame-local oracle and does not consume the
optimized retained packet.

The latest 200-box symptom is classified as a retained-rendering publication
problem, not missing simulation data. The worker had already written the
in-progress ball trajectory, but its trajectory record kept
`publishedPointCount == 0` until the committed record was rebuilt. The renderer
therefore had no root line to consume before completion/contact and then showed
the completed line at once.

The frame thread now publishes exactly the acquire-latched prediction
presentation prefix into the build-root trajectory record. It never exposes a
point beyond the worker's release/acquire frame prefix. Captures at interaction
frames 100 and 130 show the selected ball line growing progressively toward the
wall before the first wall contact rather than popping in at contact.
Deterministic reveal bypasses this transient retained-prefix publication so its
frame-local visual packet and causal proof remain the unchanged golden oracle.

## Implemented Work

- Removed the cinematic transition when entering prediction mode.
- Replaced full-frame prediction ribbon rebuilding with retained compact
  trajectory records and fixed eight-segment continuation chunks.
- Preserved canonical draw order separately from stable physical cache slots so
  command sorting does not invalidate unchanged DX12 uploads.
- Repaired cross-chunk previous/next adjacency while invalidating only the
  bounded neighboring chunk.
- Kept stable completed prediction frames on the O(1) revision path: no source
  traversal, command rebuild, sort, or geometry upload occurs when publication
  is unchanged.
- Kept the deterministic visual-fidelity renderer independent from retained
  output so the optimization cannot approve its own golden.
- Prevented the 200-box scene from nondeterministically selecting Instant build
  mode from its cheap pre-contact probe. Predictions above 64 bodies always use
  amortized work.
- Restored manual replay target picking while the expanded Legacy UI is visible.
  A visible native cursor no longer implies that UI owns a click; actual UI hit
  suppression remains authoritative.
- Published the in-progress root trajectory prefix from the frame thread so the
  striker line grows before impact.

## Evidence

- Focused retained-range canonical geometry: PASS, 9 assertions.
- Focused retained continuation adjacency: PASS, 7 assertions.
- Focused DX12 repaired-suffix upload planning: PASS, 9 assertions.
- Focused prediction scheduling: PASS, 6 assertions.
- Focused build-root prefix policy: PASS, 3 assertions.
- Repeated 200-box product probes selected `Amortized`, reported zero future-tree
  drops, and reported zero dropped trajectory segments.
- Expanded-Legacy-UI click probe: PASS; prediction remained enabled and
  `prediction_striker_ball` became the replay path target.
- Early root-prefix visual probe: PASS; frame 100 and frame 130 captures contain
  a progressively longer pre-impact ball line.
- Debug test and Automation product builds: PASS.
- Dependency-direction proofs and replay downward-include proof: PASS.
- Touched-source comment audit: complete, zero deferred.

## Commit Gate

- `tools\validate_replay_visual_fidelity.bat`: PASS in 425.2 s; one engine
  process, one prediction generation, one presentation, 2,401 ticks, all
  positive checks, and all negative/false-pass controls.
- `tools\validate_dx12_renderer.bat`: PASS in 23.7 s; zero DX12 InfoQueue
  errors and all committed image baselines accepted.
- `tools\run_graphics_stress.bat 1`: PASS in 61.1 s; the bounded one-minute
  DX12 stress run completed without a crash.
- `tools\validate_full.bat`: PASS in 182.7 s; mandatory CPU preflight and all
  Automation, replay/prediction, DX12, and physics runtime lanes passed.
- Final dependency-direction and Replay downward-include proofs: PASS with no
  prohibited rows.
- Final touched-source comment audit: 22/22 complete, zero deferred.

## Merge Review

The first owner-visible check after merge should be:

1. Load `prediction_ragdoll_wall_200.scene.json`.
2. Enable Predict and select `prediction_striker_ball`.
3. Confirm the white root line grows continuously from the ball toward the wall
   during the approximately two-second pre-impact interval.
4. Leave the expanded Legacy UI open and confirm the ball remains selectable.
5. Let the prediction finish and confirm the completed path remains stable
   without flicker or recurring CPU-side prediction draw-list work.

If the pre-impact line still disappears in the owner build, investigate the
retained DX12 range visibility/submission state first. The physics frame and
trajectory point data are already present and acquire-published.
