# Authoritative Plan 03: Explicit Service Contexts

Date: 2026-07-06
Status: Active authoritative plan
CSV: `Agentic/Plans/Done/authoritative-plan-03-explicit-service-contexts.csv`
Impact area: global services, renderer access, config, window, profiler, worker pool, UI diagnostics
Validation for this documentation-only change: none required

## Goal

Stop normal-path code from using process globals as ordinary dependencies.
Globals may remain as startup/shutdown internals or tightly bounded diagnostic
infrastructure, but runtime, render, physics, UI, and tool code should receive
explicit services, snapshots, or owner commands.

## Non-Goals

- Do not remove every singleton in one pass.
- Do not make a new service locator with a nicer name.
- Do not change renderer behavior while replacing access paths.
- Do not move profiler instrumentation until the receiving render diagnostics
  path exists.

## First-Night Slice

1. Add a service-context boundary checker mode that can ratchet new `Gfx()`,
   `Cfg()`, and `::Instance()` calls by directory, with the current census
   stored as the budget and a checker self-test (SVC-035).
2. Start with the diagnostics/UI snapshot cluster: SVC-022 (diagnostics CSV
   through injected profiler capability), SVC-032/SVC-033 (UI profiler tab
   from diagnostics snapshots), SVC-034 (classify LockOrderValidator as an
   allowed diagnostics singleton). These are `validate_fast` rows with no
   render-path overlap.
3. Update CSV rows as each call site is migrated.

Ownership note: the `LauncherLaser`/editor-tracer/debug-line overlay cluster
(SVC-024..SVC-028) is shared ground with plan 04's first slice. That on-ramp
belongs to **plan 04**; take those SVC rows only in a night that plan 04 owns,
or after plan 04's overlay migration has landed.

## Definition Of Done

- Deep runtime code does not call `Gfx()` or `Cfg()`.
- Config is passed as immutable startup/runtime snapshots at subsystem edges.
- Renderer services are passed as render contexts or frame capabilities.
- Window, worker, texture, camera, and profiler singletons are either startup
  internals or explicitly documented diagnostics exceptions.

## Validation

Depends on touched area. Broad context movement defaults to
`tools\validate_full.bat`; renderer service migration needs
`tools\validate_dx12_renderer.bat`; profiler marker changes also need
`Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`.
