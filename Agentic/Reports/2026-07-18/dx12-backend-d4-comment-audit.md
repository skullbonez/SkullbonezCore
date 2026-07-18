# DX12 Backend D4 Comment Audit

Date: 2026-07-18

Scope: D4 diagnostics and GPU-timing ownership extraction

Guide: `Agentic/Reference/comment-style-guide.md`

Skill: `Agentic/Skills/comment-style-audit/skill.md`

## Reconciled Source Inventory

The inventory combines tracked changes with the two untracked owner files from
`git status --short`. Twelve source-bearing files are in scope:

- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [x] `tools/validate_project_filters.py`

Checked: 12 / 12. Deferred: 0. Unchecked: 0.

## Findings

- The new diagnostics owner has complete Purpose, Summary, Glossary,
  Invariants, and Related sections. Nearby `Why:`, `Invariant:`, and Lane R
  comments explain non-blocking fence polling, query-slot gaps, retained timer
  values, optional driver support, and draw-reset behavior.
- The frame-owner headers and body teach the restricted diagnostics capability:
  timing/fault operations can inspect the fence timeline but cannot submit,
  advance frames, or reach uploads/descriptors.
- Mesh and dynamic-geometry comments now explain that accepted native draws
  record through one concrete diagnostics owner rather than raw trace/counter
  aliases.
- The profiler unit's previously malformed file/summary header was corrected;
  it now distinguishes diagnostics-owned timing from frame-owned PIX scope
  sequencing.
- Backend headers name `Dx12Diagnostics` as the timing/draw-evidence owner and
  contain no stale claim that timer/counter/visibility/trace state lives in the
  aggregate backend.
- The project-filter prefix addition is a trivial registration row and needs no
  extra local prose.

## Result

PASS. Every touched source-bearing file was inspected, the required teaching
content is present, and no file is deferred.
