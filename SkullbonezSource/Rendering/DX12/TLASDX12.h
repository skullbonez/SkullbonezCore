/*
File: SkullbonezSource/Rendering/DX12/TLASDX12.h
Purpose:
  Builds and owns the DX12 raytracing top-level scene acceleration structure.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/TLASDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- TLAS
-------------------------------------------------------------------------------------------------------------------------------------------------------

    Top-Level Acceleration Structure wrapper for DXR raytracing.
    Rebuilt every frame with updated instance transforms.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TLAS
{

  private:
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;
    ID3D12Resource* m_instanceDescs; // Upload heap, rewritten each frame
    int m_maxInstances;

  public:
    TLAS();
    ~TLAS();

    void Init( ID3D12Device5* device, int maxInstances );
    void Build( ID3D12Device5* device,
                ID3D12GraphicsCommandList4* cmdList,
                const D3D12_RAYTRACING_INSTANCE_DESC* instances,
                int instanceCount );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
