#include "SkullbonezUI.h"
#include "../SkullbonezInput.h"
#include "../SkullbonezPhysicsDebugVisualizer.h"
#include "../SkullbonezProfiler.h"
#include "../SkullbonezText.h"
#include "UIDraw.h"
#include "UIIconButton.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;

namespace
{
constexpr int PROFILER_UI_MAX_MARKERS = 64;
constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;
constexpr int RENDERER_GL = 0;
constexpr int RENDERER_DX11 = 1;
constexpr int RENDERER_DX12 = 2;
constexpr float CONTENT_TOGGLE_ROW_H = 30.0f;
constexpr float CONTROL_TOGGLE_ROW_H = 26.0f;
constexpr float UI_TIME_SCALE_MIN = 0.10f;
constexpr float UI_TIME_SCALE_MAX = 10.00f;
constexpr float UI_TIME_SCALE_STEP = 0.05f;
constexpr int UI_MODEL_COUNT_MIN = 0;
constexpr int UI_MODEL_COUNT_MAX = 1000;
constexpr float UI_PHYSICS_ALPHA_MIN = 0.05f;
constexpr float UI_PHYSICS_ALPHA_MAX = 1.00f;
constexpr float UI_PHYSICS_ALPHA_STEP = 0.01f;
constexpr float UI_CONTACT_LINGER_MIN = 0.00f;
constexpr float UI_CONTACT_LINGER_MAX = 5.00f;
constexpr float UI_CONTACT_LINGER_STEP = 0.05f;
constexpr int UI_FRAME_COUNT_MIN = 0;
constexpr int UI_FRAME_COUNT_MAX = 5000;
constexpr int UI_SEED_MIN = 1;
constexpr int UI_SEED_MAX = 999999;
constexpr int UI_SOLVER_COUNT_MIN = 0;
constexpr int UI_SOLVER_COUNT_MAX = 1000;
constexpr float UI_TRACK_HEIGHT_MIN = 0.0f;
constexpr float UI_TRACK_HEIGHT_MAX = 600.0f;
constexpr float UI_TRACK_HEIGHT_STEP = 5.0f;
constexpr float UI_AUTO_CYCLE_MIN = 0.0f;
constexpr float UI_AUTO_CYCLE_MAX = 10.0f;
constexpr float UI_AUTO_CYCLE_STEP = 0.10f;
constexpr float UI_WORLD_GRAVITY_MIN = 0.0f;
constexpr float UI_WORLD_GRAVITY_MAX = 100.0f;
constexpr float UI_WORLD_GRAVITY_STEP = 0.50f;
constexpr float UI_WORLD_FLUID_HEIGHT_MIN = -100.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_MAX = 200.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_STEP = 1.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MIN = 0.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MAX = 5.0f;
constexpr float UI_WORLD_FLUID_DENSITY_STEP = 0.05f;
constexpr int UI_SCENE_COMBO_VISIBLE_OPTIONS = 12;
constexpr float UI_SCENE_HEADER_BUTTON_GAP = 8.0f;
constexpr float UI_SCENE_RESET_BUTTON_W = 72.0f;
constexpr float UI_SCENE_RESET_DEFAULTS_BUTTON_W = 132.0f;
constexpr float UI_SCENE_DEMO_BUTTON_W = 112.0f;

int GetRendererIndexFromName( const char* rendererName )
{
    if ( rendererName && strstr( rendererName, "12" ) )
    {
        return RENDERER_DX12;
    }
    if ( rendererName && strstr( rendererName, "11" ) )
    {
        return RENDERER_DX11;
    }
    return RENDERER_GL;
}

bool ProfilerMarkerHasChildren( const Profiler& profiler, int markerIndex )
{
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        if ( profiler.GetMarker( i ).parentIndex == markerIndex )
        {
            return true;
        }
    }
    return false;
}


int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.waterNoReflect )
    {
        return 2;
    }
    return data.waterRTReflect ? 1 : 0;
}


int PhysicsModeFromData( const InGameUIFrameData& data )
{
    return data.legacyPhysics ? 0 : 1;
}


int SceneComboVisibleCount( int optionCount )
{
    return std::clamp( optionCount, 0, UI_SCENE_COMBO_VISIBLE_OPTIONS );
}


int ClampSceneComboScroll( int scroll, int optionCount )
{
    const int maxScroll = (std::max)( 0, optionCount - SceneComboVisibleCount( optionCount ) );
    return std::clamp( scroll, 0, maxScroll );
}


int SceneComboScrollForSelection( int selectedIndex, int optionCount )
{
    const int visibleCount = SceneComboVisibleCount( optionCount );
    if ( selectedIndex < 0 || visibleCount <= 0 )
    {
        return 0;
    }
    return ClampSceneComboScroll( selectedIndex - visibleCount / 2, optionCount );
}


float SceneTabComboWidth( float contentW )
{
    const float maxComboW = (std::min)( contentW, 520.0f );
    const float buttonW = UI_SCENE_RESET_BUTTON_W +
                          UI_SCENE_RESET_DEFAULTS_BUTTON_W +
                          UI_SCENE_DEMO_BUTTON_W +
                          UI_SCENE_HEADER_BUTTON_GAP * 3.0f;
    const float withButtons = contentW - buttonW;
    return (std::max)( 180.0f, (std::min)( maxComboW, withButtons ) );
}


bool IsVirtualKeyDown( int virtualKey )
{
    return ( GetKeyState( virtualKey ) & 0x8000 ) != 0;
}


char LowerAscii( char value )
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>( value + ( 'a' - 'A' ) ) : value;
}


bool SceneFilterMatches( const char* option, const char* filter )
{
    if ( !filter || filter[0] == '\0' )
    {
        return true;
    }
    if ( !option )
    {
        return false;
    }

    for ( int optionStart = 0; option[optionStart] != '\0'; ++optionStart )
    {
        int optionOffset = 0;
        while ( filter[optionOffset] != '\0' &&
                option[optionStart + optionOffset] != '\0' &&
                LowerAscii( option[optionStart + optionOffset] ) == LowerAscii( filter[optionOffset] ) )
        {
            ++optionOffset;
        }
        if ( filter[optionOffset] == '\0' )
        {
            return true;
        }
    }
    return false;
}

enum class TitleButtonIcon
{
    Minimize,
    Maximize,
    Restore,
    Close
};


void DrawTitleButton( const UIDrawContext& draw, const UIRect& bounds, TitleButtonIcon icon, bool hot, bool active )
{
    const float bgR = hot ? 0.050f : ( active ? 0.038f : 0.026f );
    const float bgG = hot ? 0.210f : ( active ? 0.145f : 0.080f );
    const float bgB = hot ? 0.285f : ( active ? 0.188f : 0.102f );
    const float outlineR = hot ? 0.44f : 0.18f;
    const float outlineG = hot ? 0.92f : 0.40f;
    const float outlineB = hot ? 1.00f : 0.48f;
    const float outlineA = hot ? 0.96f : ( active ? 0.90f : 0.58f );
    const float iconR = icon == TitleButtonIcon::Close && hot ? 0.95f : 0.68f;
    const float iconG = icon == TitleButtonIcon::Close && hot ? 0.99f : 0.86f;
    const float iconB = 1.00f;
    const float iconA = hot || active ? 0.98f : 0.88f;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, bgR, bgG, bgB, hot ? 0.92f : 0.78f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, outlineR, outlineG, outlineB, outlineA );
    if ( hot )
    {
        draw.Rect( bounds.x + 1.0f, bounds.y + 1.0f, bounds.w - 2.0f, 1.0f, 0.44f, 0.92f, 1.0f, 0.32f );
    }

    switch ( icon )
    {
        case TitleButtonIcon::Minimize:
            draw.Rect( cx - 5.0f, cy + 4.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
            break;
        case TitleButtonIcon::Maximize:
            draw.Outline( cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, iconR, iconG, iconB, iconA );
            draw.Rect( cx - 6.0f, cy - 6.0f, 12.0f, 2.0f, iconR, iconG, iconB, iconA );
            break;
        case TitleButtonIcon::Restore:
            draw.Outline( cx - 2.0f, cy - 7.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA * 0.72f );
            draw.Rect( cx - 2.0f, cy - 7.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA * 0.72f );
            draw.Outline( cx - 7.0f, cy - 2.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA );
            draw.Rect( cx - 7.0f, cy - 2.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
            break;
        case TitleButtonIcon::Close:
            for ( int i = 0; i < 5; ++i )
            {
                const float offset = static_cast<float>( i ) * 2.0f;
                draw.Rect( cx - 5.0f + offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );
                if ( i != 2 )
                {
                    draw.Rect( cx + 3.0f - offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );
                }
            }
            break;
    }
}


int CountFilteredSceneOptions( const char* const* options, int optionCount, const char* filter )
{
    if ( !options || optionCount <= 0 )
    {
        return 0;
    }

    int count = 0;
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( SceneFilterMatches( options[i], filter ) )
        {
            ++count;
        }
    }
    return count;
}


int FindFilteredSceneOptionIndex( const char* const* options, int optionCount, const char* filter, int filteredIndex )
{
    if ( !options || filteredIndex < 0 )
    {
        return -1;
    }

    int filteredPosition = 0;
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( SceneFilterMatches( options[i], filter ) )
        {
            if ( filteredPosition == filteredIndex )
            {
                return i;
            }
            ++filteredPosition;
        }
    }
    return -1;
}


int SceneFilteredPositionForIndex( const char* const* options, int optionCount, const char* filter, int optionIndex )
{
    if ( !options || optionIndex < 0 || optionIndex >= optionCount )
    {
        return -1;
    }

    int filteredPosition = 0;
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( !SceneFilterMatches( options[i], filter ) )
        {
            continue;
        }
        if ( i == optionIndex )
        {
            return filteredPosition;
        }
        ++filteredPosition;
    }
    return -1;
}


float ProfilerMarkerDisplayCpuMs( const Profiler::Marker& marker )
{
    return marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
}


UIRect MinimizedRect( int screenW, int screenH, float requestedW )
{
    constexpr float h = 38.0f;
    constexpr float margin = 14.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float w = std::clamp( requestedW, 154.0f, maxW );
    return { margin, (std::max)( margin, static_cast<float>( screenH ) - h - margin ), w, h };
}

float MinimizedWidthForTitle( const char* title, int screenW )
{
    constexpr float margin = 14.0f;
    constexpr float textSize = 12.5f;
    constexpr float chromeW = 76.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float textW = Text2d::MeasureText( textSize, title ? title : "" );
    return std::clamp( textW + chromeW, 154.0f, maxW );
}

float GravityStrengthFromWorld( float gravity )
{
    return std::clamp( -gravity, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}


float WorldGravityFromStrength( float strength )
{
    return -std::clamp( strength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}


void BuildWindowTitle( const InGameUIFrameData& data, char* out, size_t outSize )
{
    if ( outSize == 0 )
    {
        return;
    }

    if ( data.sceneMode && data.sceneName && data.sceneName[0] != '\0' )
    {
        const int displayedFrame = ( data.testComplete && data.targetFrameCount > 0 && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount : data.currentFrame;
        if ( data.testComplete )
        {
            if ( data.targetFrameCount > 0 )
            {
                snprintf( out, outSize, "%s  %d/%d complete", data.sceneName, displayedFrame, data.targetFrameCount );
            }
            else
            {
                snprintf( out, outSize, "%s  complete", data.sceneName );
            }
        }
        else if ( data.targetFrameCount > 0 )
        {
            snprintf( out, outSize, "%s  %d/%d", data.sceneName, displayedFrame, data.targetFrameCount );
        }
        else
        {
            snprintf( out, outSize, "%s  frame %d", data.sceneName, displayedFrame );
        }
    }
    else
    {
        snprintf( out, outSize, "Skullbonez Core" );
    }

    out[outSize - 1] = '\0';
}

void FitTitleText( char* text, size_t textSize, float fontSize, float maxWidth )
{
    if ( textSize == 0 || Text2d::MeasureText( fontSize, text ) <= maxWidth )
    {
        return;
    }

    char original[192] = {};
    strcpy_s( original, sizeof( original ), text );
    const size_t len = strlen( original );
    for ( size_t start = 1; start < len; ++start )
    {
        snprintf( text, textSize, "...%s", original + start );
        if ( Text2d::MeasureText( fontSize, text ) <= maxWidth )
        {
            return;
        }
    }

    snprintf( text, textSize, "..." );
}
} // namespace

bool InGameUI::IsVisible() const
{
    return m_isVisible;
}


void InGameUI::SetVisible( bool visible, double now )
{
    m_isVisible = visible;
    m_backdropBlur.Invalidate();
    if ( visible )
    {
        m_isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
    }
    else
    {
        m_isMinimized = true;
        m_isDragging = false;
        m_isResizing = false;
        m_blocksCameraMouse = false;
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        m_physicsModeCombo.Close();
        CloseSceneCombo();
    }
}


void InGameUI::ToggleVisible( double now )
{
    if ( !m_isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_isMinimized, now );
}


void InGameUI::SetMinimized( bool minimized, double now )
{
    if ( m_isMinimized == minimized )
    {
        return;
    }
    m_isMinimized = minimized;
    m_isDragging = false;
    m_isResizing = false;
    m_blocksCameraMouse = false;
    if ( minimized )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        m_physicsModeCombo.Close();
        CloseSceneCombo();
        m_activeSlider = 0;
    }
    else
    {
        m_scrollbarVisibleUntil = now + 1.2;
    }
    m_backdropBlur.Invalidate();
}


void InGameUI::SetActiveTab( InGameUITab tab )
{
    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    m_physicsModeCombo.Close();
    CloseSceneCombo();
    m_activeSlider = 0;
    m_previewTimeScale = -1.0f;
    m_previewModelCount = -1;
    m_previewPhysicsAlpha = -1.0f;
    m_previewContactLinger = -1.0f;
    m_previewSolverBallCount = -1;
    m_previewSolverBoxCount = -1;
    m_backdropBlur.Invalidate();
}


InGameUITab InGameUI::GetActiveTab() const
{
    return m_activeTab;
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_isVisible && !m_isMinimized && m_sceneCombo.IsOpen();
}


void InGameUI::SetWindowBounds( int x, int y, int width, int height )
{
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
    m_isMaximized = false;
    m_scrollY = 0.0f;
    m_scrollbarVisibleUntil = 0.0;
    m_backdropBlur.Invalidate();
}


void InGameUI::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate();
    }
}


