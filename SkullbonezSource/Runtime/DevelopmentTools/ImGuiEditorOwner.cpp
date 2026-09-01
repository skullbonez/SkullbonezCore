/*
Purpose:
  Owns Dear ImGui startup, deterministic editor-shell frames, and orderly shutdown.

Invariants:
  - All vendor allocation and deallocation callbacks retain DearImGui owner
    attribution even when invoked from inside vendor code.
  - The context is current only for synchronous calls made by this owner.
  - A hidden or zero-sized editor performs no ImGui frame work.
  - Shutdown balances an active frame before destroying the context.
  - Preference migration resets stale layout/panel identity while preserving
    only bounded text filters.
*/
#include "ImGuiEditorOwner.h"
#include "ImGuiEditorControlPolicy.h"
#include "ImGuiEditorCausalityProjection.h"

#include "../../Core/Allocation/DevelopmentToolAllocation.h"
#include "../../Core/FatalError.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Rendering/DX12/Dx12ImGuiRendererOwner.h"
#include "../Interaction/OperatorEditorObjectCatalog.h"
#include "../Interaction/OperatorEditorExchange.h"
#include "../Render/UIRenderAuthoringCatalog.h"
#include "../../Physics/PhysicsDebugData.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>

// The pinned backend intentionally hides this declaration behind #if 0 to
// avoid forcing Windows types on every includer. This source already owns the
// Win32 Application Binary Interface (ABI) boundary, so repeat the
// vendor-prescribed declaration here.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM wParam, LPARAM lParam );

namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
constexpr const char* IMGUI_LAYOUT_PATH = "imgui_editor_layout_v2.ini";
constexpr const char* IMGUI_PREFERENCES_PATH = "imgui_editor_preferences_v1.cfg";
constexpr const char* OPTIONAL_EDITOR_FONT_PATH = "SkullbonezData/fonts/SkoreEditor-Regular.ttf";
constexpr float EDITOR_FONT_SIZE_PIXELS = 16.0f;
constexpr float MIN_DPI_SCALE = 0.75f;
constexpr float MAX_DPI_SCALE = 4.0f;
constexpr float MIN_DELTA_SECONDS = 1.0f / 1000.0f;
constexpr float MAX_DELTA_SECONDS = 0.25f;

void* AllocateImGuiMemory( size_t size, void* ) noexcept
{
    return CoreAllocation::AllocateDevelopmentToolMemory( CoreAllocation::DevelopmentToolAllocationOwner::DearImGui, size );
}

void FreeImGuiMemory( void* pointer, void* ) noexcept
{
    CoreAllocation::FreeDevelopmentToolMemory( CoreAllocation::DevelopmentToolAllocationOwner::DearImGui, pointer );
}

bool IsReadableFile( const char* path ) noexcept
{
    const DWORD attributes = GetFileAttributesA( path );
    return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0u;
}

bool CopyAbsoluteReadablePath( const char* path, char* output, size_t outputCapacity ) noexcept
{
    if ( !IsReadableFile( path ) )
    {
        return false;
    }

    const DWORD length = GetFullPathNameA( path, static_cast<DWORD>( outputCapacity ), output, nullptr );
    return length > 0u && length < outputCapacity;
}

bool ResolveRepositoryPath( const char* repositoryRelativePath, char* output, size_t outputCapacity ) noexcept
{
    if ( CopyAbsoluteReadablePath( repositoryRelativePath, output, outputCapacity ) )
    {
        return true;
    }

    char modulePath[512] = {};
    const DWORD moduleLength = GetModuleFileNameA( nullptr, modulePath, static_cast<DWORD>( sizeof( modulePath ) ) );

    if ( moduleLength == 0u || moduleLength >= sizeof( modulePath ) )
    {
        return false;
    }

    char* separator = std::strrchr( modulePath, '\\' );

    if ( !separator )
    {
        return false;
    }

    *separator = '\0'; // Remove the executable name.
    separator = std::strrchr( modulePath, '\\' );

    if ( !separator )
    {
        return false;
    }

    *separator = '\0'; // Profile/Debug/Automation output is one level below the repository root.

    char candidate[512] = {};
    const int written = snprintf( candidate, sizeof( candidate ), "%s\\%s", modulePath, repositoryRelativePath );
    return written > 0 && static_cast<size_t>( written ) < sizeof( candidate ) &&
           CopyAbsoluteReadablePath( candidate, output, outputCapacity );
}

enum class TracyViewerLaunchTarget : uint8_t
{
    Unavailable,
    Viewer,
    BuildAndOpenTool
};

TracyViewerLaunchTarget ResolveTracyViewerLaunchPath( char* output, size_t outputCapacity ) noexcept
{
    constexpr const char* candidates[] = {
        "TestOutput/validation/tracy_profiler/Release/tracy-profiler.exe",
        "ThirdPtySource/tracy/profiler/build/Release/tracy-profiler.exe",
        "ThirdPtySource/tracy/profiler/build/tracy-profiler.exe",
    };

    for ( const char* candidate : candidates )
    {
        if ( ResolveRepositoryPath( candidate, output, outputCapacity ) )
        {
            return TracyViewerLaunchTarget::Viewer;
        }
    }

    const DWORD length = SearchPathA( nullptr, "tracy-profiler.exe", nullptr, static_cast<DWORD>( outputCapacity ), output,
                                      nullptr );

    if ( length > 0u && length < outputCapacity )
    {
        return TracyViewerLaunchTarget::Viewer;
    }

    if ( ResolveRepositoryPath( "tools/launch_tracy_viewer.bat", output, outputCapacity ) )
    {
        return TracyViewerLaunchTarget::BuildAndOpenTool;
    }

    return TracyViewerLaunchTarget::Unavailable;
}

void DrawDisabledReason( const char* reason )
{
    if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
    {
        ImGui::SetTooltip( "%s", reason );
    }
}

void DrawDisabledWrapped( const char* text )
{
    ImGui::PushTextWrapPos( 0.0f );
    ImGui::TextDisabled( "%s", text );
    ImGui::PopTextWrapPos();
}

bool DrawEditableCheckbox( const char* label, bool sourceValue, bool& editedValue )
{
    editedValue = sourceValue;
    return ImGui::Checkbox( label, &editedValue );
}

const char* SceneDisplayName( const char* path ) noexcept
{
    if ( !path || path[0] == '\0' )
    {
        return "generated demo";
    }

    const char* slash = std::strrchr( path, '/' );
    const char* backslash = std::strrchr( path, '\\' );
    const char* separator = slash;

    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }

    return separator ? separator + 1 : path;
}

char LowerAscii( char value ) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>( value - 'A' + 'a' ) : value;
}

bool ContainsAsciiInsensitive( const char* text, const char* filter ) noexcept
{
    if ( !filter || filter[0] == '\0' )
    {
        return true;
    }

    if ( !text )
    {
        return false;
    }

    for ( const char* start = text; *start != '\0'; ++start )
    {
        const char* candidate = start;
        const char* query = filter;

        while ( *candidate != '\0' && *query != '\0' && LowerAscii( *candidate ) == LowerAscii( *query ) )
        {
            ++candidate;
            ++query;
        }

        if ( *query == '\0' )
        {
            return true;
        }
    }

    return false;
}

const char* AssetCategoryForObjectType( int objectType ) noexcept
{
    using namespace SkullbonezCore::UI::EditorTab;

    if ( objectType <= OBJECT_SPHERE )
    {
        return "Primitives";
    }

    if ( objectType <= OBJECT_ROOT_LARGE )
    {
        return "Hull Props";
    }

    if ( objectType <= OBJECT_TREE_PINE_SHEDDING )
    {
        return "Trees";
    }

    if ( objectType <= OBJECT_RAGDOLL_SLEEP )
    {
        return "Characters";
    }

    return "Registered Assets";
}

double BytesToMiB( uint64_t bytes ) noexcept
{
    return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
}

} // namespace

namespace SkullbonezCore::Runtime::DevelopmentTools
{
ImGuiEditorOwner::~ImGuiEditorOwner()
{
    Shutdown();
}

SkullbonezCore::Core::SbResult ImGuiEditorOwner::Start( HWND window, Rendering::Dx12ImGuiRendererOwner* renderer )
{
    const ImGuiEditorStartDisposition startDisposition = ResolveImGuiEditorStartDisposition( m_context != nullptr,
                                                                                             m_renderer != nullptr );

    if ( startDisposition == ImGuiEditorStartDisposition::ReturnReady )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( startDisposition == ImGuiEditorStartDisposition::RestartIncomplete )
    {
        // Recoverable renderer binding can fail after the platform context is
        // live. Retry owns a fresh complete epoch, never the partial context.
        Shutdown();
    }

    if ( !window )
    {
        return m_resultDiagnostics.Failure( "DevelopmentTools/ImGui", "Win32 ImGui startup requires the runtime HWND" );
    }

    if ( !renderer )
    {
        return m_resultDiagnostics.Failure( "DevelopmentTools/ImGui",
                                            "No DX12 ImGui renderer capability was published at startup" );
    }

    // Lifetime: allocator callbacks are process-global ImGui configuration.
    // They target stateless engine wrappers and therefore remain valid after
    // this context is destroyed.
    ImGui::SetAllocatorFunctions( AllocateImGuiMemory, FreeImGuiMemory );
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();

    if ( !m_context )
    {
        // Fatal invariant: a development editor context that cannot establish its sole
        // owner cannot safely continue into frame calls.
        SB_FATAL( "DevelopmentTools/ImGui", "Dear ImGui context creation failed." );
    }

    ImGui::SetCurrentContext( m_context );

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.IniFilename = IMGUI_LAYOUT_PATH;

    // Why: the repository intentionally carries no separately licensed editor font.
    // A future optional asset can occupy this stable path; the pinned embedded
    // vector font keeps startup deterministic on every current machine.
    ImFont* editorFont = nullptr;

    if ( IsReadableFile( OPTIONAL_EDITOR_FONT_PATH ) )
    {
        editorFont = io.Fonts->AddFontFromFileTTF( OPTIONAL_EDITOR_FONT_PATH, EDITOR_FONT_SIZE_PIXELS );
    }

    if ( editorFont )
    {
        m_fontSource = ImGuiEditorFontSource::Asset;
    }
    else
    {
        ImFontConfig fontConfig;
        fontConfig.SizePixels = EDITOR_FONT_SIZE_PIXELS;
        editorFont = io.Fonts->AddFontDefaultVector( &fontConfig );

        if ( !editorFont )
        {
            SB_FATAL( "DevelopmentTools/ImGui", "Dear ImGui embedded vector font creation failed." );
        }

        m_fontSource = ImGuiEditorFontSource::EmbeddedVectorFallback;
    }

    io.FontDefault = editorFont;

    if ( !ImGui_ImplWin32_Init( window ) )
    {
        return m_resultDiagnostics.Failure( "DevelopmentTools/ImGui", "Pinned Win32 backend initialization failed" );
    }

    m_platformBackendInitialized = true;
    m_window = window;

    m_layoutTopologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    LoadPreferences();
    ApplyDpiStyle( 1.0f );
    const SkullbonezCore::Core::SbResult rendererResult = renderer->BindContext( *m_context );

    if ( !rendererResult.Ok() )
    {
        return rendererResult;
    }

    m_renderer = renderer;
    char tracyLaunchPath[512] = {};

    const TracyViewerLaunchTarget tracyLaunchTarget = ResolveTracyViewerLaunchPath( tracyLaunchPath,
                                                                                    sizeof( tracyLaunchPath ) );

    snprintf( m_tracyLaunchFeedback, sizeof( m_tracyLaunchFeedback ), "%s",
              tracyLaunchTarget == TracyViewerLaunchTarget::Viewer
                  ? "Pinned Tracy viewer ready"
                  : ( tracyLaunchTarget == TracyViewerLaunchTarget::BuildAndOpenTool
                          ? "First click builds the pinned Tracy viewer, then opens it"
                          : "Tracy viewer launcher is unavailable" ) );

    printf( "[imgui] Context ready layout_version=%d font=%s docking=on platform_viewports=off win32=bound.\n",
            LAYOUT_VERSION, m_fontSource == ImGuiEditorFontSource::Asset ? "asset" : "embedded_vector_fallback" );

    return SkullbonezCore::Core::SbResult::Success();
}

void ImGuiEditorOwner::Shutdown() noexcept
{
    if ( !m_context )
    {
        // Commands and automation values may be accepted before startup. A
        // shutdown boundary clears that epoch even when no vendor context was
        // ever created.
        ResetLifecycleStateAfterShutdown();
        return;
    }

    ImGui::SetCurrentContext( m_context );
    const uint64_t completedFrames = m_completedFrames;

    if ( m_frameActive )
    {
        // Hazard: early runtime failure may leave the CPU-only frame open.
        // End it before context destruction so ImGui can validate its stacks.
        ImGui::EndFrame();
        m_frameActive = false;
    }

    // Lifetime: layout and benign preferences are cold development-tool IO.
    // Flush them while their context and presentation values still exist.
    ImGuiIO& io = ImGui::GetIO();

    if ( io.IniFilename )
    {
        ImGui::SaveIniSettingsToDisk( io.IniFilename );
    }

    SavePreferences();

    if ( m_renderer )
    {
        // Lifetime: Run explicitly calls Shutdown only after its renderer
        // resource-release drain. The context remains current while the vendor
        // frees its two-frame buffers, font texture, and descriptor row.
        m_renderer->Shutdown( *m_context );
        m_renderer = nullptr;
    }

    if ( m_platformBackendInitialized )
    {
        ImGui_ImplWin32_Shutdown();
        m_platformBackendInitialized = false;
    }

    ImGui::DestroyContext( m_context );
    m_context = nullptr;
    m_window = nullptr;
    m_visible = false;
    m_gameViewportHovered = false;
    m_gameViewportFocused = false;
    m_gameViewportRect = {};

    m_propertyEdit = {};

    m_renderingEdit = {};

    m_diagnosticsEdit = {};

    m_nativePointerStateTouched = false;
    m_lastPlatformMouseCursor = -2;
    m_appliedDpiScale = 0.0f;
    m_automationDpiScale = 0.0f;
    m_pendingFocusPanel = ImGuiEditorPanelId::Count;
    m_frameInput = {};

    m_fontSource = ImGuiEditorFontSource::None;
    printf( "[imgui] Context shutdown completed_frames=%llu messages=%llu suppressed_mouse=%llu "
            "suppressed_keyboard=%llu suppressed_text=%llu focus=%llu dpi=%llu ime=%llu.\n",
            static_cast<unsigned long long>( completedFrames ), static_cast<unsigned long long>( m_platformMessages ),
            static_cast<unsigned long long>( m_suppressedMouseMessages ),
            static_cast<unsigned long long>( m_suppressedKeyboardMessages ),
            static_cast<unsigned long long>( m_suppressedTextMessages ), static_cast<unsigned long long>( m_focusMessages ),
            static_cast<unsigned long long>( m_dpiMessages ), static_cast<unsigned long long>( m_imeMessages ) );
    ResetLifecycleStateAfterShutdown();
}

void ImGuiEditorOwner::ResetLifecycleStateAfterShutdown() noexcept
{
    m_renderer = nullptr;
    m_window = nullptr;
    m_visible = false;
    m_surfaceSelectionInitialized = false;
    m_surfaceSelectionActivated = false;
    m_selectedSurface = DevelopmentUiMode::GameUI;
    m_frameActive = false;
    m_platformBackendInitialized = false;
    m_gameViewportHovered = false;
    m_gameViewportFocused = false;
    m_gameViewportRect = {};
    m_nativePointerStateTouched = false;
    m_lastPlatformMouseCursor = -2;
    m_appliedDpiScale = 0.0f;
    m_frameInput = {};
    m_completedFrames = 0u;
    m_sharedViewFingerprint = 0u;
    m_layoutTopologyFingerprint = 0u;
    m_layoutBuildCount = 0u;
    m_layoutResetCount = 0u;
    m_layoutResetRequested = false;
    m_automationDpiScale = 0.0f;
    m_pendingFocusPanel = ImGuiEditorPanelId::Count;
    m_lastFocusedPanel = ImGuiEditorPanelId::Count;
    m_automationFocusCount = 0u;
    m_preferencesLoaded = false;
    m_preferencesRecovered = false;
    m_preferencesSaveSucceeded = false;
    ResetDefaultPanelVisibility();
    snprintf( m_tracyLaunchFeedback, sizeof( m_tracyLaunchFeedback ), "%s", "Viewer not launched" );
    strcpy_s( m_newSceneName, "NewScene" );
    m_sceneFilter[0] = '\0';
    m_hierarchyFilter[0] = '\0';
    m_assetFilter[0] = '\0';
    m_propertyEdit = {};
    m_renderingEdit = {};
    m_diagnosticsEdit = {};
    m_causalityDetailSelectedRow = -1;
    m_focusSceneCreate = false;
    m_focusSceneFilter = false;
    ResetImGuiEditorPendingEpoch( m_frameCommands, m_pendingOperatorEditorCommands, m_frameCommandStatus );
    m_platformMessages = 0u;
    m_suppressedMouseMessages = 0u;
    m_suppressedKeyboardMessages = 0u;
    m_suppressedTextMessages = 0u;
    m_focusMessages = 0u;
    m_dpiMessages = 0u;
    m_imeMessages = 0u;
    m_fontSource = ImGuiEditorFontSource::None;
}

void ImGuiEditorOwner::SetVisible( bool visible ) noexcept
{
    if ( m_visible && !visible && m_context )
    {
        ImGui::SetCurrentContext( m_context );
        const ImGuiEditorInputCapture capture = CopyInputCapture();

        // Hazard: the vendor backend and engine both use HWND-scoped native
        // capture. Release only when editor policy currently owns mouse intent;
        // a game-viewport/camera capture must remain untouched.
        if ( capture.mouse && GetCapture() == m_window )
        {
            ReleaseCapture();
        }

        ImGuiIO& io = ImGui::GetIO();
        io.ClearInputKeys();

        // A hidden presentation cannot own a pending scalar preview. Dropping
        // it is a cancel, not an owner mutation.
        m_propertyEdit = {};

        m_renderingEdit = {};

        m_diagnosticsEdit = {};

        io.ClearInputMouse();
    }

    m_visible = visible;

    if ( !visible )
    {
        m_gameViewportHovered = false;
        m_gameViewportFocused = false;
        m_gameViewportRect = {};
    }
}

bool ImGuiEditorOwner::IsVisible() const noexcept
{
    return m_visible;
}

void ImGuiEditorOwner::InitializeSurfaceSelection( DevelopmentUiMode initialSurface ) noexcept
{
    if ( m_surfaceSelectionInitialized )
    {
        return;
    }

    m_surfaceSelectionInitialized = true;
    m_selectedSurface = initialSurface;
    SetVisible( initialSurface == DevelopmentUiMode::ImGui );
}

void ImGuiEditorOwner::SelectSurface( DevelopmentUiMode surface ) noexcept
{
    m_surfaceSelectionInitialized = true;
    m_surfaceSelectionActivated = true;
    m_selectedSurface = surface;
    SetVisible( surface == DevelopmentUiMode::ImGui );
}

DevelopmentUiMode ImGuiEditorOwner::SelectedSurface() const noexcept
{
    return m_selectedSurface;
}

bool ImGuiEditorOwner::HasActivatedSurfaceSelection() const noexcept
{
    return m_surfaceSelectionActivated;
}

SkullbonezCore::Core::SbResult
ImGuiEditorOwner::ApplyAutomationCommand( const ImGuiEditorAutomationCommand& command ) noexcept
{
    switch ( command.type )
    {
    case ImGuiEditorAutomationCommandType::SetPanelVisible:

        if ( static_cast<uint32_t>( command.panel ) >= static_cast<uint32_t>( ImGuiEditorPanelId::Count ) )
        {
            return m_resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation", "Unknown panel identity" );
        }

        ApplyPanelVisibilityMask(
            ResolveImGuiEditorPanelVisibilityCommand( CopyPanelVisibilityMask(), command.panel, command.visible ) );
        return SkullbonezCore::Core::SbResult::Success();
    case ImGuiEditorAutomationCommandType::ResetLayout:
        m_layoutResetRequested = true;
        ApplyPanelVisibilityMask( ResetImGuiEditorPanelMask() );
        return SkullbonezCore::Core::SbResult::Success();
    case ImGuiEditorAutomationCommandType::FocusPanel:

        if ( static_cast<uint32_t>( command.panel ) >= static_cast<uint32_t>( ImGuiEditorPanelId::Count ) )
        {
            return m_resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation", "Cannot focus an unknown panel" );
        }

        ApplyPanelVisibilityMask(
            ResolveImGuiEditorPanelVisibilityCommand( CopyPanelVisibilityMask(), command.panel, true ) );
        m_pendingFocusPanel = command.panel;
        return SkullbonezCore::Core::SbResult::Success();
    case ImGuiEditorAutomationCommandType::SetDpiScale:

        if ( !std::isfinite( command.dpiScale ) || command.dpiScale < MIN_DPI_SCALE || command.dpiScale > MAX_DPI_SCALE )
        {
            return m_resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation",
                                                "DPI scale must be finite and within 0.75..4.0" );
        }

        m_automationDpiScale = command.dpiScale;
        return SkullbonezCore::Core::SbResult::Success();
    }

