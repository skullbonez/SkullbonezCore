/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
Purpose:
  Declares the development-only Dear ImGui editor context and frame lifecycle.

Summary:
  ImGuiEditorOwner owns the CPU-side ImGui context, style, fonts, persisted
  layout identity, deterministic dock shell, viewport presentation geometry,
  visibility, and balanced frame begin/end calls. Runtime supplies immutable
  window, shared domain facts, and a frame-local replay publication and receives
  typed command/input values; mutable scene, replay, rendering, physics,
  and editor owners never enter this presentation owner.

Mental model:
  The editor is a presentation endpoint. It consumes immutable frame facts,
  retains only UI-local state, and publishes complete input/command/status
  values that runtime owners can apply without reaching back into ImGui.

Glossary:
  Editor frame input: Value-only display facts borrowed for one synchronous
    ImGui frame.
  Editor commands: Value-only requests emitted by ImGui presentation for the
    runtime composition root to arbitrate later.
  Layout version: Integer embedded in the persisted ini filename so an
    incompatible panel topology starts from a clean deterministic namespace.
  Surface selection: Process-lifetime choice of active Legacy or ImGui
    implementation kept outside scene and replay serialization.
  Viewport mapping: Last completed fitted image rectangle used by the next input
    frame to map Win32 client pixels back to the captured render extent.
  Property preview: One active ImGui scalar value kept inside presentation until
    release commits one typed command to the established runtime owner path.
  Causality publication: Immutable replay rows borrowed for one frame; the
    compact and detail panels may retain only local row indices and visibility.
  Automation command: Fixed presentation-only request used by validation to
    reproduce panel, layout, focus, and DPI states without pixel coordinates.

Invariants:
  - This source is compiled only with SKULLBONEZ_DEVELOPMENT_TOOLS.
  - Exactly one ImGui context is owned by this object; no subsystem receives it.
  - Platform multi-viewports remain disabled until a later evidence-backed task.
  - BeginFrame and EndFrame are balanced on the same thread.
  - ImGui allocations use the dedicated DearImGui development-tool owner.
  - ImGui is visible only while it is the selected development UI surface.
  - Benign preferences never enter authored scene or replay serialization.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - Agentic/Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md (E5-E16)
*/
#pragma once

#include "ImGuiEditorInputPolicy.h"
#include "ImGuiEditorLayoutPolicy.h"
#include "../App/RunLaunchOptions.h"
#include "../../Core/SbResult.h"
#include "../../Core/PlatformWin32.h"
#include "../../UI/OperatorEditorExchange.h"

#include <cstdint>

struct ImGuiContext;

namespace SkullbonezCore::Rendering
{
class Dx12ImGuiRendererOwner;
}

namespace SkullbonezCore::Runtime::ReplayOverlay
{
struct ReplayOverlayStateView;
}

namespace SkullbonezCore::Runtime
{
struct UiInputCaptureIntent;
}

