/*
File: SkullbonezSource/SkullbonezMeshDX11.cpp
Purpose:
  Implements mesh buffers and draw binding for the DX11 parity renderer.

Mental model:
  DX11 is a legacy parity renderer. It follows the renderer interface while
  staying close enough to DX12 and OpenGL output for visual comparison.

Glossary:
  DX11 (DirectX 11): Legacy parity renderer used to compare output while the
  engine migrates to DX12.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezMeshDX11.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezMeshDX11.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezShaderDX11.h"
#include <stdexcept>


using namespace SkullbonezCore::Rendering;


// --- Input Layout Descriptors ---
// D3D11_INPUT_ELEMENT_DESC arrays define how raw vertex buffer bytes are split into named shader
// inputs. Each entry maps a semantic (e.g. "POSITION", "NORMAL", "TEXCOORD") to a data format
// and byte offset within a single vertex. The GPU's Input Assembler stage uses this to feed the
// correct data into the vertex shader's input parameters.
// Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_input_element_desc

static D3D11_INPUT_ELEMENT_DESC s_layoutPos3[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos3Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos3Norm3Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos2Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};


MeshDX11::MeshDX11( ID3D11Device* device, ID3D11DeviceContext* context )
    : m_device( device ), m_context( context ), m_vb( nullptr ), m_inputLayout( nullptr ), m_vertexCount( 0 ), m_stride( 0 ), m_format( VertexFormatDX::Pos3 ), m_lastVSBytecode( nullptr )
{
}


MeshDX11::~MeshDX11()
{
    if ( m_inputLayout )
    {
        m_inputLayout->Release();
    }
    if ( m_vb )
    {
        m_vb->Release();
    }
}


bool MeshDX11::Create( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    int floatsPerVert = 3;
    if ( hasNormals )
    {
        floatsPerVert += 3;
    }
    if ( hasTexCoords )
    {
        floatsPerVert += 2;
    }

    m_stride = floatsPerVert * sizeof( float );
    m_vertexCount = vertexCount;

    if ( hasNormals && hasTexCoords )
    {
        m_format = VertexFormatDX::Pos3_Norm3_Tex2;
    }
    else if ( hasTexCoords )
    {
        m_format = VertexFormatDX::Pos3_Tex2;
    }
    else
    {
        m_format = VertexFormatDX::Pos3;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)( vertexCount * m_stride );
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    // Allocate a vertex buffer on the GPU and upload the mesh data. IMMUTABLE means the data
    // is written once at creation and never changes -- the GPU can optimize storage for read-only
    // access. BIND_VERTEX_BUFFER marks this buffer for use in the Input Assembler stage.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
    HRESULT hr = m_device->CreateBuffer( &bd, &initData, &m_vb );
    return SUCCEEDED( hr );
}


void MeshDX11::EnsureInputLayout() const
{
    auto* backend = RenderBackendDX11::Get();
    auto* shader = backend->GetActiveShader();
    if ( !shader )
    {
        return;
    }

    const void* vsBytecode = shader->GetVSBytecode();
    if ( vsBytecode == m_lastVSBytecode && m_inputLayout )
    {
        return;
    }

    if ( m_inputLayout )
    {
        m_inputLayout->Release();
        m_inputLayout = nullptr;
    }

    D3D11_INPUT_ELEMENT_DESC* elements = nullptr;
    UINT numElements = 0;

    switch ( m_format )
    {
    case VertexFormatDX::Pos3:
        elements = s_layoutPos3;
        numElements = 1;
        break;
    case VertexFormatDX::Pos3_Tex2:
        elements = s_layoutPos3Tex2;
        numElements = 2;
        break;
    case VertexFormatDX::Pos3_Norm3_Tex2:
        elements = s_layoutPos3Norm3Tex2;
        numElements = 3;
        break;
    case VertexFormatDX::Pos2_Tex2:
        elements = s_layoutPos2Tex2;
        numElements = 2;
        break;
    case VertexFormatDX::Pos2:
        elements = s_layoutPos2;
        numElements = 1;
        break;
    }

    // Create an Input Layout that tells the Input Assembler how to map vertex buffer bytes to
    // shader input semantics. DX11 validates the layout against the compiled vertex shader bytecode
    // to ensure the data format matches what the shader expects.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
    m_device->CreateInputLayout( elements,
                                 numElements,
                                 shader->GetVSBytecode(),
                                 shader->GetVSBytecodeSize(),
                                 &m_inputLayout );
    m_lastVSBytecode = vsBytecode;
}


void MeshDX11::Draw() const
{
    EnsureInputLayout();

    auto* backend = RenderBackendDX11::Get();
    auto* shader = backend->GetActiveShader();
    if ( shader )
    {
        shader->FlushCB();
    }

    // Bind the input layout so the Input Assembler knows how to interpret vertex data.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
    m_context->IASetInputLayout( m_inputLayout );
    UINT stride = (UINT)m_stride;
    UINT offset = 0;

    // Bind the vertex buffer to Input Assembler slot 0. The stride tells the GPU how many bytes
    // apart each vertex starts; offset is where to begin reading in the buffer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
    m_context->IASetVertexBuffers( 0, 1, &m_vb, &stride, &offset );

    // Tell the Input Assembler to interpret vertices as a triangle list (every 3 vertices = 1 triangle).
    // Other options include triangle strips, line lists, point lists, etc.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetprimitivetopology
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // Issue a non-indexed draw call. The GPU processes m_vertexCount vertices starting at vertex 0,
    // running them through the entire pipeline (vertex shader -> rasterizer -> pixel shader -> output merger).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-draw
    backend->NoteDrawCall();
    m_context->Draw( (UINT)m_vertexCount, 0 );
}


void MeshDX11::DrawInstanced( int instanceCount ) const
{
    EnsureInputLayout();

    auto* backend = RenderBackendDX11::Get();
    auto* shader = backend->GetActiveShader();
    if ( shader )
    {
        shader->FlushCB();
    }

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
    m_context->IASetInputLayout( m_inputLayout );
    UINT stride = (UINT)m_stride;
    UINT offset = 0;

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
    m_context->IASetVertexBuffers( 0, 1, &m_vb, &stride, &offset );

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetprimitivetopology
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // Issue an instanced draw call. Renders the same mesh geometry multiple times (instanceCount
    // copies) in a single GPU call. Each instance can receive unique per-instance data (e.g. a
    // different world matrix) from an instance buffer, avoiding one draw call per object.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawinstanced
    backend->NoteDrawCall();
    m_context->DrawInstanced( (UINT)m_vertexCount, (UINT)instanceCount, 0, 0 );
}
