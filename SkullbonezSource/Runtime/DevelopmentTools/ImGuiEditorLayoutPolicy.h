/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h
Purpose:
  Defines the value-only deterministic dock-shell layout contract.

Summary:
  A fixed topology descriptor and responsive pixel envelope translate the
  current dock content size into stable split fractions. The same value-only
  policy fits the captured game image into its central pane and maps pointer
  pixels back to the source render extent without constructing a vendor context.
  A fixed preference record keeps benign editor presentation choices outside
  authored scenes and replays and rejects stale topology or panel identities.

Glossary:
  Content envelope: Space below the shell menu and toolbar available to docks.
  Primary region: Editor-left, viewport-center, utility-right, replay-bottom,
    or bottommost status.
  Topology fingerprint: FNV-1a hash of the versioned stable region/panel order.
  Game viewport rect: Letterboxed image rectangle plus the source render extent
    used by picking, placement, and gizmo coordinate mapping.
  Detail panel: Stable, normally closed window identity that can float or dock
    without changing the default topology descriptor.
  Preference migration: Recovery that retains bounded text filters but resets
    panel visibility and docking when schema, layout, or topology is stale.

Invariants:
  - The viewport receives all width left after bounded editor and utility rails.
  - Status is split before replay so it remains the bottommost leaf.
  - The descriptor contains no transient node id, pixel size, or pointer.
  - A reset always feeds the same descriptor and split order to DockBuilder.
  - ImGui and Win32 client coordinates are physical pixels in the single-window
    backend; DPI scales chrome, but never scales the pointer a second time.
  - Preference parsing uses fixed caller-owned storage and never allocates.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
*/
#pragma once

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
inline constexpr int IMGUI_EDITOR_LAYOUT_VERSION = 2;
inline constexpr int IMGUI_EDITOR_PREFERENCES_VERSION = 1;
inline constexpr int IMGUI_EDITOR_MINIMUM_WINDOW_WIDTH = 1280;
inline constexpr int IMGUI_EDITOR_MINIMUM_WINDOW_HEIGHT = 720;
inline constexpr std::size_t IMGUI_EDITOR_FILTER_CAPACITY = 64u;
inline constexpr std::size_t IMGUI_EDITOR_PREFERENCES_TEXT_CAPACITY = 1024u;
inline constexpr const char* IMGUI_EDITOR_DOCKSPACE_NAME = "SkoreEditorDockspaceV2";
inline constexpr const char* IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR = "v2|status:bottommost|replay:bottom|left:scene,"
                                                                "hierarchy,assets|center:game-viewport|"
                                                                "right:inspector,world,rendering,diagnostics,causality";

namespace ImGuiEditorPanel
{
inline constexpr const char* SceneAndModes = "Scene & Modes";
inline constexpr const char* Hierarchy = "Hierarchy";
inline constexpr const char* AssetsCreate = "Assets / Create";
inline constexpr const char* GameViewport = "Game Viewport";
inline constexpr const char* Inspector = "Inspector";
inline constexpr const char* WorldSimulation = "World / Simulation";
inline constexpr const char* Rendering = "Render###SkoreRendering";
inline constexpr const char* Diagnostics = "Diag###SkoreDiagnostics";
inline constexpr const char* Causality = "Cause###SkoreCausality";
inline constexpr const char* CausalityDetail = "Cause Detail###SkoreCausalityDetail";
inline constexpr const char* Replay = "Replay";
inline constexpr const char* Status = "Status";
} // namespace ImGuiEditorPanel

enum class ImGuiEditorPanelId : uint8_t
{
    SceneAndModes,
    Hierarchy,
    AssetsCreate,
    GameViewport,
    Inspector,
    WorldSimulation,
    Rendering,
    Diagnostics,
    Causality,
    CausalityDetail,
    Replay,
    Status,
    Count
};

inline constexpr uint32_t ImGuiEditorPanelBit( ImGuiEditorPanelId panel ) noexcept
{
    return 1u << static_cast<uint32_t>( panel );
}

inline constexpr uint32_t IMGUI_EDITOR_ALL_PANEL_MASK = ( 1u << static_cast<uint32_t>( ImGuiEditorPanelId::Count ) ) - 1u;
inline constexpr uint32_t IMGUI_EDITOR_DEFAULT_PANEL_MASK = IMGUI_EDITOR_ALL_PANEL_MASK &
                                                            ~ImGuiEditorPanelBit( ImGuiEditorPanelId::CausalityDetail );

struct ImGuiEditorPreferences
{
    int preferencesVersion = IMGUI_EDITOR_PREFERENCES_VERSION;
    int layoutVersion = IMGUI_EDITOR_LAYOUT_VERSION;
    uint64_t topologyFingerprint = 0u;
    uint32_t panelVisibilityMask = IMGUI_EDITOR_DEFAULT_PANEL_MASK;
    char sceneFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};
    char hierarchyFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};
    char assetFilter[IMGUI_EDITOR_FILTER_CAPACITY] = {};
};

struct ImGuiEditorPreferenceParseResult
{
    ImGuiEditorPreferences preferences;
    bool valid = false;
    bool layoutResetRequired = true;
    bool recoveredDefaults = true;
};

struct ImGuiEditorLayoutEnvelope
{
    int contentWidth = 0;
    int contentHeight = 0;
    int editorLeftWidth = 0;
    int utilityRightWidth = 0;
    int viewportWidth = 0;
    int upperHeight = 0;
    int replayHeight = 0;
    int statusHeight = 0;
    float statusSplitFraction = 0.0f;
    float replaySplitFraction = 0.0f;
    float editorLeftSplitFraction = 0.0f;
    float utilityRightSplitFraction = 0.0f;
    bool compactToolbarLabels = false;
    bool preservesCentralViewport = false;
};

struct ImGuiGameViewportRect
{
    float imageMinX = 0.0f;
    float imageMinY = 0.0f;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
    float dpiScale = 1.0f;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool valid = false;
    bool letterboxed = false;
};

ImGuiEditorLayoutEnvelope ResolveImGuiEditorLayoutEnvelope( int contentWidth, int contentHeight ) noexcept;
ImGuiGameViewportRect ResolveImGuiGameViewportRect( float availableMinX, float availableMinY, float availableWidth,
                                                    float availableHeight, int sourceWidth, int sourceHeight,
                                                    float dpiScale ) noexcept;
bool MapImGuiGameViewportPoint( const ImGuiGameViewportRect& viewport, float clientX, float clientY, int& outSourceX,
                                int& outSourceY ) noexcept;
const char* ImGuiEditorPanelName( ImGuiEditorPanelId panel ) noexcept;
bool TryParseImGuiEditorPanel( const char* name, ImGuiEditorPanelId& outPanel ) noexcept;
ImGuiEditorPreferenceParseResult ParseImGuiEditorPreferences( const char* text, std::size_t textBytes ) noexcept;
std::size_t SerializeImGuiEditorPreferences( const ImGuiEditorPreferences& preferences, char* output,
                                             std::size_t outputCapacity ) noexcept;
uint64_t FingerprintImGuiEditorDefaultTopology() noexcept;
} // namespace SkullbonezCore::Runtime::DevelopmentTools
