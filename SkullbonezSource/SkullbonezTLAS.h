#pragma once


// --- Includes ---
#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- TLAS -------------------------------------------------------------------------------------------------------------------------------------------------------

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
    void Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, const D3D12_RAYTRACING_INSTANCE_DESC* instances, int instanceCount );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
