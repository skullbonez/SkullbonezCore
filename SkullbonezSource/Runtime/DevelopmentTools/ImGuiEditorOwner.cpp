/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
Purpose:
  Owns Dear ImGui startup, deterministic editor-shell frames, and orderly shutdown.

Summary:
  The owner installs the engine's hard-capped DearImGui allocator, creates one
  context, selects a readable scalable font, configures single-window docking,
  and translates immutable display facts into balanced ImGui frames. It
  delegates DX12 device resources and draw recording to the concrete renderer
  owner and delegates native event translation to the pinned Win32 backend.

Glossary:
  Embedded vector fallback: Dear ImGui's pinned, scalable built-in font used
    when the optional repository font asset is absent or invalid.
  Dock shell: Versioned single-window host with deterministic editor, viewport,
    utility, replay, and status regions.
  DPI style epoch: Fresh base style rescaled from 1.0 whenever monitor scale
    changes, avoiding cumulative rounding drift.

Invariants:
  - All vendor allocation and deallocation callbacks retain DearImGui owner
    attribution even when invoked from inside vendor code.
  - The context is current only for synchronous calls made by this owner.
  - A hidden or zero-sized editor performs no ImGui frame work.
  - Shutdown balances an active frame before destroying the context.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h
  - SkullbonezSource/Runtime/RunFrame.cpp
  - ThirdPtySource/imgui/imgui.h
*/
#include "ImGuiEditorOwner.h"

#include "../Allocation/DevelopmentToolAllocation.h"
#include "../../Core/FatalError.h"
#include "../../Rendering/DX12/Dx12ImGuiRendererOwner.h"
#include "../../UI/UITabEditor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// The pinned backend intentionally hides this declaration behind #if 0 to
// avoid forcing Windows types on every includer. This source already owns the
// Win32 ABI boundary, so repeat the vendor-prescribed declaration here.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM wParam, LPARAM lParam );

namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr const char* IMGUI_LAYOUT_PATH = "imgui_editor_layout_v2.ini";
constexpr const char* OPTIONAL_EDITOR_FONT_PATH = "SkullbonezData/fonts/SkoreEditor-Regular.ttf";
constexpr float EDITOR_FONT_SIZE_PIXELS = 16.0f;
constexpr float MIN_DPI_SCALE = 0.75f;
constexpr float MAX_DPI_SCALE = 4.0f;
constexpr float MIN_DELTA_SECONDS = 1.0f / 1000.0f;
constexpr float MAX_DELTA_SECONDS = 0.25f;

void* AllocateImGuiMemory( size_t size, void* ) noexcept
{
    return RuntimeAllocation::AllocateDevelopmentToolMemory(
        RuntimeAllocation::DevelopmentToolAllocationOwner::DearImGui,
        size );
}

void FreeImGuiMemory( void* pointer, void* ) noexcept
{
    RuntimeAllocation::FreeDevelopmentToolMemory( RuntimeAllocation::DevelopmentToolAllocationOwner::DearImGui,
                                                  pointer );
}

bool IsReadableFile( const char* path ) noexcept
{
    const DWORD attributes = GetFileAttributesA( path );
    return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0u;
}

bool ResolveTracyViewerPath( char* output, size_t outputCapacity ) noexcept
{
    constexpr const char* candidates[] = {
        "TestOutput/validation/tracy_profiler/Release/tracy-profiler.exe",
        "ThirdPtySource/tracy/profiler/build/Release/tracy-profiler.exe",
        "ThirdPtySource/tracy/profiler/build/tracy-profiler.exe",
    };
    for ( const char* candidate : candidates )
    {
        if ( IsReadableFile( candidate ) )
        {
            strcpy_s( output, outputCapacity, candidate );
            return true;
        }
    }
    const DWORD length =
        SearchPathA( nullptr, "tracy-profiler.exe", nullptr, static_cast<DWORD>( outputCapacity ), output, nullptr );
    return length > 0u && length < outputCapacity;
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

} // namespace

namespace SkullbonezCore::Runtime::DevelopmentTools
{
ImGuiEditorOwner::~ImGuiEditorOwner()
{
    Shutdown();
}

SkullbonezCore::Core::SbResult ImGuiEditorOwner::Start( HWND window, Rendering::Dx12ImGuiRendererOwner* renderer )
{
    if ( m_context )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( !window )
    {
        return SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
                                                        "Win32 ImGui startup requires the runtime HWND" );
    }
    if ( !renderer )
    {
        return SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
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
        // Lane F: a development editor context that cannot establish its sole
        // owner cannot safely continue into frame calls.
        SB_FATAL( "DevelopmentTools/ImGui", "Dear ImGui context creation failed." );
    }
    ImGui::SetCurrentContext( m_context );

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.IniFilename = IMGUI_LAYOUT_PATH;

