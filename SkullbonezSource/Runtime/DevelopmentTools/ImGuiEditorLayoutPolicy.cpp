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
  Preference text: Fixed key/value record saved only by the development editor.

Invariants:
  - Every divisor is positive after input normalization.
  - Side-rail caps prevent ultrawide layouts from becoming tool-heavy.
  - The topology hash depends only on the versioned descriptor bytes.
  - Letterbox math preserves source aspect and maps only pixels inside the image.
  - Unknown preference keys are ignored, but every required current key must be
    present before the record is accepted.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h
  - SkullbonezTests/TestOwnerRequestQueues.cpp
*/
#include "ImGuiEditorLayoutPolicy.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
namespace
{
template <typename T> bool ParseUnsigned( const char* begin, const char* end, T& outValue ) noexcept
{

    if ( !begin || !end || begin >= end )
    {
        return false;
    }

    const std::from_chars_result result = std::from_chars( begin, end, outValue );
    return result.ec == std::errc() && result.ptr == end;
}

void CopyPreferenceText( char* destination, std::size_t capacity, const char* begin, const char* end ) noexcept
{

    if ( !destination || capacity == 0u )
    {
        return;
    }

    const std::size_t sourceLength = begin && end && end >= begin ? static_cast<std::size_t>( end - begin ) : 0u;
    const std::size_t copyLength = (std::min)( sourceLength, capacity - 1u );

    if ( copyLength > 0u )
    {
        std::memcpy( destination, begin, copyLength );
    }

    destination[copyLength] = '\0';
}

void CopySanitizedPreferenceText( char* destination, std::size_t capacity, const char* source ) noexcept
{

    if ( !destination || capacity == 0u )
    {
        return;
    }

    std::size_t write = 0u;

    for ( std::size_t read = 0u; source && source[read] != '\0' && write + 1u < capacity; ++read )
    {
        const char value = source[read];

        if ( value != '\r' && value != '\n' )
        {
            destination[write++] = value;
        }
    }

    destination[write] = '\0';
}
} // namespace

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

