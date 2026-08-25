/*
File: SkullbonezSource/Rendering/DX12/TLASDX12.h
Purpose:
  Builds and owns the DX12 raytracing top-level scene acceleration structure.

Summary:
  TLAS rebuilds the top-level scene acceleration structure from current
  instance transforms each frame and owns its device-epoch resources.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
    must stay explicit.
  - Each allocator/fence slot owns a distinct instance-descriptor upload table.
  - Build returns a recoverable result before recording GPU work when its instance
    descriptor upload cannot be mapped safely.

Related:
  - SkullbonezSource/Rendering/DX12/TLASDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../../Core/SbResult.h"

#include <d3d12.h>
#include <span>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

inline ID3D12Resource* SelectDx12FrameUploadResource( std::span<ID3D12Resource* const> resources,
                                                       UINT frameIndex ) noexcept
{
    return frameIndex < resources.size() ? resources[frameIndex] : nullptr;
}

class TLAS
{

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;
    std::vector<ID3D12Resource*> m_instanceDescs; // One upload table per allocator/fence slot.
    int m_maxInstances;

  public:
    explicit TLAS( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );
    ~TLAS();

    SkullbonezCore::Core::SbResult Init( ID3D12Device5* device, int maxInstances, UINT frameCount );
    SkullbonezCore::Core::SbResult Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList,
                                          const D3D12_RAYTRACING_INSTANCE_DESC* instances, int instanceCount,
                                          UINT frameIndex );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
