/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
Purpose:
  Owns Dear ImGui startup, empty dockspace frames, and orderly shutdown.

Summary:
  The owner installs the engine's hard-capped DearImGui allocator, creates one
  context, selects a readable scalable font, configures single-window docking,
  and translates immutable display facts into balanced ImGui frames. It
  delegates DX12 device resources and draw recording to the concrete renderer
  owner and delegates native event translation to the pinned Win32 backend.

Glossary:
  Embedded vector fallback: Dear ImGui's pinned, scalable built-in font used
    when the optional repository font asset is absent or invalid.
  Empty dockspace: CPU-side docking host with no migrated domain panels yet.
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

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// The pinned backend intentionally hides this declaration behind #if 0 to
// avoid forcing Windows types on every includer. This source already owns the
// Win32 ABI boundary, so repeat the vendor-prescribed declaration here.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM wParam, LPARAM lParam );

namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr const char* IMGUI_LAYOUT_PATH = "imgui_editor_layout_v1.ini";
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
    m_frameActive = true;
    return true;
}

void ImGuiEditorOwner::BuildEmptyDockspace()
{
    if ( !m_frameActive )
    {
        return;
    }
    ImGui::DockSpaceOverViewport( 0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode );
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
    if ( !m_renderer || !ImGui::GetDrawData() )
    {
        result.status = SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
                                                                 "Completed frame has no DX12 draw-data target" );
        return result;
    }
    result.status = m_renderer->RenderDrawData( *m_context, *ImGui::GetDrawData() );
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
