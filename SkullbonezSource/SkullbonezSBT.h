#pragma once


// --- Includes ---
#include <d3d12.h>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- SBT --------------------------------------------------------------------------------------------------------------------------------------------------------

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

    void Build( ID3D12Device* device, ID3D12StateObjectProperties* props, const wchar_t* rayGenName, const wchar_t* missName, const wchar_t* hitGroupTerrainName, const wchar_t* hitGroupSphereName );

    D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupRange() const;
    void Reset();
};
} // namespace Rendering
} // namespace SkullbonezCore
