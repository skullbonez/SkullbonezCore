# Run Member And Include Shrink Map

Date: 2026-07-16
Branch: `nightrunner-15th-july`
Owner: runtime shell
Task: `run-member-and-include-shrink` T1-T2

## Current Measurements

At T1, `SkullbonezSource/Runtime/Run.h` is 198 lines and has 46 include
directives. The direct-member inventory has grown since the plan's dated
27-member observation: the current class declares 31 member rows in the normal
build, plus the conditional Automation interaction controller. The scoped
presentation/diagnostics residents are eight concrete values: contact audio,
live style, UI, debug state, graphics stress, and three visualizers.

The named heavy headers are currently direct includes: `UI.h`, `Text.h`,
`SkyBox.h`, and `TestScene.h`. T4 must prove they are no longer reachable
through `Run.h`, not merely remove their direct spelling.

## Final Owner Ruling

### Owner A: `RuntimeOverlayDiagnostics`

Members: `m_UI`, `m_debug`, `m_broadphaseVisualizer`,
`m_collisionVisualizer`, and `m_physicsDebugVisualizer`.

These values form one operator-facing overlay domain. `RunDebugState` is the
single presentation policy record for HUD selection, visibility flags, and
physics-debug drawing; the UI displays and mutates that policy; the three
visualizers render the selected spatial-grid, collision/sleep, and physics
pipeline views. Their shared invariants are presentation-only behavior,
scene-reset policy, input-toggle routing, and renderer-resource lifetime. The
owner must provide domain operations for startup/scene policy, input toggles,
frame-policy construction, scene reset, and post-physics visualizer updates.
Renderer construction may receive one typed overlay-render binding value, but
the owner must not expose a broad services bag or retain a back-reference to
`Run`.

Current call positions that T2 must preserve:

| Member(s) | Current call sites | Required ordering |
|---|---|---|
| three visualizers | `Run.cpp:356-358` | Borrowed while `RuntimeRenderer` is constructed; owner must outlive renderer. |
| UI | `Run.cpp:424`, `492`, `512` | Backend release, startup presentation policy, then Automation binding remain in their existing startup/shutdown positions. |
| UI, debug, physics visualizer | `Run.cpp:632-637`, `788-793`; `RunFrame.cpp:1189-1194`, `1319-1324` | Scene load/advance/reset receives the same overlay state at the same call positions. |
| UI/debug/physics visualizer | `RunFrame.cpp:600`, `607`, `613` | Existing interaction, scene, and presentation frame-view slots become one-for-one owner-A borrows; no new universal frame bag. |
| debug + three visualizers | `RunFrame.cpp:714-719` | Post-physics visualizer refresh remains after physics and before rendering. G/V/physics-debug toggle routing remains inside the existing input turn. |
| debug + UI | `RunRender.cpp:47-157` | Text-only early exit, render-frame policy, replay collision policy, and final UI render submission keep exact order. |
| debug | `Run.cpp:674`, `756-757`; `RunFrame.cpp:1062` | Replay restore/timeline and snapshot facts remain read-only presentation borrows. |

### Owner B: `RuntimeValidationHarness`

Members: `m_liveStyle` and `m_graphicsStress`.

Both values are explicitly CLI-enabled validation harnesses rather than game
presentation state. Live style watches a control directory and coordinates a
deterministic post-render capture; graphics stress drives deterministic
UI/scene/render churn and reports bounded progress. They share launch
configuration, scene-load restart behavior, per-frame validation sequencing,
capture/exit policy, and the invariant that ordinary launches pay no active
harness behavior. The owner must own those workflows with named operations
such as launch configuration, pre-simulation tick, capture pin/query, post-draw
capture, scene-load resume, and quit diagnostics. It must not become a generic
container with raw member getters or a callback route back into `Run`.

Current call positions that T3 must preserve:

| Member(s) | Current call sites | Required ordering |
|---|---|---|
| live style + graphics stress | `Run.cpp:448-458` | CLI launch policy is applied before initialise/execute exactly as today. |
| graphics stress | `RunFrame.cpp:543-548` | `WM_QUIT` diagnostics remain inside the message-drain exit branch. |
| graphics stress | `RunFrame.cpp:612`; downstream `RuntimeStressController` input turn | The existing presentation-view slot becomes owner B one-for-one; deterministic stress still runs in the input phase. |
| live style | `RunFrame.cpp:663-686` | File-watch tick remains after input and before physics; pending capture still pins presentation before render. |
| live style | `RunFrame.cpp:793-794` | Pending capture save remains in the post-draw capture phase. |
| graphics stress | `Run.cpp:635`, `791`; `RunFrame.cpp:1192`, `1322` | Scene load, advance, and reload resume stress with identical seed/action state and position. |

