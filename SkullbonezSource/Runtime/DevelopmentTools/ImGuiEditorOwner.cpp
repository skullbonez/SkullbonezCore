/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
Purpose:
  Owns Dear ImGui startup, empty dockspace frames, and orderly shutdown.

Summary:
  The owner installs the engine's hard-capped DearImGui allocator, creates one
  context, selects a readable scalable font, configures single-window docking,
  and translates immutable display facts into balanced ImGui frames. It does
  not initialize Win32 or DX12 backends; those remain later campaign tasks.

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
  - SkullbonezSource/Runtime/RunFrame.cpp
  - ThirdPtySource/imgui/imgui.h
*/
#include "ImGuiEditorOwner.h"

#include "../Allocation/DevelopmentToolAllocation.h"
#include "../../Core/FatalError.h"

#include <imgui.h>

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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

void ImGuiEditorOwner::Start()
{
    if ( m_context )
    {
        return;
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
    // Hazard: E5 intentionally has no renderer backend. Build the legacy CPU
    // atlas now so NewFrame text metrics never depend on E6's texture-update
    // handshake being present.
    if ( !io.Fonts->Build() )
    {
        SB_FATAL( "DevelopmentTools/ImGui", "Dear ImGui CPU font atlas build failed." );
    }

    ApplyDpiStyle( 1.0f );
    printf( "[imgui] Context ready layout_version=%d font=%s docking=on platform_viewports=off.\n",
            LAYOUT_VERSION,
            m_fontSource == ImGuiEditorFontSource::Asset ? "asset" : "embedded_vector_fallback" );
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
    ImGui::DestroyContext( m_context );
    m_context = nullptr;
    m_visible = false;
    m_appliedDpiScale = 0.0f;
    m_fontSource = ImGuiEditorFontSource::None;
    printf( "[imgui] Context shutdown completed_frames=%llu.\n", static_cast<unsigned long long>( completedFrames ) );
}

void ImGuiEditorOwner::SetVisible( bool visible ) noexcept
{
    m_visible = visible;
}

bool ImGuiEditorOwner::IsVisible() const noexcept
{
    return m_visible;
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

ImGuiEditorCommands ImGuiEditorOwner::EndFrame()
{
    ImGuiEditorCommands commands;
    if ( !m_context || !m_frameActive )
    {
        return commands;
    }

    ImGui::SetCurrentContext( m_context );
    ImGui::Render();
    m_frameActive = false;
    ++m_completedFrames;
    return commands;
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