    return m_resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation", "Unknown automation command" );
}

UI::OperatorEditorCommandQueues ImGuiEditorOwner::ConsumeOperatorEditorCommands() noexcept
{
    const UI::OperatorEditorCommandQueues commands = m_pendingOperatorEditorCommands;
    m_pendingOperatorEditorCommands = {};

    return commands;
}

void ImGuiEditorOwner::ReportTracyClientStartResult( bool started ) noexcept
{
    snprintf( m_tracyLaunchFeedback, sizeof( m_tracyLaunchFeedback ), "%s",
              started ? "Standard capture active; viewer connection is automatic"
                      : "Standard capture failed to start; inspect the engine console" );
}

SkullbonezCore::Core::SbResult ImGuiEditorOwner::CaptureGameViewport()
{
    if ( !ShouldCaptureImGuiGameViewport( m_visible, m_showGameViewport ) )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( !m_renderer )
    {
        return m_resultDiagnostics.Failure( "DevelopmentTools/ImGui", "Visible game viewport has no DX12 renderer owner" );
    }

    return m_renderer->CaptureGameViewport();
}

ImGuiEditorNativeMessageRoute ImGuiEditorOwner::HandleNativeMessage( HWND window, UINT message, WPARAM wParam,
                                                                     LPARAM lParam ) noexcept
{
    ImGuiEditorNativeMessageRoute route;
    route.messageClass = ClassifyImGuiEditorNativeMessage( message, wParam );

    if ( !m_context || !m_platformBackendInitialized || window != m_window )
    {
        return route;
    }

    if ( !m_visible && route.messageClass != ImGuiEditorMessageClass::Platform )
    {
        // Invariant: GameUI mode retains its native input/cursor behavior.
        // Focus, DPI, display, and device messages continue keeping the dormant
        // backend synchronized for a later atomic switch to ImGui.
        return route;
    }

    // Lifetime: WndProc and frame work run on the same application thread. Set
    // the sole owned context explicitly before entering the vendor backend so
    // no process-global current-context assumption leaks across owners.
    ImGui::SetCurrentContext( m_context );
    route.backendResult = ImGui_ImplWin32_WndProcHandler( window, message, wParam, lParam );

    if ( route.messageClass == ImGuiEditorMessageClass::Mouse )
    {
        // Hazard: imgui_impl_win32 may call SetCapture, ReleaseCapture, or
        // SetCursor even when the live game viewport ultimately keeps the
        // event. The engine input owner must republish its native intent.
        m_nativePointerStateTouched = true;
    }

    route.decision = DecideImGuiEditorMessageRoute( route.messageClass, CopyInputCapture() );
    ++m_platformMessages;

    if ( message == WM_SETFOCUS || message == WM_KILLFOCUS )
    {
        ++m_focusMessages;
    }

    if ( message == WM_DPICHANGED )
    {
        ++m_dpiMessages;
    }

    if ( message == WM_IME_STARTCOMPOSITION || message == WM_IME_COMPOSITION || message == WM_IME_ENDCOMPOSITION ||
         message == WM_IME_CHAR )
    {
        ++m_imeMessages;
    }

    if ( route.decision.editorConsumes )
    {
        switch ( route.messageClass )
        {
        case ImGuiEditorMessageClass::Mouse:
            ++m_suppressedMouseMessages;
            break;
        case ImGuiEditorMessageClass::Keyboard:
            ++m_suppressedKeyboardMessages;
            break;
        case ImGuiEditorMessageClass::Text:
            ++m_suppressedTextMessages;
            break;
        case ImGuiEditorMessageClass::Platform:
        default:
            break;
        }
    }

    return route;
}

ImGuiEditorInputCapture ImGuiEditorOwner::CopyInputCapture() const noexcept
{
    if ( !m_context )
    {
        return {};
    }

    ImGui::SetCurrentContext( m_context );
    const ImGuiIO& io = ImGui::GetIO();
    return EvaluateImGuiEditorInputCapture( ImGuiEditorInputIntent { m_visible, io.WantCaptureMouse, io.WantCaptureKeyboard,
                                                                     io.WantTextInput, m_gameViewportHovered,
                                                                     m_gameViewportFocused } );
}

ImGuiEditorInputFrameState ImGuiEditorOwner::ConsumeInputFrameState() noexcept
{
    ImGuiEditorInputFrameState state;
    state.capture = CopyInputCapture();
    state.gameViewport = m_gameViewportRect;
    state.nativePointerStateTouched = m_nativePointerStateTouched;
    m_nativePointerStateTouched = false;
    return state;
}

UI::InputCaptureIntent ImGuiEditorOwner::ConsumeInputCaptureIntent() noexcept
{
    const ImGuiEditorInputFrameState input = ConsumeInputFrameState();

    // Concept: the editor owns the fitted viewport geometry and completed-frame
    // capture request. Publish them together so Run does not reinterpret either.
    UI::InputCaptureIntent intent { input.capture.mouse, input.capture.keyboard, input.capture.text,
                                    input.nativePointerStateTouched };

    intent.gameViewportMappingActive = input.gameViewport.valid;
    intent.gameViewportMinX = input.gameViewport.imageMinX;
    intent.gameViewportMinY = input.gameViewport.imageMinY;
    intent.gameViewportWidth = input.gameViewport.imageWidth;
    intent.gameViewportHeight = input.gameViewport.imageHeight;
    intent.gameViewportDpiScale = input.gameViewport.dpiScale;
    intent.gameViewportSourceWidth = input.gameViewport.sourceWidth;
    intent.gameViewportSourceHeight = input.gameViewport.sourceHeight;
    return intent;
}

void ImGuiEditorOwner::SetGameViewportInputState( bool hovered, bool focused ) noexcept
{
    // Concept: the fitted game-viewport image publishes hover/focus through the
    // editor input-policy seam; gameplay callbacks never learn about ImGui windows.
    m_gameViewportHovered = m_visible && hovered;
    m_gameViewportFocused = m_visible && focused;
}

bool ImGuiEditorOwner::BeginFrame( const ImGuiEditorFrameInput& input )
{
    if ( !m_context || !m_visible || input.displayWidth <= 0 || input.displayHeight <= 0 )
    {
        return false;
    }

    if ( m_frameActive )
    {
        // Fatal invariant: nested frames corrupt ImGui retained stacks and settings.
        SB_FATAL( "DevelopmentTools/ImGui", "BeginFrame called while an editor frame is already active." );
    }

    ImGui::SetCurrentContext( m_context );
    const float requestedDpiScale = m_automationDpiScale > 0.0f ? m_automationDpiScale : input.dpiScale;
    const float dpiScale = std::clamp( requestedDpiScale, MIN_DPI_SCALE, MAX_DPI_SCALE );

    if ( std::fabs( dpiScale - m_appliedDpiScale ) > 0.01f )
    {
        ApplyDpiStyle( dpiScale );
    }

    ImGuiIO& io = ImGui::GetIO();

    if ( !m_renderer )
    {
        SB_FATAL( "DevelopmentTools/ImGui", "BeginFrame has no bound DX12 renderer." );
    }

    m_renderer->BeginFrame( *m_context );
    ImGui_ImplWin32_NewFrame();
    const int platformMouseCursor = static_cast<int>( ImGui::GetMouseCursor() );

    if ( platformMouseCursor != m_lastPlatformMouseCursor )
    {
        // The backend may just have changed the shared Win32 cursor shape.
        // When viewport/game input owns the pointer, the next input edge must
        // republish its established visibility/capture policy.
        m_nativePointerStateTouched = true;
        m_lastPlatformMouseCursor = platformMouseCursor;
    }

    // Why: the platform backend supplies native input/cursor facts, while the
    // engine frame remains authoritative for dimensions and timing.
    io.DisplaySize = ImVec2( static_cast<float>( input.displayWidth ), static_cast<float>( input.displayHeight ) );
    io.DisplayFramebufferScale = ImVec2( 1.0f, 1.0f );
    io.DeltaTime = std::clamp( input.deltaSeconds, MIN_DELTA_SECONDS, MAX_DELTA_SECONDS );
    ImGui::NewFrame();
    m_frameInput = input;
    m_frameActive = true;
    return true;
}

void ImGuiEditorOwner::ResetDefaultPanelVisibility() noexcept
{
    m_showSceneAndModes = true;
    m_showHierarchy = true;
    m_showAssetsCreate = true;
    m_showGameViewport = true;
    m_showInspector = true;
    m_showWorldSimulation = true;
    m_showRendering = true;
    m_showDiagnostics = true;
    m_showCausality = true;
    m_showCausalityDetail = false;
    m_showReplay = true;
    m_showStatus = true;
}

uint32_t ImGuiEditorOwner::CopyPanelVisibilityMask() const noexcept
{
    uint32_t mask = 0u;
    const auto add = [&]( ImGuiEditorPanelId panel, bool visible )
    {
        if ( visible )
        {
            mask |= ImGuiEditorPanelBit( panel );
        }
    };

    add( ImGuiEditorPanelId::SceneAndModes, m_showSceneAndModes );
    add( ImGuiEditorPanelId::Hierarchy, m_showHierarchy );
    add( ImGuiEditorPanelId::AssetsCreate, m_showAssetsCreate );
    add( ImGuiEditorPanelId::GameViewport, m_showGameViewport );
    add( ImGuiEditorPanelId::Inspector, m_showInspector );
    add( ImGuiEditorPanelId::WorldSimulation, m_showWorldSimulation );
    add( ImGuiEditorPanelId::Rendering, m_showRendering );
    add( ImGuiEditorPanelId::Diagnostics, m_showDiagnostics );
    add( ImGuiEditorPanelId::Causality, m_showCausality );
    add( ImGuiEditorPanelId::CausalityDetail, m_showCausalityDetail );
    add( ImGuiEditorPanelId::Replay, m_showReplay );
    add( ImGuiEditorPanelId::Status, m_showStatus );
    return mask;
}

bool ImGuiEditorOwner::SetPanelVisibility( ImGuiEditorPanelId panel, bool visible ) noexcept
{
    switch ( panel )
    {
    case ImGuiEditorPanelId::SceneAndModes:
        m_showSceneAndModes = visible;
        break;
    case ImGuiEditorPanelId::Hierarchy:
        m_showHierarchy = visible;
        break;
    case ImGuiEditorPanelId::AssetsCreate:
        m_showAssetsCreate = visible;
        break;
    case ImGuiEditorPanelId::GameViewport:
        m_showGameViewport = visible;
        break;
    case ImGuiEditorPanelId::Inspector:
        m_showInspector = visible;
        break;
    case ImGuiEditorPanelId::WorldSimulation:
        m_showWorldSimulation = visible;
        break;
    case ImGuiEditorPanelId::Rendering:
        m_showRendering = visible;
        break;
    case ImGuiEditorPanelId::Diagnostics:
        m_showDiagnostics = visible;
        break;
    case ImGuiEditorPanelId::Causality:
        m_showCausality = visible;
        break;
    case ImGuiEditorPanelId::CausalityDetail:
        m_showCausalityDetail = visible;
        break;
    case ImGuiEditorPanelId::Replay:
        m_showReplay = visible;
        break;
    case ImGuiEditorPanelId::Status:
        m_showStatus = visible;
        break;
    case ImGuiEditorPanelId::Count:
    default:
        return false;
    }

    m_pendingFocusPanel = ResolveImGuiPendingFocusAfterVisibility( m_pendingFocusPanel, panel, visible );
    return true;
}