ImGuiGameViewportRect ResolveImGuiGameViewportRect( float availableMinX, float availableMinY, float availableWidth,
                                                    float availableHeight, int sourceWidth, int sourceHeight,
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

bool MapImGuiGameViewportPoint( const ImGuiGameViewportRect& viewport, float clientX, float clientY, int& outSourceX,
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

const char* ImGuiEditorPanelName( ImGuiEditorPanelId panel ) noexcept
{

    switch ( panel )
    {
    case ImGuiEditorPanelId::SceneAndModes:
        return ImGuiEditorPanel::SceneAndModes;
    case ImGuiEditorPanelId::Hierarchy:
        return ImGuiEditorPanel::Hierarchy;
    case ImGuiEditorPanelId::AssetsCreate:
        return ImGuiEditorPanel::AssetsCreate;
    case ImGuiEditorPanelId::GameViewport:
        return ImGuiEditorPanel::GameViewport;
    case ImGuiEditorPanelId::Inspector:
        return ImGuiEditorPanel::Inspector;
    case ImGuiEditorPanelId::WorldSimulation:
        return ImGuiEditorPanel::WorldSimulation;
    case ImGuiEditorPanelId::Rendering:
        return ImGuiEditorPanel::Rendering;
    case ImGuiEditorPanelId::Diagnostics:
        return ImGuiEditorPanel::Diagnostics;
    case ImGuiEditorPanelId::Causality:
        return ImGuiEditorPanel::Causality;
    case ImGuiEditorPanelId::CausalityDetail:
        return ImGuiEditorPanel::CausalityDetail;
    case ImGuiEditorPanelId::Replay:
        return ImGuiEditorPanel::Replay;
    case ImGuiEditorPanelId::Status:
        return ImGuiEditorPanel::Status;
    case ImGuiEditorPanelId::Count:
    default:
        return nullptr;
    }
}

bool TryParseImGuiEditorPanel( const char* name, ImGuiEditorPanelId& outPanel ) noexcept
{

    if ( !name )
    {
        return false;
    }

    for ( uint32_t index = 0u; index < static_cast<uint32_t>( ImGuiEditorPanelId::Count ); ++index )
    {
        const ImGuiEditorPanelId candidate = static_cast<ImGuiEditorPanelId>( index );
        const char* candidateName = ImGuiEditorPanelName( candidate );

        if ( candidateName && _stricmp( candidateName, name ) == 0 )
        {
            outPanel = candidate;
            return true;
        }
    }

    return false;
}

ImGuiEditorPreferenceParseResult ParseImGuiEditorPreferences( const char* text, std::size_t textBytes ) noexcept
{
    ImGuiEditorPreferenceParseResult result;
    result.preferences.topologyFingerprint = FingerprintImGuiEditorDefaultTopology();

    if ( !text || textBytes == 0u || textBytes >= IMGUI_EDITOR_PREFERENCES_TEXT_CAPACITY )
    {
        return result;
    }

    bool hasSchema = false;
    bool hasLayout = false;
    bool hasTopology = false;
    bool hasPanels = false;
    bool malformed = false;
    const char* cursor = text;
    const char* const textEnd = text + textBytes;

    while ( cursor < textEnd && !malformed )
    {
        const char* lineEnd = cursor;

        while ( lineEnd < textEnd && *lineEnd != '\n' && *lineEnd != '\r' )
        {
            ++lineEnd;
        }

        const char* separator = cursor;

        while ( separator < lineEnd && *separator != '=' )
        {
            ++separator;
        }

        if ( separator < lineEnd )
        {
            const char* value = separator + 1;
            const std::size_t keyLength = static_cast<std::size_t>( separator - cursor );

            if ( keyLength == 6u && std::memcmp( cursor, "schema", keyLength ) == 0 )
            {
                uint32_t parsed = 0u;
                malformed = !ParseUnsigned( value, lineEnd, parsed );
                result.preferences.preferencesVersion = static_cast<int>( parsed );
                hasSchema = !malformed;
            }
            else if ( keyLength == 6u && std::memcmp( cursor, "layout", keyLength ) == 0 )
            {
                uint32_t parsed = 0u;
                malformed = !ParseUnsigned( value, lineEnd, parsed );
                result.preferences.layoutVersion = static_cast<int>( parsed );
                hasLayout = !malformed;
            }
            else if ( keyLength == 8u && std::memcmp( cursor, "topology", keyLength ) == 0 )
            {
                malformed = !ParseUnsigned( value, lineEnd, result.preferences.topologyFingerprint );
                hasTopology = !malformed;
            }
            else if ( keyLength == 6u && std::memcmp( cursor, "panels", keyLength ) == 0 )
            {
                malformed = !ParseUnsigned( value, lineEnd, result.preferences.panelVisibilityMask );
                hasPanels = !malformed;
            }
            else if ( keyLength == 12u && std::memcmp( cursor, "scene_filter", keyLength ) == 0 )
            {
                CopyPreferenceText( result.preferences.sceneFilter, sizeof( result.preferences.sceneFilter ), value,
                                    lineEnd );
            }
            else if ( keyLength == 16u && std::memcmp( cursor, "hierarchy_filter", keyLength ) == 0 )
            {
                CopyPreferenceText( result.preferences.hierarchyFilter, sizeof( result.preferences.hierarchyFilter ), value,
                                    lineEnd );
            }
            else if ( keyLength == 12u && std::memcmp( cursor, "asset_filter", keyLength ) == 0 )
            {
                CopyPreferenceText( result.preferences.assetFilter, sizeof( result.preferences.assetFilter ), value,
                                    lineEnd );
            }
        }

        while ( lineEnd < textEnd && ( *lineEnd == '\n' || *lineEnd == '\r' ) )
        {
            ++lineEnd;
        }

        cursor = lineEnd;
    }

    const bool currentSchema = result.preferences.preferencesVersion == IMGUI_EDITOR_PREFERENCES_VERSION;
    const bool validPanelMask = ( result.preferences.panelVisibilityMask & ~IMGUI_EDITOR_ALL_PANEL_MASK ) == 0u;
    result.valid = !malformed && hasSchema && hasLayout && hasTopology && hasPanels && currentSchema && validPanelMask;

    if ( !result.valid )
    {
        result.preferences = {};

        result.preferences.topologyFingerprint = FingerprintImGuiEditorDefaultTopology();
        return result;
    }

    result.layoutResetRequired = result.preferences.layoutVersion != IMGUI_EDITOR_LAYOUT_VERSION ||
                                 result.preferences.topologyFingerprint != FingerprintImGuiEditorDefaultTopology();

    result.recoveredDefaults = result.layoutResetRequired;

    if ( result.layoutResetRequired )
    {

        // Why: migration may keep harmless text filters, but stale panel IDs
        // and docking authority must never enter the current topology.
        result.preferences.layoutVersion = IMGUI_EDITOR_LAYOUT_VERSION;
        result.preferences.topologyFingerprint = FingerprintImGuiEditorDefaultTopology();
        result.preferences.panelVisibilityMask = IMGUI_EDITOR_DEFAULT_PANEL_MASK;
    }

    return result;
}

std::size_t SerializeImGuiEditorPreferences( const ImGuiEditorPreferences& preferences, char* output,
                                             std::size_t outputCapacity ) noexcept
{

    if ( !output || outputCapacity == 0u )
    {
        return 0u;
    }

    char sceneFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};
    char hierarchyFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};

    char assetFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};

    CopySanitizedPreferenceText( sceneFilter, sizeof( sceneFilter ), preferences.sceneFilter );
    CopySanitizedPreferenceText( hierarchyFilter, sizeof( hierarchyFilter ), preferences.hierarchyFilter );
    CopySanitizedPreferenceText( assetFilter, sizeof( assetFilter ), preferences.assetFilter );
    const int written = snprintf( output, outputCapacity,
                                  "schema=%d\nlayout=%d\ntopology=%llu\npanels=%u\nscene_filter=%s\n"
                                  "hierarchy_filter=%s\nasset_filter=%s\n",
                                  IMGUI_EDITOR_PREFERENCES_VERSION, IMGUI_EDITOR_LAYOUT_VERSION,
                                  static_cast<unsigned long long>( FingerprintImGuiEditorDefaultTopology() ),
                                  preferences.panelVisibilityMask & IMGUI_EDITOR_ALL_PANEL_MASK, sceneFilter,
                                  hierarchyFilter, assetFilter );

    if ( written < 0 || static_cast<std::size_t>( written ) >= outputCapacity )
    {
        output[0] = '\0';
        return 0u;
    }

    return static_cast<std::size_t>( written );
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
