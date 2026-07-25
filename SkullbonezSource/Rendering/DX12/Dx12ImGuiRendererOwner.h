/*
File: SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h
Purpose:
  Declares the development-only Dear ImGui renderer binding for the DX12 frame.

Summary:
  Dx12ImGuiRendererOwner initializes the pinned vendor renderer backend against
  a bounded development-UI descriptor heap, retains one full-client game-image
  copy, records finalized ImGui draw data into the engine command list, and
  publishes statistics separate from world rendering. It never owns the ImGui
  context, device, queue, swap chain, or engine descriptor heap.

Glossary:
  Development-UI heap: Fixed shader-visible descriptor table owned by
    Dx12DescriptorHeaps and exposed only to the pinned ImGui backend.
  Renderer binding: Device-epoch relationship between one live ImGui context
    and the current DX12 device resources.
  External command state: Root signature, PSO, descriptor heap, viewport, and
    scissor state changed by the vendor renderer outside the engine pipeline.
  Game viewport copy: One persistent shader-readable texture copied from the
    completed world backbuffer before either operator UI draws.

Invariants:
  - This source is compiled only with SKULLBONEZ_DEVELOPMENT_TOOLS.
  - FRAME_COUNT is exactly two for ImGui upload-buffer reuse.
  - Context ownership remains in ImGuiEditorOwner; this owner stores no context.
  - Shutdown occurs after a GPU drain and before context or descriptor teardown.
  - World-render counters never include ImGui command lists or indexed draws.
  - The game viewport texture matches the swap-chain extent and recreates only
    after the device owner publishes a new resize/recreation generation.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp
  - SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
  - Agentic/Plans/TODO/imgui-tracy-editor-campaign.md (E6)
*/
#pragma once

#include "Dx12DescriptorHeaps.h"
#include "../../Core/SbResult.h"

#include <cstdint>

struct ImDrawData;
struct ImGuiContext;

namespace SkullbonezCore::Rendering
{
class Dx12RenderDevice;
class Dx12DescriptorHeaps;
class Dx12FrameOwner;
class Dx12PipelineOwner;
class Dx12TextureOwner;

struct Dx12ImGuiRenderStats
{
    bool initialized = false;
    uint32_t descriptorUsed = 0;
    uint32_t descriptorCapacity = 0;
    uint32_t descriptorHighWater = 0;
    uint64_t recordedFrames = 0;
    uint64_t commandLists = 0;
    uint64_t indexedDraws = 0;
    uint64_t vertices = 0;
    uint64_t indices = 0;
    uint64_t gameViewportCaptures = 0;
    uint32_t gameViewportRecreations = 0;
    int gameViewportWidth = 0;
    int gameViewportHeight = 0;
    bool gameViewportAvailable = false;
};

class Dx12ImGuiRendererOwner
{
  public:
    Dx12ImGuiRendererOwner(
        Dx12RenderDevice& device,
        Dx12DescriptorHeaps& descriptors,
        Dx12FrameOwner& frame,
        Dx12PipelineOwner& pipeline,
        Dx12TextureOwner& textures
    ) noexcept;

    SkullbonezCore::Core::SbResult BindContext( ImGuiContext& context );
    SkullbonezCore::Core::SbResult CaptureGameViewport();
    void BeginFrame( ImGuiContext& context );
    SkullbonezCore::Core::SbResult RenderDrawData( ImGuiContext& context, ImDrawData& drawData );
    void Shutdown( ImGuiContext& context ) noexcept;
    bool IsInitialized() const noexcept;
    uint64_t GameViewportTextureId() const noexcept;
    int GameViewportWidth() const noexcept;
    int GameViewportHeight() const noexcept;
    Dx12ImGuiRenderStats CopyStats() const noexcept;

  private:
    SkullbonezCore::Core::SbResult EnsureGameViewportTexture( int width, int height );

    Dx12RenderDevice& m_device;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12FrameOwner& m_frame;
    Dx12PipelineOwner& m_pipeline;
    Dx12TextureOwner& m_textures;
    bool m_initialized = false;
    uint64_t m_recordedFrames = 0;
    uint64_t m_commandLists = 0;
    uint64_t m_indexedDraws = 0;
    uint64_t m_vertices = 0;
    uint64_t m_indices = 0;
    Dx12DevelopmentUiDescriptor m_gameViewportDescriptor;
    ID3D12Resource* m_gameViewportTexture = nullptr;
    uint64_t m_gameViewportCaptures = 0;
    uint64_t m_gameViewportDeviceGeneration = 0;
    uint32_t m_gameViewportRecreations = 0;
    int m_gameViewportWidth = 0;
    int m_gameViewportHeight = 0;
};
} // namespace SkullbonezCore::Rendering
