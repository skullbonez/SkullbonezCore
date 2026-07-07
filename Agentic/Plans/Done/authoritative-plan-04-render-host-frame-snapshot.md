# Authoritative Plan 04: Render Host Frame Snapshot

Date: 2026-07-06
Status: Active authoritative plan
CSV: `Agentic/Plans/Done/authoritative-plan-04-render-host-frame-snapshot.csv`
Impact area: runtime renderer, render passes, replay overlays, debug overlays, UI text
Validation for this documentation-only change: none required

## Goal

Replace `RuntimeRenderHost` as a broad borrowed-state bridge with narrow render
frame snapshots and capability-specific services. Render passes should consume
`RenderFrameContext`, immutable frame data, and explicit render/world services;
they should not browse through UI, replay, tools, scene browser, diagnostics,
camera, runtime settings, and world internals through one host reference.

## Non-Goals

- Do not change visual output while moving dependencies.
- Do not move all passes at once.
- Do not create another giant `RenderServices` bag.
- Do not put UI/replay/editor behavior inside DX12 backend code.

## First-Night Slice

1. Add a guardrail that prevents new `m_host.m_` access in render passes.
2. Pick one small pass, preferably `UiTextPass` or `LauncherLaser`/debug line
   overlay, and replace host access with explicit frame/service inputs.
3. Update the CSV row and keep screenshots/validation scoped to DX12.

## Definition Of Done

- `RuntimeRenderHost` no longer stores broad references to runtime state.
- Each render pass constructor receives only stable render dependencies or none.
- Per-frame data arrives through `RenderFrameContext` or typed pass input structs.
- Replay/debug/UI overlays render from snapshots rather than poking runtime
  systems through the host.

## Validation

Render pass dependency changes require `tools\validate_dx12_renderer.bat`.
Changes that alter runtime lifecycle or scene loading require
`tools\validate_full.bat`.
