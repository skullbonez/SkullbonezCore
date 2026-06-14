// --- Includes ---
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


// --- Usings ---
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

// --- RenderBackendDX12 Resources methods ---


std::unique_ptr<IShader> RenderBackendDX12::CreateShader( const char* baseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";
    auto shader = std::make_unique<ShaderDX12>();
    if ( !shader->Compile( hlslPath.c_str() ) )
    {
        throw std::runtime_error( "ShaderDX12 compilation failed: " + hlslPath );
    }
    return shader;
}


std::unique_ptr<IMesh> RenderBackendDX12::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    VertexFormat12 format;
    int floatsPerVert;
    if ( hasNormals && hasTexCoords )
    {
        format = VertexFormat12::Pos3_Norm3_Tex2;
        floatsPerVert = 8;
    }
    else if ( hasTexCoords )
    {
        format = VertexFormat12::Pos3_Tex2;
        floatsPerVert = 5;
    }
    else
    {
        format = VertexFormat12::Pos3;
        floatsPerVert = 3;
    }

    EnsureCommandListOpen();
    UINT64 dataSize = (UINT64)vertexCount * floatsPerVert * sizeof( float );
    FlushUploadBufferIfNeeded( dataSize, 4 );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = SubAllocateUpload( dataSize, 4 );
    uint8_t* uploadPtr = GetUploadPtr( uploadAddr );

    auto mesh = std::make_unique<MeshDX12>();
    mesh->Create( m_device, m_commandList, data, vertexCount, floatsPerVert, format, uploadAddr, uploadPtr );
    return mesh;
}


std::unique_ptr<IFramebuffer> RenderBackendDX12::CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat )
{
    auto fbo = std::make_unique<FramebufferDX12>( colorFormat );
    fbo->Create( width, height );
    return fbo;
}