    // Why: the repository intentionally carries no extra font license in E5.
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
        return SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
                                                        "Pinned Win32 backend initialization failed" );
    }
    m_platformBackendInitialized = true;
    m_window = window;

    ApplyDpiStyle( 1.0f );
    const SkullbonezCore::Core::SbResult rendererResult = renderer->BindContext( *m_context );
    if ( !rendererResult.ok )
    {
        return rendererResult;
    }
    m_renderer = renderer;
    m_layoutTopologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    m_tracyViewerAvailable = ResolveTracyViewerPath( m_tracyViewerPath, sizeof( m_tracyViewerPath ) );
    snprintf(
        m_tracyLaunchFeedback,
        sizeof( m_tracyLaunchFeedback ),
        "%s",
        m_tracyViewerAvailable ? "Pinned Tracy viewer ready" : "Pinned Tracy viewer is not built on this machine" );
    printf( "[imgui] Context ready layout_version=%d font=%s docking=on platform_viewports=off win32=bound.\n",
            LAYOUT_VERSION,
            m_fontSource == ImGuiEditorFontSource::Asset ? "asset" : "embedded_vector_fallback" );
    return SkullbonezCore::Core::SbResult::Success();
}

void ImGuiEditorOwner::Shutdown() noexcept
{
    if ( !m_context )
    {
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
    m_nativePointerStateTouched = false;
    m_lastPlatformMouseCursor = -2;
    m_appliedDpiScale = 0.0f;
    m_frameInput = {};
    m_fontSource = ImGuiEditorFontSource::None;
    printf( "[imgui] Context shutdown completed_frames=%llu messages=%llu suppressed_mouse=%llu "
            "suppressed_keyboard=%llu suppressed_text=%llu focus=%llu dpi=%llu ime=%llu.\n",
            static_cast<unsigned long long>( completedFrames ),
            static_cast<unsigned long long>( m_platformMessages ),
            static_cast<unsigned long long>( m_suppressedMouseMessages ),
            static_cast<unsigned long long>( m_suppressedKeyboardMessages ),
            static_cast<unsigned long long>( m_suppressedTextMessages ),
            static_cast<unsigned long long>( m_focusMessages ),
            static_cast<unsigned long long>( m_dpiMessages ),
            static_cast<unsigned long long>( m_imeMessages ) );
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
        io.ClearInputMouse();
    }
    m_visible = visible;
    if ( !visible )
    {
        m_gameViewportHovered = false;
        m_gameViewportFocused = false;
    }
}

bool ImGuiEditorOwner::IsVisible() const noexcept
{
    return m_visible;
}

void ImGuiEditorOwner::InitializeSurfacePreferences( bool legacyVisible, bool editorVisible ) noexcept
{
    if ( m_surfacePreferencesInitialized )
    {
        return;
    }
    m_surfacePreferencesInitialized = true;
    m_legacySurfaceVisible = legacyVisible;
    SetVisible( editorVisible );
}

void ImGuiEditorOwner::SetLegacySurfaceVisible( bool visible ) noexcept
{
    m_surfacePreferencesInitialized = true;
    m_legacySurfaceVisible = visible;
}

bool ImGuiEditorOwner::LegacySurfaceVisible() const noexcept
{
    return m_legacySurfaceVisible;
}

UI::OperatorEditorCommandQueues ImGuiEditorOwner::ConsumeOperatorEditorCommands() noexcept
{
    const UI::OperatorEditorCommandQueues commands = m_pendingOperatorEditorCommands;
    m_pendingOperatorEditorCommands = {};
    return commands;
}

