# Run Member And Include Shrink — Presentation/Diagnostics Owners Out Of The Composition Root

Date: 2026-07-15
Status: Active — 2/6 tasks complete
Impact area: `Runtime/Run.h`, `Run.cpp`, `RunFrame.cpp`, `RunRender.cpp`,
frame views, new mid-level owner(s), compile-time include graph
Owner: runtime shell

## Problem And Evidence

`Run` remains a 27-member composition root whose header includes the world:

1. `Run.h` (198 lines, tip `ece58a62`) carries **46 `#include` directives**,
   transitively pulling `UI.h`, `SkyBox.h`, `TestScene.h`, `Text.h`,
   `ContactAudioService.h`, three debug visualizers, and the graphics stress
   controller into every TU that touches `Run` (`Run.h:44-92`). This is the
   engine's largest compile-time coupling point.
2. Direct members that are presentation/diagnostics residents, not
   process-lifecycle concerns (`Run.h:150-159`): `m_contactAudio`,
   `m_liveStyle`, `m_UI`, `m_graphicsStress`, `m_broadphaseVisualizer`,
   `m_collisionVisualizer`, `m_physicsDebugVisualizer` (plus `m_debug`
   toggles that exist to feed them).

The 2026-07-15 god-object review (finding 1) named the member list and the
include fan-out; the owner deferred it behind signatures/PhysicsWorld, both
now complete.

Hazard this plan must not trip: the god-object closure rule bans replacing
`Run` state with a broad `*Services`/`*Context` bag. Any new owner must be
cohesive, own real state and sequencing, and expose domain APIs — not relay
`Run`'s frame calls.

## Goal

`Run` holds only process borrows, launch/exit values, and cohesive domain
owners; the seven presentation/diagnostics residents live behind at most two
concrete mid-level owners; `Run.h` drops to forward declarations plus the
includes its remaining members genuinely require (target: ≤ 25 includes, and
`UI.h`/`SkyBox.h`/`TestScene.h`/`Text.h` no longer reachable through it).
No behavior change; all baselines unchanged.

## Non-Goals

- No frame-order changes: tick/render call positions for every moved system
  stay exactly where they are in `RunFrame.cpp`/`RunRender.cpp`.
- No new universal frame bag; the existing four frame views stay as they are
  (extend a view's member list only if a moved owner replaces a direct `Run`
  member in it one-for-one).
- No UI/audio/visualizer feature work; move-only.
- No `Init.cpp` coupling changes beyond constructor wiring (that file has its
  own live plan).

## Tasks

- [x] T1 — Grouping proposal, committed before code moves to
      `Agentic/Reports/2026-07-15/run-member-shrink-map.md`. Proposed split
      (adjust with evidence, record the final ruling): owner A
      `RuntimeOverlayDiagnostics` = three debug visualizers + `m_debug`
      overlay toggles + `m_UI` (all are operator-facing diagnostics
      presentation); owner B `RuntimePresentationHarness` = `m_contactAudio`,
      `m_liveStyle`, `m_graphicsStress` (presentation-adjacent harnesses with
      file/audio side effects). The map must argue each owner's cohesion in
      one paragraph and name each moved member's tick/render call sites; if a
      member doesn't fit either owner cohesively, it stays on `Run` with a
      recorded reason rather than being forced in.
      Evidence: `Agentic/Reports/2026-07-15/run-member-shrink-map.md` records
      the final ruling. Owner A keeps UI/debug/three visualizers; owner B is
      narrowed to live-style plus graphics-stress validation workflows;
      contact audio stays on `Run` because its physics-post-step audio domain
      fits neither owner and a third owner is disallowed. Every current
      construction, launch, scene, frame-view, toggle, render, capture, and
      post-step call position is mapped. Current measurements are 198 lines,
      46 includes, and 31 ordinary member rows plus the Automation-only row.
- [x] T2 — Extract owner A with its constructor wiring, tick/render
      delegation at the exact existing call positions, and input-toggle
      routing (G/V key visualizer toggles etc.) unchanged. Frame views that
      carried the moved members now carry owner A.
      Evidence: `RuntimeOverlayDiagnostics` owns UI/debug/three visualizers,
      applies startup policy, samples render policy, and refreshes all three
      visualizers after committed physics. Scene-load and frame-view boundaries
      carry the owner one-for-one; renderer borrows remain lifetime ordered.
      Its one opaque allocation is explicitly scoped/allowlisted to Startup.
      Allocation self-test/repository scan and the final broad gate passed with
      zero warnings, zero DX12 errors, unchanged screenshots, and byte-exact
      physics.
- [ ] T3 — Extract owner B the same way (stress controller CLI wiring,
      live-style tick, contact-audio step ordering all position-identical).
- [ ] T4 — Include-graph shrink: `Run.h` moves to forward declarations +
      `unique_ptr`/value members as ownership requires (allocation policy: any
      new heap member must route through the approved startup-phase path or
      stay by-value; prefer by-value members inside the new owners' own
      headers so `Run.h` only forward-declares the owners). Record
      before/after include counts and a build-time sample in the map report.
- [ ] T5 — Independent ownership review (single, end-of-plan): confirms
      neither owner is a bag (each owns state + sequencing with domain APIs),
      no reach-back into `Run`, no new forwarding relays, and `Run`'s
      remaining members are all process-lifecycle or cohesive domain owners.
      A credible finding reopens T2/T3.
- [ ] T6 — Final gates: `Run*`/`Runtime/*` maps to `tools\validate_full.bat`;
      moved systems touch render submission and UI, so also
      `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
      (stress controller wiring moved ⇒ DX12-adjacent proof) with recorded
      command/runtime/exit evidence. Physics and screenshot baselines
      unchanged; no refresh.

## Dependencies And Decisions

- Runs after `init-startup-decomposition` only if both end up editing `Run`
  constructor wiring in conflicting commits; otherwise independent. The
  wide-call plan is independent.
- Owner ruling 2026-07-15: at most two new owners; a member that fits
  neither stays on `Run` with a reason (no forced bagging).

## Acceptance

- `Run` member count for presentation/diagnostics residents reaches zero or
  each remainder has a recorded stay-reason; `Run.h` include count ≤ ~25 and
  the named heavy headers are no longer transitively included.
- Independent review records zero credible bag/relay/reach-back findings.
- All gates pass with zero baseline changes; build-time delta recorded.

## Validation

- `tools\validate_full.bat`, then `tools\validate_dx12_renderer.bat` +
  `tools\run_graphics_stress.bat 1`, outputs pasted at closure.
