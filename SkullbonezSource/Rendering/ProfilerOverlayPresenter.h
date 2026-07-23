/*
File: SkullbonezSource/Rendering/ProfilerOverlayPresenter.h
Purpose:
  Presents Core profiler frame values through renderer-owned text commands.

Summary:
  ProfilerOverlayPresenter keeps text measurement, quads, and draw submission
  in Rendering while Core publishes only fixed read-only timing values.

Glossary:
  Frame view: Read-only spans over Core-owned marker, counter, and worker rows.
  Bar overlay: Stacked CPU/GPU timing visualization for legacy text mode.

Invariants:
  - The presenter is stateless and retains no frame view or render command
    pointer after either synchronous call returns.
  - Presentation cannot mutate profiler history or renderer diagnostics.

Related:
  - SkullbonezSource/Core/Profiler.h
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
*/
#pragma once

#include "../Core/Profiler.h"

namespace SkullbonezCore
{
namespace Text
{
class TextBatch;
}
namespace Rendering
{
class Dx12GeometryOwner;

class ProfilerOverlayPresenter
{
  public:
    void RenderOverlay( const Core::Profiler::ProfilerFrameView& frame,
                        Text::TextBatch& textBatch,
                        Dx12GeometryOwner& renderCommands,
                        float xLeft,
                        float yAnchor,
                        float lineHeight,
                        float fontSize,
                        float fps,
                        bool rightAnchored = false ) const;
    void RenderBarOverlay( const Core::Profiler::ProfilerFrameView& frame,
                           Text::TextBatch& textBatch,
                           Dx12GeometryOwner& renderCommands,
                           float xLeft,
                           float yBottom,
                           float panelWidth,
                           float panelHeight,
                           bool absolute ) const;
};
} // namespace Rendering
} // namespace SkullbonezCore