void ImGuiEditorOwner::ApplyPanelVisibilityMask( uint32_t mask ) noexcept
{
    for ( uint32_t index = 0u; index < static_cast<uint32_t>( ImGuiEditorPanelId::Count ); ++index )
    {
        const ImGuiEditorPanelId panel = static_cast<ImGuiEditorPanelId>( index );
        SetPanelVisibility( panel, ( mask & ImGuiEditorPanelBit( panel ) ) != 0u );
    }
}

void ImGuiEditorOwner::LoadPreferences() noexcept
{
    FILE* file = nullptr;

    if ( fopen_s( &file, IMGUI_PREFERENCES_PATH, "rb" ) != 0 || !file )
    {
        ApplyPanelVisibilityMask( IMGUI_EDITOR_DEFAULT_PANEL_MASK );
        return;
    }

    char text[IMGUI_EDITOR_PREFERENCES_TEXT_CAPACITY] = {};

    const std::size_t bytes = fread( text, 1u, sizeof( text ) - 1u, file );
    const bool complete = feof( file ) != 0;
    fclose( file );

    if ( !complete )
    {
        ApplyPanelVisibilityMask( IMGUI_EDITOR_DEFAULT_PANEL_MASK );
        m_layoutResetRequested = true;
        m_preferencesRecovered = true;
        return;
    }

    const ImGuiEditorPreferenceParseResult parsed = ParseImGuiEditorPreferences( text, bytes );
    m_preferencesLoaded = parsed.valid;
    m_preferencesRecovered = parsed.recoveredDefaults;
    m_layoutResetRequested = m_layoutResetRequested || parsed.layoutResetRequired;
    ApplyPanelVisibilityMask( parsed.preferences.panelVisibilityMask );
    strcpy_s( m_sceneFilter, parsed.preferences.sceneFilter );
    strcpy_s( m_hierarchyFilter, parsed.preferences.hierarchyFilter );
    strcpy_s( m_assetFilter, parsed.preferences.assetFilter );
    printf( "[imgui-preferences] mode=%s layout_reset=%d panels=%u.\n",
            parsed.valid ? ( parsed.recoveredDefaults ? "migrated" : "loaded" ) : "recovered",
            parsed.layoutResetRequired ? 1 : 0, parsed.preferences.panelVisibilityMask );
}

void ImGuiEditorOwner::SavePreferences() noexcept
{
    ImGuiEditorPreferences preferences;
    preferences.topologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    preferences.panelVisibilityMask = CopyPanelVisibilityMask();
    strcpy_s( preferences.sceneFilter, m_sceneFilter );
    strcpy_s( preferences.hierarchyFilter, m_hierarchyFilter );
    strcpy_s( preferences.assetFilter, m_assetFilter );
    char text[IMGUI_EDITOR_PREFERENCES_TEXT_CAPACITY] = {};
    const std::size_t bytes = SerializeImGuiEditorPreferences( preferences, text, sizeof( text ) );
    FILE* file = nullptr;
    m_preferencesSaveSucceeded = bytes > 0u && fopen_s( &file, IMGUI_PREFERENCES_PATH, "wb" ) == 0 && file;

    if ( m_preferencesSaveSucceeded )
    {
        m_preferencesSaveSucceeded = fwrite( text, 1u, bytes, file ) == bytes && fflush( file ) == 0;
        fclose( file );
    }

    if ( !m_preferencesSaveSucceeded )
    {
        std::fprintf( stderr, "[imgui-preferences] failed to save %s.\n", IMGUI_PREFERENCES_PATH );
    }
}

void ImGuiEditorOwner::BuildDefaultDockLayout( uint32_t rootDockId, float width, float height, bool requestedReset )
{
    const ImGuiID root = static_cast<ImGuiID>( rootDockId );
    const ImGuiEditorLayoutEnvelope envelope = ResolveImGuiEditorLayoutEnvelope( static_cast<int>( width ),
                                                                                 static_cast<int>( height ) );

    // Invariant: remove the complete versioned tree before replaying this fixed
    // split order. DockBuilder therefore cannot preserve a corrupt or partial
    // child graph across an explicit reset.
    ImGui::DockBuilderRemoveNode( root );
    ImGui::DockBuilderAddNode( root, ImGuiDockNodeFlags_DockSpace );
    ImGui::DockBuilderSetNodeSize( root, ImVec2( width, height ) );

    ImGuiID upper = root;
    const ImGuiID status = ImGui::DockBuilderSplitNode( upper, ImGuiDir_Down, envelope.statusSplitFraction, nullptr,
                                                        &upper );

    const ImGuiID replay = ImGui::DockBuilderSplitNode( upper, ImGuiDir_Down, envelope.replaySplitFraction, nullptr,
                                                        &upper );

    const ImGuiID editorLeft = ImGui::DockBuilderSplitNode( upper, ImGuiDir_Left, envelope.editorLeftSplitFraction, nullptr,
                                                            &upper );

    const ImGuiID utilityRight = ImGui::DockBuilderSplitNode( upper, ImGuiDir_Right, envelope.utilityRightSplitFraction,
                                                              nullptr, &upper );

    const ImGuiID viewport = upper;

    ImGuiID editorUpper = editorLeft;
    const ImGuiID assets = ImGui::DockBuilderSplitNode( editorUpper, ImGuiDir_Down, 0.30f, nullptr, &editorUpper );
    ImGuiID scene = editorUpper;
    const ImGuiID hierarchy = ImGui::DockBuilderSplitNode( scene, ImGuiDir_Down, 0.68f, nullptr, &scene );

    ImGuiID inspector = utilityRight;
    ImGuiID world = ImGui::DockBuilderSplitNode( inspector, ImGuiDir_Down, 0.62f, nullptr, &inspector );
    const ImGuiID utilityTabs = ImGui::DockBuilderSplitNode( world, ImGuiDir_Down, 0.64f, nullptr, &world );

    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::SceneAndModes, scene );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Hierarchy, hierarchy );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::AssetsCreate, assets );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::GameViewport, viewport );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Inspector, inspector );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::WorldSimulation, world );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Rendering, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Diagnostics, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Causality, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Replay, replay );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Status, status );
    ImGui::DockBuilderFinish( root );
    ImGui::MarkIniSettingsDirty();

    ApplyPanelVisibilityMask( ResolveImGuiEditorPanelMaskAfterDockBuild( CopyPanelVisibilityMask() ) );
    m_layoutTopologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    ++m_layoutBuildCount;

    if ( requestedReset )
    {
        ++m_layoutResetCount;
    }

    printf( "[imgui-layout] version=%d reason=%s fingerprint=%llu viewport=%dx%d left=%d right=%d replay=%d "
            "status=%d.\n",
            LAYOUT_VERSION, requestedReset ? "operator_reset" : "missing_or_version_mismatch",
            static_cast<unsigned long long>( m_layoutTopologyFingerprint ), envelope.viewportWidth, envelope.upperHeight,
            envelope.editorLeftWidth, envelope.utilityRightWidth, envelope.replayHeight, envelope.statusHeight );
}

void ImGuiEditorOwner::SubmitToolCommand( UI::OperatorEditorToolCommandType type, uint32_t sceneObjectId, int value,
                                          bool enabled )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record(
            UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.tools,
                                             UI::OperatorEditorToolCommand { type, sceneObjectId, value, enabled } ) );
    }
}

void ImGuiEditorOwner::SubmitPropertyCommand( UI::OperatorEditorPropertyCommandType type, float value, int integerValue,
                                              UI::OperatorEditorEditPhase phase )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record(
            UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.property,
                                             UI::OperatorEditorPropertyCommand { type, value, integerValue, phase } ) );
    }
}

void ImGuiEditorOwner::SubmitReplayCommand( UI::OperatorEditorReplayCommandType type, float value, int rowIndex,
                                            bool enabled )
{
    if ( !m_frameCommandStatus.Ok() )
    {
        return;
    }

    UI::OperatorEditorReplayCommand command;
    command.type = type;
    command.value = value;
    command.rowIndex = rowIndex;
    command.enabled = enabled;
    m_frameCommandStatus.Record(
        UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.replay, command ) );
}

void ImGuiEditorOwner::SubmitForecastCommand( UI::OperatorEditorForecastCommandType type )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record( UI::SubmitOperatorEditorCommand( m_resultDiagnostics,
                                                                      m_frameCommands.operatorEditor.forecast,
                                                                      UI::OperatorEditorForecastCommand { type } ) );
    }
}

void ImGuiEditorOwner::SubmitSceneCommand( const UI::OperatorEditorSceneCommand& command )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record(
            UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.scene, command ) );
    }
}

void ImGuiEditorOwner::SubmitRenderingCommand( const UI::OperatorEditorRenderingCommand& command )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record(
            UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.rendering, command ) );
    }
}

void ImGuiEditorOwner::SubmitDiagnosticsCommand( const UI::OperatorEditorDiagnosticsCommand& command )
{
    if ( m_frameCommandStatus.Ok() )
    {
        m_frameCommandStatus.Record(
            UI::SubmitOperatorEditorCommand( m_resultDiagnostics, m_frameCommands.operatorEditor.diagnostics, command ) );
    }
}

void ImGuiEditorOwner::DrawFloatPropertyEdit( const char* label, UI::OperatorEditorPropertyCommandType type,
                                              float sourceValue, float speed, float minimum, float maximum,
                                              const char* format )
{
    bool ownsPreview = m_propertyEdit.active && m_propertyEdit.type == type;
    float preview = ownsPreview ? m_propertyEdit.floatValue : sourceValue;
    ImGui::SetNextItemWidth( (std::max)( 108.0f, ImGui::GetContentRegionAvail().x * 0.48f ) );
    const bool changed = ImGui::DragFloat( label, &preview, speed, minimum, maximum, format );

    if ( ImGui::IsItemActivated() )
    {
        // Concept: only one pointer-driven scalar can be active. Its value is
        // presentation state until release emits one owner-side commit.
        m_propertyEdit = { type, sourceValue, 0, true };
        ownsPreview = true;
    }

    if ( ownsPreview && changed )
    {
        m_propertyEdit.floatValue = preview;
    }

    const bool deactivated = ImGui::IsItemDeactivated();
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    if ( ownsPreview && commit )
    {
        SubmitPropertyCommand( type, m_propertyEdit.floatValue );
        m_propertyEdit.active = false;
    }
    else if ( ownsPreview && deactivated )
    {
        m_propertyEdit.active = false;
    }
    else if ( ownsPreview && changed )
    {
        SubmitPropertyCommand( type, m_propertyEdit.floatValue, 0, UI::OperatorEditorEditPhase::Preview );
    }
}

void ImGuiEditorOwner::DrawIntegerPropertyEdit( const char* label, UI::OperatorEditorPropertyCommandType type,
                                                int sourceValue, int minimum, int maximum )
{
    bool ownsPreview = m_propertyEdit.active && m_propertyEdit.type == type;
    int preview = ownsPreview ? m_propertyEdit.integerValue : sourceValue;
    ImGui::SetNextItemWidth( (std::max)( 108.0f, ImGui::GetContentRegionAvail().x * 0.48f ) );
    const bool changed = ImGui::SliderInt( label, &preview, minimum, maximum );

    if ( ImGui::IsItemActivated() )
    {
        m_propertyEdit = { type, 0.0f, sourceValue, true };
        ownsPreview = true;
    }

    if ( ownsPreview && changed )
    {
        m_propertyEdit.integerValue = preview;
    }

    const bool deactivated = ImGui::IsItemDeactivated();
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    if ( ownsPreview && commit )
    {
        SubmitPropertyCommand( type, 0.0f, m_propertyEdit.integerValue );
        m_propertyEdit.active = false;
    }
    else if ( ownsPreview && deactivated )
    {
        m_propertyEdit.active = false;
    }
    else if ( ownsPreview && changed )
    {
        SubmitPropertyCommand( type, 0.0f, m_propertyEdit.integerValue, UI::OperatorEditorEditPhase::Preview );
    }
}

void ImGuiEditorOwner::DrawRenderingParameterEdit( const char* label, UI::OperatorEditorRenderingCommandType type,
                                                   int parameter, float sourceValue, float speed, float minimum,
                                                   float maximum, const char* format )
{
    const int action = static_cast<int>( type );
    bool ownsPreview = m_renderingEdit.active && m_renderingEdit.action == action && m_renderingEdit.parameter == parameter;
    float preview = ownsPreview ? m_renderingEdit.value : sourceValue;
    ImGui::SetNextItemWidth( (std::max)( 108.0f, ImGui::GetContentRegionAvail().x * 0.48f ) );
    const bool changed = ImGui::DragFloat( label, &preview, speed, minimum, maximum, format );

    if ( ImGui::IsItemActivated() )
    {
        m_renderingEdit = { action, parameter, -1, -1, sourceValue, true };
        ownsPreview = true;
    }

    if ( ownsPreview && changed )
    {
        m_renderingEdit.value = preview;
    }

    const bool deactivated = ImGui::IsItemDeactivated();
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    if ( ownsPreview && commit )
    {
        SubmitRenderingCommand( { type, parameter, m_renderingEdit.value, UI::OperatorEditorEditPhase::Commit } );
        m_renderingEdit.active = false;
    }
    else if ( ownsPreview && deactivated )
    {
        m_renderingEdit.active = false;
    }
    else if ( ownsPreview && changed )
    {
        SubmitRenderingCommand( { type, parameter, m_renderingEdit.value, UI::OperatorEditorEditPhase::Preview } );
    }
}

void ImGuiEditorOwner::DrawDiagnosticsScalarEdit( const char* label, UI::OperatorEditorDiagnosticsCommandType type,
                                                  float sourceValue, float speed, float minimum, float maximum,
                                                  const char* format )
{
    const int action = static_cast<int>( type );
    bool ownsPreview = m_diagnosticsEdit.active && m_diagnosticsEdit.action == action;
    float preview = ownsPreview ? m_diagnosticsEdit.value : sourceValue;
    ImGui::SetNextItemWidth( (std::max)( 108.0f, ImGui::GetContentRegionAvail().x * 0.48f ) );
    const bool changed = ImGui::DragFloat( label, &preview, speed, minimum, maximum, format );

    if ( ImGui::IsItemActivated() )
    {
        m_diagnosticsEdit = { action, -1, -1, -1, sourceValue, true };
        ownsPreview = true;
    }

    if ( ownsPreview && changed )
    {
        m_diagnosticsEdit.value = preview;
    }

    const bool deactivated = ImGui::IsItemDeactivated();
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    if ( ownsPreview && commit )
    {
        SubmitDiagnosticsCommand( { type, 0u, 0, m_diagnosticsEdit.value, UI::OperatorEditorEditPhase::Commit } );
        m_diagnosticsEdit.active = false;
    }
    else if ( ownsPreview && deactivated )
    {
        m_diagnosticsEdit.active = false;
    }
    else if ( ownsPreview && changed )
    {
        SubmitDiagnosticsCommand( { type, 0u, 0, m_diagnosticsEdit.value, UI::OperatorEditorEditPhase::Preview } );
    }
}

void ImGuiEditorOwner::LaunchTracyViewer()
{
    char launchPath[512] = {};
    const TracyViewerLaunchTarget launchTarget = ResolveTracyViewerLaunchPath( launchPath, sizeof( launchPath ) );

    if ( launchTarget == TracyViewerLaunchTarget::Unavailable )
    {
        snprintf( m_tracyLaunchFeedback, sizeof( m_tracyLaunchFeedback ), "%s", "Tracy viewer launcher is unavailable" );
        return;
    }

    if ( !m_frameInput.tracyInitialized )
    {
        // Presentation emits a value command; Runtime owns profiler startup.
        m_frameCommands.requestTracyStandardCapture = true;
    }

    const char* launchParameters = launchTarget == TracyViewerLaunchTarget::Viewer ? "-a 127.0.0.1" : nullptr;
    const HINSTANCE launch = ShellExecuteA( m_window, "open", launchPath, launchParameters, nullptr, SW_SHOWNORMAL );
    const intptr_t launchCode = reinterpret_cast<intptr_t>( launch );
    snprintf( m_tracyLaunchFeedback, sizeof( m_tracyLaunchFeedback ), "%s",
              launchCode > 32
                  ? ( launchTarget == TracyViewerLaunchTarget::Viewer
                          ? ( m_frameInput.tracyInitialized ? "Tracy viewer launched; connection is automatic"
                                                            : "Starting Standard capture; viewer connection is automatic" )
                          : ( m_frameInput.tracyInitialized
                                  ? "Building pinned Tracy viewer; it opens automatically when ready"
                                  : "Starting Standard capture and building the pinned viewer" ) )
                  : "Tracy viewer launch failed; inspect the launcher console" );
}