### Recorded Stay: `m_contactAudio`

`ContactAudioService` stays directly on `Run` for this plan. It is a cohesive
audio subsystem that consumes committed physics contacts after each step; it
does not share validation-harness policy or overlay-render state. Forcing it
into either owner would create exactly the cross-domain bag prohibited by the
plan, while extracting a third owner would exceed the owner ruling.

Its retained positions are constructor/config (`Run.cpp:369`, `598-613`),
launch policy (`Run.cpp:447`), scene load/reset (`Run.cpp:632`, `788` and
matching `RunFrame.cpp` load paths), the scene frame view (`RunFrame.cpp:609`),
physics-step enable policy (`RunFrame.cpp:934`), and committed post-step audio
submission (`RunFrame.cpp:1025-1038`). A future audio-owner plan may extract it
without reopening this two-owner campaign.

## Boundary And Allocation Rules

- Owner A is opaque to `Run.h` through one `unique_ptr`; its five heavyweight
  values remain by-value inside the owner. Construction runs under an explicit
  Startup allocation scope and the one process-lifetime allocation has a
  complete owner/phase/reason/cap/wrapper-plan allowlist row. Owner B remains
  subject to the same rule when T3 chooses its final storage.
- Neither owner stores `Run*`, a host reference, callbacks, `void*`, a services
  bag, or a whole-frame view.
- Frame views replace moved member references with one owner reference
  one-for-one; they remain stack-only and non-retained.
- Renderer wiring uses a typed overlay-render binding assembled during
  construction. Runtime work crosses domain APIs, not `Run::*` forwarding
  methods or raw member getters.
- Contact audio remains an explicit, documented remainder rather than being
  forced into an unrelated owner to satisfy a member-count metric.

## T1 Validation

Documentation-only grouping proposal. No repository validation required.

## T2 Extraction Evidence

`RuntimeOverlayDiagnostics` now owns `InGameUI`, `RunDebugState`, and the
broadphase, collision, and physics debug visualizers. It owns real domain
operations rather than only storing fields:

- startup overlay/physics-debug launch policy;
- post-physics refresh of all three visualizers plus their matching scene
  validation gates;
- immutable render-frame policy sampling;
- named UI, presentation-state, and renderer visualizer borrows at the narrow
  scene/replay/render boundaries that require the concrete type.

The renderer is declared after the opaque owner and therefore releases all
borrowed resources before owner destruction. Interaction, scene, and
presentation frame views each replace their old direct UI/debug/physics-
visualizer slot with the same owner-A reference. Scene loading similarly
receives the cohesive owner and derives its three synchronous sub-borrows
inside the cold load operation. No `Run*`, callback pack, services bag, or
retained frame view was introduced.

Formal evidence on 2026-07-16:

- targeted Profile build: passed, zero warnings;
- `python tools/check_allocation_policy.py --self-test`: passed in 0.61 s;
- `python tools/check_allocation_policy.py --repo .`: passed in approximately
  9.8 s (`365` files, `0` allowlist errors); the same edit corrected startup
  allowlist paths left stale by the preceding Init split;
- `python tools/validate_project_filters.py`: passed in approximately 2.0 s,
  `709` project/filter items and `0` errors;
- final `tools\\validate_full.bat`: passed in 207.64 s, with all CPU lanes,
  zero-warning
  Profile/Automation/Debug builds, Automation replay/prediction smoke, DX12
  InfoQueue errors `0`, all three screenshot comparisons passing against the
  committed baselines, standalone/runtime-handle physics smoke, and the
  44,401-line varied physics CSV byte-exact.

Two earlier broad-gate attempts stopped before runtime validation: formatting
preflight named four changed files, then project-filter policy named the two
new owner files. A later build attempt found two stale stress-path aliases.
All three findings were corrected before the successful final gate; no
baseline, screenshot, golden, or authored-data file was refreshed.

Comment-quality audit: touched-file scope, 16/16 source-bearing files inspected
(15 C++ headers/implementations plus `tools/validate_project_filters.py`), zero
deferred or unchecked files, and no separate subsystem checklist required.
The two legacy `Mental model` headings in touched files were normalized to the
required `Summary` section; the new owner files include the full learning
header and nearby allocation/lifetime/invariant comments.
