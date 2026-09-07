# Physics A/B Comparison with Causal Differences

Owner: Runtime/Planning; authorized by the user on 2026-09-07.
Status: PHYSICS_AB 0/6. PR branch: `codex/physics-baseline-acceptance` (#167).

## Accepted behavior

Capture identical scene inputs with two existing executable builds through
Skarness, sequentially, with fixed ticks and immutable results. Default duration
is 20 seconds. Keep executables in their normal runtime directories. Record
build hashes, scene/assets, actions, settings, logs and completeness. Publish
only complete bundles; retain failed evidence. Missing diagnostics are not zero.

One native viewer presents split, cyan-outline overlay, instant A/B toggle,
position/rotation heatmaps and absolute RGB pixel differences. Both sides use
one freely controlled world-space camera and tick cursor. Orbit/pan/zoom work
while paused and inspecting contacts; event selection never reframes the camera.
Focus frames both poses; optional follow uses A as the common pivot. Support
reverse/forward playback, speed, exact stepping, timeline dragging and looping.

The causal pane offers selected objects and all differences. Match contacts by
stable body IDs, family and compatible feature identity. Ambiguous/unmatched
events remain explicit. Compare available manifolds, impulses, velocities,
sleep and iterations. Candidate timing shifts cannot retime the recordings.
Plots show selected height and vertical velocity. Save/reopen complete findings
including object/event, time/range, camera, settings and notes.

## Boundaries

Planning owns comparison state, metrics, matching and composition. Replay owns
artifact decoding/capture. Rendering receives generic independent presentation
inputs and owns offscreen resources/compositing. The viewer never restores an
old solver checkpoint into the current solver. Neither solver nor any accepted
baseline changes. InputRouter remains the sole retained input owner. Loading
preflights combined memory and never silently drops frames or diagnostics.

## Execution and acceptance

- [ ] AB1: capture orchestration, immutable bundle and compatibility validation.
- [ ] AB2: native recording owner, tick transport, motion and causal differences.
- [ ] AB3: independent render inputs, five display modes and camera integration.
- [ ] AB4: setup/inspection UI, plots, findings and Skarness observations.
- [ ] AB5: identity, known-change and failure fixtures; native A/A and FP6/current wall.
- [ ] AB6: source/dependency/replay/graphics validation, independent review and closure.

AB5 proves A/A equality, small hop and rotation, contact identity permutations,
missing diagnostics, shifted contacts, repeatable seek and finding restoration.
Capture diagnostics must not change producer physics. Required final commands
include `tools/validate_fast.bat`, focused comparison tests, Skarness native
comparison validation and `tools/agent_validate.bat --plan-completion`.
No image or physics golden updates are authorized by this feature.

## Work state

The five pre-existing Skarness sleep-query files belong to earlier work in this
conversation and are preserved. `.claude/` is unrelated and untouched.
The live work ledger refused PHYSICS_AB because an unrelated GOV1 session owns
its active task. Preserve that ledger; use actual command logs/timestamps and
do not invent token counters or pricing. Ragdoll FP8/FP9 remain pending while
the owner-requested comparison feature takes priority.

## PR implementation checkpoint (2026-09-07)

PR #167 includes the existing native viewer and capture tool together with the
previously committed causal, cleanup and FP0-FP7 work. All six acceptance tasks
remain open: this commit is reviewable progress and does not claim full closure.
Implemented paths cover five linked view modes, both split orientations, object
orbit/WASD free camera, persistent timeline drag, selected/all contact comparison,
plots, findings, complete-bundle publication and asynchronous loading/cancellation.
Capture accepts existing executables, checks archived assets against each runtime
root, hashes evidence and records unsupported diagnostic availability explicitly.
The isolated no-ragdoll wall fixtures use the same post-current-ragdoll ball state.

Legacy capture starts at physics tick 1 and explicitly records tick zero as
unavailable. Tick-zero capture, cross-tick timing-shift candidates, the full
producer diagnostic non-interference matrix, richer diagnostic presentation and
terminal plan gates remain unfinished. Current left/right exact stepping and
inspection do not imply those missing acceptance requirements have passed.

Memory stays within the existing 512 MiB comparison budget. Loading charges both
recordings, retained prior inspection, decoded rows, transient readers and event
capacity; diagnostic arrays reserve exact counts before filling. Replay count
validation prevents small malformed files from allocating count-sized storage.
No PhysicsBodyRecord field, downward include or Replay growth registration is
introduced. Baselines and solver code are unchanged by this implementation slice.
