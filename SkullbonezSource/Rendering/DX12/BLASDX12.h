/*
File: SkullbonezSource/Rendering/DX12/BLASDX12.h
Purpose:
  Builds and owns DX12 raytracing bottom-level acceleration structures for mesh geometry.

Summary:
  BLASDX12.h builds and owns DX12 raytracing bottom-level acceleration
  structures for mesh geometry. As a public header, keep edits anchored on
  DX12 ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/BLASDX12.cpp
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
/* -- BLAS
-------------------------------------------------------------------------------------------------------------------------------------------------------

    Bottom-Level Acceleration Structure wrapper for DXR raytracing.
    Builds a BLAS from a vertex buffer (vertex-only, no index buffer).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BLAS
{

  private:
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;

  public:
    BLAS();
    ~BLAS();

    Basics::SbResult Build( ID3D12Device5* device,
                            ID3D12GraphicsCommandList4* cmdList,
                            D3D12_GPU_VIRTUAL_ADDRESS vbVA,
                            int vertexCount,
                            int vertexStride,
                            DXGI_FORMAT vertexPosFormat,
                            bool preferFastTrace );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    // Release the temporary build workspace while keeping the finished BLAS
    // result buffer alive for TLAS instances and ray traversal.
    void ReleaseAfterBuild();
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
