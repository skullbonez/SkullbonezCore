#include "SkullbonezUi.h"
#include "../SkullbonezInput.h"
#include "../SkullbonezProfiler.h"
#include "../SkullbonezText.h"
#include "UiDraw.h"
#include "UiIconButton.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Ui;

namespace
{
constexpr int PROFILER_UI_MAX_MARKERS = 64;
constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;
constexpr int RENDERER_GL = 0;
constexpr int RENDERER_DX11 = 1;
constexpr int RENDERER_DX12 = 2;

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


float ProfilerMarkerDisplayCpuMs( const Profiler::Marker& marker )
{
    return marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
}


UiRect MinimizedRect( int screenW, int screenH )
{
    (void)screenW;
    constexpr float w = 196.0f;
    constexpr float h = 38.0f;
    constexpr float margin = 14.0f;
    return { margin, (std::max)( margin, static_cast<float>( screenH ) - h - margin ), w, h };
}

void BuildWindowTitle( const InGameUiFrameData& data, char* out, size_t outSize )
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
        snprintf( out, outSize, "Skullbonez UI" );
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

bool InGameUi::IsVisible() const
{
    return m_isVisible;
}


void InGameUi::SetVisible( bool visible, double now )
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
    }
}


void InGameUi::ToggleVisible( double now )
{
    if ( !m_isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_isMinimized, now );
}


void InGameUi::SetMinimized( bool minimized, double now )
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
    }
    else
    {
        m_scrollbarVisibleUntil = now + 1.2;
    }
    m_backdropBlur.Invalidate();
}


void InGameUi::SetActiveTab( InGameUiTab tab )
{
    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_backdropBlur.Invalidate();
}


InGameUiTab InGameUi::GetActiveTab() const
{
    return m_activeTab;
}


bool InGameUi::BlocksCameraMouse() const
{
    return m_blocksCameraMouse;
}


void InGameUi::SetWindowBounds( int x, int y, int width, int height )
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


void InGameUi::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate();
    }
}


void InGameUi::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );
}


void InGameUi::SetProfilerExpandAll( bool expandAll )
{
    m_expandAllProfilerMarkers = expandAll;
    m_expandedProfilerHashCount = 0;
    m_profilerDefaultExpansionApplied = false;
    if ( expandAll )
    {
        ApplyProfilerExpandAll();
    }
}


void InGameUi::SetProfilerTimelineEnabled( bool enabled )
{
    m_profilerTimelineEnabled = enabled;
}


void InGameUi::SetMouseOverride( bool enabled, int x, int y )
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


void InGameUi::SetMaximized( bool maximized, int screenW, int screenH )
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


void InGameUi::ResetResources()
{
    m_backdropBlur.ResetResources();
}


void InGameUi::ApplyProfilerExpandAll()
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


void InGameUi::ApplyProfilerDefaultExpansion()
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


int InGameUi::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUiTab::Keys:
        return 560;
    case InGameUiTab::Profiler:
    {
        int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
        const int visibleMarkerCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
        return 54 + visibleMarkerCount * 30;
    }
    case InGameUiTab::Physics:
        return 390;
    default:
        return 330;
    }
}


int InGameUi::BuildVisibleProfilerRows( int* rows, int maxRows ) const
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


void InGameUi::BuildProfilerTimelineSegments( const int* rows, int rowCount, ProfilerTimelineSegment* segments ) const
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


bool InGameUi::IsProfilerMarkerExpanded( uint32_t hash ) const
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


void InGameUi::ToggleProfilerMarker( uint32_t hash )
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


