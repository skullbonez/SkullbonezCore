/*
File: SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp
Purpose:
  Binds Dear ImGui draw data to the engine-owned DX12 frame safely.

Summary:
  The owner gives the pinned ImGui backend one bounded descriptor capability,
  asks it to build device objects for the current context, then records draw
  data after world/UI rendering and before Present. The engine frame owner
  retains command-list and backbuffer transitions; the descriptor owner retains
  all heap rows; the vendor backend receives no swap-chain authority.

Glossary:
  Font upload: First-frame texture transfer performed by the pinned backend on
    the engine graphics queue and synchronously fenced before the upload staging
    bytes can be reused.
  State invalidation: Marking the engine pipeline and texture caches dirty after
    vendor commands changed native bindings.

Invariants:
  - The dedicated heap is rebound only for ImGui recording and the engine heap
    is restored before returning.
  - Backbuffer state is RenderTarget while ImGui draws and Present transitions
    it afterward.
  - Resize replaces only backbuffers/RTVs; ImGui device resources and its fixed
    descriptor heap survive the resize epoch without stale backbuffer aliases.
  - A failed engine recording epoch prevents any ImGui command emission.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h
  - ThirdPtySource/imgui/backends/imgui_impl_dx12.cpp
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
*/
#include "Dx12ImGuiRendererOwner.h"

#include "Dx12DescriptorHeaps.h"
#include "Dx12FrameOwner.h"
#include "RenderBackendDX12.h"
#include "RenderDeviceDX12.h"
#include "../../Runtime/Allocation/DevelopmentToolsCapability.h"
#include "../../Core/FatalError.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#include <cstdio>

using namespace SkullbonezCore::Rendering;

namespace
{
void AllocateImGuiDescriptor( ImGui_ImplDX12_InitInfo* info,
                              D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                              D3D12_GPU_DESCRIPTOR_HANDLE* outGpu )
{
    if ( !info || !info->UserData || !outCpu || !outGpu )
    {
        SB_FATAL( "Rendering/DX12/ImGui", "Descriptor allocation callback received an invalid capability." );
    }
    // Why: the pinned vendor API requires retained callbacks. UserData is the
    // concrete descriptor owner, not a backend/service bag; callbacks run only
    // for font or explicit editor texture creation, outside world hot loops.
    auto& descriptors = *static_cast<Dx12DescriptorHeaps*>( info->UserData );
    const Dx12DevelopmentUiDescriptor descriptor = descriptors.AllocateDevelopmentUi();
    *outCpu = descriptor.cpuHandle;
    *outGpu = descriptor.gpuHandle;
}

void FreeImGuiDescriptor( ImGui_ImplDX12_InitInfo* info,
                          D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                          D3D12_GPU_DESCRIPTOR_HANDLE gpu )
{
    if ( !info || !info->UserData )
    {
        SB_FATAL( "Rendering/DX12/ImGui", "Descriptor free callback received an invalid capability." );
    }
    static_cast<Dx12DescriptorHeaps*>( info->UserData )->FreeDevelopmentUi( cpu, gpu );
}
} // namespace

Dx12ImGuiRendererOwner::Dx12ImGuiRendererOwner( Dx12RenderDevice& device,
                                                Dx12DescriptorHeaps& descriptors,
                                                Dx12FrameOwner& frame,
                                                Dx12PipelineOwner& pipeline,
                                                Dx12TextureOwner& textures ) noexcept
    : m_device( device ), m_descriptors( descriptors ), m_frame( frame ), m_pipeline( pipeline ), m_textures( textures )
{
    static_assert( Dx12FrameOwner::FRAME_COUNT == 2,
                   "Dear ImGui frame resources must match the engine's two-frame reuse contract." );
}

SkullbonezCore::Core::SbResult Dx12ImGuiRendererOwner::BindContext( ImGuiContext& context )
{
    if ( m_initialized )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
    if ( !m_device.Device() || !m_device.GraphicsQueue() || !m_descriptors.DevelopmentUiHeap() )
    {
        // Lane R: device/heap publication depends on the host graphics environment.
        return SkullbonezCore::Core::SbResult::Failure(
            "Rendering/DX12/ImGui",
            "Cannot bind ImGui renderer without a complete device, queue, and development descriptor heap" );
    }

    ImGui::SetCurrentContext( &context );
    ImGui_ImplDX12_InitInfo info;
    info.Device = m_device.Device();
    info.CommandQueue = m_device.GraphicsQueue();
    info.NumFramesInFlight = Dx12FrameOwner::FRAME_COUNT;
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    info.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    info.UserData = &m_descriptors;
    // Capability boundary: this is a separate fixed heap. The vendor never
    // receives the engine's bindless descriptor heap or swap-chain owner.
    info.SrvDescriptorHeap = m_descriptors.DevelopmentUiHeap();
    info.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
    info.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    if ( !ImGui_ImplDX12_Init( &info ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12/ImGui",
                                                        "Dear ImGui DX12 backend initialization failed" );
    }
    m_initialized = true;
    if ( !ImGui_ImplDX12_CreateDeviceObjects() )
    {
        ImGui_ImplDX12_Shutdown();
        m_initialized = false;
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12/ImGui",
                                                        "Dear ImGui DX12 device-object creation failed" );
    }

    const Dx12DevelopmentUiDescriptorStats descriptorStats = m_descriptors.DevelopmentUiStats();
    printf( "[imgui-dx12] Renderer ready frames=%d descriptors=%u/%u.\n",
            Dx12FrameOwner::FRAME_COUNT,
            descriptorStats.used,
            descriptorStats.capacity );
    return SkullbonezCore::Core::SbResult::Success();
}