void ImGuiEditorOwner::BuildEditorShell( const UI::OperatorEditorFrameView& view,
                                         const ReplayOverlay::ReplayOverlayStateView& replay )
{
    if ( !m_frameActive )
    {
        return;
    }

    m_sharedViewFingerprint = UI::FingerprintOperatorEditorFrameView( view );
    const ImGuiEditorCausalityProjection causality = BuildImGuiEditorCausalityProjection( replay.timeline,
                                                                                          replay.causality );

    BuildEditorMenuAndDockspace( view.scene, view.hierarchy, view.tools );
    DrawSceneAndModesPanel( view.scene, view.tools );
    DrawHierarchyPanel( view.hierarchy );
    DrawAssetsCreatePanel( view.assets, view.tools );
    DrawGameViewportPanel( view.rendering, view.viewport );
    DrawInspectorPanel( view.inspector );
    DrawWorldSimulationPanel( view.world );
    DrawRenderingPanel( view.rendering );
    DrawDiagnosticsPanel( view.diagnostics, view.rendering );
    DrawCausalityPanel( causality, replay.causality );
    DrawCausalityDetailPanel( causality, replay.causality );
    DrawReplayPanel( view.forecast, replay.timeline, replay.causality );
    DrawStatusPanel( view.lookLab, view.scene, view.tools );
    ApplyPendingPanelFocus();
}

void ImGuiEditorOwner::BuildEditorMenuAndDockspace( const UI::OperatorEditorSceneView& scene,
                                                    const UI::OperatorEditorHierarchyView& hierarchy,
                                                    const UI::OperatorEditorToolView& tools )
{

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->WorkPos );
    ImGui::SetNextWindowSize( viewport->WorkSize );
    ImGui::SetNextWindowViewport( viewport->ID );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    constexpr ImGuiWindowFlags shellFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                            ImGuiWindowFlags_NoSavedSettings;

    const bool shellVisible = ImGui::Begin( "Skore Editor###SkoreEditorShellV2", nullptr, shellFlags );
    ImGui::PopStyleVar( 3 );

    if ( !shellVisible )
    {
        ImGui::End();
        return;
    }

    bool selectedEntityLocked = false;

    for ( uint32_t index = 0u; index < hierarchy.rowCount; ++index )
    {
        if ( hierarchy.rows[index].sceneObjectId == hierarchy.selectedSceneObjectId )
        {
            selectedEntityLocked = hierarchy.rows[index].locked;
            break;
        }
    }

    if ( ImGui::BeginMenuBar() )
    {
        if ( ImGui::BeginMenu( "File" ) )
        {
            if ( ImGui::MenuItem( "New Scene..." ) )
            {
                m_showSceneAndModes = true;
                m_focusSceneCreate = true;
            }

            if ( ImGui::MenuItem( "Load Scene..." ) )
            {
                m_showSceneAndModes = true;
                m_focusSceneFilter = true;
            }

            if ( ImGui::MenuItem( "Save Scene", "Ctrl+S", false, scene.canSaveCurrentScene ) )
            {
                SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::SaveCurrentScene } );
            }

            if ( !scene.canSaveCurrentScene )
            {
                DrawDisabledReason( "The generated demo has no authored scene path" );
            }

            ImGui::Separator();

            if ( ImGui::MenuItem( "Reset Current Scene" ) )
            {
                SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::ResetCurrentScene, -1 } );
            }

            if ( ImGui::MenuItem( "Reset To Authored Defaults", nullptr, false, scene.canSaveCurrentScene ) )
            {
                SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::ResetSceneDefaults } );
            }

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Edit" ) )
        {
            if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, tools.editorModeEnabled && tools.undoDepth > 0 ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::Undo );
            }

            if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, tools.editorModeEnabled && tools.redoDepth > 0 ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::Redo );
            }

            const bool hasSelection = hierarchy.selectedSceneObjectId != 0u;
            const bool hasMutableSelection = hasSelection && !selectedEntityLocked;

            if ( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, tools.editorModeEnabled && hasMutableSelection ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::DuplicateSelection );
            }

            if ( ImGui::MenuItem( "Delete", "Del", false, tools.editorModeEnabled && hasMutableSelection ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::DeleteSelection );
            }

            ImGui::Separator();

            if ( ImGui::MenuItem( tools.editorModeEnabled ? "Exit Edit Mode" : "Enter Edit Mode", "`" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleEditorMode );
            }

            if ( ImGui::MenuItem( "Placement Mode", "E", tools.placementModeEnabled, tools.editorModeEnabled ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::TogglePlacementMode );
            }

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "View" ) )
        {
            if ( ImGui::MenuItem( "Switch to Game UI", "Ctrl+0" ) )
            {
                // The composition root consumes this after the ImGui frame and
                // hides this source before activating the GameUI target.
                m_frameCommands.requestSurfaceSwap = true;
            }

            ImGui::Separator();
            ImGui::MenuItem( ImGuiEditorPanel::SceneAndModes, nullptr, &m_showSceneAndModes );
            ImGui::MenuItem( ImGuiEditorPanel::Hierarchy, nullptr, &m_showHierarchy );
            ImGui::MenuItem( ImGuiEditorPanel::AssetsCreate, nullptr, &m_showAssetsCreate );
            ImGui::MenuItem( ImGuiEditorPanel::GameViewport, nullptr, &m_showGameViewport );
            ImGui::MenuItem( ImGuiEditorPanel::Inspector, nullptr, &m_showInspector );
            ImGui::MenuItem( ImGuiEditorPanel::WorldSimulation, nullptr, &m_showWorldSimulation );
            ImGui::MenuItem( "Rendering", nullptr, &m_showRendering );
            ImGui::MenuItem( "Diagnostics", nullptr, &m_showDiagnostics );
            ImGui::MenuItem( "Causality", nullptr, &m_showCausality );
            ImGui::MenuItem( "Causality Detail", nullptr, &m_showCausalityDetail );
            ImGui::MenuItem( ImGuiEditorPanel::Replay, nullptr, &m_showReplay );
            ImGui::MenuItem( ImGuiEditorPanel::Status, nullptr, &m_showStatus );
            ImGui::Separator();

            if ( ImGui::MenuItem( "Reset Editor Layout" ) )
            {
                m_layoutResetRequested = true;
                ApplyPanelVisibilityMask( ResetImGuiEditorPanelMask() );
            }

            ImGui::EndMenu();
        }

        if ( ImGui::Shortcut( ImGuiMod_Ctrl | ImGuiKey_0, ImGuiInputFlags_RouteGlobal ) )
        {
            m_frameCommands.requestSurfaceSwap = true;
        }

        if ( ImGui::BeginMenu( "Debug" ) )
        {
            ImGui::TextDisabled( "Tracy: %s", m_frameInput.tracyViewerConnected
                                                  ? "connected"
                                                  : ( m_frameInput.tracyInitialized ? "waiting for viewer" : "disabled" ) );

            if ( ImGui::MenuItem( "Launch Tracy Viewer" ) )
            {
                LaunchTracyViewer();
            }

            ImGui::TextDisabled( "%s", m_tracyLaunchFeedback );
            ImGui::Separator();
            ImGui::TextDisabled( "Layout v%d / %llu", LAYOUT_VERSION,
                                 static_cast<unsigned long long>( m_layoutTopologyFingerprint ) );

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    const ImGuiEditorLayoutEnvelope
        shellEnvelope = ResolveImGuiEditorLayoutEnvelope( static_cast<int>( ImGui::GetContentRegionAvail().x ),
                                                          static_cast<int>( ImGui::GetContentRegionAvail().y ) );

    const char* modeLabel = tools.editorModeEnabled ? "EDIT" : "PLAY";
    const char* placementLabel = tools.placementModeEnabled
                                     ? ( shellEnvelope.compactToolbarLabels ? "PLACE*" : "PLACEMENT ACTIVE" )
                                     : ( shellEnvelope.compactToolbarLabels ? "PLACE" : "PLACEMENT" );

    const float toolbarHeight = 34.0f * m_frameInput.dpiScale;
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 5.0f ) );

    if ( ImGui::BeginChild( "##SkoreEditorToolbar", ImVec2( 0.0f, toolbarHeight ), ImGuiChildFlags_Borders ) )
    {
        if ( ImGui::Button( modeLabel ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleEditorMode );
        }

        ImGui::SameLine();
        ImGui::BeginDisabled( !tools.editorModeEnabled );

        if ( ImGui::Button( placementLabel ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::TogglePlacementMode );
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( !tools.editorModeEnabled || tools.undoDepth <= 0 );

        if ( ImGui::Button( "UNDO" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::Undo );
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( !tools.editorModeEnabled || tools.redoDepth <= 0 );

        if ( ImGui::Button( "REDO" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::Redo );
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if ( ImGui::Button( tools.crossScenePauseLocked ? "PLAY" : "PAUSE" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleCrossScenePause );
        }

        ImGui::SameLine();
        ImGui::BeginDisabled( !tools.crossScenePauseLocked );

        if ( ImGui::Button( "STEP" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::StepPausedScene );
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "%s  |  frame %d  |  %.2fx", SceneDisplayName( scene.sceneName ), scene.currentFrame,
                             scene.timeScale );

        ImGui::SameLine();

        if ( ImGui::Button( m_frameInput.tracyViewerConnected ? "TRACY CONNECTED" : "OPEN TRACY" ) )
        {
            LaunchTracyViewer();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    const ImVec2 dockSize = ImGui::GetContentRegionAvail();
    const ImGuiID rootDockId = ImGui::GetID( IMGUI_EDITOR_DOCKSPACE_NAME );
    const bool missingLayout = ImGui::DockBuilderGetNode( rootDockId ) == nullptr;

    if ( m_layoutResetRequested || missingLayout )
    {
        BuildDefaultDockLayout( rootDockId, dockSize.x, dockSize.y, m_layoutResetRequested );
        m_layoutResetRequested = false;
    }

    ImGui::DockSpace( rootDockId, dockSize );
    ImGui::End();
}

void ImGuiEditorOwner::DrawSceneAndModesPanel( const UI::OperatorEditorSceneView& scene,
                                               const UI::OperatorEditorToolView& tools )
{
    if ( !m_showSceneAndModes )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::SceneAndModes, &m_showSceneAndModes ) )
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted( "MODE" );

    if ( ImGui::Button( tools.editorModeEnabled ? "EDIT ACTIVE" : "ENTER EDIT" ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleEditorMode );
    }

    ImGui::SameLine();
    ImGui::BeginDisabled( !tools.editorModeEnabled );

    if ( ImGui::Button( tools.placementModeEnabled ? "PLACE ACTIVE" : "PLACE" ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::TogglePlacementMode );
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if ( ImGui::Button( tools.crossScenePauseLocked ? "RESUME" : "PAUSE" ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleCrossScenePause );
    }

    ImGui::SameLine();
    ImGui::BeginDisabled( !tools.crossScenePauseLocked );

    if ( ImGui::Button( "STEP" ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::StepPausedScene );
    }

    ImGui::EndDisabled();

    ImGui::SeparatorText( "Scene" );
    ImGui::Text( "%s%s", SceneDisplayName( scene.sceneName ), scene.dirty ? "  * modified" : "" );

    if ( m_focusSceneFilter )
    {
        ImGui::SetKeyboardFocusHere();
        m_focusSceneFilter = false;
    }

    ImGui::InputTextWithHint( "##SceneFilter", "Filter scenes", m_sceneFilter, sizeof( m_sceneFilter ) );
    const char* activeScene = SceneDisplayName( scene.sceneName );

    if ( ImGui::BeginCombo( "Active", activeScene ) )
    {
        bool anyVisible = false;

        for ( int index = 0; index < scene.sceneCount && scene.sceneOptions; ++index )
        {
            const char* label = scene.sceneOptions[index] ? scene.sceneOptions[index] : "Unnamed scene";

            if ( !ContainsAsciiInsensitive( label, m_sceneFilter ) )
            {
                continue;
            }

            anyVisible = true;

            if ( ImGui::Selectable( label, index == scene.currentSceneIndex ) )
            {
                SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::SetCurrentSceneIndex, index } );
            }
        }

        if ( !anyVisible )
        {
            ImGui::TextDisabled( "No matching scenes" );
        }

        ImGui::EndCombo();
    }

    ImGui::BeginDisabled( !scene.canSaveCurrentScene );

    if ( ImGui::Button( "Save" ) )
    {
        SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::SaveCurrentScene } );
    }

    ImGui::EndDisabled();

    if ( !scene.canSaveCurrentScene )
    {
        DrawDisabledReason( "Generated demo scenes have no authored save path" );
    }

    ImGui::SameLine();

    if ( ImGui::Button( "Reset Current Scene" ) )
    {
        SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::ResetCurrentScene, -1 } );
    }

    ImGui::SameLine();
    ImGui::BeginDisabled( !scene.canSaveCurrentScene );

    if ( ImGui::Button( "Defaults" ) )
    {
        SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::ResetSceneDefaults } );
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if ( ImGui::Button( "Demo" ) )
    {
        SubmitSceneCommand( { UI::OperatorEditorSceneCommandType::RequestDemoScene } );
    }

    ImGui::SeparatorText( "Create" );

    if ( m_focusSceneCreate )
    {
        ImGui::SetKeyboardFocusHere();
        m_focusSceneCreate = false;
    }

    ImGui::InputTextWithHint( "##NewSceneName", "New scene name", m_newSceneName, sizeof( m_newSceneName ) );
    ImGui::SameLine();
    ImGui::BeginDisabled( m_newSceneName[0] == '\0' );

    if ( ImGui::Button( "Create Scene" ) )
    {
        UI::OperatorEditorSceneCommand create;
        create.type = UI::OperatorEditorSceneCommandType::CreateScene;
        strncpy_s( create.sceneName, m_newSceneName, _TRUNCATE );
        SubmitSceneCommand( create );
    }

    ImGui::EndDisabled();
    ImGui::TextDisabled( "Undo %d  |  Redo %d  |  %s", tools.undoDepth, tools.redoDepth,
                         scene.dirty ? "unsaved edits" : "clean" );
    ImGui::End();
}