InGameUiInputResult InGameUi::UpdateInput( HWND hwnd, int screenW, int screenH, double now )
{
    InGameUiInputResult result;
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
    const int minW = 390;
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
        const UiRect minimized = MinimizedRect( screenW, screenH );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        if ( leftNow && !m_leftWasDown && insideMinimized )
        {
            SetMinimized( false, now );
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
    const UiRect minimizeButton = { static_cast<float>( m_x + m_width - 112 ), static_cast<float>( m_y + 6 ), 30.0f, 28.0f };
    const UiRect maximizeButton = { static_cast<float>( m_x + m_width - 76 ), static_cast<float>( m_y + 6 ), 30.0f, 28.0f };
    const UiRect closeButton = { static_cast<float>( m_x + m_width - 40 ), static_cast<float>( m_y + 6 ), 30.0f, 28.0f };

    m_tabBar.SetBounds( static_cast<float>( m_x + 14 ), static_cast<float>( m_y + titleH ), static_cast<float>( m_width - 28 ), static_cast<float>( tabH ) );
    m_blurToggle.SetBounds( static_cast<float>( m_x + 32 ), static_cast<float>( bottomY + 22 ), 100.0f, 24.0f );
    m_vsyncToggle.SetBounds( static_cast<float>( m_x + 158 ), static_cast<float>( bottomY + 22 ), 100.0f, 24.0f );
    m_rendererCombo.SetBounds( static_cast<float>( m_x + 32 ), static_cast<float>( bottomY + 48 ), 126.0f, 24.0f );
    m_timelineToggle.SetBounds( static_cast<float>( m_x + 158 ), static_cast<float>( bottomY + 48 ), 100.0f, 24.0f );

    if ( wheelDelta != 0 && inContent )
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
            static const int kTabCount = static_cast<int>( InGameUiTab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );
            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUiTab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
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
            }
            else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUiTab::Profiler )
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
                        UiIconButton expander;
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
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_profilerTimelineEnabled = !m_profilerTimelineEnabled;
            }
        }
        else
        {
            m_rendererCombo.Close();
        }
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
        m_isDragging = false;
        m_isResizing = false;
        ReleaseCapture();
    }

    m_leftWasDown = leftNow;
    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_blocksCameraMouse = inside || m_isDragging || m_isResizing;
    return result;
}


