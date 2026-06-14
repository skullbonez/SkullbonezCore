# DX12 Final Architecture Next Steps

Status: active implementation plan  
Created: 2026-06-14  
Branch: `codex/dx12-only-engine-architecture`  
Scope: DX12 render architecture, backend decomposition, descriptor/upload/readback ownership, render graph validation

## Purpose

This plan turns the current DX12 architecture direction into small implementation slices.

The renderer is not being rewritten in one jump. The goal is to move from a large
`RenderBackendDX12` translation unit toward a DX12-first renderer where ownership
is explicit, reviewable, and testable. Each slice must leave GL, DX11, and DX12
renderer parity intact while the legacy parity backends remain in the repository.

## Reference Model

This work follows the Direct3D 12 programming model recommended by Microsoft,
not a single file/class template. Microsoft does not prescribe one engine
architecture, but the official guidance and samples consistently push these
responsibilities into app-owned systems:

- command lists and command allocators,
- explicit CPU/GPU synchronization through fences,
- descriptor heaps and descriptor tables,
- pipeline state objects and root signatures,
- explicit resource-state transitions through barriers,
- app-owned upload/readback lifetime,
- debug layer, DRED, object names, and GPU diagnostics.

The closest Microsoft-published architectural reference is the DirectX Graphics
Samples repository and MiniEngine. This project should borrow the useful shape:
small reusable DX12 systems around device, descriptors, uploads, PSOs, profiling,
and diagnostics. It should not blindly copy MiniEngine or introduce a broad
third-party framework.

Useful references:

- Microsoft Direct3D 12 programming guide:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide
- Important changes from Direct3D 11 to Direct3D 12:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/important-changes-from-directx-11-to-directx-12
- Porting from Direct3D 11 to Direct3D 12:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12
- Basic Direct3D 12 component walkthrough:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component
- Direct3D 12 debug layer and DRED:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/understanding-the-d3d12-debug-layer
- Microsoft DirectX Graphics Samples / MiniEngine:
  https://github.com/microsoft/DirectX-Graphics-Samples

## Validation Rule For This Plan

Every implementation slice in this plan touches render architecture. Each slice
must run renderer parity validation against GL, DX11, and DX12 before commit.

Preferred validation command:

```bat
tools\validate_renderers.bat
```

That script builds the renderer validation binaries and runs the repository
renderer parity checks, including `tools\check_parity.py`.

## Item 1: Split `RenderBackendDX12` Into Subsystem Translation Units

Goal: reduce the 3,800+ line backend file without changing behavior.

This is a mechanical decomposition. Keep the `RenderBackendDX12` class and public
interface intact, but move method implementations into focused `.cpp` files:

- frame lifecycle: init, shutdown, resize, present, clear, command-list handling,
- pipeline state: root signature, PSO cache, input layouts, draw prep,
- textures and GPU mips,
- dynamic geometry and instanced meshes,
- readback and screenshots,
- DXR reflection,
- GPU timers and platform profiler markers.

Expected result:

- no behavior change,
- smaller reviewable files,
- a safer base for real ownership extraction.

## Item 2: Extract `Dx12RenderDevice` Ownership From `RenderBackendDX12`

Goal: make device/swapchain/frame ownership a real DX12 subsystem.

Move ownership and lifecycle policy for these objects behind `Dx12RenderDevice`
or a similarly named device layer:

- DXGI factory,
- D3D12 device,
- swap chain,
- command queue,
- command allocators,
- command list,
- frame index,
- frame fence,
- debug layer, InfoQueue, DRED setup,
- present, wait idle, and frame reset behavior.

Expected result:

- `RenderBackendDX12` remains the `IRenderBackend` facade,
- low-level frame/device state no longer feels like ad hoc backend fields,
- future pass systems can ask the device layer for command-list and frame state.

## Item 3: Add RTV/DSV Descriptor Allocators And Descriptor Diagnostics

Goal: stop treating RTV and DSV descriptors as loose counters.

The SRV/CBV/UAV path already has a named descriptor allocator. Apply the same
architectural clarity to render-target and depth-stencil descriptor rows.

Add:

- RTV descriptor allocator,
- DSV descriptor allocator,
- diagnostics for static and transient descriptor usage,
- fatal exhaustion logs that name the heap and capacity,
- comments explaining RTV/DSV descriptor purpose for non-GPU engineers.

Expected result:

- all descriptor row assignment is owned by named allocator policy,
- descriptor usage can be reported consistently,
- descriptor lifetime rules are easier to review.

## Item 4: Extract Upload And Readback Systems

Goal: separate CPU-written upload memory and GPU-to-CPU readback memory from the
backend facade.

Create small DX12 systems for:

- per-frame upload allocation,
- texture row upload helpers where practical,
- constant/dynamic vertex/instance upload byte ranges,
- screenshot/readback staging,
- GPU timer readback staging.

Expected result:

- upload lifetime is visibly tied to frame fences,
- readback wait behavior is isolated,
- the backend stops carrying raw allocation policy directly.

## Item 5: Compare RenderGraph Transitions Against Live Backend Barriers

Goal: make the render graph useful before it becomes the live barrier owner.

The render graph currently compiles transition records. The backend still emits
hand-written barriers. Before the graph takes over, compare graph expectations
against actual backend barrier calls.

Add debug-only or diagnostic comparison:

- record live barrier transitions with pass/resource labels where available,
- compile the diagnostic render graph,
- dump graph transitions and live backend transitions side by side,
- warn on obvious missing or mismatched transitions,
- keep rendering behavior unchanged.

Expected result:

- graph diagnostics become a safety net,
- future graph-owned barriers can be migrated pass by pass,
- current renderer parity remains protected by GL/DX11/DX12 validation.

## Later Item: Move A First Real Pass Under Graph-Owned Barriers

This is intentionally not part of the first five implementation items. It should
start only after graph-vs-live barrier comparison is stable and reviewed.

Recommended first candidate:

- a low-risk full-screen or post-style pass with simple resource access,
- not the entire scene path,
- not DXR reflection,
- not swapchain present ownership.

## Completion Criteria

The first five items are complete when:

- each item has its own atomic commit,
- each implementation commit records renderer parity validation,
- `RenderBackendDX12` is split and easier to review,
- device/frame ownership is meaningfully extracted,
- descriptor allocation policy covers SRV/CBV/UAV, RTV, and DSV descriptors,
- upload and readback policy live outside the backend facade,
- render graph transition diagnostics compare against live backend barriers,
- GL/DX11/DX12 renderer parity remains passing.
