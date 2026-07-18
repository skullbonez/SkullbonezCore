/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
Purpose:
  Declares the development-only Dear ImGui editor context and frame lifecycle.

Summary:
  ImGuiEditorOwner owns the CPU-side ImGui context, style, fonts, persisted
  layout identity, deterministic dock shell, visibility, and balanced frame
  begin/end calls. Runtime supplies immutable window and shared domain-view
  facts and receives a typed command packet; scene, replay, rendering, physics,
  audio, and editor domain state never enter this owner.

Glossary:
  Editor frame input: Value-only display facts borrowed for one synchronous
    ImGui frame.
  Editor commands: Value-only requests emitted by ImGui presentation for the
    runtime composition root to arbitrate later.
  Layout version: Integer embedded in the persisted ini filename so an
    incompatible panel topology starts from a clean deterministic namespace.
  Surface preference: Process-lifetime visibility choice kept outside scene and
    replay serialization.

Invariants:
  - This source is compiled only with SKULLBONEZ_DEVELOPMENT_TOOLS.
  - Exactly one ImGui context is owned by this object; no subsystem receives it.
  - Platform multi-viewports remain disabled until a later evidence-backed task.
  - BeginFrame and EndFrame are balanced on the same thread.
  - ImGui allocations use the dedicated DearImGui development-tool owner.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h
  - SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.h
  - Agentic/Plans/TODO/imgui-tracy-editor-campaign.md (E5-E9)
*/
#pragma once

#include "ImGuiEditorInputPolicy.h"
#include "ImGuiEditorLayoutPolicy.h"
#include "../../Core/SbResult.h"
#include "../../Core/PlatformWin32.h"
#include "../../UI/OperatorEditorExchange.h"

#include <cstdint>

struct ImGuiContext;

namespace SkullbonezCore::Rendering
{
class Dx12ImGuiRendererOwner;
}

namespace SkullbonezCore::Runtime::DevelopmentTools
{
enum class ImGuiEditorFontSource : uint8_t
{
    None = 0,
    Asset,
    EmbeddedVectorFallback
};

struct ImGuiEditorFrameInput
{
    int displayWidth = 0;
    int displayHeight = 0;
    float dpiScale = 1.0f;
    float deltaSeconds = 1.0f / 60.0f;
    bool tracyInitialized = false;
    bool tracyViewerConnected = false;
    bool tracyHeavyMode = false;
};

struct ImGuiEditorCommands
{
    UI::OperatorEditorCommandQueues operatorEditor;
    bool requestHide = false;
    bool requestLegacyVisibility = false;
    bool requestedLegacyVisible = true;
};

struct ImGuiEditorFrameResult
{
    ImGuiEditorCommands commands;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
};

struct ImGuiEditorNativeMessageRoute
{
    ImGuiEditorMessageClass messageClass = ImGuiEditorMessageClass::Platform;
    ImGuiEditorMessageDecision decision;
    LRESULT backendResult = 0;
};

struct ImGuiEditorInputFrameState
{
    ImGuiEditorInputCapture capture;
    bool nativePointerStateTouched = false;
};

struct ImGuiEditorStatus
{
    bool initialized = false;
    bool visible = false;
    bool frameActive = false;
    bool dockingEnabled = false;
    bool platformViewportsEnabled = false;
    int layoutVersion = 0;
    uint64_t completedFrames = 0u;
    uint64_t sharedViewFingerprint = 0u;
    uint64_t layoutTopologyFingerprint = 0u;
    uint32_t layoutBuildCount = 0u;
    uint32_t layoutResetCount = 0u;
    bool rendererBound = false;
    uint32_t rendererDescriptorUsed = 0u;
    uint32_t rendererDescriptorCapacity = 0u;
    uint32_t rendererDescriptorHighWater = 0u;
    uint64_t rendererRecordedFrames = 0u;
    uint64_t rendererIndexedDraws = 0u;
    uint64_t platformMessages = 0u;
    uint64_t suppressedMouseMessages = 0u;
    uint64_t suppressedKeyboardMessages = 0u;
    uint64_t suppressedTextMessages = 0u;
    uint64_t focusMessages = 0u;
    uint64_t dpiMessages = 0u;
    uint64_t imeMessages = 0u;
    ImGuiEditorFontSource fontSource = ImGuiEditorFontSource::None;
};

class ImGuiEditorOwner
{
  public:
    static constexpr int LAYOUT_VERSION = IMGUI_EDITOR_LAYOUT_VERSION;

