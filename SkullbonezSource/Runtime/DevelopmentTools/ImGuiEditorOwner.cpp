/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
Purpose:
  Owns Dear ImGui startup, empty dockspace frames, and orderly shutdown.

Summary:
  The owner installs the engine's hard-capped DearImGui allocator, creates one
  context, selects a readable scalable font, configures single-window docking,
  and translates immutable display facts into balanced ImGui frames. It does
  delegates DX12 device resources and draw recording to the concrete renderer
  owner while Win32 message routing remains a later campaign task.

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
#include "../../Rendering/DX12/Dx12ImGuiRendererOwner.h"

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

SkullbonezCore::Core::SbResult ImGuiEditorOwner::Start( Rendering::Dx12ImGuiRendererOwner* renderer )
{
    if ( m_context )
    {
        return SkullbonezCore::Core::SbResult::Success();
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
    if ( !renderer )
    {
        return SkullbonezCore::Core::SbResult::Failure( "DevelopmentTools/ImGui",
                                                        "No DX12 ImGui renderer capability was published at startup" );
    }

    ApplyDpiStyle( 1.0f );
    const SkullbonezCore::Core::SbResult rendererResult = renderer->BindContext( *m_context );
    if ( !rendererResult.ok )
    {
        return rendererResult;
    }
    m_renderer = renderer;
    printf( "[imgui] Context ready layout_version=%d font=%s docking=on platform_viewports=off.\n",
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
    if ( !m_renderer )
    {
        SB_FATAL( "DevelopmentTools/ImGui", "BeginFrame has no bound DX12 renderer." );
    }
    m_renderer->BeginFrame( *m_context );
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
