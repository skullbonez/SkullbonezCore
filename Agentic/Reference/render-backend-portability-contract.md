# Render Backend Portability Contract

Purpose: define the engine-level rendering contract a future Vulkan or Metal backend must implement, without treating retired OpenGL or DX11 code as the starting point.

Validation: none required. This is a documentation-only reference.

## Current Position

DX12 is the only active renderer. The codebase no longer maintains OpenGL or DX11 runtime paths, and future backend work must not restore those paths as hidden compatibility layers.

This document is the portability seam. It describes the engine concepts that should remain stable even if a future renderer maps them to Vulkan, Metal, or another modern explicit graphics API.

## Design Rule

Engine code should describe what it needs, not how a specific API binds it.

Good engine-level language:

- texture description
- buffer description
- shader program
- vertex layout
- render target
- pass inputs and outputs
- synchronization intent
- debug marker
- readback request

Backend-owned language:

- DX12 descriptor heap row
- Vulkan descriptor set
- Metal argument buffer slot
- DX12 root parameter
- Vulkan pipeline layout
- Metal render encoder
- native resource pointer

## Core Concepts

### Resources

A resource is GPU storage. The engine should describe resources by type and usage:

- texture 2D, texture cube, buffer
- width, height, format, mip count
- CPU upload source when created
- intended usages such as shader read, render target, depth target, unordered write, vertex buffer, readback

DX12 mapping:

- `ID3D12Resource`
- resource state flags
- SRV, UAV, RTV, and DSV descriptors
- default, upload, and readback heaps

Vulkan mapping:

- `VkImage` or `VkBuffer`
- `VkImageView` or buffer view
- image layouts and access masks
- device, host-visible, and staging memory

Metal mapping:

- `MTLTexture` or `MTLBuffer`
- usage flags
- storage modes
- texture views where needed

### Handles

Engine code should use opaque handles or small value objects for renderer resources. A handle can identify a texture, framebuffer, dynamic vertex buffer, or instanced mesh without exposing native API pointers.

Rules:

- handles are owned by the active renderer
- handles are invalid after the owning renderer is destroyed
- handle recycling must not let stale engine code bind the wrong resource silently
- debug output should identify the handle, source asset, or pass name where possible

### Shader Programs

A shader program is an engine contract between C++ and shader code.

Required metadata:

- shader source or compiled artifact identity
- entry points and stages
- uniform/constant buffer layout
- texture and sampler bindings
- vertex input layout
- optional feature flags, such as raytracing or compute support

DX12 mapping:

- HLSL/DXIL bytecode
- root signatures
- CBV/SRV/UAV descriptor ranges
- graphics, compute, or raytracing PSOs

Vulkan mapping:

- SPIR-V modules
- descriptor set layouts
- pipeline layouts
- graphics, compute, or raytracing pipelines

Metal mapping:

- Metal shader functions
- argument buffers or resource binding tables
- render, compute, or intersection pipelines

### Vertex Layouts

Engine code should describe vertex input in terms of attributes:

- semantic or role: position, normal, uv, color, instance transform
- component count and format
- slot or stream
- per-vertex or per-instance step rate

Backend code maps that to native input layout descriptors.

### Render Targets

A render target description should include:

- color format
- depth/stencil format
- dimensions
- clear values
- sample count
- whether the target will be sampled later

DX12 maps this to resources plus RTV/DSV/SRV descriptors. Vulkan maps it to image views, render pass or dynamic rendering attachments, and layouts. Metal maps it to render pass descriptors and texture attachments.

### Passes

A render pass should describe:

- pass name
- input resources
- output resources
- clear/load/store behavior
- viewport and scissor
- required pipeline
- synchronization intent

The render graph should eventually own pass ordering and barrier emission. Until then, backend code should keep live barrier diagnostics easy to compare against the graph's expected transitions.

### Synchronization Intent

Engine code should express intent:

- this pass writes color
- this pass writes depth
- this pass samples texture
- this pass writes unordered output
- this readback needs CPU access after GPU completion

Backend code maps the intent:

- DX12 resource barriers and fences
- Vulkan pipeline barriers, image layouts, semaphores, and fences
- Metal encoder boundaries, resource usage declarations, and command buffer completion

### Debug Markers And Object Names

Every backend must support human-readable diagnostics:

- frame markers
- pass markers
- GPU scope markers
- resource object names
- validation artifact paths

DX12 uses PIX markers, object names, DRED, and InfoQueue output. Vulkan would use debug utils names, debug labels, and validation layer output. Metal would use command buffer labels, resource labels, capture scopes, and validation messages.

### Capture And Readback

The engine needs bounded ways to read pixels and diagnostics:

- backbuffer screenshot
- off-screen render target screenshot when needed
- validation-friendly image artifacts
- compact diagnostic summaries instead of raw unbounded logs

Backend implementations must make readback lifetime explicit. CPU code must not read a GPU-written resource until the owning command queue has reached the fence value that protects the copy.

## Optional Features

These are not required for every future backend on day one:

- raytracing
- GPU timers
- debug line rendering
- backbuffer capture
- compute mip generation
- platform profiler marker forwarding

Optional features must be reported through capability flags or feature queries, but the engine should avoid broad renderer forks. Prefer one clear fallback path per feature.

## Future Backend Bring-Up Sequence

1. Implement device creation, swap-chain/presentation, command submission, and fence synchronization.
2. Implement resource creation for buffers, textures, render targets, depth targets, and readback.
3. Implement shader program loading plus vertex layout mapping.
4. Implement descriptor/resource binding from engine handles.
5. Implement the baseline draw path: clear, mesh draw, instancing, UI/text dynamic vertices.
6. Implement screenshots/readback so validation can produce artifacts.
7. Add GPU markers, object names, and validation-layer artifact capture.
8. Add optional features one by one: compute mips, GPU timers, raytracing, specialized debug drawing.
9. Wire the backend into the render graph contract before broad pass extraction.

## Non-Goals

- Do not restore OpenGL or DX11 as reference implementations.
- Do not copy old backend APIs into a future Vulkan or Metal backend.
- Do not expose native handles to general engine code unless a narrow subsystem owns that escape hatch.
- Do not add portability by making DX12 less explicit. Preserve explicit lifetime, synchronization, and diagnostics.

## Acceptance Checklist

A future backend design is ready to prototype when it can answer:

- Which engine handles map to which native resource objects?
- Where are descriptors or equivalent binding records allocated?
- Which code owns per-frame transient binding memory?
- Which fence or command buffer completion protects upload/readback reuse?
- How are pass inputs and outputs declared?
- Where do validation messages, object names, screenshots, and timing artifacts appear?
- Which optional features are supported, and what fallback path is used when they are not?

