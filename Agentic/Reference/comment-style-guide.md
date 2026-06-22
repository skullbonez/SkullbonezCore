# SkullbonezCore Comment Style Guide

This codebase should teach while it runs. A reader should be able to open a
source file, learn the local vocabulary, understand why the code exists, and
follow the risky parts without already being a rendering or physics specialist.

The goal is not more comments everywhere. The goal is consistent comments that
explain concepts, ownership, units, lifetimes, and hazards.

## Core Rule

Write comments for the next smart reader who does not already know this engine
or this domain.

Every comment should answer at least one of these questions:

| Question | Use when |
|----------|----------|
| What is this concept? | The code uses engine, graphics, physics, or platform vocabulary. |
| Why is this done here? | The code looks indirect, surprising, duplicated, or conservative. |
| What must stay true? | The code depends on ordering, lifetime, ownership, units, or determinism. |
| What can go wrong? | The code prevents a crash, GPU hang, data race, visual drift, or flaky test. |
| Where should I look next? | The real explanation lives in another type, file, paper, API, or tool. |

Avoid comments that only restate the code:

```cpp
// Increment i.
++i;
```

Prefer comments that teach the reason:

```cpp
// Skip slot 0 because the shader treats descriptor index 0 as "no texture".
++i;
```

## File Learning Header

Every source-bearing file should start with a learning header once we touch it
for meaningful work. Do not churn the whole repository in one formatting-only
pass unless that is the explicit task.

Use this format at the top of `.cpp`, `.h`, `.hlsl`, and substantial tool files:

```cpp
/*
File: RenderBackendDX12.cpp
Purpose:
  Owns the DirectX 12 renderer backend: device setup, swap-chain targets,
  descriptor tables, command recording, and frame presentation.

Mental model:
  DX12 separates GPU memory from small binding records called descriptors.
  This file creates the memory, creates descriptor rows that describe that
  memory, then records command-list operations that bind those rows.

Glossary:
  RTV (Render Target View): Descriptor row used when the GPU writes color pixels.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads/writes depth.
  SRV (Shader Resource View): Descriptor row used when shaders read resources.
  UAV (Unordered Access View): Descriptor row used when shaders write resources.
  DRED (Device Removed Extended Data): DX12 crash report data for device loss,
    including breadcrumb history and page-fault clues.

Invariants:
  - RTV and DSV rows are CPU-only and stable across swap-chain resize.
  - Shader-visible SRV/UAV rows are frame-scoped unless registered as static.

Related:
  - RenderBackendDX12.h owns persistent DX12 state.
  - Agentic/Reference/skullbonez-core-class-structure.md diagrams ownership.
*/
```

Header fields:

| Field | Required | Notes |
|-------|----------|-------|
| `File` | Yes | Filename only, or path if the basename is ambiguous. |
| `Purpose` | Yes | One to three lines saying what this file owns. |
| `Mental model` | Yes for non-trivial files | Plain-English model of how to think about the file. |
| `Glossary` | Yes | Terms used in this file that a non-specialist may not know. |
| `Invariants` | When relevant | Ordering, lifetime, threading, units, determinism, API contracts. |
| `Related` | When useful | Nearby files, reference docs, papers, tools, or validation scripts. |

Keep file glossaries local. Define the terms a reader needs for this file, not
every term in the engine. If a glossary grows past about twelve entries, keep the
most local terms and link to a reference document.

## Glossary Rules

No unexplained acronym should appear in a comment.

When an acronym or domain term appears in a file, define it in the file glossary
and expand it on first local use if the nearby code is dense.

Use this entry shape:

```text
TERM (Expanded Name): One-sentence concept. Optional second sentence saying how
  this file uses it.
```

Good glossary entries:

```text
RTV (Render Target View): Descriptor row used when the GPU writes color pixels
  into a texture or swap-chain back buffer.
DSV (Depth Stencil View): Descriptor row used when the GPU reads/writes depth
  and stencil values for depth testing.
DRED (Device Removed Extended Data): DX12 diagnostic data recorded when the GPU
  device is removed or reset, usually after a driver fault, GPU hang, invalid
  command stream, or page fault.
PSO (Pipeline State Object): Precompiled bundle of render state and shaders that
  DX12 binds before drawing.
BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point to BLAS geometry.
CCD (Continuous Collision Detection): Swept collision test that asks whether
  objects hit during the tick, not only where they are at the end.
```

