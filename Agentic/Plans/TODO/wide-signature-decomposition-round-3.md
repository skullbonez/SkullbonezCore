# Wide Signature Decomposition Round 3

Date: 2026-07-23
Owner: skullbonez
State: In progress
Ledger tasks: 6 (T0-T5)

## Goal

Remove every current function with 13, 14, or 15 parameters through genuine
responsibility decomposition or bounded domain values. Functions with 12 or
fewer parameters are accepted by owner direction. The final tracked-source
threshold-13 scan must return zero rows.

## Baseline

`python tools\inventory_wide_signatures.py --threshold 13 --format json`
identifies nine rows:

| Arity | Function | Area |
|---:|---|---|
| 15 | `LogReplayV2TargetRestoreDiagnostic` | Replay diagnostics |
| 15 | `ReplayRuntime::PrepareRenderFrame` | Replay/render composition |
| 14 | `SceneTab::Draw` | Legacy UI |
| 14 | `EmitFxQuad` | Tornado visual hot path |
| 14 | `PhysicsNarrowphaseWakeAccess` constructor | Physics sleep/wake |
| 13 | `SceneTab::HandleOpenComboClick` | Legacy UI |
| 13 | `RenderInstanceRenderer::RenderModels` | Render submission |
| 13 | `ShadowPass::RenderShadowMap` | Shadow submission |
| 13 | `TickReplayScrubberGesture` | Replay input |

CodeGraph and current-source review found one or two callers for every row.
No row is blocked or too difficult at registration.

## Owner-Safe Decisions

- Scene-tab drawing and open-combo input receive bounded layout/gesture values;
  widget and command owners remain explicit.
- Tornado quad emission receives read-only corner, appearance, and terrain
  values. The hot loop does not acquire an owner, callback, heap allocation, or
  polymorphic interface.
- Physics wake construction groups only sleep-controller-owned row spans into
  a private narrow capability; body, collider, and force owners remain explicit.
- Replay target-restore logging receives one diagnostic evidence value.
- Replay render preparation receives one frame-local read-only value snapshot;
  mutable render, physics, tool, and tracer owners remain explicit.
- Replay scrubber gesture receives one scalar/input snapshot; prediction,
  scrubber, input-router, and interaction owners remain explicit.
- Model rendering receives one read-only pass value; render and collider stores
  remain explicit.
- Shadow-map rendering receives one private selection policy; framebuffer,
  frame, texture, store, collider, and worker owners remain explicit.

No generic context, services, bindings, callback pack, stored host reference,
compatibility alias, or replacement forwarding facade is permitted. No
aggregate may contain a mutable subsystem owner. No baseline, golden,
screenshot, Replay artifact, physics CSV, schema, config, or allocation-policy
inventory may be refreshed.

## Ledger

- [x] T0 — Inventory and design. Reproduce all nine rows, inspect their
  definitions/callers, and ratify the owner-safe decisions above.
- [ ] T1 — UI and Gameplay. Narrow both Scene-tab functions and tornado quad
  emission while preserving hit-test/draw ordering and the allocation-free hot
  path.
- [ ] T2 — Physics. Narrow the wake-access constructor with a private
  sleep-row capability and keep all external physics owners explicit.
- [ ] T3 — Rendering. Narrow model and shadow submission without hiding
  backend/store/worker ownership or adding hot-path allocation.
- [ ] T4 — Replay. Narrow restore diagnostics, render preparation, and scrubber
  gesture input without broadening Replay authority.
- [ ] T5 — Closure. Reconcile the zero-row threshold-13 inventory, complete the
  touched-file comment audit, run dependency/allocation proofs and mapped
  gates, obtain one independent no-bag review, and archive evidence.

## Acceptance

- `python tools\inventory_wide_signatures.py --threshold 13 --format json`
  returns `[]`.
- Every replacement has one synchronous producer/consumer lifetime.
- Mutable subsystem owners remain explicit parameters.
- Hot paths add no allocation, callback, inheritance, service lookup, or owner
  reach-back.
- Comment audit checks every touched source-bearing file with zero silent
  deferrals.
- Mapped physics, Replay, rendering/performance/DX12 stress, and full gates pass
  without artifact refresh.

## Validation

- T1 iteration: focused Profile build; `tools\validate_fast.bat` at commit.
- T2: focused Profile build, then `tools\validate_physics.bat`.
- T3: focused Profile build, then `tools\validate_dx12_renderer.bat`,
  `tools\validate_perf.bat`, and `tools\run_graphics_stress.bat 1`.
- T4: focused Profile build, then the single authorized
  `tools\validate_replay_visual_fidelity.bat` invocation and
  `tools\validate_full.bat`.
- T5: inventory/static proofs, `tools\validate_replay_allocation_policy.bat`,
  `tools\validate_replay_v2_artifact.bat`, `tools\validate_replay_scrub.bat`,
  and `tools\validate_full.bat`. The scrub command is the authoritative
  visual-fidelity alias and supplies the closure invocation.

## Comment-Audit Checklist

- [ ] `SkullbonezSource/UI/UITabScene.h`
- [ ] `SkullbonezSource/UI/UITabScene.cpp`
- [ ] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`
- [ ] `SkullbonezSource/UI/UI.cpp`
- [ ] `SkullbonezSource/Gameplay/TornadoVisualPass.cpp`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [ ] `SkullbonezSource/Rendering/RenderInstanceRenderer.h`
- [ ] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`
- [ ] `SkullbonezSource/Runtime/RunRender.cpp`