void InGameUI::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );
}


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
    if ( open )
    {
        CaptureSceneFilterKeyState();
    }
    else
    {
        ClearSceneFilter();
    }
}


void InGameUI::SetSceneFilter( const char* filter )
{
    strncpy_s( m_sceneFilter, sizeof( m_sceneFilter ), filter ? filter : "", _TRUNCATE );
    m_sceneComboScroll = 0;
}


void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    m_expandAllProfilerMarkers = expandAll;
    m_expandedProfilerHashCount = 0;
    m_profilerDefaultExpansionApplied = false;
    if ( expandAll )
    {
        ApplyProfilerExpandAll();
    }
}


void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    m_profilerTimelineEnabled = enabled;
}


void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    m_performanceHistogramEnabled = enabled;
    if ( !enabled )
    {
        m_performanceHistogramCount = 0;
        m_performanceHistogramHead = 0;
        m_performanceHistogramAxisMs = 16.67f;
    }
}


void InGameUI::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarVisibleUntil = 1.2;
}


void InGameUI::SetMouseOverride( bool enabled, int x, int y )
{
    m_hasMouseOverride = enabled;
    m_mouseOverrideX = x;
    m_mouseOverrideY = y;
    if ( enabled )
    {
        m_mouseX = x;
        m_mouseY = y;
    }
}


void InGameUI::SetMaximized( bool maximized, int screenW, int screenH )
{
    if ( m_isMaximized == maximized )
    {
        return;
    }

    constexpr int minW = 390;
    constexpr int minH = 250;
    constexpr int margin = 10;
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    if ( maximized )
    {
        m_restoreX = m_x;
        m_restoreY = m_y;
        m_restoreW = m_width;
        m_restoreH = m_height;
        m_x = margin;
        m_y = margin;
        m_width = maxW;
        m_height = maxH;
    }
    else
    {
        m_x = std::clamp( m_restoreX, margin, (std::max)( margin, screenW - m_restoreW - margin ) );
        m_y = std::clamp( m_restoreY, margin, (std::max)( margin, screenH - m_restoreH - margin ) );
        m_width = std::clamp( m_restoreW, minW, maxW );
        m_height = std::clamp( m_restoreH, minH, maxH );
    }

    m_isMaximized = maximized;
    m_scrollbarVisibleUntil = 0.0;
    m_backdropBlur.Invalidate();
}


void InGameUI::ResetResources()
{
    m_backdropBlur.ResetResources();
}


void InGameUI::ApplyProfilerExpandAll()
{
    if ( !m_expandAllProfilerMarkers )
    {
        return;
    }

    const Profiler& profiler = Profiler::Instance();
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( ProfilerMarkerHasChildren( profiler, i ) && !IsProfilerMarkerExpanded( marker.hash ) && m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
        {
            m_expandedProfilerHashes[m_expandedProfilerHashCount++] = marker.hash;
        }
    }
}


void InGameUI::ApplyProfilerDefaultExpansion()
{
    if ( m_profilerDefaultExpansionApplied )
    {
        return;
    }

    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    if ( markerCount <= 0 )
    {
        return;
    }

    for ( int i = 0; i < markerCount; ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( marker.parentIndex == -1 &&
             ProfilerMarkerHasChildren( profiler, i ) &&
             !IsProfilerMarkerExpanded( marker.hash ) &&
             m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
        {
            m_expandedProfilerHashes[m_expandedProfilerHashCount++] = marker.hash;
        }
    }

    m_profilerDefaultExpansionApplied = true;
}


int InGameUI::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUITab::Keys:
        return 1024;
    case InGameUITab::Profiler:
    {
        int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
        const int visibleMarkerCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
        return 54 + visibleMarkerCount * 30;
    }
    case InGameUITab::Physics:
        return 384;
    case InGameUITab::Options:
        return 410;
    default:
        return 330;
    }
}


int InGameUI::BuildVisibleProfilerRows( int* rows, int maxRows ) const
{
    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    int childIndices[PROFILER_UI_MAX_MARKERS][PROFILER_UI_MAX_MARKERS] = {};
    int childCounts[PROFILER_UI_MAX_MARKERS] = {};

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = profiler.GetMarker( i ).parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    int stack[PROFILER_UI_MAX_MARKERS] = {};
    int stackTop = 0;
    for ( int i = markerCount - 1; i >= 0; --i )
    {
        if ( profiler.GetMarker( i ).parentIndex == -1 )
        {
            stack[stackTop++] = i;
        }
    }

    int rowCount = 0;
    while ( stackTop > 0 )
    {
        const int markerIndex = stack[--stackTop];
        if ( rowCount < maxRows )
        {
            rows[rowCount] = markerIndex;
        }
        ++rowCount;

        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        if ( !IsProfilerMarkerExpanded( marker.hash ) )
        {
            continue;
        }
        for ( int child = childCounts[markerIndex] - 1; child >= 0; --child )
        {
            if ( stackTop < PROFILER_UI_MAX_MARKERS )
            {
                stack[stackTop++] = childIndices[markerIndex][child];
            }
        }
    }

    return (std::min)( rowCount, maxRows );
}


void InGameUI::BuildProfilerTimelineSegments( const int* rows, int rowCount, ProfilerTimelineSegment* segments ) const
{
    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    int rowForMarker[PROFILER_UI_MAX_MARKERS] = {};
    int childIndices[PROFILER_UI_MAX_MARKERS][PROFILER_UI_MAX_MARKERS] = {};
    int childCounts[PROFILER_UI_MAX_MARKERS] = {};

    for ( int i = 0; i < PROFILER_UI_MAX_MARKERS; ++i )
    {
        rowForMarker[i] = -1;
    }
    for ( int row = 0; row < rowCount; ++row )
    {
        segments[row] = {};
        if ( rows[row] >= 0 && rows[row] < markerCount )
        {
            rowForMarker[rows[row]] = row;
        }
    }

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = profiler.GetMarker( i ).parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    auto assignSubtree = [&]( auto&& self, int markerIndex, float startMs ) -> void
    {
        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        const bool hasChildren = childCounts[markerIndex] > 0;
        const bool expanded = hasChildren && IsProfilerMarkerExpanded( marker.hash );
        const int row = rowForMarker[markerIndex];
        const float markerMs = (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( marker ) );

        if ( row >= 0 )
        {
            segments[row].startMs = startMs;
            segments[row].durationMs = markerMs;
            segments[row].isFilled = !expanded;
        }

        if ( !expanded )
        {
            return;
        }

        float childStartMs = startMs;
        for ( int child = 0; child < childCounts[markerIndex]; ++child )
        {
            const int childIndex = childIndices[markerIndex][child];
            self( self, childIndex, childStartMs );
            childStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( profiler.GetMarker( childIndex ) ) );
        }
    };

    float rootStartMs = 0.0f;
    for ( int i = 0; i < markerCount; ++i )
    {
        if ( profiler.GetMarker( i ).parentIndex == -1 )
        {
            assignSubtree( assignSubtree, i, rootStartMs );
            rootStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( profiler.GetMarker( i ) ) );
        }
    }
}


bool InGameUI::IsProfilerMarkerExpanded( uint32_t hash ) const
{
    for ( int i = 0; i < m_expandedProfilerHashCount; ++i )
    {
        if ( m_expandedProfilerHashes[i] == hash )
        {
            return true;
        }
    }
    return false;
}


