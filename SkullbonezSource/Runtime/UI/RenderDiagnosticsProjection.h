/*
File: SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h
Purpose:
  Declares the explicit projection from renderer measurements to UI values.

Summary:
  Runtime/UI is the composition boundary allowed to see both backend diagnostic
  snapshots and UI-owned presentation records. These functions copy one frame
  synchronously and retain no owner or source pointer.

Glossary:
  Diagnostic snapshot: Immutable renderer counters sampled for one UI frame.

Invariants:
  - Every source field and enum slot is mapped explicitly.
  - The result contains no renderer type, resource handle, or lifetime borrow.
  - Projection performs no allocation and has no side effects.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UIRenderDiagnostics.h
  - SkullbonezSource/Rendering/RenderDiagnosticsTypes.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "GameUI/UIRenderDiagnostics.h"

namespace SkullbonezCore::Rendering
{
struct RenderMemoryStats;
struct RenderVisibilityStats;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Runtime
{
UI::UIRenderMemoryStats ProjectRenderMemoryDiagnostics( const Rendering::RenderMemoryStats& source );
UI::UIRenderVisibilityStats ProjectRenderVisibilityDiagnostics( const Rendering::RenderVisibilityStats& source );
} // namespace SkullbonezCore::Runtime