Bad glossary entries:

```text
RTV: Render target view.
DRED: Device removed extended data.
```

Those expansions are technically true, but they still do not teach the concept.

## Comment Types

Use these comment types consistently.

### 1. File Learning Header

Use the full header above. It is the reader's map.

### 2. Concept Block

Place before a function, class, or long code section when a reader needs the
idea before the details.

```cpp
// Concept: Descriptor heaps are tables, not textures.
//
// A descriptor is one row that tells DX12 how to interpret a resource. RTV
// rows bind color outputs, DSV rows bind depth outputs, and SRV/UAV rows bind
// shader reads/writes. Allocating a descriptor row does not allocate GPU image
// memory; it only reserves the binding record.
```

Use a `Concept:` block for:

- DX12, DXR, shader, and GPU synchronization concepts.
- Physics solver stages, contact rows, warm starting, sleep, and determinism.
- Scene parsing rules that are not obvious from the syntax.
- Any place where a bug fix depends on a mental model.

### 3. Why Block

Use when the code has a non-obvious reason.

```cpp
// Why: ResizeBuffers replaces the back-buffer resources, but the engine keeps
// the same RTV rows so cached render-target handles remain valid engine state.
```

### 4. Invariant Block

Use when breaking the rule creates bad behavior.

```cpp
// Invariant: physics validation depends on byte-exact Debug CSV output. Do not
// change field order, precision, or frame filtering without updating baselines
// from the final Debug build and rerunning validate_physics.
```

### 5. Lifetime / Ownership Block

Use for raw COM pointers, GPU resources, pooled objects, cached handles, and
borrowed references.

```cpp
// Lifetime: the descriptor row is stable, but the resource it describes may be
// replaced during resize. Recreate the view record after each new resource.
```

### 6. Hazard Block

Use near code that prevents crashes, GPU hangs, visual divergence, stale
diagnostics, or nondeterminism.

```cpp
// Hazard: without the UAV barrier, a later TraceRay call can read the BLAS while
// the GPU is still building it.
```

### 7. Field Comments

Use short trailing comments for units, ranges, sentinel values, and ownership.
If the explanation needs more than one sentence, move it above the field.

```cpp
float m_gravity;            // m/s^2, negative means downward.
int m_activeSlider = 0;     // 0 = none; otherwise a UI slider id.
uint32_t m_srvIndex = 0;    // Row in the shader-visible SRV descriptor heap.
```

Avoid long aligned trailing comments after long declarations. They become hard
to read and create noisy diffs.

### 8. Public API Comments

For public methods and interfaces, explain the caller contract, not the body.

```cpp
// Returns the shader-visible SRV descriptor row for a texture. The returned
// index is stable until the texture is unregistered.
uint32_t RegisterTexture( const Texture& texture );
```

### 9. Local Inline Comments

Use one short sentence when a single line would otherwise be puzzling.

```cpp
barrier.UAV.pResource = m_blasResult; // Null would order all UAVs; this narrows the stall.
```

Do not use local inline comments as glossary substitutes.

## Exact Rewrite Example

Current style:

```cpp
// RTV and DSV descriptor heaps are tables
```

Preferred style:

```cpp
// Concept: RTV/DSV descriptor heaps are CPU-side binding tables.
//
// RTV (Render Target View) rows tell DX12 where color pixels can be written.
// DSV (Depth Stencil View) rows tell DX12 where depth/stencil values can be
// read or written. These rows do not own the texture memory; they only describe
// how the output textures are bound.
```

For a denser area, define RTV and DSV in the file glossary and keep the local
comment shorter:

```cpp
// RTV/DSV rows are stable binding records. Resize replaces the textures, then
// overwrites these rows with new view records.
```

## Rendering Comment Expectations

Rendering code has the highest acronym burden. Any file that uses DX12, DXR,
or shader binding terms should explain them locally.

Common render terms to define when used:

