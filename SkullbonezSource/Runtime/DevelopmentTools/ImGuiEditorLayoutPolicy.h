/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h
Purpose:
  Defines the value-only deterministic dock-shell layout contract.

Summary:
  A fixed topology descriptor and responsive pixel envelope translate the
  current dock content size into stable split fractions. The ImGui owner uses
  the values to build nodes; tests can verify 16:9, ultrawide, and minimum-size
  behavior without constructing a vendor context.

Glossary:
  Content envelope: Space below the shell menu and toolbar available to docks.
  Primary region: Editor-left, viewport-center, utility-right, replay-bottom,
    or bottommost status.
  Topology fingerprint: FNV-1a hash of the versioned stable region/panel order.

Invariants:
  - The viewport receives all width left after bounded editor and utility rails.
  - Status is split before replay so it remains the bottommost leaf.
  - The descriptor contains no transient node id, pixel size, or pointer.
  - A reset always feeds the same descriptor and split order to DockBuilder.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
  - Agentic/Plans/TODO/imgui-tracy-editor-campaign.md (E9)
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
inline constexpr int IMGUI_EDITOR_LAYOUT_VERSION = 2;
inline constexpr int IMGUI_EDITOR_MINIMUM_WINDOW_WIDTH = 1280;
inline constexpr int IMGUI_EDITOR_MINIMUM_WINDOW_HEIGHT = 720;
inline constexpr const char* IMGUI_EDITOR_DOCKSPACE_NAME = "SkoreEditorDockspaceV2";
inline constexpr const char* IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR =
    "v2|status:bottommost|replay:bottom|left:scene,hierarchy,assets|center:game-viewport|"
    "right:inspector,world,render-audio,diagnostics,causality";

namespace ImGuiEditorPanel
{
inline constexpr const char* SceneAndModes = "Scene & Modes";
inline constexpr const char* Hierarchy = "Hierarchy";
inline constexpr const char* AssetsCreate = "Assets / Create";
inline constexpr const char* GameViewport = "Game Viewport";
inline constexpr const char* Inspector = "Inspector";
inline constexpr const char* WorldSimulation = "World / Simulation";
inline constexpr const char* RenderingAudio = "Render###SkoreRenderAudio";
inline constexpr const char* Diagnostics = "Diag###SkoreDiagnostics";
inline constexpr const char* Causality = "Cause###SkoreCausality";
inline constexpr const char* Replay = "Replay";
inline constexpr const char* Status = "Status";
} // namespace ImGuiEditorPanel

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

ImGuiEditorLayoutEnvelope ResolveImGuiEditorLayoutEnvelope( int contentWidth, int contentHeight ) noexcept;
uint64_t FingerprintImGuiEditorDefaultTopology() noexcept;
} // namespace SkullbonezCore::Runtime::DevelopmentTools