void ImGuiEditorOwner::DrawHierarchyPanel( const UI::OperatorEditorHierarchyView& hierarchy )
{
    if ( !m_showHierarchy )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Hierarchy, &m_showHierarchy ) )
    {
        ImGui::End();
        return;
    }
    ImGui::Text( "%u / %u scene objects", hierarchy.rowCount, hierarchy.totalRowCount );
    ImGui::InputTextWithHint( "##HierarchyFilter", "Filter by name", m_hierarchyFilter, sizeof( m_hierarchyFilter ) );

    ImGui::Separator();
    ImGui::BeginChild( "##HierarchyRows", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders );

    bool anyVisible = false;

    for ( uint32_t index = 0u; index < hierarchy.rowCount; ++index )
    {
        const UI::OperatorEditorHierarchyRow& row = hierarchy.rows[index];

        if ( !ContainsAsciiInsensitive( row.displayName, m_hierarchyFilter ) )
        {
            continue;
        }

        anyVisible = true;
        ImGui::PushID( static_cast<int>( row.sceneObjectId ) );

        if ( ImGui::SmallButton( row.visible ? "eye" : "hidden" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::SetEntityVisible, row.sceneObjectId, 0, !row.visible );
        }

        ImGui::SameLine();

        if ( ImGui::SmallButton( row.locked ? "locked" : "open" ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::SetEntityLocked, row.sceneObjectId, 0, !row.locked );
        }

        ImGui::SameLine();
        const char* displayName = row.displayName && row.displayName[0] != '\0' ? row.displayName : "Unnamed object";

        if ( ImGui::Selectable( displayName, row.selected, ImGuiSelectableFlags_AllowDoubleClick ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::SelectSceneObject, row.sceneObjectId );
        }

        if ( ImGui::BeginPopupContextItem( "##HierarchyContext" ) )
        {
            if ( ImGui::MenuItem( "Select" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::SelectSceneObject, row.sceneObjectId );
            }

            if ( ImGui::MenuItem( row.visible ? "Hide" : "Show" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::SetEntityVisible, row.sceneObjectId, 0, !row.visible );
            }

            if ( ImGui::MenuItem( row.locked ? "Unlock" : "Lock" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::SetEntityLocked, row.sceneObjectId, 0, !row.locked );
            }

            ImGui::BeginDisabled( row.locked || !row.selected );

            if ( ImGui::MenuItem( "Duplicate" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::DuplicateSelection );
            }

            if ( ImGui::MenuItem( "Delete" ) )
            {
                SubmitToolCommand( UI::OperatorEditorToolCommandType::DeleteSelection );
            }

            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        if ( ImGui::IsItemHovered() && row.assetBacked )
        {
            ImGui::SetTooltip( "Registered asset group root %u, part %d", row.groupRootObjectId, row.groupPartIndex );
        }

        ImGui::PopID();
    }

    if ( !anyVisible )
    {
        ImGui::TextDisabled( "No matching scene objects" );
    }

    ImGui::EndChild();

    if ( hierarchy.truncated )
    {
        ImGui::TextDisabled( "Showing the first %u objects; narrow the filter after simplifying the scene.",
                             hierarchy.rowCount );
    }
    else
    {
        ImGui::TextDisabled( "Single selection uses stable scene identity; asset groups select as one root." );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawAssetsCreatePanel( const UI::OperatorEditorAssetView& assets,
                                              const UI::OperatorEditorToolView& tools )
{
    if ( !m_showAssetsCreate )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::AssetsCreate, &m_showAssetsCreate ) )
    {
        ImGui::End();
        return;
    }
    ImGui::InputTextWithHint( "##AssetFilter", "Search assets", m_assetFilter, sizeof( m_assetFilter ) );
    bool placeStatic = tools.placeStaticObject;

    if ( ImGui::Checkbox( "Static", &placeStatic ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::SetPlaceStatic, 0u, 0, placeStatic );
    }

    ImGui::SameLine();
    bool terrainAlign = tools.autoTerrainAlign;

    if ( ImGui::Checkbox( "Align to terrain", &terrainAlign ) )
    {
        SubmitToolCommand( UI::OperatorEditorToolCommandType::ToggleTerrainAlign );
    }

    ImGui::Separator();
    ImGui::BeginChild( "##AssetRows", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders );

    const char* previousCategory = nullptr;
    bool anyVisible = false;

    for ( int objectType = 0; objectType < assets.objectTypeCount; ++objectType )
    {
        const char* label = UI::EditorTab::ObjectLabel( objectType );

        if ( !ContainsAsciiInsensitive( label, m_assetFilter ) )
        {
            continue;
        }

        anyVisible = true;
        const char* category = AssetCategoryForObjectType( objectType );

        if ( previousCategory != category )
        {
            ImGui::SeparatorText( category );
            previousCategory = category;
        }

        const bool registeredUnavailable = objectType >= UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP &&
                                           !assets.registeredLibraryAvailable;

        ImGui::PushID( objectType );
        ImGui::BeginDisabled( registeredUnavailable );

        if ( ImGui::Selectable( label, objectType == assets.selectedObjectType ) )
        {
            SubmitToolCommand( UI::OperatorEditorToolCommandType::SetPlacementObjectType, 0u, objectType );
        }

        if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceAllowNullID ) )
        {
            ImGui::SetDragDropPayload( "SKORE_ASSET_OBJECT_TYPE", &objectType, sizeof( objectType ) );
            ImGui::Text( "Place %s", label );
            ImGui::EndDragDropSource();
        }

        ImGui::EndDisabled();

        if ( registeredUnavailable && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
        {
            ImGui::SetTooltip( "Registered asset library assetlib.buildings is unavailable" );
        }

        ImGui::PopID();
    }

    if ( !anyVisible )
    {
        ImGui::TextDisabled( "No matching assets" );
    }

    ImGui::EndChild();
    ImGui::TextDisabled( "Click or drag an asset to enter placement mode." );
    ImGui::End();
}

void ImGuiEditorOwner::DrawGameViewportPanel( const UI::OperatorEditorRenderingView& rendering,
                                              const UI::OperatorEditorViewportView& viewport )
{
    bool gameViewportHovered = false;
    bool gameViewportFocused = false;
    m_gameViewportRect = {};

    if ( !m_showGameViewport )
    {
        SetGameViewportInputState( false, false );
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::GameViewport, &m_showGameViewport ) )
    {
        ImGui::End();
        SetGameViewportInputState( false, false );
        return;
    }
    gameViewportFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
    ImGui::TextDisabled( "Camera" );
    ImGui::SameLine();
    ImGui::TextUnformatted( viewport.cameraModeLabel ? viewport.cameraModeLabel : "unknown" );
    ImGui::SameLine();
    ImGui::TextDisabled( "| Gizmo" );
    ImGui::SameLine();
    ImGui::TextUnformatted( viewport.gizmoModeLabel ? viewport.gizmoModeLabel : "translate" );
    ImGui::SameLine();
    ImGui::TextDisabled( "| Snap free" );
    ImGui::SameLine();

    if ( ImGui::SmallButton( rendering.vsyncEnabled ? "VSync on" : "VSync off" ) )
    {
        SubmitRenderingCommand( { UI::OperatorEditorRenderingCommandType::ToggleVsync } );
    }

    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::SmallButton( "Pop-out: single window" );
    ImGui::EndDisabled();
    DrawDisabledReason( "Native platform viewports are disabled; the editor owns one application window" );

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 availableMin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled( availableMin, ImVec2( availableMin.x + available.x, availableMin.y + available.y ),
                             IM_COL32( 8, 10, 14, 255 ) );

    const uint64_t textureId = m_renderer ? m_renderer->GameViewportTextureId() : 0u;
    const int sourceWidth = m_renderer ? m_renderer->GameViewportWidth() : 0;
    const int sourceHeight = m_renderer ? m_renderer->GameViewportHeight() : 0;

    // Why: fitting the persistent full-client copy here keeps dock
    // movement CPU-only. Picking receives the same value rectangle, so
    // image pixels, world outlines, placement previews, and input agree.
    m_gameViewportRect = ResolveImGuiGameViewportRect( availableMin.x, availableMin.y, available.x, available.y, sourceWidth,
                                                       sourceHeight, m_frameInput.dpiScale );

    if ( textureId != 0u && m_gameViewportRect.valid )
    {
        // Lifetime: textureId names the owner's stable descriptor row;
        // the resource behind it may change only after a drained
        // swap-chain resize, never while this draw list is in flight.
        const ImVec2 imageMin( m_gameViewportRect.imageMinX, m_gameViewportRect.imageMinY );
        const ImVec2 imageMax( imageMin.x + m_gameViewportRect.imageWidth, imageMin.y + m_gameViewportRect.imageHeight );
        ImGui::SetCursorScreenPos( imageMin );
        ImGui::InvisibleButton( "##GameViewportImage",
                                ImVec2( m_gameViewportRect.imageWidth, m_gameViewportRect.imageHeight ) );

        gameViewportHovered = ImGui::IsItemHovered();
        drawList->AddImage( ImTextureRef( static_cast<ImTextureID>( textureId ) ), imageMin, imageMax );

        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "SKORE_ASSET_OBJECT_TYPE" ) )
            {
                if ( payload->DataSize == sizeof( int ) )
                {
                    SubmitToolCommand( UI::OperatorEditorToolCommandType::SetPlacementObjectType, 0u,
                                       *static_cast<const int*>( payload->Data ) );
                }
            }

            ImGui::EndDragDropTarget();
        }
    }
    else
    {
        ImGui::SetCursorScreenPos( ImVec2( availableMin.x + 18.0f, availableMin.y + 18.0f ) );
        ImGui::TextDisabled( "DX12 game viewport image unavailable" );
    }
    ImGui::End();

    SetGameViewportInputState( gameViewportHovered, gameViewportFocused );
}