namespace SkullbonezCore::Runtime::DevelopmentTools
{
enum class ImGuiEditorFontSource : uint8_t
{
    None = 0,
    Asset,
    EmbeddedVectorFallback
};

enum class ImGuiEditorAutomationCommandType : uint8_t
{
    SetPanelVisible,
    ResetLayout,
    FocusPanel,
    SetDpiScale
};

struct ImGuiEditorAutomationCommand
{
    ImGuiEditorAutomationCommandType type = ImGuiEditorAutomationCommandType::ResetLayout;
    ImGuiEditorPanelId panel = ImGuiEditorPanelId::Count;
    bool visible = false;
    float dpiScale = 0.0f;
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
    bool requestSurfaceSwap = false;
    bool requestTracyStandardCapture = false;
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
    ImGuiGameViewportRect gameViewport;
    bool nativePointerStateTouched = false;
};

struct ImGuiEditorStatus
{
    bool initialized = false;
    bool visible = false;
    bool frameActive = false;
    bool dockingEnabled = false;
    bool platformViewportsEnabled = false;
    DevelopmentUiMode selectedSurface = DevelopmentUiMode::Legacy;
    int layoutVersion = 0;
    uint64_t completedFrames = 0u;
    uint64_t sharedViewFingerprint = 0u;
    uint64_t layoutTopologyFingerprint = 0u;
    uint32_t layoutBuildCount = 0u;
    uint32_t layoutResetCount = 0u;
    uint32_t panelVisibilityMask = 0u;
    float appliedDpiScale = 0.0f;
    uint32_t automationFocusCount = 0u;
    ImGuiEditorPanelId lastFocusedPanel = ImGuiEditorPanelId::Count;
    bool preferencesLoaded = false;
    bool preferencesRecovered = false;
    bool preferencesSaveSucceeded = false;
    bool rendererBound = false;
    uint32_t rendererDescriptorUsed = 0u;
    uint32_t rendererDescriptorCapacity = 0u;
    uint32_t rendererDescriptorHighWater = 0u;
    uint64_t rendererRecordedFrames = 0u;
    uint64_t rendererIndexedDraws = 0u;
    uint64_t gameViewportCaptures = 0u;
    uint32_t gameViewportRecreations = 0u;
    int gameViewportWidth = 0;
    int gameViewportHeight = 0;
    bool gameViewportAvailable = false;
    uint64_t platformMessages = 0u;
    uint64_t suppressedMouseMessages = 0u;
    uint64_t suppressedKeyboardMessages = 0u;
    uint64_t suppressedTextMessages = 0u;
    uint64_t focusMessages = 0u;
    uint64_t dpiMessages = 0u;
    uint64_t imeMessages = 0u;
    ImGuiEditorFontSource fontSource = ImGuiEditorFontSource::None;
};

struct ImGuiEditorPropertyEditState
{
    UI::OperatorEditorPropertyCommandType type = UI::OperatorEditorPropertyCommandType::SetTimeScale;
    float floatValue = 0.0f;
    int integerValue = 0;
    bool active = false;
};

struct ImGuiEditorParameterizedEditState
{
    int action = -1;
    int parameter = -1;
    int setIndex = -1;
    int bandIndex = -1;
    float value = 0.0f;
    bool active = false;
};


// Concept: one editor frame retains only its first command-submission failure.
// Invariant: Take releases the owner-held lease while the returned copy remains
// valid for Runtime consumption after the frame owner resets.
class ImGuiEditorFrameStatusLease
{
  public:
    bool Ok() const noexcept
    {
        return m_status.Ok();
    }

    void Record( const SkullbonezCore::Core::SbResult& status ) noexcept
    {

        if ( m_status.Ok() && !status.Ok() )
        {
            m_status = status;
        }
    }

    SkullbonezCore::Core::SbResult Take() noexcept
    {
        SkullbonezCore::Core::SbResult result = m_status;
        m_status = SkullbonezCore::Core::SbResult::Success();
        return result;
    }

  private:
    SkullbonezCore::Core::SbResult m_status = SkullbonezCore::Core::SbResult::Success();
};


class ImGuiEditorOwner
{
  public:
    static constexpr int LAYOUT_VERSION = IMGUI_EDITOR_LAYOUT_VERSION;

    explicit ImGuiEditorOwner( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics ) noexcept
        : m_resultDiagnostics( resultDiagnostics )
    {
    }
    ~ImGuiEditorOwner();

    ImGuiEditorOwner( const ImGuiEditorOwner& ) = delete;
    ImGuiEditorOwner& operator=( const ImGuiEditorOwner& ) = delete;

    SkullbonezCore::Core::SbResult Start( HWND window, Rendering::Dx12ImGuiRendererOwner* renderer );
    void Shutdown() noexcept;
    void SetVisible( bool visible ) noexcept;
    bool IsVisible() const noexcept;
    void InitializeSurfaceSelection( DevelopmentUiMode initialSurface ) noexcept;
    void SelectSurface( DevelopmentUiMode surface ) noexcept;
    DevelopmentUiMode SelectedSurface() const noexcept;
    bool HasActivatedSurfaceSelection() const noexcept;

    // Applies one bounded presentation-only validation command. The caller
    // remains responsible for surface selection and native-window authority.
    SkullbonezCore::Core::SbResult ApplyAutomationCommand( const ImGuiEditorAutomationCommand& command ) noexcept;
    UI::OperatorEditorCommandQueues ConsumeOperatorEditorCommands() noexcept;

    // Reports the runtime-owned result of a cold Tracy client start without
    // lending profiler lifetime authority to this presentation owner.
    void ReportTracyClientStartResult( bool started ) noexcept;
    SkullbonezCore::Core::SbResult CaptureGameViewport();

