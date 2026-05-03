#pragma once


// --- Includes ---
#include "SkullbonezIMesh.h"
#include <d3d12.h>
#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX12;


// Vertex format enum for PSO input layout selection
enum class VertexFormat12
{
    Pos3,
    Pos3_Tex2,
    Pos3_Norm3_Tex2,
    Pos2_Tex2,
    Pos2
};


/* -- MeshDX12 ---------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 static mesh implementation. Holds a committed vertex buffer resource on the DEFAULT heap.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class MeshDX12 : public IMesh
{

  private:
    ID3D12Resource* m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView;
    int m_vertexCount;
    int m_stride;
    VertexFormat12 m_format;

  public:
    MeshDX12();
    ~MeshDX12() override;

    void Create( ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const float* data, int vertexCount, int floatsPerVertex, VertexFormat12 format, D3D12_GPU_VIRTUAL_ADDRESS uploadAddr, uint8_t* uploadPtr );

    void Draw() const override;
    void DrawInstanced( int instanceCount ) const override;
    int GetVertexCount() const override
    {
        return m_vertexCount;
    }
    VertexFormat12 GetFormat() const
    {
        return m_format;
    }
    void ResetResources();
};
} // namespace Rendering
} // namespace SkullbonezCore
