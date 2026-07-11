
/*
File: UIFrameComposition.h
Purpose:
  Shares UI frame layout/value helpers and editor mini-palette contracts across
  the main UI owner and its palette translation units.

Mental model:
  UI.cpp owns the frame and widget state. These inline helpers derive immutable
  geometry/signatures, while palette translation units consume the same layout
  records for input and drawing.

Glossary:
  Content signature: Hash of UI-visible values used to invalidate cached draws.
  Interaction signature: Hash of pointer/focus state used to invalidate hit data.
  Mini palette: Compact editor placement surface shown while UI is minimized.
  Flyout: Secondary row of palette variants anchored to one palette entry.

Invariants:
  - Hit testing and drawing consume the same EditorMiniPaletteLayout record.
  - Helpers retain no UI owner pointer or frame borrow.
  - Palette constants remain one compatibility vocabulary across both units.

Related:
  - UI.cpp owns frame orchestration and retained widget state.
  - UIEditorMiniPalette.cpp owns palette policy, layout, and hit testing.
  - UIEditorMiniPaletteDraw.cpp owns palette and minimized-window drawing.
*/
#pragma once

#include "UI.h"
#include "../Runtime/InputRouter.h"
#include "../Assets/AssetSystem.h"
#include "../Rendering/IRenderCommandContext.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/IRenderResourceFactory.h"
#include "../Maths/Matrix4.h"
#include "../Runtime/Debug/PhysicsDebugVisualizer.h"
#include "../Core/Profiler.h"
#include "../Rendering/Text.h"
#include "UIDraw.h"
#include "UIDrawList.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UIStyle.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::UI::FrameComposition
{
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;

uint32_t HashCombine( uint32_t seed, uint32_t value );


uint32_t HashTextValue( uint32_t seed, const char* value );


uint32_t HashBool( uint32_t seed, bool value );


uint32_t HashInt( uint32_t seed, int value );


uint32_t HashFloat( uint32_t seed, float value, float scale = 100.0f );

// Invariant: camera mode options are indexed by static_cast<int>(RunCameraMode)
// even though this UI file stays decoupled from the runtime enum header. Keep
// this table in enum order and keep UI.h default masks at one bit per option.
constexpr int CAMERA_MODE_OPTION_COUNT = 7;
const char* const kCameraModeOptions[CAMERA_MODE_OPTION_COUNT] =
    { "Demo", "Scene", "Inspect", "Attach", "Launcher", "Manipulator", "Director" };
constexpr float MINIMIZED_CAMERA_MODE_COMBO_W = 104.0f;
constexpr float MINIMIZED_CAMERA_MODE_GAP = 8.0f;
constexpr float MINIMIZED_RESTORE_W = 42.0f;
constexpr float MINIMIZED_RUN_MAX_W = 330.0f;

UIRect MinimizedCameraModeComboBounds( const UIRect& minimized );

float MinimizedWidthWithCameraModeCombo( const char* title, int screenW );

void StripMinimizedRuntimeModeSuffix( const InGameUIFrameData& data, char* title, size_t titleSize );

uint32_t HashRenderTargetPreviewCatalog( uint32_t hash, const InGameUIFrameData& data );


uint32_t HashProfilerFrameSnapshot( uint32_t hash, const ProfilerTab::FrameSnapshot& frame );


uint32_t BuildUIContentSignature( const InGameUIFrameData& data );


uint32_t BuildUIInteractionSignature( int mouseX,
                                      int mouseY,
                                      bool rendererOpen,
                                      bool reflectionOpen,
                                      bool sceneOpen,
                                      bool cineSceneOpen,
                                      bool editorObjectOpen,
                                      bool renderTargetOpen,
                                      bool cameraModeOpen,
                                      int selectedRenderTarget,
                                      int activeSlider );


void FlushUIDrawList( const UIDrawList& drawList,
                      IRenderCommandContext& renderCommands,
                      IRenderDiagnostics& renderDiagnostics,
                      int screenW,
                      int screenH,
                      float offsetX = 0.0f,
                      float offsetY = 0.0f );

int RenderTargetPreviewCount( const InGameUIFrameData& data );

uint32_t RenderTargetPreviewDisabledMask( const InGameUIFrameData& data );

int FirstAvailableRenderTargetPreview( const InGameUIFrameData& data );

int ResolveRenderTargetPreviewSelection( const InGameUIFrameData& data, int selectedIndex );

const char* RenderTargetPreviewTypeText( const UIRenderTargetPreviewResource& resource );

UIRect IntersectRect( const UIRect& a, const UIRect& b );

UIRect FitRectToAspect( const UIRect& bounds, int width, int height );


void BuildEditorObjectCounterText( const InGameUIFrameData& data, char* out, size_t outSize );


UIRect TitleButtonGroupBounds( const Chrome::TitleButtonRects& titleButtons );


void DrawEditorObjectCounter( const UIDrawContext& draw,
                              const InGameUIFrameData& data,
                              int screenW,
                              int screenH,
                              const UIRect* avoidBounds = nullptr );


void EnsureRenderTargetPreviewResources( std::unique_ptr<IShader>& shader,
                                         uint32_t& dynamicVB,
                                         const UIRenderContext& render );

void ResetRenderTargetPreviewResources( std::unique_ptr<IShader>& shader,
                                        uint32_t& dynamicVB,
                                        IRenderResourceFactory* resources );

void DrawRenderTargetPreviewTexture( std::unique_ptr<IShader>& shader,
                                     uint32_t& dynamicVB,
                                     const UIDrawContext& draw,
                                     const UIRenderTargetPreviewResource& resource,
                                     const UIRect& bounds,
                                     const UIRect& clipBounds,
                                     const UIRenderContext& render );

int WaterReflectionModeFromData( const InGameUIFrameData& data );

constexpr int UI_RENDER_SLIDER_BASE = 6000;
constexpr float UI_RENDER_FEATURE_START_Y = 48.0f;
constexpr float UI_RENDER_START_Y = 118.0f;
constexpr float UI_RENDER_SECTION_H = 28.0f;
constexpr float UI_RENDER_ROW_H = 42.0f;
constexpr float UI_RENDER_SAVE_BUTTON_W = 126.0f;
constexpr float UI_TARGETS_COMBO_Y = 42.0f;
constexpr float UI_TARGETS_META_Y = 86.0f;
constexpr float UI_TARGETS_PREVIEW_Y = 132.0f;
constexpr float UI_TARGETS_PREVIEW_H = 260.0f;
constexpr float UI_TARGETS_CONTENT_H = 430.0f;

struct RenderSliderSpec
{
    const char* section;
    const char* label;
    UIRenderParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

constexpr RenderSliderSpec kRenderSliderSpecs[] = {
    { "Light", "Sun intensity", UIRenderParam::SunIntensity, 0.00f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Sun R", UIRenderParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun G", UIRenderParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun B", UIRenderParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Ambient", UIRenderParam::AmbientStrength, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Sky Ambient", "Sky R", UIRenderParam::SkyRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Sky G", UIRenderParam::SkyGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Sky B", UIRenderParam::SkyBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Ground Ambient", "Ground R", UIRenderParam::GroundRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground G", UIRenderParam::GroundGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground B", UIRenderParam::GroundBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Shadows", "Strength", UIRenderParam::ShadowStrength, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Softness", UIRenderParam::ShadowSoftness, 0.25f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Depth bias", UIRenderParam::ShadowDepthBias, 0.00000f, 0.00500f, 0.00001f, "%.5f" },
    { nullptr, "Slope bias", UIRenderParam::ShadowSlopeBias, 0.00000f, 0.00500f, 0.00001f, "%.5f" },
    { "Water", "Water R", UIRenderParam::WaterRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water G", UIRenderParam::WaterGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water B", UIRenderParam::WaterBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Alpha", UIRenderParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Reflection", UIRenderParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Fresnel F0", UIRenderParam::WaterFresnel, 0.000f, 0.120f, 0.001f, "%.3f" },
    { "Materials", "Ball roughness", UIRenderParam::BallRoughness, 0.25f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Ball specular", UIRenderParam::BallSpecular, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Box roughness", UIRenderParam::BoxRoughness, 0.25f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Box specular", UIRenderParam::BoxSpecular, 0.00f, 2.00f, 0.01f, "%.2f" },
};
static_assert( sizeof( kRenderSliderSpecs ) / sizeof( kRenderSliderSpecs[0] ) ==
                   static_cast<int>( UIRenderParam::Count ),
               "Render slider specs must match UIRenderParam." );

constexpr int EDITOR_MINI_TREE_TYPE_NONE = -1;
constexpr int EDITOR_MINI_TREE_TYPE_SMALL = 0;
constexpr int EDITOR_MINI_TREE_TYPE_PINE = 1;
constexpr int EDITOR_MINI_TREE_TYPE_CEDAR = 2;
constexpr int EDITOR_MINI_TREE_TYPE_COUNT = 3;
constexpr int EDITOR_MINI_TREE_PLACEMENT_NONE = -1;
constexpr int EDITOR_MINI_TREE_PLACEMENT_FIXED = 0;
constexpr int EDITOR_MINI_TREE_PLACEMENT_SLEEPING = 1;
constexpr int EDITOR_MINI_TREE_PLACEMENT_ROOTED = 2;
constexpr int EDITOR_MINI_RAGDOLL_MODE_SLEEPING = 1;
constexpr int EDITOR_MINI_RAGDOLL_MODE_COUNT = 2;
constexpr int EDITOR_MINI_FLYOUT_OPTION_MAX = 3;
constexpr double EDITOR_MINI_HOLD_SECONDS = 0.32;
constexpr int EDITOR_MINI_HOLD_MODE_NONE = 0;
constexpr int EDITOR_MINI_HOLD_MODE_TREE_TYPES = 1;
constexpr int EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES = 2;

struct EditorMiniPaletteEntry
{
    int objectType;
    int treePlacement;
    int holdMode;
};

constexpr EditorMiniPaletteEntry kEditorMiniPaletteEntries[] = {
    { EditorTab::OBJECT_BOX, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_BALL, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_SPHERE, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_WEDGE, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_TRI_PRISM, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_TAPERED_BLOCK, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_PYRAMID, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_HEX_PRISM, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_DIAMOND, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_SLAB, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_LUMP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_SHARD, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_CHIPPED, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_BRICK_HOUSE_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_CUTE_HOUSE_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_TRIPLE_DECKER_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_BRICK_WALL_200_SLEEP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_TREE_BIG, EDITOR_MINI_TREE_PLACEMENT_FIXED, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_TREE_BIG_SLEEP, EDITOR_MINI_TREE_PLACEMENT_SLEEPING, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_TREE_BIG_ROOTED, EDITOR_MINI_TREE_PLACEMENT_ROOTED, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_RAGDOLL, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES },
};
constexpr int EDITOR_MINI_PALETTE_ENTRY_COUNT =
    static_cast<int>( sizeof( kEditorMiniPaletteEntries ) / sizeof( kEditorMiniPaletteEntries[0] ) );

struct EditorMiniPaletteLayout
{
    UIRect buttons[EDITOR_MINI_PALETTE_ENTRY_COUNT];
    UIRect flyoutOptions[EDITOR_MINI_FLYOUT_OPTION_MAX];
    UIRect bounds;
    UIRect flyoutBounds;
    float buttonSize = 0.0f;
    int buttonCount = 0;
    int flyoutOptionCount = 0;
    bool flyoutVisible = false;
};

struct EditorMinimizedStatusLayout
{
    UIRect restoreButton;
    UIRect glyph;
    UIRect modeChip;
    UIRect bodyChip;
    UIRect alignChip;
    float labelX = 0.0f;
    float labelMaxW = 0.0f;
};


bool IsEditorMiniTreePlacementValid( int placement );
int EditorMiniPaletteFlyoutOptionCount( int holdMode );
bool EditorMiniTreeTypeForType( int objectType, int& outTreeType, int& outPlacement );
bool EditorMiniPaletteTreeStateForType( int objectType, bool editorPlaceStatic, int& outPlacement, int& outTreeType );
int EditorMiniTreeObjectType( int treeType, int placement );
EditorMiniPaletteLayout BuildEditorMiniPaletteLayout( int screenW,
                                                      int screenH,
                                                      const UIRect& minimized,
                                                      int flyoutAnchorEntry,
                                                      bool flyoutOpen );
int HitEditorMiniPaletteButton( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY );
int HitEditorMiniPaletteFlyoutOption( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY );
bool EditorMiniPaletteContains( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY );
bool IsBlockVisible( float contentY, float contentH, float blockY, float blockH );
void DrawHitboxRect( const UIDrawContext& draw,
                     const UIRect& bounds,
                     float r,
                     float g,
                     float b,
                     float fillA = 0.060f,
                     float outlineA = 0.94f );
void DrawComboHitboxes( const UIDrawContext& draw,
                        const UIComboBox& combo,
                        int optionCount,
                        float r,
                        float g,
                        float b );
void DrawTabHitboxes( const UIDrawContext& draw, const UITabBar& tabBar, int tabCount );
int SceneDropdownHitboxOptionCount( const SceneTab::UISceneTabState& state, const InGameUIFrameData& data );
void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth );
void DrawFittedText( const UIDrawContext& draw,
                     float x,
                     float y,
                     float pxSize,
                     const Style::UIColor& color,
                     const char* value,
                     float maxWidth );
int RenderSliderIndexFromActiveSlider( int activeSlider );
float RenderSliderY( int index, float baseY );
int RenderContentHeight();
int RenderTargetsContentHeight();
float RenderValueForParam( const OrdinaryRenderConfig& ordinary, UIRenderParam param );
void SetRenderSliderResult( InGameUIInputResult& result,
                            const UISlider& slider,
                            int mouseX,
                            const RenderSliderSpec& spec );
float EditorMiniChipWidth( const char* label );
EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized,
                                                              bool editorPlacementMode,
                                                              bool editorPlaceStatic,
                                                              bool editorTerrainAlign );
EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized, const InGameUIFrameData& data );
float EditorMinimizedWidth( const InGameUIFrameData& data, int screenW );
void DrawEditorMiniChip( const UIDrawContext& draw,
                         float x,
                         float y,
                         const char* label,
                         const Style::UIColor& fill,
                         const Style::UIColor& text,
                         bool hot );
bool IsEditorMiniRootType( int objectType );
bool IsEditorMiniRockType( int objectType );
bool IsEditorMiniHullType( int objectType );
bool EditorMiniTreeVisualForType( int objectType,
                                  int& outTreeType,
                                  int& outPlacement,
                                  bool& outSlope,
                                  bool& outShedding );
void DrawEditorMiniRootSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   bool largeRoot,
                                   const Style::UIColor& color,
                                   float alpha );
void DrawEditorMiniTreeSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int family,
                                   bool rooted,
                                   bool slope,
                                   bool shedding,
                                   const Style::UIColor& color,
                                   float alpha );
void DrawEditorMiniHullSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int objectType,
                                   const Style::UIColor& color,
                                   float alpha );
void DrawEditorMiniRockSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int objectType,
                                   const Style::UIColor& color,
                                   float alpha );
void DrawEditorMiniIcon( const UIDrawContext& draw,
                         const UIRect& bounds,
                         int objectType,
                         const Style::UIColor& color,
                         float alpha );
void DrawEditorMiniGlyph( const UIDrawContext& draw, const UIRect& bounds, int objectType );
void DrawEditorMiniVariantMarker( const UIDrawContext& draw,
                                  const UIRect& bounds,
                                  int variant,
                                  const Style::UIColor& color );
void DrawEditorMiniHoldMarker( const UIDrawContext& draw,
                               const UIRect& bounds,
                               const Style::UIColor& color,
                               bool active );
void DrawEditorMiniPaletteButton( const UIDrawContext& draw,
                                  const UIRect& bounds,
                                  int objectType,
                                  bool selected,
                                  bool hot,
                                  int variantMarker,
                                  bool holdCapable,
                                  bool holdActive );
void DrawEditorMiniTooltip( const UIDrawContext& draw,
                            const UIRect& anchor,
                            const char* label,
                            int screenW,
                            int screenH );
int EditorMiniRagdollObjectType( int mode );
bool EditorMiniSelectionRequestsStatic( int holdMode, int treePlacement, bool& outPlaceStatic );
void DrawEditorMiniPalette( const UIDrawContext& draw,
                            const EditorMiniPaletteLayout& layout,
                            int editorObjectType,
                            bool editorPlaceStatic,
                            int mouseX,
                            int mouseY,
                            int flyoutTreePlacement,
                            int flyoutHoldMode,
                            int pressedEntry,
                            int screenW,
                            int screenH );
void DrawEditorMinimizedWindow( const UIDrawContext& draw,
                                const UIRect& minimized,
                                const InGameUIFrameData& data,
                                int mouseX,
                                int mouseY );
} // namespace SkullbonezCore::UI::FrameComposition