    ImGuiEditorOwner() noexcept = default;
    ~ImGuiEditorOwner();

    ImGuiEditorOwner( const ImGuiEditorOwner& ) = delete;
    ImGuiEditorOwner& operator=( const ImGuiEditorOwner& ) = delete;

    SkullbonezCore::Core::SbResult Start( HWND window, Rendering::Dx12ImGuiRendererOwner* renderer );
    void Shutdown() noexcept;
    void SetVisible( bool visible ) noexcept;
    bool IsVisible() const noexcept;
    void InitializeSurfacePreferences( bool legacyVisible, bool editorVisible ) noexcept;
    void SetLegacySurfaceVisible( bool visible ) noexcept;
    bool LegacySurfaceVisible() const noexcept;
    UI::OperatorEditorCommandQueues ConsumeOperatorEditorCommands() noexcept;

    ImGuiEditorNativeMessageRoute
    HandleNativeMessage( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) noexcept;
    ImGuiEditorInputCapture CopyInputCapture() const noexcept;
    ImGuiEditorInputFrameState ConsumeInputFrameState() noexcept;
    void SetGameViewportInputState( bool hovered, bool focused ) noexcept;

    bool BeginFrame( const ImGuiEditorFrameInput& input );
    void BuildEditorShell( const UI::OperatorEditorFrameView& view );
    ImGuiEditorFrameResult EndFrame();
    ImGuiEditorStatus CopyStatus() const noexcept;

  private:
    void ApplyDpiStyle( float dpiScale );
    void BuildDefaultDockLayout( uint32_t rootDockId, float width, float height, bool requestedReset );
    void ResetDefaultPanelVisibility() noexcept;

    ImGuiContext* m_context = nullptr;
    Rendering::Dx12ImGuiRendererOwner* m_renderer = nullptr;
    HWND m_window = nullptr;
    bool m_visible = false;
    bool m_surfacePreferencesInitialized = false;
    bool m_legacySurfaceVisible = true;
    bool m_frameActive = false;
    bool m_platformBackendInitialized = false;
    bool m_gameViewportHovered = false;
    bool m_gameViewportFocused = false;
    bool m_nativePointerStateTouched = false;
    int m_lastPlatformMouseCursor = -2;
    float m_appliedDpiScale = 0.0f;
    ImGuiEditorFrameInput m_frameInput;
    uint64_t m_completedFrames = 0u;
    uint64_t m_sharedViewFingerprint = 0u;
    uint64_t m_layoutTopologyFingerprint = 0u;
    uint32_t m_layoutBuildCount = 0u;
    uint32_t m_layoutResetCount = 0u;
    bool m_layoutResetRequested = false;
    bool m_showSceneAndModes = true;
    bool m_showHierarchy = true;
    bool m_showAssetsCreate = true;
    bool m_showGameViewport = true;
    bool m_showInspector = true;
    bool m_showWorldSimulation = true;
    bool m_showRenderingAudio = true;
    bool m_showDiagnostics = true;
    bool m_showCausality = true;
    bool m_showReplay = true;
    bool m_showStatus = true;
    bool m_tracyViewerAvailable = false;
    char m_tracyViewerPath[512] = {};
    char m_tracyLaunchFeedback[160] = "Viewer not launched";
    char m_newSceneName[64] = "NewScene";
    char m_sceneFilter[64] = {};
    char m_hierarchyFilter[64] = {};
    char m_assetFilter[64] = {};
    bool m_focusSceneCreate = false;
    bool m_focusSceneFilter = false;
    ImGuiEditorCommands m_frameCommands;
    UI::OperatorEditorCommandQueues m_pendingOperatorEditorCommands;
    SkullbonezCore::Core::SbResult m_frameCommandStatus = SkullbonezCore::Core::SbResult::Success();
    uint64_t m_platformMessages = 0u;
    uint64_t m_suppressedMouseMessages = 0u;
    uint64_t m_suppressedKeyboardMessages = 0u;
    uint64_t m_suppressedTextMessages = 0u;
    uint64_t m_focusMessages = 0u;
    uint64_t m_dpiMessages = 0u;
    uint64_t m_imeMessages = 0u;
    ImGuiEditorFontSource m_fontSource = ImGuiEditorFontSource::None;
};
} // namespace SkullbonezCore::Runtime::DevelopmentTools