void InGameUI::ToggleProfilerMarker( uint32_t hash )
{
    m_profilerDefaultExpansionApplied = true;
    for ( int i = 0; i < m_expandedProfilerHashCount; ++i )
    {
        if ( m_expandedProfilerHashes[i] == hash )
        {
            for ( int j = i; j < m_expandedProfilerHashCount - 1; ++j )
            {
                m_expandedProfilerHashes[j] = m_expandedProfilerHashes[j + 1];
            }
            --m_expandedProfilerHashCount;
            return;
        }
    }
    if ( m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
    {
        m_expandedProfilerHashes[m_expandedProfilerHashCount++] = hash;
    }
}


void InGameUI::PushPerformanceHistogramSample( float cpuMs, float gpuMs )
{
    cpuMs = std::clamp( cpuMs, 0.0f, 250.0f );
    gpuMs = std::clamp( gpuMs, 0.0f, 250.0f );

    float previousMaxMs = 0.0f;
    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        previousMaxMs = (std::max)( previousMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    const float sampleMaxMs = (std::max)( cpuMs, gpuMs );
    PerformanceHistogramSample& writeSample = m_performanceHistogramSamples[m_performanceHistogramHead];
    writeSample.cpuMs = cpuMs;
    writeSample.gpuMs = gpuMs;
    writeSample.spikeMs = 0.0f;
    if ( m_performanceHistogramCount > 8 &&
         sampleMaxMs > 1.0f &&
         sampleMaxMs > (std::max)( previousMaxMs * 1.20f, m_performanceHistogramAxisMs * 0.92f ) )
    {
        writeSample.spikeMs = sampleMaxMs;
    }

    m_performanceHistogramHead = ( m_performanceHistogramHead + 1 ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
    if ( m_performanceHistogramCount < PERFORMANCE_HISTOGRAM_SAMPLE_COUNT )
    {
        ++m_performanceHistogramCount;
    }

    float visibleMaxMs = 0.0f;
    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        visibleMaxMs = (std::max)( visibleMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    float targetAxisMs = 8.0f;
    const float targetRawMs = (std::max)( 8.0f, visibleMaxMs * 1.18f );
    while ( targetAxisMs < targetRawMs )
    {
        targetAxisMs += targetAxisMs < 32.0f ? 4.0f : 8.0f;
    }

    if ( targetAxisMs > m_performanceHistogramAxisMs )
    {
        m_performanceHistogramAxisMs = targetAxisMs;
    }
    else
    {
        m_performanceHistogramAxisMs += ( targetAxisMs - m_performanceHistogramAxisMs ) * 0.055f;
    }
}


void InGameUI::DrawPerformanceHistogram( const UIDrawContext& draw, const InGameUIFrameData& data ) const
{
    if ( m_performanceHistogramCount <= 0 )
    {
        return;
    }

    const float panelX = 16.0f;
    const float panelY = 16.0f;
    const float panelW = (std::max)( 180.0f, (std::min)( 260.0f, static_cast<float>( data.screenW ) - 32.0f ) );
    const float panelH = 116.0f;
    const float plotX = panelX + 12.0f;
    const float plotY = panelY + 32.0f;
    const float plotW = panelW - 24.0f;
    const float plotH = 58.0f;
    const float baseY = plotY + plotH;
    const float axisMs = (std::max)( 1.0f, m_performanceHistogramAxisMs );

    draw.Rect( panelX - 5.0f, panelY - 5.0f, panelW + 10.0f, panelH + 10.0f, 0.03f, 0.54f, 0.86f, 0.10f );
    draw.Rect( panelX, panelY, panelW, panelH, 0.012f, 0.030f, 0.040f, 0.76f );
    draw.Outline( panelX, panelY, panelW, panelH, 0.39f, 0.88f, 1.0f, 0.82f );
    draw.Rect( panelX + 1.0f, panelY + 1.0f, panelW - 2.0f, 1.0f, 0.44f, 0.92f, 1.0f, 0.30f );
    draw.Text( panelX + 10.0f, panelY + 8.0f, 10.5f, 1.0f, 0.85f, 0.34f, "Frame Time" );

    char text[64] = {};
    snprintf( text, sizeof( text ), "%.0f ms", axisMs );
    draw.Text( panelX + panelW - 58.0f, panelY + 8.0f, 10.0f, 0.68f, 0.86f, 0.92f, text );

    draw.Rect( plotX, plotY, plotW, plotH, 0.005f, 0.014f, 0.020f, 0.68f );
    draw.Rect( plotX, plotY + plotH * 0.50f, plotW, 1.0f, 0.18f, 0.30f, 0.34f, 0.45f );
    draw.Rect( plotX, plotY, plotW, 1.0f, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( plotX, baseY, plotW, 1.0f, 0.26f, 0.82f, 1.0f, 0.34f );

    const float step = plotW / static_cast<float>( PERFORMANCE_HISTOGRAM_SAMPLE_COUNT );
    const float barW = (std::max)( 1.0f, step * 0.42f );
    float spikeX = -1.0f;
    float spikeY = plotY;
    float spikeMs = 0.0f;

    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        const float x = plotX + static_cast<float>( PERFORMANCE_HISTOGRAM_SAMPLE_COUNT - m_performanceHistogramCount + i ) * step;
        const float cpuH = std::clamp( sample.cpuMs / axisMs, 0.0f, 1.0f ) * plotH;
        const float gpuH = std::clamp( sample.gpuMs / axisMs, 0.0f, 1.0f ) * plotH;

        if ( cpuH > 0.5f )
        {
            draw.Rect( x, baseY - cpuH, barW, cpuH, 0.48f, 0.90f, 0.22f, 0.66f );
        }
        if ( gpuH > 0.5f )
        {
            draw.Rect( x + barW + 0.5f, baseY - gpuH, barW, gpuH, 0.34f, 0.91f, 1.0f, 0.78f );
        }
        if ( sample.spikeMs > spikeMs )
        {
            spikeMs = sample.spikeMs;
            spikeX = x + barW;
            spikeY = baseY - std::clamp( sample.spikeMs / axisMs, 0.0f, 1.0f ) * plotH;
        }
    }

    if ( spikeMs > 0.0f && spikeX >= plotX )
    {
        snprintf( text, sizeof( text ), "%.1f ms", spikeMs );
        const float labelX = ( spikeX + 54.0f < panelX + panelW ) ? spikeX + 4.0f : spikeX - 54.0f;
        const float labelY = (std::max)( plotY + 2.0f, spikeY - 16.0f );
        draw.Rect( spikeX, plotY, 1.0f, plotH, 1.0f, 0.85f, 0.34f, 0.58f );
        draw.Text( labelX, labelY, 9.5f, 1.0f, 0.85f, 0.34f, text );
    }

    const int newestIndex = ( m_performanceHistogramHead - 1 + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
    const PerformanceHistogramSample& newest = m_performanceHistogramSamples[newestIndex];
    snprintf( text, sizeof( text ), "CPU %.2f", newest.cpuMs );
    draw.Text( panelX + 10.0f, panelY + panelH - 20.0f, 10.0f, 0.48f, 0.90f, 0.22f, text );
    snprintf( text, sizeof( text ), "GPU %.2f", newest.gpuMs );
    draw.Text( panelX + 96.0f, panelY + panelH - 20.0f, 10.0f, 0.34f, 0.91f, 1.0f, text );
}


void InGameUI::ClearSceneFilter()
{
    m_sceneFilter[0] = '\0';
    m_sceneComboScroll = 0;
}


void InGameUI::CloseSceneCombo()
{
    m_sceneCombo.Close();
    ClearSceneFilter();
}


void InGameUI::CaptureSceneFilterKeyState()
{
    for ( int key = 0; key < 256; ++key )
    {
        m_sceneFilterKeyWasDown[key] = IsVirtualKeyDown( key );
    }
}


bool InGameUI::SceneFilterKeyPressed( int virtualKey )
{
    if ( virtualKey < 0 || virtualKey >= 256 )
    {
        return false;
    }

    const bool isDown = IsVirtualKeyDown( virtualKey );
    const bool wasPressed = isDown && !m_sceneFilterKeyWasDown[virtualKey];
    m_sceneFilterKeyWasDown[virtualKey] = isDown;
    return wasPressed;
}


void InGameUI::AppendSceneFilterChar( char value )
{
    const size_t len = strlen( m_sceneFilter );
    if ( len >= sizeof( m_sceneFilter ) - 1 )
    {
        return;
    }
    m_sceneFilter[len] = value;
    m_sceneFilter[len + 1] = '\0';
    m_sceneComboScroll = 0;
}


void InGameUI::BackspaceSceneFilter()
{
    const size_t len = strlen( m_sceneFilter );
    if ( len == 0 )
    {
        return;
    }
    m_sceneFilter[len - 1] = '\0';
    m_sceneComboScroll = 0;
}


void InGameUI::UpdateSceneFilterTyping( InGameUIInputResult& result, const char* const* sceneOptions, int sceneOptionCount )
{
    if ( !m_sceneCombo.IsOpen() || m_activeTab != InGameUITab::Scene )
    {
        return;
    }

    for ( int key = 'A'; key <= 'Z'; ++key )
    {
        if ( SceneFilterKeyPressed( key ) )
        {
            AppendSceneFilterChar( static_cast<char>( 'a' + key - 'A' ) );
            result.userInteracted = true;
        }
    }
    for ( int key = '0'; key <= '9'; ++key )
    {
        if ( SceneFilterKeyPressed( key ) )
        {
            AppendSceneFilterChar( static_cast<char>( key ) );
            result.userInteracted = true;
        }
    }

    const bool isShiftDown = IsVirtualKeyDown( VK_SHIFT );
    if ( SceneFilterKeyPressed( VK_SPACE ) )
    {
        AppendSceneFilterChar( ' ' );
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_OEM_MINUS ) )
    {
        AppendSceneFilterChar( isShiftDown ? '_' : '-' );
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_OEM_PERIOD ) )
    {
        AppendSceneFilterChar( '.' );
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_BACK ) )
    {
        BackspaceSceneFilter();
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_DELETE ) )
    {
        ClearSceneFilter();
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_ESCAPE ) )
    {
        if ( m_sceneFilter[0] != '\0' )
        {
            ClearSceneFilter();
        }
        else
        {
            CloseSceneCombo();
        }
        result.userInteracted = true;
    }
    if ( SceneFilterKeyPressed( VK_RETURN ) && m_sceneCombo.IsOpen() )
    {
        const int sceneIndex = FindFilteredSceneOptionIndex( sceneOptions, sceneOptionCount, m_sceneFilter, 0 );
        if ( sceneIndex >= 0 )
        {
            result.requestedSceneIndex = sceneIndex;
            CloseSceneCombo();
            result.userInteracted = true;
        }
    }
}


