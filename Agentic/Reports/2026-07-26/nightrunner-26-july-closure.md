# Nightrunner 26 July Closure

Date: 2026-07-26
Branch: `nightrunner-26th-JUL-26`
Plan: completed at 3/3; live TODO deleted under master-plan inventory rule 4
Closure commit: the commit containing this report
Status: **complete**

## Outcome

The three owner-directed tasks are complete.

1. Replay scrub playback now reuses a dense solver reconstruction until the
   retained offset or recorder content revision changes. Availability checks
   are metadata-only, frame selection shares the selected sample, and world
   reconstruction writes directly into its destination.
2. The repository now records and mechanically applies the owner's code style:
   top-of-function preconditions where practical, blank space after assertion
   blocks and around control-flow blocks, a blank line above comments, compact
   one-to-three parameter lists, first arguments beside opening parentheses,
   aligned continuation arguments, a 125-column soft limit, and
   pointer/reference or complex parameters before primitive values unless a
   concrete API reason overrides that order.
3. Space-scene velocity dragging now redraws only the selected body's
   provisional path from the accepted velocity delta. Other committed paths
   remain stable, held samples start no complete prediction, release requests
   exactly one authoritative replacement, and the provisional line remains
   visible until that generation commits.

The first two task commits are `e991c175` and `cc432a4a`. The closure commit
contains N26-3 and this final evidence.

## Replay Spike Findings And Disposition

| Spike | Cause | Disposition |
|---|---|---|
| Repeated scrub sample resolution | Input, render, overlay, and UI paths independently asked for the same normalized solver sample | cache the dense value by retained offset and recorder content revision |
| `ApplySolverWorldDeltaFrame` vector assignments | Complete persistent-contact and solver vectors were reconstructed for each query, producing the visible `std::vector`/`memcpy` spans | perform the reconstruction once per stable sample and share the selected value |
| Scratch-to-world snapshot copy | A complete temporary world snapshot was built and then copied again into the output world | reconstruct directly into the destination |
| Pause/availability query | `IsScrubPaused` decoded a complete solver sample merely to answer whether one existed | answer from retained recorder metadata |
| Multiple consumers in one frame | Presentation and operator paths selected the same sample separately | select once at the frame boundary and pass the shared pointer through the existing narrow boundary |

Complete solver state remains present for restore/export correctness. The
change removes redundant reconstruction and copies rather than narrowing the
authoritative snapshot.

## Velocity Preview Design

`ReplayAuthoringPredictionRequest` carries fixed-size update and finish
commands. Accepted held samples mutate the selected body's velocity and replace
the queued preview delta, but do not dirty Prediction. Prediction owns the
preview lifecycle and publishes a narrow immutable view. Drawing omits the
selected retained root and redraws only its display-stride points with
`committedPosition + velocityDelta * elapsedSeconds`; non-selected retained
geometry is untouched.

Finishing a changed drag arms the preview with the replacement generation and
queues one refresh. Normal mouse release, lost rays, invalid targets, input
clear, and the direct ALT-VEL toggle-off path use the same finish operation.
The preview clears only when the armed authoritative generation commits.

The instant and amortized automation scenarios prove visible held feedback,
zero superseded held-drag restarts, one release replacement, and preview
persistence while that replacement is in flight. Captures are under
`TestOutput/interaction/`.

## Independent Review

The whole campaign received one independent rubber-duck review. It found one
blocking seam: the direct Replay scrubber button consumer manually copied only
part of `ReplayAuthoringPredictionRequest`, so toggle-off could drop the
velocity-preview finish command.

Prediction now owns `ApplyAuthoringRequest`, and both ReplayRuntime and the
direct scrubber path pass the complete request through it. The amortized
click-off scenario exercises that exact consumer. The reviewer rechecked the
repair and reported no blocking code findings. The remaining non-blocking
observation is that non-selected geometry stability is source- and
capture-evidenced rather than byte-compared.

## Comment Audit

The completed plan checklist covered 24 meaningful source/tool files from
N26-1 and N26-3 plus the N26-2 formatter implementation.

Checked: 24/24. Deferred: 0. Unchecked: 0.

The repository-wide N26-2 whitespace migration was mechanical and excluded
from semantic comment review. Every checked file was inspected against
`Agentic/Reference/comment-style-guide.md`; stale held-drag ownership comments
were corrected and dense lifecycle code retains the guide-required nearby
invariant and rationale comments.

## Validation

| Gate | Final result |
|---|---|
| N26-1 focused Replay case | pass; 49 assertions cover cache reuse and mutation invalidation |
| N26-3 focused owner-request cases | pass; 2 cases / 24 assertions |
| `tools\validate_alt_velocity_visualization.bat` | pass; instant and amortized held/release behavior |
| `tools\validate_format.bat` and formatter self-tests | pass |
| `tools\validate_fast.bat` | pass |
| `tools\validate_replay_allocation_policy.bat` | pass |
| `tools\validate_dependency_graph.bat` | pass |
| `tools\validate_replay_scrub.bat` | pass; 17 cases / 75 assertions, 2,401 ticks, every positive and false-pass control |
| `tools\validate_perf.bat` | pass in 58.55 seconds |
| `tools\validate_full.bat` | pass; 402 doctests, Automation/Replay, zero-error DX12, accepted baselines, byte-exact 44,401-line Physics regression |
| `tools\run_graphics_stress.bat 1` | pass in 60.83 seconds; crash-free PID-scoped stop, descriptor-churn proof, empty stderr |

The first final full-gate attempt exposed stale generated shader-reflection
formatting inherited from the branch baseline. Running the canonical shader
baker changed only `SkullbonezData/generated/GeneratedShaderReflection.h`;
`bake_shaders.bat --check` then passed for all 43 stages with DXC 1.8.2502.11.
No shader bytecode, visual baseline, replay artifact, physics baseline, schema,
or allocation/dependency policy was refreshed.
