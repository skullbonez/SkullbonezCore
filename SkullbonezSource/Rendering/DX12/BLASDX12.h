/*
File: SkullbonezSource/Rendering/DX12/BLASDX12.h
Purpose:
  Builds and owns DX12 raytracing bottom-level acceleration structures for mesh geometry.

Summary:
  BLAS owns one bottom-level acceleration structure built from vertex-only mesh
  geometry plus its scratch/result resources for the active device epoch.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/BLASDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../../Core/SbResult.h"

#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{

class BLAS
{

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;

  public:
    explicit BLAS( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );
    ~BLAS();

    SkullbonezCore::Core::SbResult Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList,
                                          D3D12_GPU_VIRTUAL_ADDRESS vbVA, int vertexCount, int vertexStride,
                                          DXGI_FORMAT vertexPosFormat, bool preferFastTrace );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;

    // Release the temporary build workspace while keeping the finished BLAS
    // result buffer alive for TLAS instances and ray traversal.
    void ReleaseAfterBuild();
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
