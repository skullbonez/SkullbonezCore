/*
File: SkullbonezSource/Rendering/DX12/SBTDX12.h
Purpose:
  Builds the DX12 raytracing shader binding table that maps ray records to shaders.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  SBT (Shader Binding Table): DXR table that maps ray records to
  ray-generation, miss, and hit shaders.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/SBTDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../../Core/SbResult.h"

#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- SBT
--------------------------------------------------------------------------------------------------------------------------------------------------------

    Shader Binding Table for DXR raytracing pipeline.
    Lays out raygen, miss, and hit group records with proper alignment.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SBT
{

  private:
    ID3D12Resource* m_buffer;
    UINT64 m_rayGenOffset;
    UINT64 m_rayGenSize;
    UINT64 m_missOffset;
    UINT64 m_missSize;
    UINT64 m_hitGroupOffset;
    UINT64 m_hitGroupStride;
    UINT64 m_hitGroupSize;

  public:
    SBT();
    ~SBT();

    Basics::SbResult Build( ID3D12Device* device,
                            ID3D12StateObjectProperties* props,
                            const wchar_t* rayGenName,
                            const wchar_t* missName,
                            const wchar_t* hitGroupTerrainName,
                            const wchar_t* hitGroupSphereName );

    D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupRange() const;
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