    ImGuiEditorNativeMessageRoute HandleNativeMessage( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) noexcept;
    ImGuiEditorInputCapture CopyInputCapture() const noexcept;

    // Publishes the completed editor frame as the generic value consumed by
    // InputRouter; the input owner remains authoritative for arbitration.
    UiInputCaptureIntent ConsumeInputCaptureIntent() noexcept;
    void SetGameViewportInputState( bool hovered, bool focused ) noexcept;

    bool BeginFrame( const ImGuiEditorFrameInput& input );
    void BuildEditorShell( const UI::OperatorEditorFrameView& view, const ReplayOverlay::ReplayOverlayStateView& replay );
    ImGuiEditorFrameResult EndFrame();

    // Records draw data published by EndFrame. The caller must invoke this
    // synchronously from the current frame's graph callback before Present.
    SkullbonezCore::Core::SbResult RenderPreparedDrawData();
    ImGuiEditorStatus CopyStatus() const noexcept;

  private:
    ImGuiEditorInputFrameState ConsumeInputFrameState() noexcept;
    void ApplyDpiStyle( float dpiScale );
    void BuildDefaultDockLayout( uint32_t rootDockId, float width, float height, bool requestedReset );
    void ResetDefaultPanelVisibility() noexcept;
    uint32_t CopyPanelVisibilityMask() const noexcept;
    bool SetPanelVisibility( ImGuiEditorPanelId panel, bool visible ) noexcept;
    void ApplyPanelVisibilityMask( uint32_t mask ) noexcept;
    void LoadPreferences() noexcept;
    void SavePreferences() noexcept;

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    ImGuiContext* m_context = nullptr;
    Rendering::Dx12ImGuiRendererOwner* m_renderer = nullptr;
    HWND m_window = nullptr;
    bool m_visible = false;
    bool m_surfaceSelectionInitialized = false;
    bool m_surfaceSelectionActivated = false;
    DevelopmentUiMode m_selectedSurface = DevelopmentUiMode::Legacy;
    bool m_frameActive = false;
    bool m_platformBackendInitialized = false;
    bool m_gameViewportHovered = false;
    bool m_gameViewportFocused = false;
    ImGuiGameViewportRect m_gameViewportRect;
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
    float m_automationDpiScale = 0.0f;
    ImGuiEditorPanelId m_pendingFocusPanel = ImGuiEditorPanelId::Count;
    ImGuiEditorPanelId m_lastFocusedPanel = ImGuiEditorPanelId::Count;
    uint32_t m_automationFocusCount = 0u;
    bool m_preferencesLoaded = false;
    bool m_preferencesRecovered = false;
    bool m_preferencesSaveSucceeded = false;
    bool m_showSceneAndModes = true;
    bool m_showHierarchy = true;
    bool m_showAssetsCreate = true;
    bool m_showGameViewport = true;
    bool m_showInspector = true;
    bool m_showWorldSimulation = true;
    bool m_showRendering = true;
    bool m_showDiagnostics = true;
    bool m_showCausality = true;

    // Invariant: detail visibility and its row index are presentation-only;
    // neither value selects a replay row or enters authored serialization.
    bool m_showCausalityDetail = false;
    bool m_showReplay = true;
    bool m_showStatus = true;
    char m_tracyLaunchFeedback[160] = "Viewer not launched";
    char m_newSceneName[64] = "NewScene";
    char m_sceneFilter[64] = {};
    char m_hierarchyFilter[64] = {};
    char m_assetFilter[64] = {};
    ImGuiEditorPropertyEditState m_propertyEdit;

    // Invariant: each authoring domain owns one active scalar identity so a
    // pointer release cannot commit a preview that originated in another rail.
    ImGuiEditorParameterizedEditState m_renderingEdit;
    ImGuiEditorParameterizedEditState m_diagnosticsEdit;
    int m_causalityDetailSelectedRow = -1;
    bool m_focusSceneCreate = false;
    bool m_focusSceneFilter = false;
    ImGuiEditorCommands m_frameCommands;
    UI::OperatorEditorCommandQueues m_pendingOperatorEditorCommands;
    ImGuiEditorFrameStatusLease m_frameCommandStatus;
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
