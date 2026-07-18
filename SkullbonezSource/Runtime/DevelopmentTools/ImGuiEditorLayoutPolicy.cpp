/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp
Purpose:
  Resolves the deterministic editor dock layout for a content envelope.

Summary:
  Pixel-bounded side rails and bottom strips protect a useful central viewport
  at the supported minimum while allowing ultrawide space to accrue to the
  viewport. Fractions are derived from those pixels for Dear ImGui DockBuilder.

Glossary:
  Split fraction: Portion taken by the directional child passed to DockBuilder.
  Compact toolbar: Label policy used below 1500 pixels to preserve controls.

Invariants:
  - Every divisor is positive after input normalization.
  - Side-rail caps prevent ultrawide layouts from becoming tool-heavy.
  - The topology hash depends only on the versioned descriptor bytes.

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