ImGuiEditorNativeMessageRoute
ImGuiEditorOwner::HandleNativeMessage( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) noexcept
{
    ImGuiEditorNativeMessageRoute route;
    route.messageClass = ClassifyImGuiEditorNativeMessage( message, wParam );
    if ( !m_context || !m_platformBackendInitialized || window != m_window )
    {
        return route;
    }
    if ( !m_visible && route.messageClass != ImGuiEditorMessageClass::Platform )
    {
        // Invariant: Legacy mode retains its native input/cursor behavior.
        // Focus, DPI, display, and device messages continue keeping the dormant
        // backend synchronized for a later explicit switch to ImGui/Both.
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
    return EvaluateImGuiEditorInputCapture( ImGuiEditorInputIntent{ m_visible,
                                                                    io.WantCaptureMouse,
                                                                    io.WantCaptureKeyboard,
                                                                    io.WantTextInput,
                                                                    m_gameViewportHovered,
                                                                    m_gameViewportFocused } );
}

ImGuiEditorInputFrameState ImGuiEditorOwner::ConsumeInputFrameState() noexcept
{
    ImGuiEditorInputFrameState state;
    state.capture = CopyInputCapture();
    state.nativePointerStateTouched = m_nativePointerStateTouched;
    m_nativePointerStateTouched = false;
    return state;
}

void ImGuiEditorOwner::SetGameViewportInputState( bool hovered, bool focused ) noexcept
{
    // Concept: E10 will publish the central image item's hover/focus result
    // through this value seam. E7 establishes the policy now so later panels do
    // not invent a second input path or special-case gameplay callbacks.
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
        // Lane F: nested frames corrupt ImGui retained stacks and settings.
        SB_FATAL( "DevelopmentTools/ImGui", "BeginFrame called while an editor frame is already active." );
    }

    ImGui::SetCurrentContext( m_context );
    const float dpiScale = std::clamp( input.dpiScale, MIN_DPI_SCALE, MAX_DPI_SCALE );
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
    m_showRenderingAudio = true;
    m_showDiagnostics = true;
    m_showCausality = true;
    m_showReplay = true;
    m_showStatus = true;
}

void ImGuiEditorOwner::BuildDefaultDockLayout( uint32_t rootDockId, float width, float height, bool requestedReset )
{
    const ImGuiID root = static_cast<ImGuiID>( rootDockId );
    const ImGuiEditorLayoutEnvelope envelope =
        ResolveImGuiEditorLayoutEnvelope( static_cast<int>( width ), static_cast<int>( height ) );

    // Invariant: remove the complete versioned tree before replaying this fixed
    // split order. DockBuilder therefore cannot preserve a corrupt or partial
    // child graph across an explicit reset.
    ImGui::DockBuilderRemoveNode( root );
    ImGui::DockBuilderAddNode( root, ImGuiDockNodeFlags_DockSpace );
    ImGui::DockBuilderSetNodeSize( root, ImVec2( width, height ) );

    ImGuiID upper = root;
    const ImGuiID status =
        ImGui::DockBuilderSplitNode( upper, ImGuiDir_Down, envelope.statusSplitFraction, nullptr, &upper );
    const ImGuiID replay =
        ImGui::DockBuilderSplitNode( upper, ImGuiDir_Down, envelope.replaySplitFraction, nullptr, &upper );
    const ImGuiID editorLeft =
        ImGui::DockBuilderSplitNode( upper, ImGuiDir_Left, envelope.editorLeftSplitFraction, nullptr, &upper );
    const ImGuiID utilityRight =
        ImGui::DockBuilderSplitNode( upper, ImGuiDir_Right, envelope.utilityRightSplitFraction, nullptr, &upper );
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
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::RenderingAudio, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Diagnostics, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Causality, utilityTabs );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Replay, replay );
    ImGui::DockBuilderDockWindow( ImGuiEditorPanel::Status, status );
    ImGui::DockBuilderFinish( root );
    ImGui::MarkIniSettingsDirty();

    ResetDefaultPanelVisibility();
    m_layoutTopologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    ++m_layoutBuildCount;
    if ( requestedReset )
    {
        ++m_layoutResetCount;
    }
    printf( "[imgui-layout] version=%d reason=%s fingerprint=%llu viewport=%dx%d left=%d right=%d replay=%d "
            "status=%d.\n",
            LAYOUT_VERSION,
            requestedReset ? "operator_reset" : "missing_or_version_mismatch",
            static_cast<unsigned long long>( m_layoutTopologyFingerprint ),
            envelope.viewportWidth,
            envelope.upperHeight,
            envelope.editorLeftWidth,
            envelope.utilityRightWidth,
            envelope.replayHeight,
            envelope.statusHeight );
}

