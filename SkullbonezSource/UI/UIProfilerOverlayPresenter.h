/*
File: SkullbonezSource/UI/UIProfilerOverlayPresenter.h
Purpose:
  Declares profiler panels as renderer-independent UI draw recording.

Summary:
  UIProfilerOverlayPresenter owns operator-facing labels, layout, colors, and
  timing charts. It reads a detached Core profiler frame and records bounded
  screen-space commands; Runtime/Render submits those commands afterward.

Glossary:
  Frame view: Read-only spans over Core-owned marker, counter, and worker rows.
  Bar overlay: Stacked CPU/GPU timing visualization.
  Projection coordinate: Legacy 45-degree text-space unit preserved so the
    existing overlay keeps its exact proportions while recording pixels.

Invariants:
  - The presenter retains no frame view or draw context after a call returns.
  - Presentation cannot mutate profiler history or name a renderer type.
  - Recording performs no GPU submission and allocates no steady-runtime memory.

Related:
  - SkullbonezSource/Core/Profiler.h
  - SkullbonezSource/UI/UIDraw.h
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
*/
#pragma once

#include "../Core/Profiler.h"

namespace SkullbonezCore::UI
{
class UIDrawContext;

class UIProfilerOverlayPresenter
{
  public:
    void RecordOverlay( const Core::Profiler::ProfilerFrameView& frame, const UIDrawContext& draw, float xLeft,
                        float yAnchor, float lineHeight, float fontSize, float fps, bool rightAnchored = false ) const;
    void RecordBarOverlay( const Core::Profiler::ProfilerFrameView& frame, const UIDrawContext& draw, float xLeft,
                           float yBottom, float panelWidth, float panelHeight, bool absolute ) const;
};
} // namespace SkullbonezCore::UI