void ImGuiEditorOwner::DrawInspectorPanel( const UI::OperatorEditorInspectorView& inspector )
{
    if ( !m_showInspector )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Inspector, &m_showInspector ) )
    {
        ImGui::End();
        return;
    }
    if ( inspector.selectionState == UI::OperatorEditorInspectorSelectionState::None )
    {
        ImGui::TextDisabled( "No selection" );
        ImGui::Separator();
        ImGui::TextWrapped( "Select one scene object in the hierarchy or game viewport to inspect it." );
    }
    else if ( inspector.selectionState == UI::OperatorEditorInspectorSelectionState::Stale )
    {
        ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.28f, 1.0f ), "Selection is stale" );
        ImGui::TextWrapped( "The selected stable identity no longer resolves in the current scene." );
    }
    else if ( inspector.selectionState == UI::OperatorEditorInspectorSelectionState::Mixed )
    {
        ImGui::Text( "%u selected", inspector.selectionCount );
        ImGui::SeparatorText( "Transform" );
        ImGui::TextDisabled( "Mixed values" );
        ImGui::SeparatorText( "Identity" );
        ImGui::TextDisabled( "Mixed stable identities" );
        ImGui::SeparatorText( "Render / Physics" );
        ImGui::TextDisabled( "Mixed values" );
    }
    else
    {
        ImGui::Text( "%s",
                     inspector.displayName && inspector.displayName[0] != '\0' ? inspector.displayName : "Unnamed object" );

        ImGui::SameLine();
        ImGui::TextDisabled( "#%u", inspector.sceneObjectId );
        ImGui::TextDisabled( "%s | %s", inspector.visible ? "visible" : "hidden", inspector.locked ? "locked" : "editable" );

        if ( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Position  %.3f  %.3f  %.3f", inspector.position[0], inspector.position[1], inspector.position[2] );

            ImGui::Text( "Rotation q  %.3f  %.3f  %.3f  %.3f", inspector.orientation[0], inspector.orientation[1],
                         inspector.orientation[2], inspector.orientation[3] );

            ImGui::TextDisabled( "Author with the viewport translate/rotate/scale gizmos." );
        }

        if ( ImGui::CollapsingHeader( "Identity", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Scene object  %u", inspector.sceneObjectId );
            ImGui::Text( "Behavior group  %d  |  part %d", inspector.behaviorGroupKind, inspector.behaviorPartIndex );

            ImGui::Text( "Source  %s", inspector.assetBacked ? "registered asset" : "standalone primitive" );
        }

        if ( ImGui::CollapsingHeader( "Render", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Material  %s", inspector.renderMaterialName && inspector.renderMaterialName[0] != '\0'
                                             ? inspector.renderMaterialName
                                             : "default" );

            ImGui::Text( "RGBA  %.2f  %.2f  %.2f  %.2f", inspector.baseColor[0], inspector.baseColor[1],
                         inspector.baseColor[2], inspector.baseColor[3] );

            ImGui::Text( "Rough %.2f  Metal %.2f  Spec %.2f", inspector.roughness, inspector.metallic, inspector.specular );
        }

        if ( ImGui::CollapsingHeader( "Physics", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "%s | %s", inspector.fixed ? "fixed" : "dynamic", inspector.sleeping ? "sleeping" : "awake" );

            ImGui::Text( "Mass %.3f  Volume %.3f  Radius %.3f", inspector.mass, inspector.volume, inspector.boundingRadius );

            ImGui::Text( "Friction %.3f  Restitution %.3f  Drag %.3f", inspector.friction, inspector.restitution,
                         inspector.dragCoefficient );

            ImGui::Text( "Linear v  %.3f  %.3f  %.3f", inspector.linearVelocity[0], inspector.linearVelocity[1],
                         inspector.linearVelocity[2] );

            ImGui::Text( "Angular v  %.3f  %.3f  %.3f", inspector.angularVelocity[0], inspector.angularVelocity[1],
                         inspector.angularVelocity[2] );
        }

        if ( ImGui::CollapsingHeader( "Object-specific" ) )
        {
            static constexpr const char* shapeNames[] = { "sphere", "box", "convex hull" };
            const char* shape = inspector.colliderShapeKind >= 0 && inspector.colliderShapeKind < 3
                                    ? shapeNames[inspector.colliderShapeKind]
                                    : "unknown";

            ImGui::Text( "Shape  %s", shape );

            if ( inspector.assetBacked )
            {
                ImGui::Text( "Asset  %s",
                             inspector.assetName && inspector.assetName[0] != '\0' ? inspector.assetName : "unnamed" );

                ImGui::Text( "Instance  %s", inspector.assetInstanceName && inspector.assetInstanceName[0] != '\0'
                                                 ? inspector.assetInstanceName
                                                 : "unnamed" );

                ImGui::Text( "Part  %s", inspector.assetPartName && inspector.assetPartName[0] != '\0'
                                             ? inspector.assetPartName
                                             : "unnamed" );
            }
            else
            {
                ImGui::TextDisabled( "No registered asset affiliation" );
            }
        }
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawWorldSimulationPanel( const UI::OperatorEditorWorldView& world )
{
    if ( !m_showWorldSimulation )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::WorldSimulation, &m_showWorldSimulation ) )
    {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled( "Preview locally; commit once on release." );

    if ( m_propertyEdit.active )
    {
        ImGui::SameLine();
        ImGui::TextColored( ImVec4( 0.35f, 0.78f, 1.0f, 1.0f ), "PREVIEW" );
    }

    if ( ImGui::CollapsingHeader( "Simulation", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        bool captureLockstepRequested = world.fixedStep;

        if ( ImGui::Checkbox( "Capture lockstep", &captureLockstepRequested ) )
        {
            SubmitPropertyCommand( UI::OperatorEditorPropertyCommandType::ToggleFixedStep );
        }

        ImGui::SameLine();
        bool sleepEnabled = world.physicsSleepEnabled;

        if ( ImGui::Checkbox( "Sleep policy", &sleepEnabled ) )
        {
            SubmitPropertyCommand( UI::OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy );
        }

        DrawFloatPropertyEdit( "Time scale", UI::OperatorEditorPropertyCommandType::SetTimeScale, world.timeScale,
                               UI::OperatorControlPolicy::UI_TIME_SCALE_STEP, UI::OperatorControlPolicy::UI_TIME_SCALE_MIN,
                               UI::OperatorControlPolicy::UI_TIME_SCALE_MAX, "%.2fx" );
    }

    if ( ImGui::CollapsingHeader( "Population / seed", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const int capacity = (std::max)( 0, world.modelCapacity );
        DrawIntegerPropertyEdit( "Population", UI::OperatorEditorPropertyCommandType::SetModelCount, world.modelCount,
                                 UI::OperatorControlPolicy::UI_MODEL_COUNT_MIN, capacity );

        DrawIntegerPropertyEdit( "Seed", UI::OperatorEditorPropertyCommandType::SetSeed, world.rngSeed,
                                 UI::OperatorControlPolicy::UI_SEED_MIN, UI::OperatorControlPolicy::UI_SEED_MAX );

        DrawIntegerPropertyEdit( "Solver balls", UI::OperatorEditorPropertyCommandType::SetSolverBallCount,
                                 world.solverBallCount, UI::OperatorControlPolicy::UI_SOLVER_COUNT_MIN,
                                 (std::max)( 0, capacity - world.solverBoxCount ) );

        DrawIntegerPropertyEdit( "Solver boxes", UI::OperatorEditorPropertyCommandType::SetSolverBoxCount,
                                 world.solverBoxCount, UI::OperatorControlPolicy::UI_SOLVER_COUNT_MIN,
                                 (std::max)( 0, capacity - world.solverBallCount ) );

        ImGui::TextDisabled( "Population controls rebuild generated scene topology on commit." );
    }

    if ( ImGui::CollapsingHeader( "World forces / fluid", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        DrawFloatPropertyEdit( "Gravity", UI::OperatorEditorPropertyCommandType::SetWorldGravity, world.gravity,
                               UI::OperatorControlPolicy::UI_WORLD_GRAVITY_STEP,
                               -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MAX,
                               -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MIN, "%.2f m/s^2" );

        DrawFloatPropertyEdit( "Fluid surface", UI::OperatorEditorPropertyCommandType::SetWorldFluidHeight,
                               world.fluidHeight, UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_STEP,
                               UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MIN,
                               UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MAX, "%.1f m" );

        DrawFloatPropertyEdit( "Fluid density", UI::OperatorEditorPropertyCommandType::SetWorldFluidDensity,
                               world.fluidDensity, UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_STEP,
                               UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MIN,
                               UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MAX, "%.2f kg/m3" );
    }

    if ( ImGui::CollapsingHeader( "Contact friction", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        DrawFloatPropertyEdit( "Terrain friction", UI::OperatorEditorPropertyCommandType::SetTerrainFriction,
                               world.terrainFriction, UI::OperatorControlPolicy::UI_FRICTION_COEFF_STEP,
                               UI::OperatorControlPolicy::UI_FRICTION_COEFF_MIN,
                               UI::OperatorControlPolicy::UI_FRICTION_COEFF_MAX, "%.2f" );

        DrawFloatPropertyEdit( "Object friction", UI::OperatorEditorPropertyCommandType::SetObjectFriction,
                               world.objectFriction, UI::OperatorControlPolicy::UI_FRICTION_COEFF_STEP,
                               UI::OperatorControlPolicy::UI_FRICTION_COEFF_MIN,
                               UI::OperatorControlPolicy::UI_FRICTION_COEFF_MAX, "%.2f" );

        DrawFloatPropertyEdit( "Rolling friction", UI::OperatorEditorPropertyCommandType::SetRollingFriction,
                               world.rollingFriction, UI::OperatorControlPolicy::UI_ROLLING_FRICTION_COEFF_STEP,
                               UI::OperatorControlPolicy::UI_ROLLING_FRICTION_COEFF_MIN,
                               UI::OperatorControlPolicy::UI_ROLLING_FRICTION_COEFF_MAX, "%.3f" );
    }

    if ( ImGui::CollapsingHeader( "Tornado / environment force", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        bool tornadoEnabled = world.tornadoEnabled;

        if ( ImGui::Checkbox( "Enabled", &tornadoEnabled ) )
        {
            SubmitPropertyCommand( UI::OperatorEditorPropertyCommandType::ToggleTornado );
        }

        DrawFloatPropertyEdit( "Radius", UI::OperatorEditorPropertyCommandType::SetTornadoRadius, world.tornadoRadius,
                               UI::OperatorControlPolicy::UI_TORNADO_RADIUS_STEP,
                               UI::OperatorControlPolicy::UI_TORNADO_RADIUS_MIN,
                               UI::OperatorControlPolicy::UI_TORNADO_RADIUS_MAX, "%.1f m" );

        DrawFloatPropertyEdit( "Height", UI::OperatorEditorPropertyCommandType::SetTornadoHeight, world.tornadoHeight,
                               UI::OperatorControlPolicy::UI_TORNADO_HEIGHT_STEP,
                               UI::OperatorControlPolicy::UI_TORNADO_HEIGHT_MIN,
                               UI::OperatorControlPolicy::UI_TORNADO_HEIGHT_MAX, "%.1f m" );

        DrawFloatPropertyEdit( "Inward", UI::OperatorEditorPropertyCommandType::SetTornadoInward, world.tornadoInward,
                               UI::OperatorControlPolicy::UI_TORNADO_INWARD_STEP,
                               UI::OperatorControlPolicy::UI_TORNADO_INWARD_MIN,
                               UI::OperatorControlPolicy::UI_TORNADO_INWARD_MAX, "%.1f" );

        DrawFloatPropertyEdit( "Swirl", UI::OperatorEditorPropertyCommandType::SetTornadoSwirl, world.tornadoSwirl,
                               UI::OperatorControlPolicy::UI_TORNADO_SWIRL_STEP,
                               UI::OperatorControlPolicy::UI_TORNADO_SWIRL_MIN,
                               UI::OperatorControlPolicy::UI_TORNADO_SWIRL_MAX, "%.1f" );

        DrawFloatPropertyEdit( "Lift", UI::OperatorEditorPropertyCommandType::SetTornadoLift, world.tornadoLift,
                               UI::OperatorControlPolicy::UI_TORNADO_LIFT_STEP,
                               UI::OperatorControlPolicy::UI_TORNADO_LIFT_MIN,
                               UI::OperatorControlPolicy::UI_TORNADO_LIFT_MAX, "%.1f" );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawRenderingPanel( const UI::OperatorEditorRenderingView& rendering )
{
    if ( !m_showRendering )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Rendering, &m_showRendering ) )
    {
        ImGui::End();
        return;
    }
    if ( ImGui::BeginTabBar( "##RenderingTabs" ) )
    {
        if ( ImGui::BeginTabItem( "Rendering" ) )
        {
            DrawRenderingAuthoringTab( rendering );
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawRenderingAuthoringTab( const UI::OperatorEditorRenderingView& rendering )
{
    const auto submitRendering = [&]( UI::OperatorEditorRenderingCommandType type, int parameter = -1, float value = 0.0f,
                                      UI::OperatorEditorEditPhase phase = UI::OperatorEditorEditPhase::Commit )
    { SubmitRenderingCommand( { type, parameter, value, phase } ); };

    bool vsync;

    if ( DrawEditableCheckbox( "VSync", rendering.vsyncEnabled, vsync ) )
    {
        submitRendering( UI::OperatorEditorRenderingCommandType::ToggleVsync );
    }

    ImGui::SameLine();
    bool cinematic;

    if ( DrawEditableCheckbox( "Cinematic", rendering.cinematicRendering, cinematic ) )
    {
        submitRendering( UI::OperatorEditorRenderingCommandType::ToggleCinematicRendering );
    }

    ImGui::SameLine();

    if ( ImGui::SmallButton( "Save profile" ) )
    {
        submitRendering( cinematic ? UI::OperatorEditorRenderingCommandType::SaveSkyDefaults
                                   : UI::OperatorEditorRenderingCommandType::SaveOrdinaryDefaults );
    }

    ImGui::TextDisabled( "%s profile | preview locally, commit once on release", cinematic ? "cinematic" : "ordinary" );

    // Concept: the shared section catalog changes presentation
    // topology only. Ordinary and cinematic values still travel
    // through their existing domain enums and runtime owners.
    for ( int rawSection = static_cast<int>( UI::UIRenderAuthoringSection::Lighting );
          rawSection <= static_cast<int>( UI::UIRenderAuthoringSection::PredictionPaths ); ++rawSection )
    {
        const UI::UIRenderAuthoringSection section = static_cast<UI::UIRenderAuthoringSection>( rawSection );

        const bool commonSection = section == UI::UIRenderAuthoringSection::Lighting ||
                                   section == UI::UIRenderAuthoringSection::Environment;

        const ImGuiTreeNodeFlags flags = commonSection ? ImGuiTreeNodeFlags_DefaultOpen : 0;

        if ( !ImGui::CollapsingHeader( UI::UIRenderAuthoringSectionName( section ), flags ) )
        {
            continue;
        }

        if ( section == UI::UIRenderAuthoringSection::Shadows )
        {
            bool shadows;

            if ( DrawEditableCheckbox( "Enabled##CanonicalShadows", rendering.shadowsEnabled, shadows ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleShadows );
            }
        }
        else if ( section == UI::UIRenderAuthoringSection::Water )
        {
            bool hidden;

            if ( DrawEditableCheckbox( "Hidden", rendering.waterHidden, hidden ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleWaterHidden );
            }

            ImGui::SameLine();
            bool frozen;

            if ( DrawEditableCheckbox( "Freeze", rendering.waterFrozen, frozen ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleWaterFreeze );
            }

            ImGui::SameLine();
            bool flat;

            if ( DrawEditableCheckbox( "Flat", rendering.waterFlat, flat ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleWaterFlat );
            }

            if ( ImGui::Button( rendering.waterReflectionMode == 0
                                    ? "Reflection: raster"
                                    : ( rendering.waterReflectionMode == 1 ? "Reflection: DXR" : "Reflection: off" ) ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::CycleWaterReflection );
            }
        }
        else if ( section == UI::UIRenderAuthoringSection::TerrainMaterials )
        {
            bool terrainHidden;

            if ( DrawEditableCheckbox( "Terrain hidden", rendering.terrainHidden, terrainHidden ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleTerrainHidden );
            }
        }

        ImGui::PushID( rawSection );
        ImGui::SeparatorText( "Ordinary" );

        for ( const UI::RenderSliderSpec& spec : UI::kRenderSliderSpecs )
        {
            if ( spec.section != section )
            {
                continue;
            }

            const int parameter = static_cast<int>( spec.param );
            ImGui::PushID( parameter );
            DrawRenderingParameterEdit( spec.label, UI::OperatorEditorRenderingCommandType::SetOrdinaryParameter, parameter,
                                        rendering.ordinaryParameters[parameter], spec.step, spec.minValue, spec.maxValue,
                                        spec.valueFormat );

            ImGui::PopID();
        }

        if ( section == UI::UIRenderAuthoringSection::PredictionPaths )
        {
            if ( ImGui::SmallButton( "Save path style" ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::SaveOrdinaryDefaults );
            }

            ImGui::PopID();
            continue;
        }

        ImGui::SeparatorText( "Cinematic" );

        for ( const UI::CinematicFeatureSpec& feature : UI::kCinematicFeatureSpecs )
        {
            if ( feature.section != section )
            {
                continue;
            }

            const int parameter = static_cast<int>( feature.feature );
            bool enabled;
            ImGui::PushID( 1000 + parameter );

            if ( DrawEditableCheckbox( feature.label, rendering.cinematicFeatures[parameter], enabled ) )
            {
                submitRendering( UI::OperatorEditorRenderingCommandType::ToggleCinematicFeature, parameter );
            }

            ImGui::PopID();
        }

        for ( const UI::CinematicSliderSpec& spec : UI::kCinematicSliderSpecs )
        {
            if ( spec.section != section )
            {
                continue;
            }

            const int parameter = static_cast<int>( spec.param );
            ImGui::PushID( 2000 + parameter );
            DrawRenderingParameterEdit( spec.label, UI::OperatorEditorRenderingCommandType::SetCinematicParameter, parameter,
                                        rendering.cinematicParameters[parameter], spec.step, spec.minValue, spec.maxValue,
                                        spec.valueFormat );

            ImGui::PopID();
        }

        ImGui::PopID();
    }
}

void ImGuiEditorOwner::DrawDiagnosticsPanel( const UI::OperatorEditorDiagnosticsView& diagnostics,
                                             const UI::OperatorEditorRenderingView& rendering )
{
    if ( !m_showDiagnostics )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Diagnostics, &m_showDiagnostics ) )
    {
        ImGui::End();
        return;
    }
    const auto submitDiagnostics = [&]( UI::OperatorEditorDiagnosticsCommandType type,

                                        uint32_t flag = 0u, int integerValue = 0, float value = 0.0f,
                                        UI::OperatorEditorEditPhase phase = UI::OperatorEditorEditPhase::Commit )
    { SubmitDiagnosticsCommand( { type, flag, integerValue, value, phase } ); };

    // Why: retain domain counters and owner controls here while Tracy
    // remains the sole generic timeline/histogram/percentile surface.
    ImGui::Text( "Tracy %s", m_frameInput.tracyViewerConnected ? "connected" : "waiting" );
    ImGui::TextDisabled( "Timeline, histograms, and percentiles live in Tracy." );

    if ( ImGui::CollapsingHeader( "Physics overlays / pipeline", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        bool collision;

        if ( DrawEditableCheckbox( "Collision visualizer", diagnostics.collisionVisualizer, collision ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleCollisionVisualizer );
        }

        bool transparent;

        if ( DrawEditableCheckbox( "Transparent volumes", diagnostics.physicsDebugTransparent, transparent ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugTransparent );
        }

        bool broadphase;

        if ( DrawEditableCheckbox( "Broadphase grid", diagnostics.broadphaseOverlay, broadphase ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleBroadphaseOverlay );
        }

        const struct
        {
            const char* label;
            uint32_t physicsFlag;
            UI::UIPhysicsDebugOverlay overlay;
        } flagRows[] = {
            { "Axes", Physics::PHYSICS_DEBUG_AXES, UI::UIPhysicsDebugOverlay::Axes },
            { "Contacts", Physics::PHYSICS_DEBUG_CONTACTS, UI::UIPhysicsDebugOverlay::Contacts },
            { "Sleep state", Physics::PHYSICS_DEBUG_SLEEP, UI::UIPhysicsDebugOverlay::Sleep },
            { "Pipeline", Physics::PHYSICS_DEBUG_PIPELINE, UI::UIPhysicsDebugOverlay::Pipeline },
        };

        for ( const auto& row : flagRows )
        {
            bool enabled = ( diagnostics.physicsDebugFlags & row.physicsFlag ) != 0u;
            ImGui::PushID( static_cast<int>( row.physicsFlag ) );

            if ( ImGui::Checkbox( row.label, &enabled ) )
            {
                submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag,
                                   static_cast<uint32_t>( row.overlay ) );
            }

            ImGui::PopID();
        }

        bool tornadoShell;

        if ( DrawEditableCheckbox( "Tornado shell", diagnostics.tornadoVisualShell, tornadoShell ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleTornadoVisualShell );
        }

        bool tornadoVectors;

        if ( DrawEditableCheckbox( "Tornado vectors", diagnostics.tornadoFieldVectors, tornadoVectors ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleTornadoFieldVectors );
        }

        bool ray;

        if ( DrawEditableCheckbox( "Ray casts", diagnostics.rayCastVisualization, ray ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleRayCastVisualization );
        }

        if ( ImGui::Button( "Toggle terrain contact probe" ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::ToggleTerrainContactProbe );
        }

        ImGui::Text( "Pipeline %d / %d: %s", diagnostics.physicsPipelineStageIndex + 1,
                     diagnostics.physicsPipelineStageCount, diagnostics.physicsPipelineStageName );

        if ( ImGui::Button( "Previous stage" ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::StepPhysicsPipelinePrevious );
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Next stage" ) )
        {
            submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::StepPhysicsPipelineNext );
        }

        struct DiagnosticScalar
        {
            const char* label;
            UI::OperatorEditorDiagnosticsCommandType type;
            float value;
            const char* format;
        } diagnosticScalars[] = {
            { "Volume alpha", UI::OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha, diagnostics.physicsDebugAlpha,
              "%.2f" },
            { "Contact linger", UI::OperatorEditorDiagnosticsCommandType::SetPhysicsContactLinger,
              diagnostics.physicsDebugContactLinger, "%.2fs" },
            { "Ray impulse", UI::OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength,
              diagnostics.rayCastImpulseStrength, "%.0f" },
            { "Projectile speed", UI::OperatorEditorDiagnosticsCommandType::SetLauncherProjectileSpeed,
              diagnostics.launcherProjectileSpeed, "%.0f" },
        };

        for ( const DiagnosticScalar& scalar : diagnosticScalars )
        {
            const ImGuiEditorScalarControlPolicy policy = ResolveImGuiEditorDiagnosticsControlPolicy( scalar.type );
            const int action = static_cast<int>( scalar.type );
            ImGui::PushID( action );

            // Why: DragFloat speed controls pointer ergonomics, not
            // canonicalization; App snaps the submitted typed command.
            DrawDiagnosticsScalarEdit( scalar.label, scalar.type, scalar.value, policy.step, policy.minValue,
                                       policy.maxValue, scalar.format );

            ImGui::PopID();
        }
    }

    if ( ImGui::CollapsingHeader( "Renderer", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::Text( "%s | draw %d + UI %d", diagnostics.rendererName, diagnostics.drawCalls, diagnostics.uiDrawCalls );

        ImGui::Text( "Render %.2f ms | GPU %.2f ms | alpha %.3f", diagnostics.renderMs, diagnostics.gpuFrameMs,
                     rendering.presentationAlpha );

        if ( ImGui::CollapsingHeader( "Render Targets" ) )
        {
            for ( int index = 0; index < diagnostics.renderTargetCount; ++index )
            {
                const UI::OperatorEditorRenderTargetView& target = diagnostics.renderTargets[index];
                ImGui::Text( "%s  %dx%d  %s%s%s", target.label, target.width, target.height,
                             target.available ? "ready" : "unavailable", target.depth ? " depth" : "",
                             target.hdr ? " HDR" : "" );
            }

            if ( diagnostics.renderTargetCount == 0 )
            {
                ImGui::TextDisabled( "No render targets are currently published." );
            }
        }
    }

    if ( ImGui::CollapsingHeader( "Engine Memory" ) )
    {
        ImGui::Text( "Tracked %.2f MiB | reconciled %.2f MiB", BytesToMiB( diagnostics.trackedEngineBytes ),
                     BytesToMiB( diagnostics.reconciledTotalBytes ) );

        ImGui::Text( "Upload %.2f / %.2f MiB", BytesToMiB( diagnostics.uploadUsedBytes ),
                     BytesToMiB( diagnostics.uploadCapacityBytes ) );

        ImGui::Text( "Replay reserve growth events %llu",
                     static_cast<unsigned long long>( diagnostics.replayReserveGrowthEvents ) );

        ImGui::TextDisabled( "Cached owner counters only; this panel does not trigger an unbounded scan." );
    }

    if ( ImGui::CollapsingHeader( "Workers" ) )
    {
        const char* preview = diagnostics.workerThreadCount == 0 ? "disabled" : "explicit";

        if ( ImGui::BeginCombo( "Worker threads", preview ) )
        {
            if ( ImGui::Selectable( "Auto", false ) )
            {
                submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::SetWorkerThreads, 0u, -1 );
            }

            if ( ImGui::Selectable( "Disabled", diagnostics.workerThreadCount == 0 ) )
            {
                submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::SetWorkerThreads, 0u, 0 );
            }

            for ( int count = 1; count <= diagnostics.maxWorkerThreadCount; ++count )
            {
                char label[24] = {};

                snprintf( label, sizeof( label ), "%d", count );

                if ( ImGui::Selectable( label, diagnostics.workerThreadCount == count ) )
                {
                    submitDiagnostics( UI::OperatorEditorDiagnosticsCommandType::SetWorkerThreads, 0u, count );
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text( "Current %d | worker core %.2f ms", diagnostics.workerThreadCount, diagnostics.workerCoreTotalMs );
    }

    if ( ImGui::CollapsingHeader( "UI" ) )
    {
        ImGui::Text( "%.1f FPS | CPU %.2f ms | physics %.2f ms", diagnostics.fps, diagnostics.cpuFrameMs,
                     diagnostics.physicsMs );

        ImGui::Text( "Frames %llu | messages %llu | suppressed mouse %llu / keyboard %llu",
                     static_cast<unsigned long long>( m_completedFrames ),
                     static_cast<unsigned long long>( m_platformMessages ),
                     static_cast<unsigned long long>( m_suppressedMouseMessages ),
                     static_cast<unsigned long long>( m_suppressedKeyboardMessages ) );

        ImGui::TextDisabled( "%s", m_tracyLaunchFeedback );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawCausalityPanel( const ImGuiEditorCausalityProjection& causality,
                                           const ReplayOverlay::ReplayOverlayCausalityView& replayCausality )
{
    if ( !m_showCausality )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Causality, &m_showCausality ) )
    {
        ImGui::End();
        return;
    }
    ImGui::Text( "Context: %s", ImGuiEditorCausalityStateName( causality.status.state ) );
    ImGui::SameLine();

    if ( ImGui::SmallButton( "Open Detail" ) )
    {
        m_showCausalityDetail = true;
    }

    if ( causality.status.hasReplayTick )
    {
        ImGui::Text( "Replay tick %llu | prediction %s", static_cast<unsigned long long>( causality.status.replayTick ),
                     ImGuiEditorPredictionStateName( causality.status.predictionState ) );
    }
    else
    {
        ImGui::Text( "Replay tick -- | prediction %s", ImGuiEditorPredictionStateName( causality.status.predictionState ) );
    }

    if ( causality.selected.selectedObjectRow )
    {
        ImGui::SeparatorText( "Selected object" );
        ImGui::Text( "%s  [body %u]", causality.selected.selectedObjectRow->name,
                     causality.selected.selectedObjectRow->id.value );

        ImGui::TextWrapped( "%s", causality.selected.selectedObjectRow->detail );

        ImGui::SeparatorText( "Immediate cause / effect" );

        if ( causality.selected.immediateCauseRow )
        {
            ImGui::Text( "Cause: %s", causality.selected.immediateCauseRow->name );
        }
        else
        {
            ImGui::TextDisabled( "Cause: root or retained live state" );
        }

        if ( causality.selected.selectedRow )
        {
            ImGui::Text( "Effect: %s", causality.selected.selectedRow->name );

            if ( causality.selected.selectedRow->detail[0] != '\0' )
            {
                ImGui::TextWrapped( "%s", causality.selected.selectedRow->detail );
            }
        }

        ImGui::SeparatorText( "Relevant links" );

        std::size_t linkIndex = 0u;

        for ( const RunReplayCauseTreeRow* rowPointer : causality.related.Rows() )
        {
            const RunReplayCauseTreeRow& row = *rowPointer;
            char linkLabel[160] = {};

            sprintf_s( linkLabel, "%s: %s###CauseLink%zu", ImGuiEditorCauseRowKindName( row.kind ), row.name, linkIndex );

            if ( ImGui::Selectable( linkLabel, false ) )
            {
                const std::ptrdiff_t rowIndex = rowPointer - replayCausality.tree.rows.data();
                SubmitReplayCommand( UI::OperatorEditorReplayCommandType::SelectCauseRow, 0.0f,
                                     static_cast<int>( rowIndex ) );
            }

            ++linkIndex;
        }

        if ( causality.status.compactScanTruncated )
        {
            ImGui::TextDisabled( "Bounded compact list; open detail for all %zu rows.", causality.status.totalRowCount );
        }
    }
    else if ( causality.status.state == ImGuiEditorCausalityState::CapacityLimited )
    {
        ImGui::TextWrapped( "The replay explanation exceeded its pre-reserved row capacity. "
                            "The GameUI overlay and editor both fail closed; reduce scene/contact scope." );
    }
    else if ( causality.status.state == ImGuiEditorCausalityState::Stale )
    {
        ImGui::TextWrapped( "The prior causal focus no longer resolves at this replay tick." );
    }
    else
    {
        ImGui::TextDisabled( "Select a replay path target to inspect causes and effects." );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawCausalityDetailPanel( const ImGuiEditorCausalityProjection& causality,
                                                 const ReplayOverlay::ReplayOverlayCausalityView& replayCausality )
{
    if ( !m_showCausalityDetail )
    {
        return;
    }

    ImGui::SetNextWindowSize( ImVec2( 760.0f, 520.0f ), ImGuiCond_FirstUseEver );

    if ( !ImGui::Begin( ImGuiEditorPanel::CausalityDetail, &m_showCausalityDetail ) )
    {
        ImGui::End();
        return;
    }
    const RunReplayCauseTreeState& tree = replayCausality.tree;
    ImGui::Text( "%zu published rows | %s | prediction %s", tree.rows.size(),
                 ImGuiEditorCausalityStateName( causality.status.state ),
                 ImGuiEditorPredictionStateName( causality.status.predictionState ) );

    ImGui::TextDisabled( "Borrowed replay rows; no second tree, full-tree rescan, or scene serialization." );

    if ( m_causalityDetailSelectedRow < 0 || m_causalityDetailSelectedRow >= static_cast<int>( tree.rows.size() ) )
    {
        m_causalityDetailSelectedRow = causality.selected.selectedRowIndex;
    }

    const float detailHeight = m_causalityDetailSelectedRow >= 0 ? 250.0f : -1.0f;

    if ( ImGui::BeginTable( "##CausalityRows", 6,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable,
                            ImVec2( 0.0f, detailHeight ) ) )
    {
        ImGui::TableSetupScrollFreeze( 0, 1 );
        ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, 42.0f );
        ImGui::TableSetupColumn( "Kind", ImGuiTableColumnFlags_WidthFixed, 108.0f );
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 0.23f );
        ImGui::TableSetupColumn( "Parent", ImGuiTableColumnFlags_WidthFixed, 62.0f );
        ImGui::TableSetupColumn( "First tick", ImGuiTableColumnFlags_WidthFixed, 82.0f );
        ImGui::TableSetupColumn( "Detail", ImGuiTableColumnFlags_WidthStretch, 0.77f );
        ImGui::TableHeadersRow();

        // Invariant: the detail panel virtualizes the owner-published
        // rows. Large cause trees cost only the visible table slice and
        // never allocate or copy a replacement hierarchy.
        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int>( tree.rows.size() ) );

        while ( clipper.Step() )
        {
            for ( int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex )
            {
                const RunReplayCauseTreeRow& row = tree.rows[static_cast<std::size_t>( rowIndex )];
                ImGui::PushID( rowIndex );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                char rowLabel[24] = {};

                sprintf_s( rowLabel, "%d", rowIndex );

                if ( ImGui::Selectable( rowLabel, rowIndex == m_causalityDetailSelectedRow,
                                        ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    m_causalityDetailSelectedRow = rowIndex;
                }

                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( ImGuiEditorCauseRowKindName( row.kind ) );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::Indent( static_cast<float>( (std::max)( 0, row.depth ) ) * 10.0f );
                ImGui::TextUnformatted( row.name );
                ImGui::Unindent( static_cast<float>( (std::max)( 0, row.depth ) ) * 10.0f );
                ImGui::TableSetColumnIndex( 3 );
                ImGui::Text( "%u", row.parentId.value );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::Text( "%llu", static_cast<unsigned long long>( row.firstFrame ) );
                ImGui::TableSetColumnIndex( 5 );
                ImGui::TextUnformatted( row.detail );
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    if ( m_causalityDetailSelectedRow >= 0 && m_causalityDetailSelectedRow < static_cast<int>( tree.rows.size() ) )
    {
        const RunReplayCauseTreeRow& row = tree.rows[static_cast<std::size_t>( m_causalityDetailSelectedRow )];
        ImGui::SeparatorText( "Selected row detail" );
        ImGui::Text( "%s | body %u | parent %u | counterpart %u | depth %d", ImGuiEditorCauseRowKindName( row.kind ),
                     row.id.value, row.parentId.value, row.counterpartId.value, row.depth );

        ImGui::TextWrapped( "%s — %s", row.name, row.detail );
        ImGui::Text( "model %d / counterpart %d | contact %d | solver %d | pipeline %d | feature %d", row.modelRow.value,
                     row.counterpartModelRow.value, row.contactIndex, row.solverRowIndex, row.pipelineIndex, row.featureId );

        ImGui::Text( "points %d | penetration %.4f | normal %.4f | tangent %.4f | warm %.4f", row.manifoldPointCount,
                     row.penetration, row.normalImpulse, row.tangentImpulse, row.warmStartImpulse );

        ImGui::Text( "bias %.4f | effective mass %.4f | friction limit %.4f | %s%s", row.bias, row.effectiveMass,
                     row.frictionLimit, row.prediction ? "prediction " : "",
                     row.terrain ? "terrain" : ( row.warmStarted ? "warm-started" : "" ) );

        ImGui::Text( "point %.3f %.3f %.3f | normal %.3f %.3f %.3f | impulse %.3f %.3f %.3f", row.point.x, row.point.y,
                     row.point.z, row.normal.x, row.normal.y, row.normal.z, row.impulse.x, row.impulse.y, row.impulse.z );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawReplayPanel( const UI::OperatorEditorForecastView& forecast,
                                        const ReplayOverlay::ReplayOverlayTimelineView& timeline,
                                        const ReplayOverlay::ReplayOverlayCausalityView& causality )
{
    if ( !m_showReplay )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Replay, &m_showReplay ) )
    {
        ImGui::End();
        return;
    }
    const ReplayPresentationSelection& selection = timeline.selection;
    const bool loaded = selection.loadedPresentation;
    const bool hasRetainedTimeline = loaded ? selection.loadedSampleCount >= 2u : timeline.solverStats.sampleCount >= 2u;

    const bool hasTimeline = hasRetainedTimeline || timeline.predictionTimelineAvailable;
    const bool compact = ImGui::GetContentRegionAvail().x < 1180.0f * m_frameInput.dpiScale;
    const float trackPosition = loaded ? timeline.scrubber.presentationPosition : timeline.scrubber.solverPosition;
    ReplayFrameIndex selectedTick = 0;
    bool hasSelectedTick = false;

    if ( timeline.selectedPrediction )
    {
        selectedTick = timeline.selectedPrediction->frameIndex;
        hasSelectedTick = true;
    }
    else if ( selection.selectedSolver )
    {
        selectedTick = selection.selectedSolver->frameIndex;
        hasSelectedTick = true;
    }
    else if ( selection.selectedPresentation )
    {
        selectedTick = selection.selectedPresentation->frameIndex;
        hasSelectedTick = true;
    }

    const bool recordingMutable = timeline.recordingConfigured && !timeline.recordingLockedByHashLog;
    ImGui::BeginDisabled( !recordingMutable );

    if ( ImGui::Button( timeline.recordingEnabled ? "STOP" : "REC" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::SetRecordingEnabled, 0.0f, -1,
                             !timeline.recordingEnabled );
    }

    ImGui::EndDisabled();

    if ( !recordingMutable && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
    {
        ImGui::SetTooltip( "%s", timeline.recordingLockedByHashLog ? "Hash-log capture is fixed by launch policy"
                                                                   : "Launch with replay enabled to reserve recording" );
    }

    ImGui::SameLine();
    ImGui::BeginDisabled( !hasTimeline );

    if ( ImGui::Button( "|<" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::JumpToStart );
    }

    ImGui::SameLine();

    if ( ImGui::Button( "<" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::StepBackward );
    }

    ImGui::SameLine();
    const char* playPauseLabel = timeline.scrubber.historicalSamplePaused || timeline.scrubber.liveAdvanceHeld
                                     ? ( compact ? ">" : "PLAY" )
                                     : ( compact ? "||" : "PAUSE" );
    ImGui::BeginDisabled( timeline.prediction.controls.enabled );

    if ( ImGui::Button( playPauseLabel ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::TogglePlayPause );
    }

    ImGui::EndDisabled();

    ImGui::SameLine();

    if ( ImGui::Button( ">" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::StepForward );
    }

    ImGui::SameLine();

    if ( ImGui::Button( ">|" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::JumpToEnd );
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if ( ImGui::Button( compact ? "LIVE" : "RETURN LIVE" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::ReturnToLive );
    }

    ImGui::SameLine();

    if ( hasSelectedTick )
    {
        ImGui::TextDisabled( "%s  tick %llu  %zu/%zu", loaded ? "FILE" : "SOLVER",
                             static_cast<unsigned long long>( selectedTick ),
                             loaded ? selection.loadedSampleCount : timeline.solverStats.sampleCount,
                             loaded ? selection.loadedSampleCount : timeline.solverStats.sampleCapacity );
    }
    else
    {
        ImGui::TextDisabled( "%s  tick --  %zu/%zu", loaded ? "FILE" : "SOLVER",
                             loaded ? selection.loadedSampleCount : timeline.solverStats.sampleCount,
                             loaded ? selection.loadedSampleCount : timeline.solverStats.sampleCapacity );
    }

    float scrubPosition = trackPosition;
    ImGui::SetNextItemWidth( (std::max)( 320.0f, ImGui::GetContentRegionAvail().x * ( compact ? 0.62f : 0.72f ) ) );
    ImGui::BeginDisabled( !hasTimeline );

    if ( ImGui::SliderFloat( "##ReplayTransportTrack", &scrubPosition, 0.0f, 1.0f, "%.3f" ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::Scrub, scrubPosition );
    }

    ImGui::EndDisabled();
    const ImVec2 trackMin = ImGui::GetItemRectMin();
    const ImVec2 trackMax = ImGui::GetItemRectMax();
    const float presentPosition = loaded ? 1.0f : std::clamp( selection.solverPresentTrackPosition, 0.0f, 1.0f );

    const float presentX = trackMin.x + ( trackMax.x - trackMin.x ) * presentPosition;

    // Concept: the thin marker exposes the replay owner's live-present
    // boundary without making this panel calculate timeline ranges.
    ImGui::GetWindowDrawList()->AddLine( ImVec2( presentX, trackMin.y - 2.0f ), ImVec2( presentX, trackMax.y + 2.0f ),
                                         IM_COL32( 255, 196, 64, 255 ), 2.0f );

    ImGui::SameLine();
    const char* predictionLabel = timeline.prediction.controls.building
                                      ? ( compact ? "BUILD" : "PREDICTING" )
                                      : ( timeline.prediction.controls.enabled ? "PRED*" : "PRED" );

    ImGui::BeginDisabled( !timeline.prediction.controls.generationPermitted );

    if ( ImGui::Button( predictionLabel ) )
    {
        SubmitReplayCommand( UI::OperatorEditorReplayCommandType::TogglePrediction );
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if ( ImGui::Button( "MORE" ) )
    {
        ImGui::OpenPopup( "##ReplayMore" );
    }

    if ( ImGui::BeginPopup( "##ReplayMore" ) )
    {
        float revealSpeed = static_cast<float>( timeline.prediction.controls.revealSecondsPerSecond );

        if ( ImGui::SliderFloat( "Reveal speed", &revealSpeed,
                                 static_cast<float>( timeline.prediction.controls.revealRateMinimum ),
                                 static_cast<float>( timeline.prediction.controls.revealRateMaximum ), "%.2fx" ) )
        {
            SubmitReplayCommand( UI::OperatorEditorReplayCommandType::SetRevealSpeed, revealSpeed );
        }

        float horizon = timeline.prediction.controls.horizonSeconds;

        if ( ImGui::SliderFloat( "Prediction horizon", &horizon, timeline.predictionHorizonMinimum,
                                 timeline.predictionHorizonMaximum, "%.1fs" ) )
        {
            SubmitReplayCommand( UI::OperatorEditorReplayCommandType::SetPredictionHorizon, horizon );
        }

        ImGui::BeginDisabled( !timeline.scrubber.historicalSamplePaused );

        if ( ImGui::MenuItem( "Restore as branch" ) )
        {
            SubmitReplayCommand( UI::OperatorEditorReplayCommandType::RestoreBranch );
        }

        ImGui::EndDisabled();

        if ( ImGui::MenuItem( "Save replay" ) )
        {
            SubmitReplayCommand( UI::OperatorEditorReplayCommandType::Save );
        }

        if ( ImGui::MenuItem( "Load timeline..." ) )
        {
            SubmitReplayCommand( UI::OperatorEditorReplayCommandType::Load );
        }

        if ( causality.tree.selectedRow >= 0 && causality.tree.selectedRow < static_cast<int>( causality.tree.rows.size() ) )
        {
            const RunReplayCauseTreeRow& row = causality.tree.rows[static_cast<std::size_t>( causality.tree.selectedRow )];

            ImGui::SeparatorText( "Selected cause" );
            ImGui::Text( "%s: %s", ImGuiEditorCauseRowKindName( row.kind ), row.name );
        }

        ImGui::EndPopup();
    }

    if ( timeline.scrubber.saveMessage[0] != '\0' )
    {
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", timeline.scrubber.saveMessage );
    }
    else if ( !hasTimeline )
    {
        ImGui::SameLine();
        ImGui::TextDisabled( "NO REPLAY TIMELINE" );
    }

    ImGui::SeparatorText( "Continuous orbital forecast" );
    bool rollingPredictionEnabled = forecast.active;

    if ( ImGui::Checkbox( "Rolling prediction", &rollingPredictionEnabled ) )
    {
        SubmitForecastCommand( UI::OperatorEditorForecastCommandType::ToggleContinuous );
    }

    ImGui::SameLine();

    if ( ImGui::Button( "Reset forecast" ) )
    {
        SubmitForecastCommand( UI::OperatorEditorForecastCommandType::Reset );
    }

    ImGui::Text( "Simulated %.2fs | sim / real %.1fx | window %.2fs", forecast.simulatedSeconds,
                 forecast.simulatedSecondsPerRealSecond, forecast.rollingWindowAgeSeconds );
    ImGui::Text( "Producer %s / %s | tick %llu | retained %llu bytes", forecast.available ? "available" : "unavailable",
                 forecast.failed ? "failed" : ( forecast.workerInFlight ? "running" : "idle" ),
                 static_cast<unsigned long long>( forecast.newestAbsoluteTick ),
                 static_cast<unsigned long long>( forecast.retainedBytes ) );

    if ( forecast.configured )
    {
        ImGui::Text( "Stability numeric %s | system %s | auxiliary %s", forecast.numericalHealthy ? "ok" : "fail",
                     forecast.systemOrbitalHealthy ? "ok" : "fail", forecast.auxiliaryOrbitalHealthy ? "ok" : "fail" );
    }
    else
    {
        ImGui::TextDisabled( "Stability not started" );
    }

    if ( forecast.firstFailureCause == UI::OperatorEditorForecastCause::None )
    {
        ImGui::TextDisabled( "First cause: none" );
    }
    else
    {
        ImGui::Text( "First cause: %s @ %.2fs (%u/%u)", UI::OperatorEditorForecastCauseName( forecast.firstFailureCause ),
                     forecast.firstFailureSeconds, forecast.firstFailureSubject, forecast.firstFailureOther );
    }

    if ( forecast.energyDriftAvailable )
    {
        ImGui::Text( "Energy drift %.3e (max %.3e)", forecast.energyDrift, forecast.maximumAbsoluteEnergyDrift );
    }
    else
    {
        ImGui::TextDisabled( "Energy drift unavailable" );
    }

    if ( forecast.angularMomentumDriftAvailable )
    {
        ImGui::Text( "Angular-momentum drift %.3e (max %.3e)", forecast.angularMomentumDrift,
                     forecast.maximumAngularMomentumDrift );
    }
    else
    {
        ImGui::TextDisabled( "Angular-momentum drift unavailable" );
    }
    ImGui::End();
}

void ImGuiEditorOwner::DrawStatusPanel( const UI::OperatorEditorLookLabView& lookLab,
                                        const UI::OperatorEditorSceneView& scene, const UI::OperatorEditorToolView& tools )
{
    if ( !m_showStatus )
    {
        return;
    }

    if ( !ImGui::Begin( ImGuiEditorPanel::Status, &m_showStatus,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
    {
        ImGui::End();
        return;
    }
    const float framesPerSecond = m_frameInput.deltaSeconds > 0.0f ? 1.0f / m_frameInput.deltaSeconds : 0.0f;
    ImGui::Text( "%s%s  |  %d objects  |  undo %d / redo %d  |  %.1f FPS  |  Tracy %s",
                 tools.editorModeEnabled ? "EDIT" : "PLAY", tools.placementModeEnabled ? "/PLACE" : "", scene.modelCount,
                 tools.undoDepth, tools.redoDepth, framesPerSecond,
                 m_frameInput.tracyViewerConnected ? "connected" : "waiting" );

    ImGui::Text( "Look Lab: F10 reroll | F11 save | %s%s%s%s", lookLab.detail.data(),
                 lookLab.savePending ? " | capture pending" : "", lookLab.bundleDirectory[0] != '\0' ? " | " : "",
                 lookLab.bundleDirectory[0] != '\0' ? lookLab.bundleDirectory.data() : "" );
    ImGui::End();
}

void ImGuiEditorOwner::ApplyPendingPanelFocus()
{
    if ( m_pendingFocusPanel != ImGuiEditorPanelId::Count )
    {
        const ImGuiEditorPanelId panel = m_pendingFocusPanel;
        const char* panelName = ImGuiEditorPanelName( panel );
        const bool panelVisible = panelName && ( CopyPanelVisibilityMask() & ImGuiEditorPanelBit( panel ) ) != 0u;
        ImGuiWindow* panelWindow = panelVisible ? ImGui::FindWindowByName( panelName ) : nullptr;
        ImGuiContext* context = ImGui::GetCurrentContext();
        const bool windowSubmitted = panelWindow && context && panelWindow->LastFrameActive == context->FrameCount;
        bool focusApplied = false;

        if ( !panelVisible )
        {
            // A close or reset after the request cancels it without claiming a
            // focus completion that could be replayed on a later reopen.
            m_pendingFocusPanel = ImGuiEditorPanelId::Count;
        }
        else if ( windowSubmitted )
        {
            // Focus only after this frame has submitted the named window. The
            // counter is completion evidence, not command-acceptance evidence.
            ImGui::FocusWindow( panelWindow );
            focusApplied = context->NavWindow == panelWindow;
        }

        if ( CanCompleteImGuiPanelFocus( panelVisible, windowSubmitted, focusApplied ) )
        {
            m_pendingFocusPanel = ImGuiEditorPanelId::Count;
            m_lastFocusedPanel = panel;
            ++m_automationFocusCount;
        }
    }
}

ImGuiEditorFrameResult ImGuiEditorOwner::EndFrame()
{
    ImGuiEditorFrameResult result;

    if ( !m_context || !m_frameActive )
    {
        return result;
    }

    ImGui::SetCurrentContext( m_context );
    ImGui::Render();
    m_frameActive = false;
    ++m_completedFrames;
    result.commands = m_frameCommands;
    result.status = m_frameCommandStatus.Take();
    m_frameCommands = {};

    if ( result.status.Ok() )
    {
        const UI::OperatorEditorArbitrationResult
            queued = UI::ArbitrateOperatorEditorCommands( m_resultDiagnostics, m_pendingOperatorEditorCommands,
                                                          result.commands.operatorEditor );

        result.status = queued.status;

        if ( result.status.Ok() )
        {
            m_pendingOperatorEditorCommands = queued.commands;
        }
    }

    return result;
}


ImGuiPreparedDrawDataView ImGuiEditorOwner::PreparedDrawData() noexcept
{
    if ( !m_context )
    {
        return {};
    }

    ImGui::SetCurrentContext( m_context );
    return ImGuiPreparedDrawDataView { m_context, ImGui::GetDrawData() };
}

ImGuiEditorStatus ImGuiEditorOwner::CopyStatus() const noexcept
{
    ImGuiEditorStatus status;
    status.initialized = m_context != nullptr;
    status.visible = m_visible;
    status.frameActive = m_frameActive;
    status.dockingEnabled = m_context != nullptr;
    status.platformViewportsEnabled = false;
    status.selectedSurface = m_selectedSurface;
    status.layoutVersion = LAYOUT_VERSION;
    status.completedFrames = m_completedFrames;
    status.sharedViewFingerprint = m_sharedViewFingerprint;
    status.layoutTopologyFingerprint = m_layoutTopologyFingerprint;
    status.layoutBuildCount = m_layoutBuildCount;
    status.layoutResetCount = m_layoutResetCount;
    status.panelVisibilityMask = CopyPanelVisibilityMask();
    status.appliedDpiScale = m_appliedDpiScale;
    status.automationFocusCount = m_automationFocusCount;
    status.lastFocusedPanel = m_lastFocusedPanel;
    status.preferencesLoaded = m_preferencesLoaded;
    status.preferencesRecovered = m_preferencesRecovered;
    status.preferencesSaveSucceeded = m_preferencesSaveSucceeded;
    status.fontSource = m_fontSource;

    if ( m_renderer )
    {
        const Rendering::Dx12ImGuiRenderStats rendererStats = m_renderer->CopyStats();
        status.rendererBound = rendererStats.initialized;
        status.rendererDescriptorUsed = rendererStats.descriptorUsed;
        status.rendererDescriptorCapacity = rendererStats.descriptorCapacity;
        status.rendererDescriptorHighWater = rendererStats.descriptorHighWater;
        status.rendererRecordedFrames = rendererStats.recordedFrames;
        status.rendererIndexedDraws = rendererStats.indexedDraws;
        status.gameViewportCaptures = rendererStats.gameViewportCaptures;
        status.gameViewportRecreations = rendererStats.gameViewportRecreations;
        status.gameViewportWidth = rendererStats.gameViewportWidth;
        status.gameViewportHeight = rendererStats.gameViewportHeight;
        status.gameViewportAvailable = rendererStats.gameViewportAvailable;
    }

    status.platformMessages = m_platformMessages;
    status.suppressedMouseMessages = m_suppressedMouseMessages;
    status.suppressedKeyboardMessages = m_suppressedKeyboardMessages;
    status.suppressedTextMessages = m_suppressedTextMessages;
    status.focusMessages = m_focusMessages;
    status.dpiMessages = m_dpiMessages;
    status.imeMessages = m_imeMessages;
    return status;
}

void ImGuiEditorOwner::ApplyDpiStyle( float dpiScale )
{
    ImGui::SetCurrentContext( m_context );
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark( &style );

    // Concept: one restrained editor palette distinguishes content, selected
    // state, warnings, and interactive controls without domain-specific skins.
    style.WindowPadding = ImVec2( 8.0f, 8.0f );
    style.FramePadding = ImVec2( 7.0f, 4.0f );
    style.ItemSpacing = ImVec2( 7.0f, 5.0f );
    style.ItemInnerSpacing = ImVec2( 5.0f, 4.0f );
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.WindowRounding = 3.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.DisabledAlpha = 0.48f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4( 0.055f, 0.064f, 0.080f, 1.0f );
    colors[ImGuiCol_ChildBg] = ImVec4( 0.065f, 0.075f, 0.092f, 1.0f );
    colors[ImGuiCol_PopupBg] = ImVec4( 0.075f, 0.086f, 0.105f, 0.98f );
    colors[ImGuiCol_Border] = ImVec4( 0.22f, 0.25f, 0.30f, 0.75f );
    colors[ImGuiCol_FrameBg] = ImVec4( 0.105f, 0.12f, 0.15f, 1.0f );
    colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.16f, 0.22f, 0.29f, 1.0f );
    colors[ImGuiCol_FrameBgActive] = ImVec4( 0.18f, 0.29f, 0.40f, 1.0f );
    colors[ImGuiCol_TitleBg] = ImVec4( 0.075f, 0.088f, 0.11f, 1.0f );
    colors[ImGuiCol_TitleBgActive] = ImVec4( 0.11f, 0.17f, 0.23f, 1.0f );
    colors[ImGuiCol_Button] = ImVec4( 0.12f, 0.20f, 0.28f, 1.0f );
    colors[ImGuiCol_ButtonHovered] = ImVec4( 0.16f, 0.34f, 0.48f, 1.0f );
    colors[ImGuiCol_ButtonActive] = ImVec4( 0.11f, 0.43f, 0.61f, 1.0f );
    colors[ImGuiCol_Header] = ImVec4( 0.11f, 0.25f, 0.36f, 1.0f );
    colors[ImGuiCol_HeaderHovered] = ImVec4( 0.15f, 0.38f, 0.52f, 1.0f );
    colors[ImGuiCol_HeaderActive] = ImVec4( 0.10f, 0.48f, 0.66f, 1.0f );
    colors[ImGuiCol_CheckMark] = ImVec4( 0.25f, 0.76f, 0.96f, 1.0f );
    colors[ImGuiCol_SliderGrab] = ImVec4( 0.20f, 0.64f, 0.86f, 1.0f );
    colors[ImGuiCol_SliderGrabActive] = ImVec4( 0.32f, 0.82f, 1.0f, 1.0f );
    colors[ImGuiCol_Tab] = ImVec4( 0.09f, 0.13f, 0.17f, 1.0f );
    colors[ImGuiCol_TabHovered] = ImVec4( 0.15f, 0.37f, 0.51f, 1.0f );
    colors[ImGuiCol_TabSelected] = ImVec4( 0.12f, 0.28f, 0.39f, 1.0f );
    colors[ImGuiCol_DockingPreview] = ImVec4( 0.20f, 0.66f, 0.92f, 0.70f );
    colors[ImGuiCol_NavCursor] = ImVec4( 0.42f, 0.84f, 1.0f, 1.0f );
    style.ScaleAllSizes( dpiScale );
    style.FontSizeBase = EDITOR_FONT_SIZE_PIXELS;
    style.FontScaleDpi = dpiScale;
    m_appliedDpiScale = dpiScale;
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
