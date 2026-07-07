# Authoritative Plan 01: Run Composition Root

Date: 2026-07-06
Status: Active authoritative plan
CSV: `Agentic/Plans/Done/authoritative-plan-01-run-composition-root.csv`
Impact area: runtime architecture, scene lifecycle, input, replay, diagnostics, render lifecycle
Validation for this documentation-only change: none required

## Goal

Keep `Run` as the lifetime owner and frame scheduler, but stop using it as the
behavioral owner for every subsystem. `Run` may construct the hierarchy; it must
not keep absorbing scene load policy, input command routing, replay tools,
camera transitions, diagnostics, capture, asset registration, and render
resource lifecycle.

## Non-Goals

- Do not rewrite the engine loop.
- Do not remove `Run` as a stack/member lifetime owner.
- Do not change scene, physics, replay, or render behavior in boundary slices.
- Do not split files without moving authority.

## First-Night Slice

1. Add or tighten a runtime boundary check that rejects new broad `Run::`
   methods outside approved composition-root responsibilities.
2. Pick one row cluster from the CSV, preferably screenshot/capture or render
   resource lifecycle because those have the clearest subsystem owner.
3. Move behavior behind the existing owner or create a narrowly named owner.
4. Leave `Run` with construction, dependency wiring, and one-line delegation.

## Definition Of Done

- `Run.h` no longer grows for feature work.
- `Run` owns lifetime and frame order only.
- Scene loading, input routing, replay interaction, capture, diagnostics, and
  render lifecycle each have a narrower owner API.
- `BuildRuntimeRenderHostBindings()` and `BindEngineContext()` shrink instead of
  becoming larger service bags.

## Validation

Implementation slices touching runtime lifecycle default to
`tools\validate_full.bat`. Narrow input-only or diagnostics-only slices may use
`tools\validate_fast.bat` only when launch, scene, physics, and DX12 output
cannot change.
