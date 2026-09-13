/*
File: SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
Purpose:
  Declares product-specific GameUI geometry and small display policy.

Summary:
  The component foundation supplies generic rectangles and interpolation. This
  Runtime-owned layer arranges the scene combo, Physics pipeline buttons, and
  footer controls, and describes when reflection choices are disabled, without
  teaching those product rules to SKULLBONEZ_UI.

Invariants:
  - Drawing and hit testing call the same bounds helpers.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp
  - SkullbonezSource/UI/UILayout.h
*/
#pragma once

#include "../../../UI/UIDraw.h"
#include "../../../UI/UIStyle.h"

#include <cstdint>
#include <array>

namespace SkullbonezCore::UI::GameLayout
{
inline constexpr float EDITOR_CONTROLS_HEIGHT = 470.0f;
inline constexpr float EDITOR_PALETTE_TOP = EDITOR_CONTROLS_HEIGHT + 22.0f;
float EditorContentHeight( float width );

std::array<char, 64> HeaderTitle( const char* sceneName );

enum class LayoutMode : uint8_t
{
    Canvas,
    Editor
};

enum class Workspace : uint8_t
{
    Scene,
    SolverLab
};

// Presentation values only. A layout switch never owns a scene, camera,
// simulation clock, comparison recording, or functional editor mode.
struct PresentationPreferences
{
    static constexpr uint32_t VERSION = 5;
    LayoutMode layout = LayoutMode::Canvas;
    Style::Theme theme = Style::Theme::Blue;
    float leftWidth = 280.0f;
    float rightWidth = 360.0f;
    float drawerHeight = 360.0f;
    float diagnosticsHeight = 140.0f;
    // Independent evidence sections; all start folded.
    uint32_t foldedSections = 7;
    int lastTool = 1;
    bool leftFolded = true;
    bool replayFolded = true;
    bool rightFolded = true;
};

struct PresentationState
{
    PresentationPreferences preferences;
    Workspace workspace = Workspace::Scene;
    bool toolsOpen = false;
    bool markerHistoryOpen = false;
    bool memoryWaterlineOpen = false;
    bool detailsOpen = false;
    bool detailsCauses = false;
    bool editorInTools = false;
    int focusedDiagnostic = 0;
    float replayScroll = 0.0f;
    float editorScroll = 0.0f;
};

struct PresentationRects
{
    UIRect window;
    UIRect header;
    UIRect viewport;
    UIRect statusContent;
    UIRect left;
    UIRect right;
    UIRect editorPane;
    UIRect replayPane;
    UIRect transport;
    UIRect drawer;
    UIRect markerHistory;
    UIRect memoryWaterline;
    UIRect leftResize;
    UIRect replayResize;
    UIRect rightResize;
    UIRect drawerResize;
    UIRect replayControls;
    UIRect replayDetails;
    UIRect causeControls;
    UIRect detailsReplayTab;
    UIRect detailsCausesTab;
    UIRect editorTab;
    UIRect editorReplayTab;
    UIRect causeTab;
    UIRect editorControls;
    UIRect leftFold;
    UIRect replayFold;
    UIRect rightFold;
    float editorScroll = 0.0f;
    float replayScroll = 0.0f;
};

struct HeaderRects
{
    UIRect close;
    UIRect skull;
    UIRect scene;
    UIRect scenes;
    UIRect camera;
    UIRect workspace;
    UIRect layout;
    UIRect tools;
};

struct DiagnosticPresentation
{
    bool markerHistoryVisible = false;
    bool memoryWaterlineVisible = false;
    int markerSamples = 0;
    int memorySamples = 0;
    int focusedPanel = 0;
    uint32_t markerSelectionHash = 0;
    bool profilerTimeline = false;
    int profilerMarkerCount = 0;
    int profilerDrawNodeCount = 0;
    uint32_t profilerExpansionHash = 0;
    uint32_t drawExpansionHash = 0;
    UIRect drawExpanderBounds;
    UIRect markerBounds;
    UIRect memoryBounds;
    UIRect workerToggleBounds;
    UIRect workerSliderBounds;
};

struct ComboPopupPresentation
{
    UIRect bounds;
    int firstOption = 0;
    int visibleOptions = 0;
    int totalOptions = 0;
    bool open = false;
    int selectedOption = -1;
    uint32_t disabledMask = 0;
};

// Shared by drawer drawing, input, and tooltips; compact chrome leaves a locally
// scrollable content region even when the perimeter consumes most of the window.
struct ToolsChromeRects
{
    UIRect title;
    UIRect tabs;
    UIRect content;
    UIRect footer;
    UIRect close;
    bool compact = false;
};
ToolsChromeRects ComputeToolsChromeRects( const UIRect& bounds, bool sharedShell );
inline constexpr const char* TOOL_NAMES[] = { "Profiler", "Scene", "Editor", "Physics", "Options", "Render", "Targets", "Keys", "Sky", "Cinematic", "Memory" };

HeaderRects ComputeHeaderRects( const UIRect& header, Workspace workspace = Workspace::Scene );
UIRect DiagnosticDetailsBounds( const UIRect& panel );

// Invariant: all rectangles are window coordinates and computed before input
// and rendering for that frame. Closed panes have no hit-test area.
PresentationRects ComputePresentationRects( const PresentationState& state, int width, int height );
PresentationPreferences SanitizePreferences( const PresentationPreferences& preferences );
void DrawSkullLogo( const UIDrawContext& draw, const UIRect& bounds );

inline constexpr float CONTENT_TOGGLE_ROW_H = 30.0f;

inline constexpr int UI_SCENE_COMBO_VISIBLE_OPTIONS = 12;
inline constexpr float UI_SCENE_HEADER_BUTTON_GAP = 8.0f;
inline constexpr float UI_SCENE_RESET_BUTTON_W = 72.0f;
inline constexpr float UI_SCENE_RESET_DEFAULTS_BUTTON_W = 132.0f;
inline constexpr float UI_SCENE_SAVE_DEFAULTS_BUTTON_W = 132.0f;

inline constexpr float UI_PIPELINE_STEP_BUTTON_W = 26.0f;
inline constexpr float UI_PIPELINE_STEP_BUTTON_H = 22.0f;
inline constexpr float UI_PIPELINE_STEP_BUTTON_GAP = 6.0f;

int SceneComboVisibleCount( int optionCount );
int ClampSceneComboScroll( int scroll, int optionCount );
int SceneComboScrollForSelection( int selectedIndex, int optionCount );

struct SceneHeaderWidths
{
    float combo = 0.0f;
    float reset = 0.0f;
    float resetDefaults = 0.0f;
    float saveDefaults = 0.0f;
    float gap = 0.0f;
};

SceneHeaderWidths ResolveSceneHeaderWidths( float contentW );
float SceneTabComboWidth( float contentW );

void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y );

UIRect FooterRendererComboBounds( float x, float bottomY );
UIRect FooterWaterComboBounds( float x, float bottomY );
UIRect FooterBlurBounds( float x, float bottomY );
UIRect FooterVsyncBounds( float x, float bottomY );
UIRect FooterHitboxBounds( float x, float bottomY );
UIRect FooterTimelineBounds( float x, float bottomY );
UIRect FooterPerfBounds( float x, float bottomY );
uint32_t ReflectionDisabledMask();
} // namespace SkullbonezCore::UI::GameLayout