void InGameUi::Draw( const InGameUiFrameData& data )
{
    if ( !m_isVisible )
    {
        return;
    }

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    const UiDrawContext draw( screenW, screenH );

    if ( m_isMinimized )
    {
        const UiRect minimized = MinimizedRect( screenW, screenH );
        char titleText[192] = {};
        BuildWindowTitle( data, titleText, sizeof( titleText ) );
        FitTitleText( titleText, sizeof( titleText ), 12.5f, minimized.w - 58.0f );
        draw.Rect( minimized.x - 5.0f, minimized.y - 5.0f, minimized.w + 10.0f, minimized.h + 10.0f, 0.03f, 0.54f, 0.86f, 0.12f );
        draw.Rect( minimized.x, minimized.y, minimized.w, minimized.h, 0.018f, 0.040f, 0.056f, 0.76f );
        draw.Outline( minimized.x, minimized.y, minimized.w, minimized.h, 0.39f, 0.88f, 1.0f, 0.92f );
        draw.Rect( minimized.x + 10.0f, minimized.y + 12.0f, 12.0f, 12.0f, 0.34f, 0.91f, 1.0f, 0.90f );
        draw.Text( minimized.x + 32.0f, minimized.y + 11.0f, 12.5f, 0.90f, 0.98f, 1.0f, titleText );
        draw.Text( minimized.x + minimized.w - 25.0f, minimized.y + 10.0f, 14.0f, 0.82f, 0.98f, 1.0f, "+" );
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
    FitTitleText( titleText, sizeof( titleText ), 15.5f, w - 150.0f );
    ApplyProfilerDefaultExpansion();
    ApplyProfilerExpandAll();

    const UiRect blurBounds = { x, y, w, h };
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
    draw.Rect( x + w - 112.0f, y + 6.0f, 30.0f, 28.0f, 0.026f, 0.080f, 0.102f, 0.70f );
    draw.Rect( x + w - 76.0f, y + 6.0f, 30.0f, 28.0f, 0.026f, 0.080f, 0.102f, 0.70f );
    draw.Rect( x + w - 40.0f, y + 6.0f, 30.0f, 28.0f, 0.026f, 0.080f, 0.102f, 0.70f );
    draw.Outline( x + w - 112.0f, y + 6.0f, 30.0f, 28.0f, 0.18f, 0.40f, 0.48f, 0.58f );
    draw.Outline( x + w - 76.0f, y + 6.0f, 30.0f, 28.0f, 0.18f, 0.40f, 0.48f, m_isMaximized ? 0.90f : 0.58f );
    draw.Outline( x + w - 40.0f, y + 6.0f, 30.0f, 28.0f, 0.18f, 0.40f, 0.48f, 0.58f );
    draw.Text( x + w - 101.0f, y + 12.0f, 13.0f, 0.68f, 0.86f, 0.92f, "-" );
    draw.Text( x + w - 67.0f, y + 12.0f, 12.0f, 0.68f, 0.86f, 0.92f, m_isMaximized ? "><" : "[]" );
    draw.Text( x + w - 31.0f, y + 12.0f, 13.0f, 0.82f, 0.92f, 0.96f, "X" );

    static const char* kTabs[] = { "Overview", "Profiler", "Scene", "Physics", "Renderer", "Keys" };
    const int tabCount = static_cast<int>( InGameUiTab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    draw.Rect( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.010f, 0.020f, 0.028f, 0.56f );
    draw.Outline( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.16f, 0.28f, 0.34f, 0.62f );

    auto visible = [&]( float rowY, float rowH ) -> bool
    {
        return rowY + rowH >= contentY && rowY <= contentY + contentH;
    };
    auto labelValue = [&]( float rowY, const char* label, const char* value, float vr, float vg, float vb )
    {
        if ( !visible( rowY, 18.0f ) )
        {
            return;
        }
        draw.Text( contentX, rowY, 11.5f, 0.52f, 0.76f, 0.84f, label );
        draw.Text( contentX + 150.0f, rowY, 11.5f, vr, vg, vb, value );
    };
    if ( m_activeTab == InGameUiTab::Profiler )
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
                UiIconButton expander;
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
    else if ( m_activeTab == InGameUiTab::Overview )
    {
        char buf[128];
        draw.Text( contentX, scrolledY, 16.0f, 1.0f, 0.85f, 0.34f, "Overview" );
        labelValue( scrolledY + 42.0f, "Renderer", data.rendererName, 0.60f, 0.90f, 1.0f );
        snprintf( buf, sizeof( buf ), "%d", data.modelCount );
        labelValue( scrolledY + 68.0f, "Model count", buf, 0.88f, 0.92f, 0.94f );
        labelValue( scrolledY + 94.0f, "Physics", data.legacyPhysics ? "Legacy solver" : "Impulse solver", 0.36f, 0.95f, 0.56f );
        snprintf( buf, sizeof( buf ), "%.1f FPS", data.fps );
        labelValue( scrolledY + 120.0f, "Frame rate", buf, 0.52f, 0.94f, 1.0f );
        snprintf( buf, sizeof( buf ), "%.6f", data.sceneEnergy );
        labelValue( scrolledY + 146.0f, "Scene energy", buf, 0.98f, 0.78f, 0.35f );
    }
    else if ( m_activeTab == InGameUiTab::Scene )
    {
        char buf[160];
        draw.Text( contentX, scrolledY, 16.0f, 1.0f, 0.85f, 0.34f, "Scene Telemetry" );
        if ( data.targetFrameCount > 0 )
        {
            const int displayedFrame = ( data.testComplete && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount : data.currentFrame;
            snprintf( buf, sizeof( buf ), "%d / %d", displayedFrame, data.targetFrameCount );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "%d", data.currentFrame );
        }
        labelValue( scrolledY + 42.0f, "Frame", buf, 0.88f, 0.92f, 0.94f );
        snprintf( buf, sizeof( buf ), "%d / %d", data.currentSceneIndex + 1, data.sceneCount );
        labelValue( scrolledY + 68.0f, "Scene index", buf, 0.88f, 0.92f, 0.94f );
        snprintf( buf, sizeof( buf ), "%.6f", data.sceneEnergy );
        labelValue( scrolledY + 94.0f, "Kinetic energy", buf, 0.98f, 0.78f, 0.35f );
        labelValue( scrolledY + 120.0f, "Fixed step", data.fixedStep ? "on" : "off", 0.52f, 0.94f, 1.0f );
        labelValue( scrolledY + 146.0f, "Status", data.testComplete ? "test complete" : "running", 0.36f, 0.95f, 0.56f );
    }
    else if ( m_activeTab == InGameUiTab::Physics )
    {
        char buf[128];
        draw.Text( contentX, scrolledY, 16.0f, 1.0f, 0.85f, 0.34f, "Physics Controls" );
        labelValue( scrolledY + 42.0f, "Solver", data.legacyPhysics ? "Legacy" : "Impulse", 0.36f, 0.95f, 0.56f );
        snprintf( buf, sizeof( buf ), "0x%04X", data.physicsDebugFlags );
        labelValue( scrolledY + 68.0f, "Debug flags", buf, 0.52f, 0.94f, 1.0f );
        snprintf( buf, sizeof( buf ), "%.2f", data.physicsDebugAlpha );
        labelValue( scrolledY + 94.0f, "Body alpha", buf, 0.88f, 0.92f, 0.94f );
        snprintf( buf, sizeof( buf ), "%.2fs", data.physicsDebugContactLinger );
        labelValue( scrolledY + 120.0f, "Contact linger", buf, 0.88f, 0.92f, 0.94f );
        labelValue( scrolledY + 146.0f, "Collision visual", data.collisionVisualizer ? "on" : "off", 0.98f, 0.78f, 0.35f );
    }
    else if ( m_activeTab == InGameUiTab::Renderer )
    {
        draw.Text( contentX, scrolledY, 16.0f, 1.0f, 0.85f, 0.34f, "Renderer" );
        labelValue( scrolledY + 42.0f, "Backend", data.rendererName, 0.60f, 0.90f, 1.0f );
        labelValue( scrolledY + 68.0f, "VSync", data.vsyncEnabled ? "on" : "off", 0.36f, 0.95f, 0.56f );
        labelValue( scrolledY + 94.0f, "Pipeline sync", data.pipelineSyncEnabled ? "on" : "off", 0.88f, 0.92f, 0.94f );
        labelValue( scrolledY + 120.0f, "Water reflect", data.waterNoReflect ? "disabled" : ( data.waterRTReflect ? "DXR" : "FBO" ), 0.52f, 0.94f, 1.0f );
        labelValue( scrolledY + 146.0f, "Glass blur", m_blurPreviewEnabled ? "cached backdrop" : "transparent only", 0.98f, 0.78f, 0.35f );
    }
    else
    {
        draw.Text( contentX, scrolledY, 16.0f, 1.0f, 0.85f, 0.34f, "Keyboard Reference" );
        static const char* kKeys[] = {
            "0  Toggle UI window",
            "F  Fly mode",
            "N  Nudge mode",
            "WASD  Move camera",
            "Mouse  Look / UI pointer",
            "Shift  Sprint",
            "Z  Fire ball",
            "X  Fire box",
            "P  Physics solver",
            "Q  Cycle renderer",
            "V  Collision visual",
            "C  Physics debug",
            "G  Broadphase overlay",
            "1-6  Water/debug toggles",
            "9  Debug vectors",
            "PgUp/PgDn  Water height",
            "F2  Save scene",
            "F3  Screenshot",
            "Esc  Quit" };
        const int keyCount = static_cast<int>( sizeof( kKeys ) / sizeof( kKeys[0] ) );
        for ( int i = 0; i < keyCount; ++i )
        {
            const float rowY = scrolledY + 42.0f + static_cast<float>( i ) * 25.0f;
            if ( visible( rowY, 18.0f ) )
            {
                draw.Text( contentX, rowY, 11.5f, 0.70f, 0.90f, 1.0f, kKeys[i] );
            }
        }
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 2.0f, by, w - 4.0f, bottomH - 2.0f, 0.014f, 0.042f, 0.056f, 0.82f );
    draw.Rect( x + 2.0f, by, w - 4.0f, 1.0f, 0.30f, 0.88f, 1.0f, 0.46f );
    draw.Rect( x + 18.0f, by + 16.0f, 244.0f, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
    draw.Outline( x + 18.0f, by + 16.0f, 244.0f, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

    m_blurToggle.SetBounds( x + 32.0f, by + 22.0f, 100.0f, 24.0f );
    m_vsyncToggle.SetBounds( x + 158.0f, by + 22.0f, 100.0f, 24.0f );
    m_rendererCombo.SetBounds( x + 32.0f, by + 48.0f, 126.0f, 24.0f );
    m_timelineToggle.SetBounds( x + 158.0f, by + 48.0f, 100.0f, 24.0f );
    m_blurToggle.DrawToggle( draw, "Blur", m_blurPreviewEnabled, 0.34f, 0.91f, 1.0f );
    m_vsyncToggle.DrawToggle( draw, "VSync", data.vsyncEnabled, 0.34f, 0.91f, 1.0f );
    const int currentRendererIndex = GetRendererIndexFromName( data.rendererName );
    static const char* kRendererOptions[] = { "GL", "DX11", "DX12" };
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 3, currentRendererIndex, m_mouseX, m_mouseY );
    m_timelineToggle.DrawToggle( draw, "Timeline", m_profilerTimelineEnabled, 0.34f, 0.91f, 1.0f );

    const float statsX = x + 274.0f;
    const float statsW = (std::max)( 120.0f, w - 292.0f );
    draw.Rect( statsX, by + 16.0f, statsW, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
    draw.Outline( statsX, by + 16.0f, statsW, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUi + data.uiDrawCalls;
    auto statCell = [&]( float tx, const char* name, const char* value, float r, float g, float b )
    {
        draw.Text( tx, by + 25.0f, 10.0f, 0.67f, 0.74f, 0.77f, name );
        draw.Text( tx, by + 47.0f, 11.5f, r, g, b, value );
    };
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( statsW < 350.0f )
    {
        char fpsText[32];
        char frameText[32];
        char drawText[32];
        snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
        snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
        snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.uiDrawCalls );
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
        snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.uiDrawCalls );
        statCell( statsX + statsW - 112.0f, "Draws / UI", status, 0.32f, 0.90f, 1.0f );
    }

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.88f );
    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.72f );
    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.60f );

    Text2d::FlushQuads();
}
