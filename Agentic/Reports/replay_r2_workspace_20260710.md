# Replay R2 Workspace Ownership

Date: 2026-07-10
Branch: `engine-cleanup-10th-july`

## Closure result

Replay workspace behavior is owned by `ReplayRuntime`. The scoped replay source
inventory remains 24 tracked files and is now 24,350 physical lines. The 1,003
line increase from R1 is explicit owner code moved out of `Run`, including
workspace coordination, live restore, scene reset, startup workflows, and probe
transactions; no replacement compatibility module or forwarding `Run` method
was added. R5 still owns file-size closure and the remaining `RunReplay*`
filename cleanup.

`ReplayRuntime::TickWorkspace(const ReplayWorkspaceInput&,
ReplayWorkspaceOutput&)` is the one UI-frame entry point. It owns scrubber,
cause-tree, velocity-edit, prediction-horizon, inspection-camera, gesture, and
mouse-consumption decisions. Replay overlay producers emit into the existing
fixed-capacity tracer records through replay-owned methods.

Live scrub restore is a typed two-step owner command:

1. `ReplayInteractionController` builds `ReplayLiveRestoreRequest`.
2. `ReplayRuntime::ApplyLiveRestoreRequest` selects the solver or v2 transaction,
   verifies hashes, rolls back on failure, publishes scrubber status, and emits
   only the application-mode result the shell must sequence.

`ReplayLiveWorld` is a frame-scoped cold-restore borrow, never stored. Owner is
the replay subsystem; the reason is that R2 must centralize restore ordering
before R4 narrows live access; deletion condition is R4 replacement of the broad
model collection with body, collider, render, and scene views. The source
comment records that condition. No replay owner header stores `Run*`, `Run&`,
`void*`, a callback pack, or Run friendship.

Replay startup load/probe sequencing and frame-driven scrub/restore/save probes
now live on `ReplayRuntime`. `Run` constructs the frame-scoped owner view,
sequences one replay call, and preserves any returned Lane R failure in
`ApplicationExitState`.

## Deletion proof

The final source checks proved:

- `Run.h` has zero `TickReplay*`, `RenderReplay*`, replay restore/hash,
  replay-camera, replay-probe, replay-load, or replay-timeline business methods.
- Runtime source has zero replay-named `Run::*` method definitions.
- Replay owner headers have zero `Run*`, `Run&`, `void*`, or Run friendship.
- The three replay UI callback parameters were deleted from
  `ApplyRuntimeUIFrameCommands`; it calls `TickWorkspace` once and returns the
  typed output.
- `m_replayProbes` moved from `Run` to `ReplayRuntime`; the owner sequences all
  three frame probes through `TickProbes`.

## Behavioral coverage

`TestReplayRecorder.cpp` now locks the pure replay scene-reset decisions:
ordinary reset clears branch metadata, live-branch reset preserves it,
generated-scene flags retain exact/UI count provenance, and authored scenes
without generated solver counts do not synthesize a generated-config event.
The production scrub, v2, interaction, physics, and renderer gates exercise the
cross-owner transactions that the lightweight doctest executable does not link.

## Validation evidence

| Command | Result | Time |
|---|---|---:|
| `tools\validate_build.bat Debug` | zero-warning Debug build | 9.8s |
| `tools\validate_tests.bat` | 121/121 cases, 2,640 assertions | 5.7s |
| `tools\validate_all_cpu_tests.bat` | doctest, interaction policy, scene parser, and DX12 architecture lanes passed | 11.2s |
| `python tools\check_allocation_policy.py --self-test` | synthetic cases passed | included below |
| `python tools\check_allocation_policy.py --repo .` | 302 files, 0 allowlist errors | 7.3s combined |
| `tools\validate_replay_scrub.bat` | scrub, restore, prediction determinism, and geometry submission passed | 81.5s |
| `tools\validate_replay_v2_artifact.bat` | save/load/checkpoint/target/branch/failure probes passed | 25.2s |
| `tools\validate_interaction_clicks.bat` | inspect and replay-prediction reports both `ok=1` | 8.8s |
| `tools\validate_physics.bat` | standalone/runtime-handle smoke and 20,001-line byte-exact baseline passed | 12.9s |
| `tools\validate_dx12_renderer.bat` | 0 InfoQueue errors and all screenshots matched | 41.5s |
| `tools\validate_full.bat` | CPU umbrella, DX12, standalone physics, and byte-exact physics all passed | 52.3s |

The first renderer attempt stopped at the formatting preflight. Clang-format
and the repository header-alignment post-pass were applied only to touched
files; the final renderer and full gates both passed from the formatted source.
Logs are under `TestOutput/validation/replay_r2_*.log`.

## Comment audit

The touched-file comment-style audit checked 21/21 source-bearing files with no
deferrals: `Init.cpp`; `ReplayInteractionController.cpp/.h`;
`ReplayRuntime.cpp/.h`; the six `RunReplay*` implementation files;
`Run.cpp/.h`; `RunFrame.cpp`; `RunInput.cpp`; `RunInteractionAutomation.cpp`;
`RunLaunchOptions.h`; `RunReplayProbeState.h`; `RunStress.cpp`; `RunScene.cpp`;
and `TestReplayRecorder.cpp`. Stale Run-callback/compatibility descriptions were
rewritten around the typed workspace, restore transaction, probe owner, and R4
deletion condition. This was a bounded touched-file audit, so no subsystem
checklist plan was required.

No SkullScope trace was generated or queried for R2.
