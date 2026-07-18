/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp
Purpose:
  Resolves the deterministic editor dock layout for a content envelope.

Summary:
  Pixel-bounded side rails and bottom strips protect a useful central viewport
  at the supported minimum while allowing ultrawide space to accrue to the
  viewport. The policy also fits and maps a source render image without placing
  ImGui types or renderer authority in input composition.

Glossary:
  Split fraction: Portion taken by the directional child passed to DockBuilder.
  Compact toolbar: Label policy used below 1500 pixels to preserve controls.

Invariants:
  - Every divisor is positive after input normalization.
  - Side-rail caps prevent ultrawide layouts from becoming tool-heavy.
  - The topology hash depends only on the versioned descriptor bytes.
  - Letterbox math preserves source aspect and maps only pixels inside the image.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h
  - SkullbonezTests/TestOwnerRequestQueues.cpp
*/
#include "ImGuiEditorLayoutPolicy.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
ImGuiEditorLayoutEnvelope ResolveImGuiEditorLayoutEnvelope( int contentWidth, int contentHeight ) noexcept
{
    ImGuiEditorLayoutEnvelope result;
    result.contentWidth = (std::max)( 1, contentWidth );
    result.contentHeight = (std::max)( 1, contentHeight );

    result.editorLeftWidth = std::clamp( static_cast<int>( std::lround( result.contentWidth * 0.22 ) ), 260, 420 );
    result.utilityRightWidth = std::clamp( static_cast<int>( std::lround( result.contentWidth * 0.24 ) ), 280, 460 );
    result.viewportWidth = (std::max)( 1, result.contentWidth - result.editorLeftWidth - result.utilityRightWidth );

    result.statusHeight = std::clamp( static_cast<int>( std::lround( result.contentHeight * 0.075 ) ), 48, 68 );
    const int aboveStatus = (std::max)( 1, result.contentHeight - result.statusHeight );
    result.replayHeight = std::clamp( static_cast<int>( std::lround( aboveStatus * 0.22 ) ), 112, 280 );
    result.upperHeight = (std::max)( 1, aboveStatus - result.replayHeight );

    result.statusSplitFraction = static_cast<float>( result.statusHeight ) / result.contentHeight;
    result.replaySplitFraction = static_cast<float>( result.replayHeight ) / aboveStatus;
    result.editorLeftSplitFraction = static_cast<float>( result.editorLeftWidth ) / result.contentWidth;
    const int afterLeft = (std::max)( 1, result.contentWidth - result.editorLeftWidth );
    result.utilityRightSplitFraction = static_cast<float>( result.utilityRightWidth ) / afterLeft;
    result.compactToolbarLabels = result.contentWidth < 1500;
    result.preservesCentralViewport = result.viewportWidth >= 480 && result.upperHeight >= 360;
    return result;
}

ImGuiGameViewportRect ResolveImGuiGameViewportRect( float availableMinX,
                                                    float availableMinY,
                                                    float availableWidth,
                                                    float availableHeight,
                                                    int sourceWidth,
                                                    int sourceHeight,
                                                    float dpiScale ) noexcept
{
    ImGuiGameViewportRect result;
    result.dpiScale = std::isfinite( dpiScale ) && dpiScale > 0.0f ? dpiScale : 1.0f;
    result.sourceWidth = sourceWidth;
    result.sourceHeight = sourceHeight;
    if ( !std::isfinite( availableMinX ) || !std::isfinite( availableMinY ) || !std::isfinite( availableWidth ) ||
         !std::isfinite( availableHeight ) || availableWidth <= 0.0f || availableHeight <= 0.0f || sourceWidth <= 0 ||
         sourceHeight <= 0 )
    {
        return result;
    }

    const float sourceAspect = static_cast<float>( sourceWidth ) / static_cast<float>( sourceHeight );
    const float availableAspect = availableWidth / availableHeight;
    if ( availableAspect > sourceAspect )
    {
        result.imageHeight = availableHeight;
        result.imageWidth = availableHeight * sourceAspect;
    }
    else
    {
        result.imageWidth = availableWidth;
        result.imageHeight = availableWidth / sourceAspect;
    }
    result.imageMinX = availableMinX + ( availableWidth - result.imageWidth ) * 0.5f;
    result.imageMinY = availableMinY + ( availableHeight - result.imageHeight ) * 0.5f;
    result.letterboxed = std::fabs( result.imageWidth - availableWidth ) > 0.5f ||
                         std::fabs( result.imageHeight - availableHeight ) > 0.5f;
    result.valid = result.imageWidth >= 1.0f && result.imageHeight >= 1.0f;
    return result;
}

bool MapImGuiGameViewportPoint( const ImGuiGameViewportRect& viewport,
                                float clientX,
                                float clientY,
                                int& outSourceX,
                                int& outSourceY ) noexcept
{
    outSourceX = 0;
    outSourceY = 0;
    if ( !viewport.valid || !std::isfinite( clientX ) || !std::isfinite( clientY ) )
    {
        return false;
    }
    const float maxX = viewport.imageMinX + viewport.imageWidth;
    const float maxY = viewport.imageMinY + viewport.imageHeight;
    if ( clientX < viewport.imageMinX || clientY < viewport.imageMinY || clientX >= maxX || clientY >= maxY )
    {
        return false;
    }

    // Invariant: both coordinate systems are physical client pixels. DPI is
    // retained for evidence/status only; applying it here would double-scale.
    const float normalizedX = ( clientX - viewport.imageMinX ) / viewport.imageWidth;
    const float normalizedY = ( clientY - viewport.imageMinY ) / viewport.imageHeight;
    outSourceX = std::clamp( static_cast<int>( normalizedX * viewport.sourceWidth ), 0, viewport.sourceWidth - 1 );
    outSourceY = std::clamp( static_cast<int>( normalizedY * viewport.sourceHeight ), 0, viewport.sourceHeight - 1 );
    return true;
}

uint64_t FingerprintImGuiEditorDefaultTopology() noexcept
{
    uint64_t hash = 1469598103934665603ull;
    const size_t length = std::strlen( IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR );
    for ( size_t index = 0u; index < length; ++index )
    {
        hash ^= static_cast<uint8_t>( IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR[index] );
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