InGameUIInputResult InGameUI::UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption )
{
    InGameUIInputResult result;
    m_blocksCameraMouse = false;
    const int wheelDelta = Input::ConsumeMouseWheelDelta();
    if ( !m_isVisible )
    {
        return result;
    }
    ApplyProfilerDefaultExpansion();

    POINT mouse = Input::GetClientMouseCoordinates();
    m_mouseX = static_cast<int>( mouse.x );
    m_mouseY = static_cast<int>( mouse.y );
    if ( m_hasMouseOverride )
    {
        m_mouseX = m_mouseOverrideX;
        m_mouseY = m_mouseOverrideY;
    }

    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    const int minW = 430;
    const int minH = 250;
    const int margin = 10;
    const int titleH = 44;
    const int tabH = 54;
    const int bottomH = 88;
    const int contentPad = 18;
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    m_width = std::clamp( m_width, minW, maxW );
    m_height = std::clamp( m_height, minH, maxH );
    m_x = std::clamp( m_x, margin, (std::max)( margin, screenW - m_width - margin ) );
    m_y = std::clamp( m_y, margin, (std::max)( margin, screenH - m_height - margin ) );

    const bool leftNow = Input::IsLeftMouseDown();
    if ( m_isMinimized )
    {
        const UIRect minimized = MinimizedRect( screenW, screenH, m_minimizedWidth );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        if ( leftNow && !m_leftWasDown && insideMinimized )
        {
            SetMinimized( false, now );
            result.userInteracted = true;
        }
        m_leftWasDown = leftNow;
        m_blocksCameraMouse = insideMinimized;
        return result;
    }

    const bool inside = m_mouseX >= m_x && m_mouseX <= m_x + m_width &&
                        m_mouseY >= m_y && m_mouseY <= m_y + m_height;
    const bool inTitle = inside && m_mouseY < m_y + titleH;
    const bool inTabs = inside && m_mouseY >= m_y + titleH && m_mouseY < m_y + titleH + tabH;
    const bool inResize = !m_isMaximized && inside && m_mouseX >= m_x + m_width - 26 && m_mouseY >= m_y + m_height - 26;
    const int contentY = m_y + titleH + tabH + 12;
    const int contentH = (std::max)( 24, m_height - titleH - tabH - bottomH - contentPad );
    const int bottomY = m_y + m_height - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const UIRect minimizeButton = { static_cast<float>( m_x + m_width - 112 ), static_cast<float>( m_y + 8 ), 30.0f, 28.0f };
    const UIRect maximizeButton = { static_cast<float>( m_x + m_width - 76 ), static_cast<float>( m_y + 8 ), 30.0f, 28.0f };
    const UIRect closeButton = { static_cast<float>( m_x + m_width - 40 ), static_cast<float>( m_y + 8 ), 30.0f, 28.0f };

    m_tabBar.SetBounds( static_cast<float>( m_x + 14 ), static_cast<float>( m_y + titleH ), static_cast<float>( m_width - 28 ), static_cast<float>( tabH ) );
    m_blurToggle.SetBounds( static_cast<float>( m_x + 32 ), static_cast<float>( bottomY + 22 ), 100.0f, 24.0f );
    m_vsyncToggle.SetBounds( static_cast<float>( m_x + 166 ), static_cast<float>( bottomY + 22 ), 100.0f, 24.0f );
    m_histogramToggle.SetBounds( static_cast<float>( m_x + 292 ), static_cast<float>( bottomY + 22 ), 108.0f, 24.0f );
    m_timelineToggle.SetBounds( static_cast<float>( m_x + 32 ), static_cast<float>( bottomY + 48 ), 100.0f, 24.0f );
    m_rendererCombo.SetBounds( static_cast<float>( m_x + 292 ), static_cast<float>( bottomY + 48 ), 112.0f, 24.0f );
    m_rendererCombo.SetDropUp( true );

    if ( ( leftNow && ( inside || m_isDragging || m_isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && inside ) )
    {
        result.userInteracted = true;
    }

    UpdateSceneFilterTyping( result, sceneOptions, sceneOptionCount );

    bool wheelHandled = false;
    if ( wheelDelta != 0 && m_sceneCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( m_x + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        const float sceneComboW = SceneTabComboWidth( contentW );
        const int filteredSceneCount = CountFilteredSceneOptions( sceneOptions, sceneOptionCount, m_sceneFilter );
        const int visibleSceneOptions = SceneComboVisibleCount( filteredSceneCount );
        const int sceneDrawOptions = filteredSceneCount > 0 ? visibleSceneOptions : ( m_sceneFilter[0] != '\0' ? 1 : 0 );
        m_sceneCombo.SetBounds( contentX, rowBase, sceneComboW, 24.0f );
        m_sceneCombo.SetDropUp( false );
        if ( m_sceneCombo.HitBox( m_mouseX, m_mouseY ) || m_sceneCombo.HitOption( m_mouseX, m_mouseY, sceneDrawOptions ) >= 0 )
        {
            const int wheelSteps = wheelDelta / WHEEL_DELTA;
            m_sceneComboScroll = ClampSceneComboScroll( m_sceneComboScroll - wheelSteps, filteredSceneCount );
            wheelHandled = true;
        }
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / static_cast<float>( WHEEL_DELTA ) * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( leftNow && !m_leftWasDown )
    {
        if ( minimizeButton.Contains( m_mouseX, m_mouseY ) || closeButton.Contains( m_mouseX, m_mouseY ) )
        {
            SetMinimized( true, now );
        }
        else if ( maximizeButton.Contains( m_mouseX, m_mouseY ) )
        {
            SetMaximized( !m_isMaximized, screenW, screenH );
        }
        else if ( inResize )
        {
            m_isResizing = true;
            m_resizeStartMouseX = m_mouseX;
            m_resizeStartMouseY = m_mouseY;
            m_resizeStartW = m_width;
            m_resizeStartH = m_height;
            SetCapture( hwnd );
        }
        else if ( inTitle )
        {
            m_isDragging = true;
            m_dragOffsetX = m_mouseX - m_x;
            m_dragOffsetY = m_mouseY - m_y;
            SetCapture( hwnd );
        }
        else if ( inTabs )
        {
            static const int kTabCount = static_cast<int>( InGameUITab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );
            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUITab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
        }
        else if ( m_sceneCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( m_x + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                const float sceneComboW = SceneTabComboWidth( contentW );
                const int filteredSceneCount = CountFilteredSceneOptions( sceneOptions, sceneOptionCount, m_sceneFilter );
                const int visibleSceneOptions = SceneComboVisibleCount( filteredSceneCount );
                const int sceneDrawOptions = filteredSceneCount > 0 ? visibleSceneOptions : ( m_sceneFilter[0] != '\0' ? 1 : 0 );
                m_sceneComboScroll = ClampSceneComboScroll( m_sceneComboScroll, filteredSceneCount );
                m_sceneCombo.SetBounds( contentX, rowBase, sceneComboW, 24.0f );
                const float resetX = contentX + sceneComboW + UI_SCENE_HEADER_BUTTON_GAP;
                const float defaultsX = resetX + UI_SCENE_RESET_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
                const float demoX = defaultsX + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
                m_resetSceneButton.SetBounds( resetX, rowBase, UI_SCENE_RESET_BUTTON_W, 24.0f );
                m_resetDefaultsButton.SetBounds( defaultsX, rowBase, UI_SCENE_RESET_DEFAULTS_BUTTON_W, 24.0f );
                m_demoSceneButton.SetBounds( demoX, rowBase, UI_SCENE_DEMO_BUTTON_W, 24.0f );
                m_sceneCombo.SetDropUp( false );
                const int option = m_sceneCombo.HitOption( m_mouseX, m_mouseY, sceneDrawOptions );
                if ( m_resetSceneButton.HitTest( m_mouseX, m_mouseY ) )
                {
                    result.resetScene = true;
                    CloseSceneCombo();
                }
                else if ( m_resetDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
                {
                    result.resetSceneDefaults = true;
                    CloseSceneCombo();
                }
                else if ( m_demoSceneButton.HitTest( m_mouseX, m_mouseY ) )
                {
                    result.requestDemoScene = true;
                    CloseSceneCombo();
                }
                else if ( filteredSceneCount > 0 && option >= 0 && option < visibleSceneOptions )
                {
                    result.requestedSceneIndex = FindFilteredSceneOptionIndex( sceneOptions, sceneOptionCount, m_sceneFilter, m_sceneComboScroll + option );
                    CloseSceneCombo();
                }
                else if ( m_sceneCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    CloseSceneCombo();
                }
                else
                {
                    CloseSceneCombo();
                }
            }
            else
            {
                CloseSceneCombo();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_physicsModeCombo.Close();
        }
        else if ( m_reflectionCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Keys )
            {
                const float contentX = static_cast<float>( m_x + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                m_reflectionCombo.SetBounds( contentX, rowBase, 172.0f, 24.0f );
                m_reflectionCombo.SetDropUp( false );
                const int option = m_reflectionCombo.HitOption( m_mouseX, m_mouseY, 3 );
                if ( option >= 0 && option < 3 )
                {
                    result.requestedWaterReflectionMode = option;
                    m_reflectionCombo.Close();
                }
                else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    m_reflectionCombo.ToggleOpen();
                }
                else
                {
                    m_reflectionCombo.Close();
                }
            }
            else
            {
                m_reflectionCombo.Close();
            }
            m_rendererCombo.Close();
            m_physicsModeCombo.Close();
            CloseSceneCombo();
        }
        else if ( m_physicsModeCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Keys )
            {
                const float contentX = static_cast<float>( m_x + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                m_physicsModeCombo.SetBounds( contentX + 188.0f, rowBase, 172.0f, 24.0f );
                m_physicsModeCombo.SetDropUp( false );
                const int option = m_physicsModeCombo.HitOption( m_mouseX, m_mouseY, 2 );
                if ( option >= 0 && option < 2 )
                {
                    result.requestedPhysicsMode = option;
                    m_physicsModeCombo.Close();
                }
                else if ( m_physicsModeCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    m_physicsModeCombo.ToggleOpen();
                }
                else
                {
                    m_physicsModeCombo.Close();
                }
            }
            else
            {
                m_physicsModeCombo.Close();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_physicsModeCombo.Close();
            CloseSceneCombo();
        }
        else if ( m_rendererCombo.IsOpen() )
        {
            const int option = m_rendererCombo.HitOption( m_mouseX, m_mouseY, 3 );
            if ( option >= 0 && option < 3 )
            {
                result.requestedRendererIndex = option;
                m_rendererCombo.Close();
            }
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                m_physicsModeCombo.Close();
                CloseSceneCombo();
            }
            else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Profiler )
        {
            const int headerH = 32;
            const int rowH = 30;
            const int localY = static_cast<int>( static_cast<float>( m_mouseY - contentY ) + m_scrollY );
            const int targetRow = ( localY - headerH ) / rowH;
            if ( targetRow >= 0 )
            {
                const Profiler& profiler = Profiler::Instance();
                int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
                const int visibleRowCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
                if ( targetRow < visibleRowCount )
                {
                    const int markerIndex = visibleRows[targetRow];
                    const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
                    if ( ProfilerMarkerHasChildren( profiler, markerIndex ) )
                    {
                        const float plusX = static_cast<float>( m_x + contentPad + 12 + marker.depth * 18 );
                        const float plusY = static_cast<float>( contentY + headerH + targetRow * rowH ) - m_scrollY + 8.0f;
                        UIIconButton expander;
                        expander.SetBounds( plusX, plusY, 14.0f, 14.0f );
                        if ( expander.HitTest( m_mouseX, m_mouseY ) )
                        {
                            ToggleProfilerMarker( marker.hash );
                            m_scrollbarVisibleUntil = now + 1.2;
                        }
                    }
                }
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
        }
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( m_x + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float sceneComboW = SceneTabComboWidth( contentW );
            m_sceneCombo.SetBounds( contentX, rowBase, sceneComboW, 24.0f );
            const float resetX = contentX + sceneComboW + UI_SCENE_HEADER_BUTTON_GAP;
            const float defaultsX = resetX + UI_SCENE_RESET_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
            const float demoX = defaultsX + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
            m_resetSceneButton.SetBounds( resetX, rowBase, UI_SCENE_RESET_BUTTON_W, 24.0f );
            m_resetDefaultsButton.SetBounds( defaultsX, rowBase, UI_SCENE_RESET_DEFAULTS_BUTTON_W, 24.0f );
            m_demoSceneButton.SetBounds( demoX, rowBase, UI_SCENE_DEMO_BUTTON_W, 24.0f );
            m_sceneCombo.SetDropUp( false );
            if ( m_resetSceneButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.resetScene = true;
                CloseSceneCombo();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                m_physicsModeCombo.Close();
            }
            else if ( m_resetDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.resetSceneDefaults = true;
                CloseSceneCombo();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                m_physicsModeCombo.Close();
            }
            else if ( m_demoSceneButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.requestDemoScene = true;
                CloseSceneCombo();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                m_physicsModeCombo.Close();
            }
            else if ( m_sceneCombo.HitBox( m_mouseX, m_mouseY ) && sceneOptionCount > 0 )
            {
                ClearSceneFilter();
                CaptureSceneFilterKeyState();
                m_sceneComboScroll = SceneComboScrollForSelection( SceneFilteredPositionForIndex( sceneOptions, sceneOptionCount, m_sceneFilter, selectedSceneOption ), sceneOptionCount );
                m_sceneCombo.SetOpen( true );
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                m_physicsModeCombo.Close();
            }
            else
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Physics )
        {
            const float contentX = static_cast<float>( m_x + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            const float col1 = contentX;
            const float col2 = contentX + colW + 18.0f;
            const auto setToggle = [&]( int index, int row, int column ) -> void
            {
                const float tx = column == 0 ? col1 : col2;
                m_physicsToggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
            };

            setToggle( 0, 0, 0 );
            setToggle( 1, 1, 0 );
            setToggle( 2, 2, 0 );
            setToggle( 3, 3, 0 );
            setToggle( 4, 0, 1 );
            setToggle( 5, 1, 1 );
            m_worldGravitySlider.SetBounds( contentX, rowBase + 276.0f, contentW, 34.0f );

            if ( m_physicsToggles[0].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleCollisionVisualizer = true;
            }
            else if ( m_physicsToggles[1].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_AXES;
            }
            else if ( m_physicsToggles[2].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
            }
            else if ( m_physicsToggles[3].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_SLEEP;
            }
            else if ( m_physicsToggles[4].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugTransparent = true;
            }
            else if ( m_physicsToggles[5].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleBroadphaseOverlay = true;
            }
            else if ( m_worldGravitySlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 11;
                result.requestWorldGravity = true;
                result.requestedWorldGravity = WorldGravityFromStrength( m_worldGravitySlider.ValueFromMouse( m_mouseX,
                                                                                                             UI_WORLD_GRAVITY_MIN,
                                                                                                             UI_WORLD_GRAVITY_MAX,
                                                                                                             UI_WORLD_GRAVITY_STEP ) );
                SetCapture( hwnd );
            }
            m_rendererCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( m_x + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            const float col1 = contentX;
            const float col2 = contentX + colW + 18.0f;
            const auto setToggle = [&]( int index, int row, int column ) -> void
            {
                const float tx = column == 0 ? col1 : col2;
                m_optionToggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
            };

            setToggle( 0, 0, 0 );
            setToggle( 1, 0, 1 );
            setToggle( 2, 1, 0 );
            setToggle( 3, 1, 1 );
            setToggle( 4, 2, 0 );
            setToggle( 5, 2, 1 );
            setToggle( 6, 3, 0 );
            setToggle( 7, 3, 1 );
            setToggle( 8, 4, 0 );
            m_timeScaleSlider.SetBounds( contentX, rowBase + 166.0f, contentW, 34.0f );
            m_modelCountSlider.SetBounds( contentX, rowBase + 214.0f, contentW, 34.0f );
            m_saveDefaultsButton.SetBounds( col1, rowBase + 264.0f, 132.0f, 32.0f );
            m_resetSceneButton.SetBounds( col2, rowBase + 264.0f, 124.0f, 32.0f );

            if ( m_optionToggles[0].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleScenePhysics = true;
            }
            else if ( m_optionToggles[1].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleSceneText = true;
            }
            else if ( m_optionToggles[2].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleFixedStep = true;
            }
            else if ( m_optionToggles[3].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleRollAlign = true;
            }
            else if ( m_optionToggles[4].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleTerrainHidden = true;
            }
            else if ( m_optionToggles[5].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterHidden = true;
            }
            else if ( m_optionToggles[6].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterFreeze = true;
            }
            else if ( m_optionToggles[7].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterFlat = true;
            }
            else if ( m_optionToggles[8].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterReflection = true;
            }
            else if ( m_timeScaleSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 1;
                m_previewTimeScale = m_timeScaleSlider.ValueFromMouse( m_mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
                result.requestedTimeScale = m_previewTimeScale;
                SetCapture( hwnd );
            }
            else if ( m_modelCountSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 2;
                m_previewModelCount = static_cast<int>( m_modelCountSlider.ValueFromMouse( m_mouseX,
                                                                                           static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                                           static_cast<float>( UI_MODEL_COUNT_MAX ),
                                                                                           1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_resetSceneButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.resetScene = true;
            }
            else if ( m_saveDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.saveSceneDefaults = true;
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Keys )
        {
            const float contentX = static_cast<float>( m_x + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( m_width ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            const float col1 = contentX;
            const float col2 = contentX + colW + 18.0f;
            const auto setToggle = [&]( int index, int row, int column ) -> void
            {
                const float tx = column == 0 ? col1 : col2;
                m_controlToggles[index].SetBounds( tx, rowBase + 34.0f + static_cast<float>( row ) * CONTROL_TOGGLE_ROW_H, colW, 24.0f );
            };

            m_reflectionCombo.SetBounds( contentX, rowBase, 172.0f, 24.0f );
            m_reflectionCombo.SetDropUp( false );
            m_physicsModeCombo.SetBounds( contentX + 188.0f, rowBase, 172.0f, 24.0f );
            m_physicsModeCombo.SetDropUp( false );
            setToggle( 0, 0, 0 );
            setToggle( 1, 0, 1 );
            setToggle( 2, 1, 0 );
            setToggle( 3, 1, 1 );
            setToggle( 4, 2, 0 );
            setToggle( 5, 2, 1 );
            setToggle( 6, 3, 0 );
            setToggle( 7, 3, 1 );
            setToggle( 8, 4, 0 );
            setToggle( 9, 4, 1 );
            setToggle( 10, 5, 0 );
            setToggle( 11, 5, 1 );
            setToggle( 12, 6, 0 );
            setToggle( 13, 6, 1 );
            setToggle( 14, 7, 0 );
            setToggle( 15, 7, 1 );
            m_modelCountSlider.SetBounds( contentX, rowBase + 262.0f, contentW, 34.0f );
            m_physicsAlphaSlider.SetBounds( contentX, rowBase + 302.0f, contentW, 34.0f );
            m_contactLingerSlider.SetBounds( contentX, rowBase + 342.0f, contentW, 34.0f );
            m_frameCountSlider.SetBounds( contentX, rowBase + 420.0f, contentW, 34.0f );
            m_seedSlider.SetBounds( contentX, rowBase + 460.0f, contentW, 34.0f );
            m_solverBallSlider.SetBounds( contentX, rowBase + 540.0f, contentW, 34.0f );
            m_solverBoxSlider.SetBounds( contentX, rowBase + 580.0f, contentW, 34.0f );
            m_trackHeightSlider.SetBounds( contentX, rowBase + 660.0f, contentW, 34.0f );
            m_autoCycleSlider.SetBounds( contentX, rowBase + 700.0f, contentW, 34.0f );
            m_worldGravitySlider.SetBounds( contentX, rowBase + 780.0f, contentW, 34.0f );
            m_worldFluidHeightSlider.SetBounds( contentX, rowBase + 820.0f, contentW, 34.0f );
            m_worldFluidDensitySlider.SetBounds( contentX, rowBase + 860.0f, contentW, 34.0f );
            m_saveDefaultsButton.SetBounds( col1, rowBase + 910.0f, 132.0f, 32.0f );
            m_resetSceneButton.SetBounds( col2, rowBase + 910.0f, 124.0f, 32.0f );

            if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_physicsModeCombo.Close();
            }
            else if ( m_physicsModeCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_physicsModeCombo.ToggleOpen();
                m_reflectionCombo.Close();
            }
            else if ( m_controlToggles[0].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleScenePhysics = true;
            }
            else if ( m_controlToggles[1].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleSceneText = true;
            }
            else if ( m_controlToggles[2].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleFixedStep = true;
            }
            else if ( m_controlToggles[3].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleRollAlign = true;
            }
            else if ( m_controlToggles[4].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleTerrainHidden = true;
            }
            else if ( m_controlToggles[5].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterHidden = true;
            }
            else if ( m_controlToggles[6].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleCollisionVisualizer = true;
            }
            else if ( m_controlToggles[7].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugTransparent = true;
            }
            else if ( m_controlToggles[8].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleBroadphaseOverlay = true;
            }
            else if ( m_controlToggles[9].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterFreeze = true;
            }
            else if ( m_controlToggles[10].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleWaterFlat = true;
            }
            else if ( m_controlToggles[11].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_AXES;
            }
            else if ( m_controlToggles[12].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
            }
            else if ( m_controlToggles[13].HitTest( m_mouseX, m_mouseY ) )
            {
                result.togglePhysicsDebugFlags = PHYSICS_DEBUG_SLEEP;
            }
            else if ( m_controlToggles[14].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleTextOnly = true;
            }
            else if ( m_controlToggles[15].HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleExitOnComplete = true;
            }
            else if ( m_modelCountSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 2;
                m_previewModelCount = static_cast<int>( m_modelCountSlider.ValueFromMouse( m_mouseX,
                                                                                           static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                                           static_cast<float>( UI_MODEL_COUNT_MAX ),
                                                                                           1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_physicsAlphaSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 3;
                m_previewPhysicsAlpha = m_physicsAlphaSlider.ValueFromMouse( m_mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );
                result.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
                SetCapture( hwnd );
            }
            else if ( m_contactLingerSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 4;
                m_previewContactLinger = m_contactLingerSlider.ValueFromMouse( m_mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );
                result.requestedPhysicsDebugContactLinger = m_previewContactLinger;
                SetCapture( hwnd );
            }
            else if ( m_frameCountSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 5;
                result.requestedFrameCount = static_cast<int>( m_frameCountSlider.ValueFromMouse( m_mouseX,
                                                                                                  static_cast<float>( UI_FRAME_COUNT_MIN ),
                                                                                                  static_cast<float>( UI_FRAME_COUNT_MAX ),
                                                                                                  1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_seedSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 6;
                result.requestedSeed = static_cast<int>( m_seedSlider.ValueFromMouse( m_mouseX,
                                                                                      static_cast<float>( UI_SEED_MIN ),
                                                                                      static_cast<float>( UI_SEED_MAX ),
                                                                                      1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_solverBallSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 7;
                m_previewSolverBallCount = static_cast<int>( m_solverBallSlider.ValueFromMouse( m_mouseX,
                                                                                                static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                                static_cast<float>( UI_SOLVER_COUNT_MAX ),
                                                                                                1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_solverBoxSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 8;
                m_previewSolverBoxCount = static_cast<int>( m_solverBoxSlider.ValueFromMouse( m_mouseX,
                                                                                              static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                              static_cast<float>( UI_SOLVER_COUNT_MAX ),
                                                                                              1.0f ) );
                SetCapture( hwnd );
            }
            else if ( m_trackHeightSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 9;
                result.requestedTrackHeight = m_trackHeightSlider.ValueFromMouse( m_mouseX, UI_TRACK_HEIGHT_MIN, UI_TRACK_HEIGHT_MAX, UI_TRACK_HEIGHT_STEP );
                SetCapture( hwnd );
            }
            else if ( m_autoCycleSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 10;
                result.requestedAutoCycleInterval = m_autoCycleSlider.ValueFromMouse( m_mouseX, UI_AUTO_CYCLE_MIN, UI_AUTO_CYCLE_MAX, UI_AUTO_CYCLE_STEP );
                SetCapture( hwnd );
            }
            else if ( m_worldGravitySlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 11;
                result.requestWorldGravity = true;
                result.requestedWorldGravity = WorldGravityFromStrength( m_worldGravitySlider.ValueFromMouse( m_mouseX,
                                                                                                             UI_WORLD_GRAVITY_MIN,
                                                                                                             UI_WORLD_GRAVITY_MAX,
                                                                                                             UI_WORLD_GRAVITY_STEP ) );
                SetCapture( hwnd );
            }
            else if ( m_worldFluidHeightSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 12;
                result.requestWorldFluidHeight = true;
                result.requestedWorldFluidHeight = m_worldFluidHeightSlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
                SetCapture( hwnd );
            }
            else if ( m_worldFluidDensitySlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 13;
                result.requestWorldFluidDensity = true;
                result.requestedWorldFluidDensity = m_worldFluidDensitySlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
                SetCapture( hwnd );
            }
            else if ( m_saveDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.saveSceneDefaults = true;
            }
            else if ( m_resetSceneButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.resetScene = true;
            }
            m_rendererCombo.Close();
        }
        else if ( inside && m_mouseY >= m_y + m_height - bottomH )
        {
            if ( m_blurToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_blurPreviewEnabled = !m_blurPreviewEnabled;
                m_backdropBlur.Invalidate();
            }
            else if ( m_vsyncToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.toggleVsync = true;
            }
            else if ( m_histogramToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetPerformanceHistogramEnabled( !m_performanceHistogramEnabled );
            }
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                CloseSceneCombo();
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_profilerTimelineEnabled = !m_profilerTimelineEnabled;
            }
        }
        else
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_physicsModeCombo.Close();
        }
    }

    if ( leftNow && m_activeSlider == 1 )
    {
        m_previewTimeScale = m_timeScaleSlider.ValueFromMouse( m_mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
        result.requestedTimeScale = m_previewTimeScale;
    }
    else if ( leftNow && m_activeSlider == 2 )
    {
        m_previewModelCount = static_cast<int>( m_modelCountSlider.ValueFromMouse( m_mouseX,
                                                                                   static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                                   static_cast<float>( UI_MODEL_COUNT_MAX ),
                                                                                   1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 3 )
    {
        m_previewPhysicsAlpha = m_physicsAlphaSlider.ValueFromMouse( m_mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );
        result.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
    }
    else if ( leftNow && m_activeSlider == 4 )
    {
        m_previewContactLinger = m_contactLingerSlider.ValueFromMouse( m_mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );
        result.requestedPhysicsDebugContactLinger = m_previewContactLinger;
    }
    else if ( leftNow && m_activeSlider == 5 )
    {
        result.requestedFrameCount = static_cast<int>( m_frameCountSlider.ValueFromMouse( m_mouseX,
                                                                                          static_cast<float>( UI_FRAME_COUNT_MIN ),
                                                                                          static_cast<float>( UI_FRAME_COUNT_MAX ),
                                                                                          1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 6 )
    {
        result.requestedSeed = static_cast<int>( m_seedSlider.ValueFromMouse( m_mouseX,
                                                                              static_cast<float>( UI_SEED_MIN ),
                                                                              static_cast<float>( UI_SEED_MAX ),
                                                                              1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 7 )
    {
        m_previewSolverBallCount = static_cast<int>( m_solverBallSlider.ValueFromMouse( m_mouseX,
                                                                                        static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                        static_cast<float>( UI_SOLVER_COUNT_MAX ),
                                                                                        1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 8 )
    {
        m_previewSolverBoxCount = static_cast<int>( m_solverBoxSlider.ValueFromMouse( m_mouseX,
                                                                                      static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                      static_cast<float>( UI_SOLVER_COUNT_MAX ),
                                                                                      1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 9 )
    {
        result.requestedTrackHeight = m_trackHeightSlider.ValueFromMouse( m_mouseX, UI_TRACK_HEIGHT_MIN, UI_TRACK_HEIGHT_MAX, UI_TRACK_HEIGHT_STEP );
    }
    else if ( leftNow && m_activeSlider == 10 )
    {
        result.requestedAutoCycleInterval = m_autoCycleSlider.ValueFromMouse( m_mouseX, UI_AUTO_CYCLE_MIN, UI_AUTO_CYCLE_MAX, UI_AUTO_CYCLE_STEP );
    }
    else if ( leftNow && m_activeSlider == 11 )
    {
        result.requestWorldGravity = true;
        result.requestedWorldGravity = WorldGravityFromStrength( m_worldGravitySlider.ValueFromMouse( m_mouseX,
                                                                                                     UI_WORLD_GRAVITY_MIN,
                                                                                                     UI_WORLD_GRAVITY_MAX,
                                                                                                     UI_WORLD_GRAVITY_STEP ) );
    }
    else if ( leftNow && m_activeSlider == 12 )
    {
        result.requestWorldFluidHeight = true;
        result.requestedWorldFluidHeight = m_worldFluidHeightSlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
    }
    else if ( leftNow && m_activeSlider == 13 )
    {
        result.requestWorldFluidDensity = true;
        result.requestedWorldFluidDensity = m_worldFluidDensitySlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
    }

    if ( leftNow && m_isDragging )
    {
        const int oldX = m_x;
        const int oldY = m_y;
        m_x = std::clamp( m_mouseX - m_dragOffsetX, margin, (std::max)( margin, screenW - m_width - margin ) );
        m_y = std::clamp( m_mouseY - m_dragOffsetY, margin, (std::max)( margin, screenH - m_height - margin ) );
        if ( oldX != m_x || oldY != m_y )
        {
            m_backdropBlur.Invalidate();
        }
    }
    if ( leftNow && m_isResizing )
    {
        const int oldW = m_width;
        const int oldH = m_height;
        m_width = std::clamp( m_resizeStartW + m_mouseX - m_resizeStartMouseX, minW, maxW );
        m_height = std::clamp( m_resizeStartH + m_mouseY - m_resizeStartMouseY, minH, maxH );
        m_scrollbarVisibleUntil = now + 1.4;
        if ( oldW != m_width || oldH != m_height )
        {
            m_backdropBlur.Invalidate();
        }
    }

    if ( !leftNow && m_leftWasDown )
    {
        if ( m_activeSlider == 1 && m_previewTimeScale > 0.0f )
        {
            result.requestedTimeScale = m_previewTimeScale;
        }
        else if ( m_activeSlider == 2 && m_previewModelCount >= 0 )
        {
            result.requestedModelCount = m_previewModelCount;
        }
        else if ( m_activeSlider == 3 && m_previewPhysicsAlpha >= 0.0f )
        {
            result.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
        }
        else if ( m_activeSlider == 4 && m_previewContactLinger >= 0.0f )
        {
            result.requestedPhysicsDebugContactLinger = m_previewContactLinger;
        }
        else if ( m_activeSlider == 7 && m_previewSolverBallCount >= 0 )
        {
            result.requestedSolverBallCount = m_previewSolverBallCount;
        }
        else if ( m_activeSlider == 8 && m_previewSolverBoxCount >= 0 )
        {
            result.requestedSolverBoxCount = m_previewSolverBoxCount;
        }
        m_activeSlider = 0;
        m_isDragging = false;
        m_isResizing = false;
        ReleaseCapture();
    }

    m_leftWasDown = leftNow;
    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_blocksCameraMouse = inside || m_isDragging || m_isResizing || m_activeSlider != 0;
    return result;
}


void InGameUI::Draw( const InGameUIFrameData& data )
{
    if ( !m_isVisible )
    {
        return;
    }

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    const UIDrawContext draw( screenW, screenH );
    if ( m_performanceHistogramEnabled )
    {
        PushPerformanceHistogramSample( data.cpuFrameMs, data.gpuFrameMs );
    }

    if ( m_isMinimized )
    {
        char titleText[192] = {};
        BuildWindowTitle( data, titleText, sizeof( titleText ) );
        m_minimizedWidth = MinimizedWidthForTitle( titleText, screenW );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_minimizedWidth );
        FitTitleText( titleText, sizeof( titleText ), 12.5f, minimized.w - 76.0f );
        const UIRect restoreButton = { minimized.x + minimized.w - 36.0f, minimized.y + 7.0f, 26.0f, 22.0f };
        draw.Rect( minimized.x - 5.0f, minimized.y - 5.0f, minimized.w + 10.0f, minimized.h + 10.0f, 0.03f, 0.54f, 0.86f, 0.12f );
        draw.Rect( minimized.x, minimized.y, minimized.w, minimized.h, 0.018f, 0.040f, 0.056f, 0.76f );
        draw.Outline( minimized.x, minimized.y, minimized.w, minimized.h, 0.39f, 0.88f, 1.0f, 0.92f );
        draw.Rect( minimized.x + 10.0f, minimized.y + 12.0f, 12.0f, 12.0f, 0.34f, 0.91f, 1.0f, 0.90f );
        draw.Rect( restoreButton.x, restoreButton.y, restoreButton.w, restoreButton.h, 0.026f, 0.100f, 0.132f, 0.78f );
        draw.Outline( restoreButton.x, restoreButton.y, restoreButton.w, restoreButton.h, 0.24f, 0.58f, 0.70f, 0.78f );
        draw.Text( minimized.x + 32.0f, minimized.y + 11.0f, 12.5f, 0.90f, 0.98f, 1.0f, titleText );
        const float plusX = restoreButton.x + restoreButton.w * 0.5f;
        const float plusY = restoreButton.y + restoreButton.h * 0.5f;
        draw.Rect( plusX - 5.0f, plusY - 1.0f, 10.0f, 2.0f, 0.82f, 0.98f, 1.0f, 0.96f );
        draw.Rect( plusX - 1.0f, plusY - 5.0f, 2.0f, 10.0f, 0.82f, 0.98f, 1.0f, 0.96f );
        if ( m_performanceHistogramEnabled )
        {
            DrawPerformanceHistogram( draw, data );
        }
        Text2d::FlushQuads();
        return;
    }

    const float x = static_cast<float>( m_x );
    const float y = static_cast<float>( m_y );
    const float w = static_cast<float>( m_width );
    const float h = static_cast<float>( m_height );
    const float titleH = 44.0f;
    const float tabH = 54.0f;
    const float bottomH = 88.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};
    BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};
    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;
    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = Text2d::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }
    FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ApplyProfilerDefaultExpansion();
    ApplyProfilerExpandAll();

    const UIRect blurBounds = { x, y, w, h };
    Text2d::FlushQuads();
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, m_blurPreviewEnabled );

    draw.Rect( x - 8.0f, y - 8.0f, w + 16.0f, h + 16.0f, 0.03f, 0.54f, 0.86f, 0.10f );
    draw.Rect( x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, 0.06f, 0.34f, 0.48f, 0.18f );
    draw.Rect( x, y, w, h, 0.018f, 0.040f, 0.056f, m_blurPreviewEnabled ? 0.60f : 0.66f );
    draw.Rect( x + 2.0f, y + 2.0f, w - 4.0f, titleH - 3.0f, 0.040f, 0.100f, 0.132f, 0.74f );
    draw.Rect( x + 2.0f, y + titleH, w - 4.0f, tabH, 0.026f, 0.060f, 0.078f, 0.58f );
    draw.Rect( x + 2.0f, y + titleH + tabH, w - 4.0f, 1.0f, 0.26f, 0.82f, 1.0f, 0.38f );
    draw.Outline( x, y, w, h, 0.39f, 0.88f, 1.0f, 0.88f );
    draw.Outline( x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, 0.08f, 0.26f, 0.34f, 0.64f );

    draw.Text( x + 20.0f, y + 12.0f, 15.5f, 0.90f, 0.98f, 1.0f, titleText );
    const UIRect minimizeButton = { x + w - 112.0f, y + 8.0f, 30.0f, 28.0f };
    const UIRect maximizeButton = { x + w - 76.0f, y + 8.0f, 30.0f, 28.0f };
    const UIRect closeButton = { x + w - 40.0f, y + 8.0f, 30.0f, 28.0f };
    DrawTitleButton( draw, minimizeButton, TitleButtonIcon::Minimize, minimizeButton.Contains( m_mouseX, m_mouseY ), false );
    DrawTitleButton( draw, maximizeButton, m_isMaximized ? TitleButtonIcon::Restore : TitleButtonIcon::Maximize, maximizeButton.Contains( m_mouseX, m_mouseY ), m_isMaximized );
    DrawTitleButton( draw, closeButton, TitleButtonIcon::Close, closeButton.Contains( m_mouseX, m_mouseY ), false );

    static const char* kTabs[] = { "Profile", "Scene", "Physics", "Options", "Controls" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    draw.Rect( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.010f, 0.020f, 0.028f, 0.56f );
    draw.Outline( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.16f, 0.28f, 0.34f, 0.62f );

    auto visible = [&]( float rowY, float rowH ) -> bool
    {
        return rowY >= contentY && rowY + rowH <= contentY + contentH;
    };
    auto labelValueAt = [&]( float tx, float rowY, const char* label, const char* value, float vr, float vg, float vb )
    {
        if ( !visible( rowY, 18.0f ) )
        {
            return;
        }
        draw.Text( tx, rowY, 11.5f, 0.52f, 0.76f, 0.84f, label );
        draw.Text( tx + 126.0f, rowY, 11.5f, vr, vg, vb, value );
    };
    auto labelValue = [&]( float rowY, const char* label, const char* value, float vr, float vg, float vb )
    {
        labelValueAt( contentX, rowY, label, value, vr, vg, vb );
    };
    auto drawSectionTitle = [&]( float rowY, float textSize, const char* text )
    {
        if ( !visible( rowY, textSize + 4.0f ) )
        {
            return;
        }
        draw.Text( contentX, rowY, textSize, 1.0f, 0.85f, 0.34f, text );
    };
    auto drawContentToggle = [&]( UICheckBox& toggle, float tx, float rowY, float controlW, const char* label, bool checked )
    {
        if ( !visible( rowY, 24.0f ) )
        {
            return;
        }
        toggle.SetBounds( tx, rowY, controlW, 24.0f );
        toggle.DrawToggle( draw, label, checked, 0.34f, 0.91f, 1.0f );
    };
    if ( m_activeTab == InGameUITab::Profiler )
    {
        char buf[128];
        const Profiler& profiler = Profiler::Instance();
        const float tableX = contentX;
        const float tableY = contentY;
        const float tableW = contentW;
        const float rowH = 30.0f;
        const float headerH = 32.0f;
        const float colMarker = tableX + 18.0f;
        const float colCpu = tableX + tableW * 0.36f;
        const float colGpu = tableX + tableW * 0.46f;
        const float colP50 = tableX + tableW * 0.56f;
        const float colP99 = tableX + tableW * 0.66f;
        const float barX = tableX + tableW * 0.76f;
        const float barW = (std::max)( 95.0f, tableW * 0.20f );
        static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
        float timelineBudgetMs = PROFILER_UI_TIMELINE_BUDGET_MS;
        for ( int i = 0; i < profiler.MarkerCount(); ++i )
        {
            const Profiler::Marker& marker = profiler.GetMarker( i );
            if ( marker.hash == kFrameHash )
            {
                timelineBudgetMs = (std::max)( PROFILER_UI_TIMELINE_BUDGET_MS, ProfilerMarkerDisplayCpuMs( marker ) );
                break;
            }
        }
        draw.Rect( tableX, tableY, tableW, contentH + 2.0f, 0.018f, 0.030f, 0.038f, 0.58f );
        draw.Outline( tableX, tableY, tableW, contentH + 2.0f, 0.18f, 0.30f, 0.34f, 0.62f );
        draw.Rect( tableX, tableY + headerH, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
        draw.Text( colMarker, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Marker" );
        draw.Text( colCpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "CPU" );
        draw.Text( colGpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "GPU" );
        draw.Text( colP50, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P50" );
        draw.Text( colP99, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P99" );
        draw.Text( barX, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, m_profilerTimelineEnabled ? "Span" : "0 ms" );
        draw.Text( barX + barW - 44.0f, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, m_profilerTimelineEnabled ? "Frame" : "16.67 ms" );

        int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
        const int visibleRowCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
        ProfilerTimelineSegment timelineSegments[PROFILER_UI_MAX_MARKERS] = {};
        BuildProfilerTimelineSegments( visibleRows, visibleRowCount, timelineSegments );

        auto profilerRow = [&]( int rowIndex, const Profiler::Marker& marker, const ProfilerTimelineSegment& segment, bool hasChildren, bool isExpanded )
        {
            const float rowY = tableY + headerH + static_cast<float>( rowIndex ) * rowH - m_scrollY;
            if ( rowY + rowH < tableY + headerH || rowY > tableY + contentH )
            {
                return;
            }
            const Profiler::BarColor& color = Profiler::BAR_PALETTE[marker.colorIndex % Profiler::BAR_PALETTE_SIZE];
            const float r = color.r;
            const float g = color.g;
            const float b = color.b;
            const float indent = static_cast<float>( (std::min)( marker.depth, 8 ) ) * 18.0f;
            const float nameX = colMarker + indent;
            const float cpuMs = ProfilerMarkerDisplayCpuMs( marker );
            const float gpuMs = marker.hasGpu ? ( marker.gpuAvgMs > 0.0f ? marker.gpuAvgMs : marker.gpuLastFrameMs ) : -1.0f;
            const float p50Ms = marker.p50Ms > 0.0f ? marker.p50Ms : cpuMs;
            const float p99Ms = marker.p99Ms > 0.0f ? marker.p99Ms : cpuMs;
            draw.Rect( tableX, rowY + rowH - 1.0f, tableW, 1.0f, 0.16f, 0.26f, 0.30f, 0.38f );
            if ( hasChildren )
            {
                UIIconButton expander;
                expander.SetBounds( nameX, rowY + 8.0f, 14.0f, 14.0f );
                expander.DrawExpander( draw, isExpanded );
            }
            else
            {
                draw.Rect( nameX + 3.0f, rowY + 12.0f, 8.0f, 8.0f, r, g, b, 0.94f );
            }
            draw.Text( nameX + 22.0f, rowY + 8.0f, 12.0f, 0.92f, 0.96f, 0.97f, marker.leafName );
            snprintf( buf, sizeof( buf ), "%.2f", cpuMs );
            draw.Text( colCpu, rowY + 8.0f, 11.5f, r, g, b, buf );
            if ( gpuMs >= 0.0f )
            {
                snprintf( buf, sizeof( buf ), "%.2f", gpuMs );
            }
            else
            {
                snprintf( buf, sizeof( buf ), "-" );
            }
            draw.Text( colGpu, rowY + 8.0f, 11.5f, 0.38f, 0.84f, 1.0f, buf );
            snprintf( buf, sizeof( buf ), "%.2f", p50Ms );
            draw.Text( colP50, rowY + 8.0f, 11.5f, 0.78f, 0.84f, 0.86f, buf );
            snprintf( buf, sizeof( buf ), "%.2f", p99Ms );
            draw.Text( colP99, rowY + 8.0f, 11.5f, 0.78f, 0.84f, 0.86f, buf );
            draw.Rect( barX, rowY + 16.0f, barW, 1.0f, 0.46f, 0.56f, 0.60f, 0.86f );
            const float fill = std::clamp( cpuMs / 16.67f, 0.0f, 1.0f );
            if ( m_profilerTimelineEnabled )
            {
                if ( segment.isFilled && segment.durationMs > 0.0f )
                {
                    const float start = std::clamp( segment.startMs / timelineBudgetMs, 0.0f, 1.0f );
                    const float end = std::clamp( ( segment.startMs + segment.durationMs ) / timelineBudgetMs, start, 1.0f );
                    draw.Rect( barX + barW * start, rowY + 11.0f, barW * ( end - start ), 10.0f, r, g, b, 0.88f );
                    const float p99Tick = std::clamp( ( segment.startMs + p99Ms ) / timelineBudgetMs, start, end );
                    draw.Rect( barX + barW * p99Tick, rowY + 7.0f, 1.0f, 18.0f, r, g, b, 0.98f );
                }
            }
            else
            {
                const float p99Tick = std::clamp( p99Ms / 16.67f, 0.0f, 1.0f );
                draw.Rect( barX, rowY + 11.0f, barW * fill, 10.0f, r, g, b, 0.88f );
                draw.Rect( barX + barW * p99Tick, rowY + 7.0f, 1.0f, 18.0f, r, g, b, 0.98f );
            }
        };

        for ( int visibleRow = 0; visibleRow < visibleRowCount; ++visibleRow )
        {
            const int markerIndex = visibleRows[visibleRow];
            const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
            const bool hasChildren = ProfilerMarkerHasChildren( profiler, markerIndex );
            profilerRow( visibleRow, marker, timelineSegments[visibleRow], hasChildren, IsProfilerMarkerExpanded( marker.hash ) );
        }
    }
    else if ( m_activeTab == InGameUITab::Scene )
    {
        char buf[160];
        char filterDisplay[80] = {};
        const bool sceneFilterActive = m_sceneFilter[0] != '\0';
        const int filteredSceneCount = CountFilteredSceneOptions( data.sceneOptions, data.sceneOptionCount, m_sceneFilter );
        const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
        m_sceneComboScroll = ClampSceneComboScroll( m_sceneComboScroll, filteredSceneCount );
        const int sceneFirstOption = m_sceneComboScroll;
        const int selectedFilteredPosition = SceneFilteredPositionForIndex( data.sceneOptions, data.sceneOptionCount, m_sceneFilter, data.selectedSceneOption );
        const int sceneSelectedInSlice = selectedFilteredPosition >= sceneFirstOption && selectedFilteredPosition < sceneFirstOption + sceneVisibleCount ? selectedFilteredPosition - sceneFirstOption : -1;
        const char* visibleSceneOptions[UI_SCENE_COMBO_VISIBLE_OPTIONS] = {};
        for ( int i = 0; i < sceneVisibleCount; ++i )
        {
            const int sceneIndex = FindFilteredSceneOptionIndex( data.sceneOptions, data.sceneOptionCount, m_sceneFilter, sceneFirstOption + i );
            visibleSceneOptions[i] = sceneIndex >= 0 ? data.sceneOptions[sceneIndex] : "";
        }
        int sceneDrawCount = sceneVisibleCount;
        if ( sceneDrawCount == 0 && sceneFilterActive )
        {
            visibleSceneOptions[0] = "No matches";
            sceneDrawCount = 1;
        }
        const char* selectedSceneName = "No scenes";
        if ( data.sceneOptions && data.selectedSceneOption >= 0 && data.selectedSceneOption < data.sceneOptionCount )
        {
            selectedSceneName = data.sceneOptions[data.selectedSceneOption];
        }
        else if ( data.sceneName && data.sceneName[0] != '\0' )
        {
            selectedSceneName = data.sceneName;
        }
        if ( m_sceneCombo.IsOpen() && sceneFilterActive )
        {
            snprintf( filterDisplay, sizeof( filterDisplay ), "%s", m_sceneFilter );
            selectedSceneName = filterDisplay;
        }
        const float sceneComboW = SceneTabComboWidth( contentW );
        drawSectionTitle( scrolledY, 16.0f, "Scene" );
        m_sceneCombo.SetBounds( contentX, scrolledY + 42.0f, sceneComboW, 24.0f );
        const float resetX = contentX + sceneComboW + UI_SCENE_HEADER_BUTTON_GAP;
        const float defaultsX = resetX + UI_SCENE_RESET_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
        const float demoX = defaultsX + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
        m_resetSceneButton.SetBounds( resetX, scrolledY + 42.0f, UI_SCENE_RESET_BUTTON_W, 24.0f );
        m_resetDefaultsButton.SetBounds( defaultsX, scrolledY + 42.0f, UI_SCENE_RESET_DEFAULTS_BUTTON_W, 24.0f );
        m_demoSceneButton.SetBounds( demoX, scrolledY + 42.0f, UI_SCENE_DEMO_BUTTON_W, 24.0f );
        m_sceneCombo.SetDropUp( false );
        if ( data.targetFrameCount > 0 )
        {
            const int displayedFrame = ( data.testComplete && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount : data.currentFrame;
            snprintf( buf, sizeof( buf ), "%d / %d", displayedFrame, data.targetFrameCount );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "%d", data.currentFrame );
        }
        if ( !m_sceneCombo.IsOpen() )
        {
            const float sceneCol2 = contentX + (std::max)( 208.0f, contentW * 0.48f );
            char statusBuf[64] = {};
            labelValueAt( contentX, scrolledY + 82.0f, "Renderer", data.rendererName, 0.60f, 0.90f, 1.0f );
            labelValueAt( sceneCol2, scrolledY + 82.0f, "Physics", data.legacyPhysics ? "Legacy solver" : "Impulse solver", 0.36f, 0.95f, 0.56f );
            labelValueAt( contentX, scrolledY + 108.0f, "Frame", buf, 0.88f, 0.92f, 0.94f );
            snprintf( statusBuf, sizeof( statusBuf ), "%s / fixed %s", data.testComplete ? "complete" : "running", data.fixedStep ? "on" : "off" );
            labelValueAt( sceneCol2, scrolledY + 108.0f, "Status", statusBuf, 0.36f, 0.95f, 0.56f );
            snprintf( buf, sizeof( buf ), "%d / %d", data.currentSceneIndex + 1, data.sceneCount );
            labelValueAt( contentX, scrolledY + 134.0f, "Scene index", buf, 0.88f, 0.92f, 0.94f );
            snprintf( buf, sizeof( buf ), "%.1f FPS", data.fps );
            labelValueAt( sceneCol2, scrolledY + 134.0f, "Frame rate", buf, 0.52f, 0.94f, 1.0f );
            snprintf( buf, sizeof( buf ), "%d", data.modelCount );
            labelValueAt( contentX, scrolledY + 160.0f, "Model count", buf, 0.88f, 0.92f, 0.94f );
            snprintf( buf, sizeof( buf ), "%.6f", data.sceneEnergy );
            labelValueAt( sceneCol2, scrolledY + 160.0f, "Kinetic energy", buf, 0.98f, 0.78f, 0.35f );
        }
        if ( visible( scrolledY + 42.0f, m_sceneCombo.IsOpen() ? 286.0f : 24.0f ) )
        {
            m_sceneCombo.Draw( draw,
                               "Load scene",
                               selectedSceneName,
                               visibleSceneOptions,
                               sceneDrawCount,
                               sceneSelectedInSlice,
                               m_mouseX,
                               m_mouseY );
        }
        if ( visible( scrolledY + 42.0f, 24.0f ) )
        {
            m_resetSceneButton.Draw( draw, "Reset", m_mouseX, m_mouseY );
            m_resetDefaultsButton.Draw( draw, "Reset Defaults", m_mouseX, m_mouseY );
            m_demoSceneButton.Draw( draw, "Demo Scene", m_mouseX, m_mouseY );
        }
    }
    else if ( m_activeTab == InGameUITab::Physics )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const float col1 = contentX;
        const float col2 = contentX + colW + 18.0f;
        drawSectionTitle( scrolledY, 16.0f, "Physics Controls" );
        drawContentToggle( m_physicsToggles[0], col1, scrolledY + 42.0f, colW, "Collision mesh", data.collisionVisualizer );
        drawContentToggle( m_physicsToggles[1], col1, scrolledY + 72.0f, colW, "Axes", ( data.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 );
        drawContentToggle( m_physicsToggles[2], col1, scrolledY + 102.0f, colW, "Contacts", ( data.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 );
        drawContentToggle( m_physicsToggles[3], col1, scrolledY + 132.0f, colW, "Sleep state", ( data.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 );
        drawContentToggle( m_physicsToggles[4], col2, scrolledY + 42.0f, colW, "Transparent", data.physicsDebugTransparent );
        drawContentToggle( m_physicsToggles[5], col2, scrolledY + 72.0f, colW, "Broadphase", data.broadphaseOverlay );
        labelValue( scrolledY + 178.0f, "Solver", data.legacyPhysics ? "Legacy" : "Impulse", 0.36f, 0.95f, 0.56f );
        snprintf( buf, sizeof( buf ), "0x%04X", data.physicsDebugFlags );
        labelValue( scrolledY + 204.0f, "Debug flags", buf, 0.52f, 0.94f, 1.0f );
        snprintf( buf, sizeof( buf ), "%.2f", data.physicsDebugAlpha );
        labelValue( scrolledY + 230.0f, "Body alpha", buf, 0.88f, 0.92f, 0.94f );
        snprintf( buf, sizeof( buf ), "%.2fs", data.physicsDebugContactLinger );
        labelValue( scrolledY + 256.0f, "Contact linger", buf, 0.88f, 0.92f, 0.94f );
        if ( visible( scrolledY + 292.0f, 18.0f ) )
        {
            drawSectionTitle( scrolledY + 292.0f, 12.0f, "World" );
        }
        const float displayGravityStrength = GravityStrengthFromWorld( data.worldGravity );
        snprintf( buf, sizeof( buf ), "%.1f", displayGravityStrength );
        m_worldGravitySlider.SetBounds( contentX, scrolledY + 318.0f, contentW, 34.0f );
        if ( visible( scrolledY + 318.0f, 34.0f ) )
        {
            m_worldGravitySlider.Draw( draw, "Gravity", buf, displayGravityStrength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
        }
    }
    else if ( m_activeTab == InGameUITab::Options )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const float col1 = contentX;
        const float col2 = contentX + colW + 18.0f;
        const float displayTimeScale = ( m_activeSlider == 1 && m_previewTimeScale > 0.0f ) ? m_previewTimeScale : data.timeScale;
        const int displayModelCount = ( m_activeSlider == 2 && m_previewModelCount >= 0 ) ? m_previewModelCount : data.modelCount;
        drawSectionTitle( scrolledY, 16.0f, "Scene Options" );
        drawContentToggle( m_optionToggles[0], col1, scrolledY + 42.0f, colW, "Scene physics", data.scenePhysicsEnabled );
        drawContentToggle( m_optionToggles[1], col2, scrolledY + 42.0f, colW, "Scene text", data.sceneTextEnabled );
        drawContentToggle( m_optionToggles[2], col1, scrolledY + 72.0f, colW, "Fixed step", data.fixedStep );
        drawContentToggle( m_optionToggles[3], col2, scrolledY + 72.0f, colW, "Roll align", data.rollAlignEnabled );
        drawContentToggle( m_optionToggles[4], col1, scrolledY + 102.0f, colW, "Hide terrain", data.terrainHidden );
        drawContentToggle( m_optionToggles[5], col2, scrolledY + 102.0f, colW, "Hide water", data.waterHidden );
        drawContentToggle( m_optionToggles[6], col1, scrolledY + 132.0f, colW, "Freeze water", data.waterFreezeDebug );
        drawContentToggle( m_optionToggles[7], col2, scrolledY + 132.0f, colW, "Flat water", data.waterFlatDebug );
        drawContentToggle( m_optionToggles[8], col1, scrolledY + 162.0f, colW, "Water reflect", !data.waterNoReflect );
        snprintf( buf, sizeof( buf ), "%.2fx", displayTimeScale );
        m_timeScaleSlider.SetBounds( contentX, scrolledY + 208.0f, contentW, 34.0f );
        if ( visible( scrolledY + 208.0f, 34.0f ) )
        {
            m_timeScaleSlider.Draw( draw, "Time scale", buf, displayTimeScale, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX );
        }
        snprintf( buf, sizeof( buf ), "%d", displayModelCount );
        m_modelCountSlider.SetBounds( contentX, scrolledY + 256.0f, contentW, 34.0f );
        if ( visible( scrolledY + 256.0f, 34.0f ) )
        {
            m_modelCountSlider.Draw( draw, "Model count", buf, static_cast<float>( displayModelCount ), static_cast<float>( UI_MODEL_COUNT_MIN ), static_cast<float>( UI_MODEL_COUNT_MAX ) );
        }
        m_saveDefaultsButton.SetBounds( col1, scrolledY + 306.0f, 132.0f, 32.0f );
        m_resetSceneButton.SetBounds( col2, scrolledY + 306.0f, 124.0f, 32.0f );
        if ( visible( scrolledY + 306.0f, 32.0f ) )
        {
            m_saveDefaultsButton.Draw( draw, "Save defaults", m_mouseX, m_mouseY );
            m_resetSceneButton.Draw( draw, "Reset scene", m_mouseX, m_mouseY );
        }
        labelValue( scrolledY + 356.0f, "Water mode", data.waterNoReflect ? "no reflection" : ( data.waterRTReflect ? "DXR reflect" : "FBO reflect" ), 0.98f, 0.78f, 0.35f );
    }
    else
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const float col1 = contentX;
        const float col2 = contentX + colW + 18.0f;
        const int displayModelCount = ( m_activeSlider == 2 && m_previewModelCount >= 0 ) ? m_previewModelCount : data.modelCount;
        const float displayAlpha = ( m_activeSlider == 3 && m_previewPhysicsAlpha >= 0.0f ) ? m_previewPhysicsAlpha : data.physicsDebugAlpha;
        const float displayLinger = ( m_activeSlider == 4 && m_previewContactLinger >= 0.0f ) ? m_previewContactLinger : data.physicsDebugContactLinger;
        const int displayFrameLimit = data.targetFrameCount > 0 ? data.targetFrameCount : 0;
        const int displaySeed = static_cast<int>( (std::max)( 1u, data.rngSeed ) );
        const int displaySolverBalls = ( m_activeSlider == 7 && m_previewSolverBallCount >= 0 ) ? m_previewSolverBallCount : data.solverBallCount;
        const int displaySolverBoxes = ( m_activeSlider == 8 && m_previewSolverBoxCount >= 0 ) ? m_previewSolverBoxCount : data.solverBoxCount;
        const float displayTrackHeight = data.trackHeight > 0.0f ? data.trackHeight : 0.0f;
        const float displayAutoCycle = data.autoCycleInterval > 0.0f ? data.autoCycleInterval : 0.0f;
        static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };
        static const char* kPhysicsOptions[] = { "Legacy", "Solver" };

        drawSectionTitle( scrolledY, 16.0f, "Scene Controls" );
        m_reflectionCombo.SetBounds( contentX, scrolledY + 42.0f, 172.0f, 24.0f );
        m_reflectionCombo.SetDropUp( false );
        m_physicsModeCombo.SetBounds( contentX + 188.0f, scrolledY + 42.0f, 172.0f, 24.0f );
        m_physicsModeCombo.SetDropUp( false );

        drawContentToggle( m_controlToggles[0], col1, scrolledY + 76.0f, colW, "Scene physics", data.scenePhysicsEnabled );
        drawContentToggle( m_controlToggles[1], col2, scrolledY + 76.0f, colW, "Scene text", data.sceneTextEnabled );
        drawContentToggle( m_controlToggles[2], col1, scrolledY + 102.0f, colW, "Fixed step", data.fixedStep );
        drawContentToggle( m_controlToggles[3], col2, scrolledY + 102.0f, colW, "Roll align", data.rollAlignEnabled );
        drawContentToggle( m_controlToggles[4], col1, scrolledY + 128.0f, colW, "Hide terrain", data.terrainHidden );
        drawContentToggle( m_controlToggles[5], col2, scrolledY + 128.0f, colW, "Hide water", data.waterHidden );
        drawContentToggle( m_controlToggles[6], col1, scrolledY + 154.0f, colW, "Collision mesh", data.collisionVisualizer );
        drawContentToggle( m_controlToggles[7], col2, scrolledY + 154.0f, colW, "Transparent", data.physicsDebugTransparent );
        drawContentToggle( m_controlToggles[8], col1, scrolledY + 180.0f, colW, "Broadphase", data.broadphaseOverlay );
        drawContentToggle( m_controlToggles[9], col2, scrolledY + 180.0f, colW, "Freeze water", data.waterFreezeDebug );
        drawContentToggle( m_controlToggles[10], col1, scrolledY + 206.0f, colW, "Flat water", data.waterFlatDebug );
        drawContentToggle( m_controlToggles[11], col2, scrolledY + 206.0f, colW, "Axes", ( data.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 );
        drawContentToggle( m_controlToggles[12], col1, scrolledY + 232.0f, colW, "Contacts", ( data.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 );
        drawContentToggle( m_controlToggles[13], col2, scrolledY + 232.0f, colW, "Sleep state", ( data.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 );
        drawContentToggle( m_controlToggles[14], col1, scrolledY + 258.0f, colW, "Text only", data.textOnly );
        drawContentToggle( m_controlToggles[15], col2, scrolledY + 258.0f, colW, "Exit on complete", data.exitOnComplete );

        snprintf( buf, sizeof( buf ), "%d", displayModelCount );
        m_modelCountSlider.SetBounds( contentX, scrolledY + 304.0f, contentW, 34.0f );
        if ( visible( scrolledY + 304.0f, 34.0f ) )
        {
            m_modelCountSlider.Draw( draw, "Model count", buf, static_cast<float>( displayModelCount ), static_cast<float>( UI_MODEL_COUNT_MIN ), static_cast<float>( UI_MODEL_COUNT_MAX ) );
        }
        snprintf( buf, sizeof( buf ), "%.2f", displayAlpha );
        m_physicsAlphaSlider.SetBounds( contentX, scrolledY + 344.0f, contentW, 34.0f );
        if ( visible( scrolledY + 344.0f, 34.0f ) )
        {
            m_physicsAlphaSlider.Draw( draw, "Body alpha", buf, displayAlpha, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX );
        }
        snprintf( buf, sizeof( buf ), "%.2fs", displayLinger );
        m_contactLingerSlider.SetBounds( contentX, scrolledY + 384.0f, contentW, 34.0f );
        if ( visible( scrolledY + 384.0f, 34.0f ) )
        {
            m_contactLingerSlider.Draw( draw, "Contact linger", buf, displayLinger, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX );
        }
        if ( visible( scrolledY + 436.0f, 18.0f ) )
        {
            drawSectionTitle( scrolledY + 436.0f, 12.0f, "Playback" );
        }
        if ( displayFrameLimit > 0 )
        {
            snprintf( buf, sizeof( buf ), "%d", displayFrameLimit );
        }
        else
        {
            strcpy_s( buf, sizeof( buf ), "unlimited" );
        }
        m_frameCountSlider.SetBounds( contentX, scrolledY + 462.0f, contentW, 34.0f );
        if ( visible( scrolledY + 462.0f, 34.0f ) )
        {
            m_frameCountSlider.Draw( draw, "Frame limit", buf, static_cast<float>( displayFrameLimit ), static_cast<float>( UI_FRAME_COUNT_MIN ), static_cast<float>( UI_FRAME_COUNT_MAX ) );
        }
        snprintf( buf, sizeof( buf ), "%d", displaySeed );
        m_seedSlider.SetBounds( contentX, scrolledY + 502.0f, contentW, 34.0f );
        if ( visible( scrolledY + 502.0f, 34.0f ) )
        {
            m_seedSlider.Draw( draw, "Seed", buf, static_cast<float>( displaySeed ), static_cast<float>( UI_SEED_MIN ), static_cast<float>( UI_SEED_MAX ) );
        }
        if ( visible( scrolledY + 556.0f, 18.0f ) )
        {
            drawSectionTitle( scrolledY + 556.0f, 12.0f, "Generation" );
        }
        snprintf( buf, sizeof( buf ), "%d", displaySolverBalls );
        m_solverBallSlider.SetBounds( contentX, scrolledY + 582.0f, contentW, 34.0f );
        if ( visible( scrolledY + 582.0f, 34.0f ) )
        {
            m_solverBallSlider.Draw( draw, "Solver balls", buf, static_cast<float>( displaySolverBalls ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( UI_SOLVER_COUNT_MAX ) );
        }
        snprintf( buf, sizeof( buf ), "%d", displaySolverBoxes );
        m_solverBoxSlider.SetBounds( contentX, scrolledY + 622.0f, contentW, 34.0f );
        if ( visible( scrolledY + 622.0f, 34.0f ) )
        {
            m_solverBoxSlider.Draw( draw, "Solver boxes", buf, static_cast<float>( displaySolverBoxes ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( UI_SOLVER_COUNT_MAX ) );
        }
        if ( visible( scrolledY + 676.0f, 18.0f ) )
        {
            drawSectionTitle( scrolledY + 676.0f, 12.0f, "Camera" );
        }
        snprintf( buf, sizeof( buf ), displayTrackHeight > 0.0f ? "%.0f" : "off", displayTrackHeight );
        m_trackHeightSlider.SetBounds( contentX, scrolledY + 702.0f, contentW, 34.0f );
        if ( visible( scrolledY + 702.0f, 34.0f ) )
        {
            m_trackHeightSlider.Draw( draw, "Track height", buf, displayTrackHeight, UI_TRACK_HEIGHT_MIN, UI_TRACK_HEIGHT_MAX );
        }
        snprintf( buf, sizeof( buf ), displayAutoCycle > 0.0f ? "%.1fs" : "off", displayAutoCycle );
        m_autoCycleSlider.SetBounds( contentX, scrolledY + 742.0f, contentW, 34.0f );
        if ( visible( scrolledY + 742.0f, 34.0f ) )
        {
            m_autoCycleSlider.Draw( draw, "Auto-cycle", buf, displayAutoCycle, UI_AUTO_CYCLE_MIN, UI_AUTO_CYCLE_MAX );
        }
        if ( visible( scrolledY + 796.0f, 18.0f ) )
        {
            drawSectionTitle( scrolledY + 796.0f, 12.0f, "World" );
        }
        const float displayGravityStrength = GravityStrengthFromWorld( data.worldGravity );
        snprintf( buf, sizeof( buf ), "%.1f", displayGravityStrength );
        m_worldGravitySlider.SetBounds( contentX, scrolledY + 822.0f, contentW, 34.0f );
        if ( visible( scrolledY + 822.0f, 34.0f ) )
        {
            m_worldGravitySlider.Draw( draw, "Gravity", buf, displayGravityStrength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
        }
        snprintf( buf, sizeof( buf ), "%.0f", data.worldFluidHeight );
        m_worldFluidHeightSlider.SetBounds( contentX, scrolledY + 862.0f, contentW, 34.0f );
        if ( visible( scrolledY + 862.0f, 34.0f ) )
        {
            m_worldFluidHeightSlider.Draw( draw, "Fluid height", buf, data.worldFluidHeight, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX );
        }
        snprintf( buf, sizeof( buf ), "%.2f", data.worldFluidDensity );
        m_worldFluidDensitySlider.SetBounds( contentX, scrolledY + 902.0f, contentW, 34.0f );
        if ( visible( scrolledY + 902.0f, 34.0f ) )
        {
            m_worldFluidDensitySlider.Draw( draw, "Fluid density", buf, data.worldFluidDensity, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX );
        }
        m_saveDefaultsButton.SetBounds( col1, scrolledY + 952.0f, 132.0f, 32.0f );
        m_resetSceneButton.SetBounds( col2, scrolledY + 952.0f, 124.0f, 32.0f );
        if ( visible( scrolledY + 952.0f, 32.0f ) )
        {
            m_saveDefaultsButton.Draw( draw, "Save defaults", m_mouseX, m_mouseY );
            m_resetSceneButton.Draw( draw, "Reset scene", m_mouseX, m_mouseY );
        }
        if ( visible( scrolledY + 42.0f, 86.0f ) )
        {
            m_reflectionCombo.Draw( draw, "Reflect", kReflectionOptions, 3, WaterReflectionModeFromData( data ), m_mouseX, m_mouseY );
            m_physicsModeCombo.Draw( draw, "Physics", kPhysicsOptions, 2, PhysicsModeFromData( data ), m_mouseX, m_mouseY );
        }
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 2.0f, by, w - 4.0f, bottomH - 2.0f, 0.014f, 0.042f, 0.056f, 0.82f );
    draw.Rect( x + 2.0f, by, w - 4.0f, 1.0f, 0.30f, 0.88f, 1.0f, 0.46f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 414.0f : footerW;
    draw.Rect( footerX, by + 16.0f, controlsW, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
    draw.Outline( footerX, by + 16.0f, controlsW, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

    m_blurToggle.SetBounds( x + 32.0f, by + 22.0f, 100.0f, 24.0f );
    m_vsyncToggle.SetBounds( x + 166.0f, by + 22.0f, 100.0f, 24.0f );
    m_histogramToggle.SetBounds( x + 292.0f, by + 22.0f, 108.0f, 24.0f );
    m_timelineToggle.SetBounds( x + 32.0f, by + 48.0f, 100.0f, 24.0f );
    m_rendererCombo.SetBounds( x + 292.0f, by + 48.0f, 112.0f, 24.0f );
    m_rendererCombo.SetDropUp( true );
    m_blurToggle.DrawToggle( draw, "Blur", m_blurPreviewEnabled, 0.34f, 0.91f, 1.0f );
    m_vsyncToggle.DrawToggle( draw, "VSync", data.vsyncEnabled, 0.34f, 0.91f, 1.0f );
    m_histogramToggle.DrawToggle( draw, "Perf", m_performanceHistogramEnabled, 0.34f, 0.91f, 1.0f );
    const int currentRendererIndex = GetRendererIndexFromName( data.rendererName );
    static const char* kRendererOptions[] = { "GL", "DX11", "DX12" };
    m_timelineToggle.DrawToggle( draw, "Timeline", m_profilerTimelineEnabled, 0.34f, 0.91f, 1.0f );
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 3, currentRendererIndex, m_mouseX, m_mouseY );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.Rect( statsX, by + 16.0f, statsW, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
        draw.Outline( statsX, by + 16.0f, statsW, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

        auto statCell = [&]( float tx, const char* name, const char* value, float r, float g, float b )
        {
            draw.Text( tx, by + 25.0f, 10.0f, 0.67f, 0.74f, 0.77f, name );
            draw.Text( tx, by + 47.0f, 11.5f, r, g, b, value );
        };
        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            auto compactStat = [&]( float ty, const char* name, const char* value, float r, float g, float b )
            {
                draw.Text( statsX + 12.0f, ty, 9.0f, 0.67f, 0.74f, 0.77f, name );
                draw.Text( statsX + 66.0f, ty, 9.5f, r, g, b, value );
            };
            compactStat( by + 23.0f, "FPS", fpsText, 0.48f, 0.90f, 0.22f );
            compactStat( by + 41.0f, "Frame", frameText, 0.32f, 0.90f, 1.0f );
            compactStat( by + 59.0f, "Draw/UI", drawText, 0.32f, 0.90f, 1.0f );
        }
        else
        {
            statCell( statsX + 18.0f, "FPS", status, 0.48f, 0.90f, 0.22f );
            draw.Rect( statsX + 78.0f, by + 23.0f, 1.0f, 42.0f, 0.28f, 0.38f, 0.42f, 0.78f );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            statCell( statsX + 100.0f, "Frame Time", status, 0.32f, 0.90f, 1.0f );
            draw.Rect( statsX + 190.0f, by + 23.0f, 1.0f, 42.0f, 0.28f, 0.38f, 0.42f, 0.78f );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            statCell( statsX + 212.0f, "CPU", status, 0.48f, 0.90f, 0.22f );
            draw.Rect( statsX + 266.0f, by + 23.0f, 1.0f, 42.0f, 0.28f, 0.38f, 0.42f, 0.78f );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            statCell( statsX + 288.0f, "GPU", status, 0.48f, 0.90f, 0.22f );
            draw.Rect( statsX + 342.0f, by + 23.0f, 1.0f, 42.0f, 0.28f, 0.38f, 0.42f, 0.78f );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            statCell( statsX + statsW - 112.0f, "Draws / UI", status, 0.32f, 0.90f, 1.0f );
        }
    }
    else
    {
        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, 0.48f, 0.90f, 0.22f, titleStat );
        }
    }

    if ( m_performanceHistogramEnabled )
    {
        DrawPerformanceHistogram( draw, data );
    }

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.88f );
    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.72f );
    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.60f );

    Text2d::FlushQuads();
}
