# Authoritative Plan 05: Render Graph Backend Split

Date: 2026-07-06
Status: Active authoritative plan
CSV: `Agentic/Plans/In_Progress/authoritative-plan-05-render-graph-backend-split.csv`
Impact area: DX12 backend, render interfaces, render graph, resource lifetime, pass execution
Validation for this documentation-only change: none required

## Goal

Stop treating `IRenderBackend` and `RenderBackendDX12` as one huge renderer API.
Move callers to narrow capabilities and make the render graph the owner of pass
execution/resource-state transitions for selected passes, one pass family at a
time.

## Non-Goals

- Do not reintroduce GL or DX11 runtime choices.
- Do not expand the root signature or descriptor model without a concrete pass
  contract.
- Do not move all DX12 resource ownership in one branch.
- Do not update visual baselines as a cleanup side effect.

## First-Night Slice

1. Add a boundary checker rule that rejects new `IRenderBackend&` dependencies
   outside startup and compatibility allowlists.
2. Pick one graph-ready pass path and move from diagnostic/declaration mode to
   callback-owned execution, with DX12 screenshots as the PR gate.
3. Update CSV status and keep graph/DX12 errors at zero.

## Definition Of Done

- Runtime draw code asks for `IRenderCommandContext`, `IRenderResourceFactory`,
  `IRenderDiagnostics`, capture, or raytracing only as needed.
- `RenderBackendDX12::Get()` is not used by mesh/framebuffer/shader helpers for
  normal resource ownership.
- Render graph pass descriptors use callback/execute ownership for migrated
  passes instead of diagnostic declaration only.
- DX12 resource transitions for migrated passes are graph-owned and validated by
  zero InfoQueue errors plus screenshot baselines.

## Validation

Renderer/backend/graph slices require `tools\validate_dx12_renderer.bat`.
Binding or hot-path changes that can affect frame cost also require
`tools\validate_perf.bat`.