| Term | First-reader explanation |
|------|--------------------------|
| RTV | Render Target View, a descriptor row for writing color pixels. |
| DSV | Depth Stencil View, a descriptor row for depth/stencil testing. |
| SRV | Shader Resource View, a descriptor row for shader reads. |
| UAV | Unordered Access View, a descriptor row for shader writes. |
| CBV | Constant Buffer View, a descriptor row for shader constants. |
| Descriptor heap | DX12 table that stores descriptor rows. |
| Shader-visible heap | Descriptor table the GPU can index directly from shaders. |
| PSO | Pipeline State Object, compiled render/raytracing state bundle. |
| Root signature | DX12 binding contract between shaders and descriptor tables/constants. |
| Resource state | DX12 usage mode that controls legal reads/writes and barriers. |
| Barrier | Command-list ordering or state transition operation. |
| DRED | Device Removed Extended Data, DX12 device-loss diagnostic report. |
| PIX | Microsoft GPU debugging/profiling tool. |
| BLAS | Bottom-level raytracing acceleration structure for mesh triangles. |
| TLAS | Top-level raytracing acceleration structure for scene instances. |
| SBT | Shader Binding Table, DXR table mapping rays to shader records. |

When comments mention official API names, add a docs link only where it helps a
reader understand an API call. Do not link every line.

## Physics Comment Expectations

Physics code should make units, determinism, solver ownership, and validation
contracts visible.

Common physics terms to define when used:

| Term | First-reader explanation |
|------|--------------------------|
| Broadphase | Cheap pass that finds pairs that might collide. |
| Narrowphase | Precise pass that computes actual contact points/manifolds. |
| Manifold | Set of contact points and normals for one colliding pair. |
| Contact row | Solver constraint row used to apply impulses at contact. |
| Warm starting | Reusing last frame's impulse to stabilize the solver. |
| Restitution | Bounce response. |
| Friction | Tangential impulse that resists sliding. |
| CCD | Continuous Collision Detection, swept hit test during a tick. |
| Sleep | Optimization that stops simulating stable bodies until woken. |
| Determinism | Same inputs produce byte-exact validation output. |
| SkullScope | Queryable physics diagnostics trace workflow. |

Every physics comment that touches validation-sensitive behavior should say
whether it affects byte-exact baselines.

## What To Avoid

Avoid acronym-only comments:

```cpp
// Create DSV.
```

Prefer:

```cpp
// Create a DSV (Depth Stencil View) so the output-merger stage can perform
// depth testing against this texture.
```

Avoid vague labels:

```cpp
// Do stuff.
// Fix weird issue.
// HACK.
```

Prefer a named reason or hazard:

```cpp
// Hazard: terrain edge contacts can look stable for one frame, but they should
// inhibit sleep because the support point may disappear on the next tick.
```

Avoid comments that promise future behavior without an owner:

```cpp
// TODO: make this better.
```

Prefer actionable TODOs:

```cpp
// TODO(dx12-framegraph): Move this transition into RenderGraph once all passes
// declare read/write access explicitly.
```

Avoid stale historical comments unless the history explains a current rule.

## Comment Maintenance Checklist

When editing a file, check:

- Does the file have a learning header?
- Does the glossary define local acronyms and domain terms?
- Does the first dense use of an acronym expand or point to the glossary?
- Are comments explaining concepts, reasons, invariants, lifetimes, hazards, or units?
- Did any code change make an existing comment stale?
- Are public methods documented from the caller's point of view?
- Are validation-sensitive physics/rendering assumptions called out?
- Are links useful and sparse?

## Should This Become A Skill?

Yes, but the guide should be the source of truth and the skill should be the
repeatable audit procedure.

Recommended follow-up skill:

```text
Agentic/Skills/comment-style-audit/skill.md
```

That skill should be run intermittently:

- Before PR prep on files with heavy comment changes.
- After adding a new DX12, physics, scene, or diagnostics subsystem.
- When touching an old file that has acronym-heavy or random comments.
- When a reviewer says the code is hard to learn from.

The skill should do four things:

1. Inspect the touched files, not the whole repository by default.
2. Add or refresh file learning headers and glossaries.
3. Replace acronym-only and restatement comments with concept/why/invariant comments.
4. Report any terms that still need a human-approved explanation.

This should not start as a strict linter. A script can later flag likely issues,
such as all-caps terms without glossary entries, but explanation quality needs
human judgment.

## Rollout Plan

Use gradual cleanup to avoid noisy churn:

1. New files must use this style.
2. Files touched for meaningful work should get a learning header.
3. Acronym-heavy files get glossary fixes before deeper comment rewrites.
4. Subsystem passes can happen later, one area at a time: DX12 first, then
   physics, scene parsing, UI, tools, and tests.

Documentation-only comment rewrites require no repository validation. Code
behavior changes still follow the validation map in `AGENTS.md`.
