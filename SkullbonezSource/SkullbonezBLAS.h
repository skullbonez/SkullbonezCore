#pragma once


// --- Includes ---
#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- BLAS -------------------------------------------------------------------------------------------------------------------------------------------------------

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

    void Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, D3D12_GPU_VIRTUAL_ADDRESS vbVA, int vertexCount, int vertexStride, DXGI_FORMAT vertexPosFormat, bool preferFastTrace );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void ReleaseAfterBuild(); // Free scratch, keep result
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