void Dx12ImGuiRendererOwner::BeginFrame( ImGuiContext& context )
{
    if ( !m_initialized )
    {
        SB_FATAL( "Rendering/DX12/ImGui", "BeginFrame called without a live renderer binding." );
    }
    ImGui::SetCurrentContext( &context );
    ImGui_ImplDX12_NewFrame();
}

SkullbonezCore::Core::SbResult Dx12ImGuiRendererOwner::RenderDrawData( ImGuiContext& context, ImDrawData& drawData )
{
    if ( !m_initialized )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12/ImGui",
                                                        "Draw submission has no live renderer binding" );
    }
    if ( drawData.DisplaySize.x <= 0.0f || drawData.DisplaySize.y <= 0.0f )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult result = m_frame.EnsureOpen();
    if ( !result.ok )
    {
        return result;
    }
    if ( m_frame.BackBufferAccess() != RenderGraphResourceAccess::RenderTarget &&
         !m_frame.TransitionBackbuffer( "DevelopmentUi", RenderGraphResourceAccess::RenderTarget ) )
    {
        return m_frame.CurrentResult();
    }

    ID3D12GraphicsCommandList* commandList = m_frame.CommandList();
    const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = m_descriptors.BackBufferRtv( m_frame.FrameIndex() );
    commandList->OMSetRenderTargets( 1, &renderTarget, FALSE, nullptr );
    m_descriptors.BindDevelopmentUi( commandList );

    ImGui::SetCurrentContext( &context );
    ImGui_ImplDX12_RenderDrawData( &drawData, commandList );

    // Hazard: the vendor changes the root signature, PSO, heap, viewport, and
    // scissors. Restore heap authority now and dirty every engine cache that
    // could otherwise skip a required bind on a later command.
    m_descriptors.Bind( commandList );
    m_pipeline.InvalidateCommandState();
    m_textures.InvalidateBindings();

    ++m_recordedFrames;
    m_commandLists += static_cast<uint64_t>( drawData.CmdListsCount );
    m_vertices += static_cast<uint64_t>( drawData.TotalVtxCount );
    m_indices += static_cast<uint64_t>( drawData.TotalIdxCount );
    for ( const ImDrawList* drawList : drawData.CmdLists )
    {
        for ( const ImDrawCmd& command : drawList->CmdBuffer )
        {
            if ( !command.UserCallback && command.ElemCount > 0u )
            {
                ++m_indexedDraws;
            }
        }
    }
    return SkullbonezCore::Core::SbResult::Success();
}

void Dx12ImGuiRendererOwner::Shutdown( ImGuiContext& context ) noexcept
{
    if ( !m_initialized )
    {
        return;
    }
    ImGui::SetCurrentContext( &context );
    ImGui_ImplDX12_Shutdown();
    m_initialized = false;

    const Dx12DevelopmentUiDescriptorStats descriptorStats = m_descriptors.DevelopmentUiStats();
    if ( descriptorStats.used != 0u )
    {
        SB_FATAL( "Rendering/DX12/ImGui",
                  "Renderer shutdown leaked bounded descriptor rows. used=%u capacity=%u high_water=%u",
                  descriptorStats.used,
                  descriptorStats.capacity,
                  descriptorStats.highWater );
    }
    printf( "[imgui-dx12] Renderer shutdown frames=%llu draws=%llu descriptors=%u high_water=%u/%u.\n",
            static_cast<unsigned long long>( m_recordedFrames ),
            static_cast<unsigned long long>( m_indexedDraws ),
            descriptorStats.used,
            descriptorStats.highWater,
            descriptorStats.capacity );
}

bool Dx12ImGuiRendererOwner::IsInitialized() const noexcept
{
    return m_initialized;
}

Dx12ImGuiRenderStats Dx12ImGuiRendererOwner::CopyStats() const noexcept
{
    const Dx12DevelopmentUiDescriptorStats descriptors = m_descriptors.DevelopmentUiStats();
    Dx12ImGuiRenderStats stats;
    stats.initialized = m_initialized;
    stats.descriptorUsed = descriptors.used;
    stats.descriptorCapacity = descriptors.capacity;
    stats.descriptorHighWater = descriptors.highWater;
    stats.recordedFrames = m_recordedFrames;
    stats.commandLists = m_commandLists;
    stats.indexedDraws = m_indexedDraws;
    stats.vertices = m_vertices;
    stats.indices = m_indices;
    return stats;
}