void ImGuiEditorOwner::BuildEditorShell( const UI::OperatorEditorFrameView& view )
{
    if ( !m_frameActive )
    {
        return;
    }
    m_sharedViewFingerprint = UI::FingerprintOperatorEditorFrameView( view );

    const auto submit = [&]( auto& queue, const auto& command )
    {
        if ( m_frameCommandStatus.ok )
        {
            m_frameCommandStatus = UI::SubmitOperatorEditorCommand( queue, command );
        }
    };
    const auto submitTool =
        [&]( UI::OperatorEditorToolCommandType type, uint32_t sceneObjectId = 0u, int value = 0, bool enabled = false )
    {
        submit( m_frameCommands.operatorEditor.tools,
                UI::OperatorEditorToolCommand{ type, sceneObjectId, value, enabled } );
    };
    const auto launchTracyViewer = [&]()
    {
        if ( !m_tracyViewerAvailable )
        {
            return;
        }
        const HINSTANCE launch = ShellExecuteA( m_window, "open", m_tracyViewerPath, nullptr, nullptr, SW_SHOWNORMAL );
        const intptr_t launchCode = reinterpret_cast<intptr_t>( launch );
        snprintf( m_tracyLaunchFeedback,
                  sizeof( m_tracyLaunchFeedback ),
                  "%s",
                  launchCode > 32 ? "Tracy viewer launched; connection is automatic"
                                  : "Tracy viewer launch failed; rebuild or inspect the pinned executable" );
    };

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->WorkPos );
    ImGui::SetNextWindowSize( viewport->WorkSize );
    ImGui::SetNextWindowViewport( viewport->ID );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    constexpr ImGuiWindowFlags shellFlags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    const bool shellVisible = ImGui::Begin( "Skore Editor###SkoreEditorShellV2", nullptr, shellFlags );
    ImGui::PopStyleVar( 3 );
    if ( !shellVisible )
    {
        ImGui::End();
        return;
    }

    bool selectedEntityLocked = false;
    for ( uint32_t index = 0u; index < view.hierarchy.rowCount; ++index )
    {
        if ( view.hierarchy.rows[index].sceneObjectId == view.hierarchy.selectedSceneObjectId )
        {
            selectedEntityLocked = view.hierarchy.rows[index].locked;
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
            if ( ImGui::MenuItem( "Save Scene", "Ctrl+S", false, view.scene.canSaveCurrentScene ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::SaveCurrentScene } );
            }
            if ( !view.scene.canSaveCurrentScene )
            {
                DrawDisabledReason( "The generated demo has no authored scene path" );
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Reset Current Scene" ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::ResetCurrentScene, -1 } );
            }
            if ( ImGui::MenuItem( "Reset To Authored Defaults", nullptr, false, view.scene.canSaveCurrentScene ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::ResetSceneDefaults } );
            }
            if ( ImGui::MenuItem( "Hide Editor" ) )
            {
                m_frameCommands.requestHide = true;
                if ( !m_legacySurfaceVisible )
                {
                    m_legacySurfaceVisible = true;
                    m_frameCommands.requestLegacyVisibility = true;
                    m_frameCommands.requestedLegacyVisible = true;
                }
            }
            ImGui::EndMenu();
        }
        if ( ImGui::BeginMenu( "Edit" ) )
        {
            if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, view.tools.editorModeEnabled && view.tools.undoDepth > 0 ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::Undo );
            }
            if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, view.tools.editorModeEnabled && view.tools.redoDepth > 0 ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::Redo );
            }
            const bool hasSelection = view.hierarchy.selectedSceneObjectId != 0u;
            const bool hasMutableSelection = hasSelection && !selectedEntityLocked;
            if ( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, view.tools.editorModeEnabled && hasMutableSelection ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::DuplicateSelection );
            }
            if ( ImGui::MenuItem( "Delete", "Del", false, view.tools.editorModeEnabled && hasMutableSelection ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::DeleteSelection );
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( view.tools.editorModeEnabled ? "Exit Edit Mode" : "Enter Edit Mode", "`" ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::ToggleEditorMode );
            }
            if ( ImGui::MenuItem( "Placement Mode",
                                  "E",
                                  view.tools.placementModeEnabled,
                                  view.tools.editorModeEnabled ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::TogglePlacementMode );
            }
            ImGui::EndMenu();
        }
        if ( ImGui::BeginMenu( "View" ) )
        {
            bool legacyVisible = m_legacySurfaceVisible;
            if ( ImGui::MenuItem( "Legacy Surface", "0", &legacyVisible ) )
            {
                m_legacySurfaceVisible = legacyVisible;
                m_frameCommands.requestLegacyVisibility = true;
                m_frameCommands.requestedLegacyVisible = legacyVisible;
            }
            ImGui::Separator();
            ImGui::MenuItem( ImGuiEditorPanel::SceneAndModes, nullptr, &m_showSceneAndModes );
            ImGui::MenuItem( ImGuiEditorPanel::Hierarchy, nullptr, &m_showHierarchy );
            ImGui::MenuItem( ImGuiEditorPanel::AssetsCreate, nullptr, &m_showAssetsCreate );
            ImGui::MenuItem( ImGuiEditorPanel::GameViewport, nullptr, &m_showGameViewport );
            ImGui::MenuItem( ImGuiEditorPanel::Inspector, nullptr, &m_showInspector );
            ImGui::MenuItem( ImGuiEditorPanel::WorldSimulation, nullptr, &m_showWorldSimulation );
            ImGui::MenuItem( "Rendering / Audio", nullptr, &m_showRenderingAudio );
            ImGui::MenuItem( "Diagnostics", nullptr, &m_showDiagnostics );
            ImGui::MenuItem( "Causality", nullptr, &m_showCausality );
            ImGui::MenuItem( ImGuiEditorPanel::Replay, nullptr, &m_showReplay );
            ImGui::MenuItem( ImGuiEditorPanel::Status, nullptr, &m_showStatus );
            ImGui::Separator();
            if ( ImGui::MenuItem( "Reset Editor Layout" ) )
            {
                m_layoutResetRequested = true;
            }
            ImGui::EndMenu();
        }
        if ( ImGui::BeginMenu( "Debug" ) )
        {
            ImGui::TextDisabled( "Tracy: %s",
                                 m_frameInput.tracyViewerConnected
                                     ? "connected"
                                     : ( m_frameInput.tracyInitialized ? "waiting for viewer" : "disabled" ) );
            if ( ImGui::MenuItem( "Launch Tracy Viewer", nullptr, false, m_tracyViewerAvailable ) )
            {
                launchTracyViewer();
            }
            if ( !m_tracyViewerAvailable )
            {
                DrawDisabledReason( "Build the pinned Tracy profiler or place tracy-profiler.exe on PATH" );
            }
            ImGui::TextDisabled( "%s", m_tracyLaunchFeedback );
            ImGui::Separator();
            ImGui::TextDisabled( "Layout v%d / %llu",
                                 LAYOUT_VERSION,
                                 static_cast<unsigned long long>( m_layoutTopologyFingerprint ) );
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImGuiEditorLayoutEnvelope shellEnvelope =
        ResolveImGuiEditorLayoutEnvelope( static_cast<int>( ImGui::GetContentRegionAvail().x ),
                                          static_cast<int>( ImGui::GetContentRegionAvail().y ) );
    const char* modeLabel = view.tools.editorModeEnabled ? "EDIT" : "PLAY";
    const char* placementLabel = view.tools.placementModeEnabled
                                     ? ( shellEnvelope.compactToolbarLabels ? "PLACE*" : "PLACEMENT ACTIVE" )
                                     : ( shellEnvelope.compactToolbarLabels ? "PLACE" : "PLACEMENT" );
    const float toolbarHeight = 34.0f * m_frameInput.dpiScale;
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 5.0f ) );
    if ( ImGui::BeginChild( "##SkoreEditorToolbar", ImVec2( 0.0f, toolbarHeight ), ImGuiChildFlags_Borders ) )
    {
        if ( ImGui::Button( modeLabel ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::ToggleEditorMode );
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( !view.tools.editorModeEnabled );
        if ( ImGui::Button( placementLabel ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::TogglePlacementMode );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( !view.tools.editorModeEnabled || view.tools.undoDepth <= 0 );
        if ( ImGui::Button( "UNDO" ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::Undo );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( !view.tools.editorModeEnabled || view.tools.redoDepth <= 0 );
        if ( ImGui::Button( "REDO" ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::Redo );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::Button( view.tools.crossScenePauseLocked ? "PLAY" : "PAUSE" ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::ToggleCrossScenePause );
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( !view.tools.crossScenePauseLocked );
        if ( ImGui::Button( "STEP" ) )
        {
            submitTool( UI::OperatorEditorToolCommandType::StepPausedScene );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "%s  |  frame %d  |  %.2fx",
                             SceneDisplayName( view.scene.sceneName ),
                             view.scene.currentFrame,
                             view.scene.timeScale );
        ImGui::SameLine();
        ImGui::BeginDisabled( !m_tracyViewerAvailable );
        if ( ImGui::Button( m_frameInput.tracyViewerConnected ? "TRACY CONNECTED" : "OPEN TRACY" ) )
        {
            launchTracyViewer();
        }
        ImGui::EndDisabled();
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

    if ( m_showSceneAndModes )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::SceneAndModes, &m_showSceneAndModes ) )
        {
            ImGui::TextUnformatted( "MODE" );
            if ( ImGui::Button( view.tools.editorModeEnabled ? "EDIT ACTIVE" : "ENTER EDIT" ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::ToggleEditorMode );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !view.tools.editorModeEnabled );
            if ( ImGui::Button( view.tools.placementModeEnabled ? "PLACE ACTIVE" : "PLACE" ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::TogglePlacementMode );
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if ( ImGui::Button( view.tools.crossScenePauseLocked ? "RESUME" : "PAUSE" ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::ToggleCrossScenePause );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !view.tools.crossScenePauseLocked );
            if ( ImGui::Button( "STEP" ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::StepPausedScene );
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText( "Scene" );
            ImGui::Text( "%s%s", SceneDisplayName( view.scene.sceneName ), view.scene.dirty ? "  * modified" : "" );
            if ( m_focusSceneFilter )
            {
                ImGui::SetKeyboardFocusHere();
                m_focusSceneFilter = false;
            }
            ImGui::InputTextWithHint( "##SceneFilter", "Filter scenes", m_sceneFilter, sizeof( m_sceneFilter ) );
            const char* activeScene = SceneDisplayName( view.scene.sceneName );
            if ( ImGui::BeginCombo( "Active", activeScene ) )
            {
                bool anyVisible = false;
                for ( int index = 0; index < view.scene.sceneCount && view.scene.sceneOptions; ++index )
                {
                    const char* label =
                        view.scene.sceneOptions[index] ? view.scene.sceneOptions[index] : "Unnamed scene";
                    if ( !ContainsAsciiInsensitive( label, m_sceneFilter ) )
                    {
                        continue;
                    }
                    anyVisible = true;
                    if ( ImGui::Selectable( label, index == view.scene.currentSceneIndex ) )
                    {
                        submit(
                            m_frameCommands.operatorEditor.scene,
                            UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::SetCurrentSceneIndex,
                                                            index } );
                    }
                }
                if ( !anyVisible )
                {
                    ImGui::TextDisabled( "No matching scenes" );
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled( !view.scene.canSaveCurrentScene );
            if ( ImGui::Button( "Save" ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::SaveCurrentScene } );
            }
            ImGui::EndDisabled();
            if ( !view.scene.canSaveCurrentScene )
            {
                DrawDisabledReason( "Generated demo scenes have no authored save path" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Reset Current Scene" ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::ResetCurrentScene, -1 } );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !view.scene.canSaveCurrentScene );
            if ( ImGui::Button( "Defaults" ) )
            {
                submit( m_frameCommands.operatorEditor.scene,
                        UI::OperatorEditorSceneCommand{ UI::OperatorEditorSceneCommandType::ResetSceneDefaults } );
            }
            ImGui::EndDisabled();

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
                submit( m_frameCommands.operatorEditor.scene, create );
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled( "Undo %d  |  Redo %d  |  %s",
                                 view.tools.undoDepth,
                                 view.tools.redoDepth,
                                 view.scene.dirty ? "unsaved edits" : "clean" );
        }
        ImGui::End();
    }

    if ( m_showHierarchy )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Hierarchy, &m_showHierarchy ) )
        {
            ImGui::Text( "%u / %u scene objects", view.hierarchy.rowCount, view.hierarchy.totalRowCount );
            ImGui::InputTextWithHint( "##HierarchyFilter",
                                      "Filter by name",
                                      m_hierarchyFilter,
                                      sizeof( m_hierarchyFilter ) );
            ImGui::Separator();
            ImGui::BeginChild( "##HierarchyRows",
                               ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ),
                               ImGuiChildFlags_Borders );
            bool anyVisible = false;
            for ( uint32_t index = 0u; index < view.hierarchy.rowCount; ++index )
            {
                const UI::OperatorEditorHierarchyRow& row = view.hierarchy.rows[index];
                if ( !ContainsAsciiInsensitive( row.displayName, m_hierarchyFilter ) )
                {
                    continue;
                }

                anyVisible = true;
                ImGui::PushID( static_cast<int>( row.sceneObjectId ) );
                if ( ImGui::SmallButton( row.visible ? "eye" : "hidden" ) )
                {
                    submitTool( UI::OperatorEditorToolCommandType::SetEntityVisible,
                                row.sceneObjectId,
                                0,
                                !row.visible );
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( row.locked ? "locked" : "open" ) )
                {
                    submitTool( UI::OperatorEditorToolCommandType::SetEntityLocked, row.sceneObjectId, 0, !row.locked );
                }
                ImGui::SameLine();
                const char* displayName =
                    row.displayName && row.displayName[0] != '\0' ? row.displayName : "Unnamed object";
                if ( ImGui::Selectable( displayName, row.selected, ImGuiSelectableFlags_AllowDoubleClick ) )
                {
                    submitTool( UI::OperatorEditorToolCommandType::SelectSceneObject, row.sceneObjectId );
                }
                if ( ImGui::BeginPopupContextItem( "##HierarchyContext" ) )
                {
                    if ( ImGui::MenuItem( "Select" ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::SelectSceneObject, row.sceneObjectId );
                    }
                    if ( ImGui::MenuItem( row.visible ? "Hide" : "Show" ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::SetEntityVisible,
                                    row.sceneObjectId,
                                    0,
                                    !row.visible );
                    }
                    if ( ImGui::MenuItem( row.locked ? "Unlock" : "Lock" ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::SetEntityLocked,
                                    row.sceneObjectId,
                                    0,
                                    !row.locked );
                    }
                    ImGui::BeginDisabled( row.locked || !row.selected );
                    if ( ImGui::MenuItem( "Duplicate" ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::DuplicateSelection );
                    }
                    if ( ImGui::MenuItem( "Delete" ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::DeleteSelection );
                    }
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                }
                if ( ImGui::IsItemHovered() && row.assetBacked )
                {
                    ImGui::SetTooltip( "Registered asset group root %u, part %d",
                                       row.groupRootObjectId,
                                       row.groupPartIndex );
                }
                ImGui::PopID();
            }
            if ( !anyVisible )
            {
                ImGui::TextDisabled( "No matching scene objects" );
            }
            ImGui::EndChild();
            if ( view.hierarchy.truncated )
            {
                ImGui::TextDisabled( "Showing the first %u objects; narrow the filter after simplifying the scene.",
                                     view.hierarchy.rowCount );
            }
            else
            {
                ImGui::TextDisabled( "Single selection uses stable scene identity; asset groups select as one root." );
            }
        }
        ImGui::End();
    }

    if ( m_showAssetsCreate )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::AssetsCreate, &m_showAssetsCreate ) )
        {
            ImGui::InputTextWithHint( "##AssetFilter", "Search assets", m_assetFilter, sizeof( m_assetFilter ) );
            bool placeStatic = view.tools.placeStaticObject;
            if ( ImGui::Checkbox( "Static", &placeStatic ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::SetPlaceStatic, 0u, 0, placeStatic );
            }
            ImGui::SameLine();
            bool terrainAlign = view.tools.autoTerrainAlign;
            if ( ImGui::Checkbox( "Align to terrain", &terrainAlign ) )
            {
                submitTool( UI::OperatorEditorToolCommandType::ToggleTerrainAlign );
            }
            ImGui::Separator();
            ImGui::BeginChild( "##AssetRows",
                               ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ),
                               ImGuiChildFlags_Borders );
            const char* previousCategory = nullptr;
            bool anyVisible = false;
            for ( int objectType = 0; objectType < view.assets.objectTypeCount; ++objectType )
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

                const bool registeredUnavailable =
                    objectType >= UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP && !view.assets.registeredLibraryAvailable;
                ImGui::PushID( objectType );
                ImGui::BeginDisabled( registeredUnavailable );
                if ( ImGui::Selectable( label, objectType == view.assets.selectedObjectType ) )
                {
                    submitTool( UI::OperatorEditorToolCommandType::SetPlacementObjectType, 0u, objectType );
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
        }
        ImGui::End();
    }

    bool gameViewportHovered = false;
    bool gameViewportFocused = false;
    if ( m_showGameViewport )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::GameViewport, &m_showGameViewport ) )
        {
            gameViewportHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
            gameViewportFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 dropOrigin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton( "##GameViewportDropSurface", available );
            if ( ImGui::BeginDragDropTarget() )
            {
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "SKORE_ASSET_OBJECT_TYPE" ) )
                {
                    if ( payload->DataSize == sizeof( int ) )
                    {
                        submitTool( UI::OperatorEditorToolCommandType::SetPlacementObjectType,
                                    0u,
                                    *static_cast<const int*>( payload->Data ) );
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::GetWindowDrawList()->AddText( ImVec2( dropOrigin.x + 18.0f, dropOrigin.y + 18.0f ),
                                                 ImGui::GetColorU32( ImGuiCol_TextDisabled ),
                                                 "GAME VIEWPORT | drop assets here" );
            ImGui::GetWindowDrawList()->AddText( ImVec2( dropOrigin.x + 18.0f, dropOrigin.y + 40.0f ),
                                                 ImGui::GetColorU32( ImGuiCol_TextDisabled ),
                                                 "DX12 image, picking, gizmos, and DPI mapping arrive in E11" );
        }
        ImGui::End();
    }
    SetGameViewportInputState( gameViewportHovered, gameViewportFocused );

    if ( m_showInspector )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Inspector, &m_showInspector ) )
        {
            ImGui::TextDisabled( "No selection" );
            ImGui::Separator();
            ImGui::TextDisabled( "Contextual properties arrive in E12" );
        }
        ImGui::End();
    }

    if ( m_showWorldSimulation )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::WorldSimulation, &m_showWorldSimulation ) )
        {
            ImGui::Text( "Gravity %.2f", view.property.worldGravity );
            ImGui::Text( "Fluid %.2fm / %.1fkg/m3", view.property.worldFluidHeight, view.property.worldFluidDensity );
        }
        ImGui::End();
    }

    if ( m_showRenderingAudio )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::RenderingAudio, &m_showRenderingAudio ) )
        {
            ImGui::Text( "VSync %s", view.rendering.vsyncEnabled ? "on" : "off" );
            ImGui::Text( "Shadows %s", view.rendering.shadowsEnabled ? "on" : "off" );
            if ( ImGui::Button( "Toggle VSync" ) )
            {
                submit( m_frameCommands.operatorEditor.rendering,
                        UI::OperatorEditorRenderingCommand{ UI::OperatorEditorRenderingCommandType::ToggleVsync } );
            }
        }
        ImGui::End();
    }

    if ( m_showDiagnostics )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Diagnostics, &m_showDiagnostics ) )
        {
            ImGui::Text( "Tracy %s", m_frameInput.tracyViewerConnected ? "connected" : "waiting" );
            ImGui::Text( "Presentation alpha %.3f", view.rendering.presentationAlpha );
            ImGui::TextDisabled( "%s", m_tracyLaunchFeedback );
        }
        ImGui::End();
    }

    if ( m_showCausality )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Causality, &m_showCausality ) )
        {
            ImGui::TextDisabled( "Compact contextual summary" );
            ImGui::TextDisabled( "Full cause detail arrives in E14" );
        }
        ImGui::End();
    }

    if ( m_showReplay )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Replay, &m_showReplay ) )
        {
            ImGui::Text( "Memory preset %d  |  retention %ds  |  budget %dMiB",
                         view.replay.memoryPreset,
                         view.replay.requestedRetentionSeconds,
                         view.replay.requestedBudgetMiB );
            ImGui::TextDisabled( "Record, scrub, prediction, and cause transport arrive in E15" );
        }
        ImGui::End();
    }

    if ( m_showStatus )
    {
        if ( ImGui::Begin( ImGuiEditorPanel::Status,
                           &m_showStatus,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
        {
            const float framesPerSecond = m_frameInput.deltaSeconds > 0.0f ? 1.0f / m_frameInput.deltaSeconds : 0.0f;
            ImGui::Text( "%s%s  |  %d objects  |  undo %d / redo %d  |  %.1f FPS  |  Tracy %s",
                         view.tools.editorModeEnabled ? "EDIT" : "PLAY",
                         view.tools.placementModeEnabled ? "/PLACE" : "",
                         view.scene.modelCount,
                         view.tools.undoDepth,
                         view.tools.redoDepth,
                         framesPerSecond,
                         m_frameInput.tracyViewerConnected ? "connected" : "waiting" );
        }
        ImGui::End();
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
    result.status = m_frameCommandStatus;
    m_frameCommands = {};
    m_frameCommandStatus = SkullbonezCore::Core::SbResult::Success();
    if ( result.status.ok )
    {
        const UI::OperatorEditorArbitrationResult queued =
            UI::ArbitrateOperatorEditorCommands( m_pendingOperatorEditorCommands, result.commands.operatorEditor );
        result.status = queued.status;
        if ( result.status.ok )
        {
            m_pendingOperatorEditorCommands = queued.commands;
        }
    }
    if ( !m_renderer || !ImGui::GetDrawData() )
    {
        result.status = SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
                                                                 "Completed frame has no DX12 draw-data target" );
        return result;
    }
    const SkullbonezCore::Core::SbResult renderStatus = m_renderer->RenderDrawData( *m_context, *ImGui::GetDrawData() );
    if ( result.status.ok )
    {
        result.status = renderStatus;
    }
    return result;
}

ImGuiEditorStatus ImGuiEditorOwner::CopyStatus() const noexcept
{
    ImGuiEditorStatus status;
    status.initialized = m_context != nullptr;
    status.visible = m_visible;
    status.frameActive = m_frameActive;
    status.dockingEnabled = m_context != nullptr;
    status.platformViewportsEnabled = false;
    status.layoutVersion = LAYOUT_VERSION;
    status.completedFrames = m_completedFrames;
    status.sharedViewFingerprint = m_sharedViewFingerprint;
    status.layoutTopologyFingerprint = m_layoutTopologyFingerprint;
    status.layoutBuildCount = m_layoutBuildCount;
    status.layoutResetCount = m_layoutResetCount;
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
    style.ScaleAllSizes( dpiScale );
    style.FontSizeBase = EDITOR_FONT_SIZE_PIXELS;
    style.FontScaleDpi = dpiScale;
    m_appliedDpiScale = dpiScale;
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
