//
// File: SkullbonezTests/TestRenderResourceDoubles.h
// Purpose:
//   Provides renderer-neutral test doubles for code paths that construct render resources.
//
// Mental model:
//   Some unit tests exercise runtime objects such as Terrain whose constructors
//   build shaders and meshes as part of normal production setup. These doubles
//   satisfy the render-resource contracts without creating backend state, so the
//   tests can assert CPU-side behavior without initializing DX12.
//
// Glossary:
//   Render-resource double: Test-owned implementation of the resource factory
//     interface that records no backend state.
//   Shader double: No-op shader object that accepts uniform writes.
//   Mesh double: No-op mesh object that preserves basic vertex metadata.
//
// Invariants:
//   - No method here creates GPU resources, files, windows, or backend handles.
//   - Test subjects that need real renderer validation must use the DX12 gates
//     instead of these doubles.
//
// Related:
//   - SkullbonezSource/Rendering/IRenderResourceFactory.h
//   - SkullbonezSource/World/Terrain.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//
#pragma once

#include "../SkullbonezSource/Rendering/IRenderResourceFactory.h"

#include <cstdint>
#include <memory>

namespace SkullbonezTests
{
class NullShader final : public SkullbonezCore::Rendering::IShader
{
  public:
    void Use() const override
    {
    }

    void SetInt( const char*, int ) const override
    {
    }

    void SetFloat( const char*, float ) const override
    {
    }

    void SetVec3( const char*, const SkullbonezCore::Math::Vector::Vector3& ) const override
    {
    }

    void SetVec3( const char*, float, float, float ) const override
    {
    }

    void SetVec4( const char*, float, float, float, float ) const override
    {
    }

    void SetMat4( const char*, const SkullbonezCore::Math::Transformation::Matrix4& ) const override
    {
    }

    bool SetConstantBufferBytes( const void*, size_t, const char* ) const override
    {
        return true;
    }
};

class NullMesh final : public SkullbonezCore::Rendering::IMesh
{
  public:
    explicit NullMesh( int vertexCount, int stride ) : m_vertexCount( vertexCount ), m_stride( stride )
    {
    }

    void Draw() const override
    {
    }

    void DrawInstanced( int ) const override
    {
    }

    int GetVertexCount() const override
    {
        return m_vertexCount;
    }

    int GetStride() const override
    {
        return m_stride;
    }

    uint64_t GetVertexBufferGPUVA() const override
    {
        return 0u;
    }

  private:
    int m_vertexCount = 0;
    int m_stride = 0;
};

class NullRenderResourceFactory final : public SkullbonezCore::Rendering::IRenderResourceFactory
{
  public:
    std::unique_ptr<SkullbonezCore::Rendering::IShader> CreateShader( const char* ) override
    {
        return std::make_unique<NullShader>();
    }

    std::unique_ptr<SkullbonezCore::Rendering::IMesh> CreateMesh( const float*,
                                                                 int vertexCount,
                                                                 bool hasNormals,
                                                                 bool hasTexCoords ) override
    {
        const int stride = hasNormals && hasTexCoords ? 8 : ( hasTexCoords ? 5 : 3 );
        return std::make_unique<NullMesh>( vertexCount, stride );
    }

    std::unique_ptr<SkullbonezCore::Rendering::IFramebuffer>
    CreateFramebuffer( int, int, SkullbonezCore::Rendering::FramebufferColorFormat ) override
    {
        return nullptr;
    }

    uint32_t CreateTexture2D( const uint8_t*, int, int, int, bool, bool ) override
    {
        return 0u;
    }

    void DeleteTexture( uint32_t ) override
    {
    }

    uint32_t CreateDynamicVB( const int*, int, int ) override
    {
        return 0u;
    }

    void DestroyDynamicVB( uint32_t ) override
    {
    }

    uint32_t CreateInstancedMesh( const float*,
                                  int,
                                  int,
                                  int,
                                  int,
                                  int,
                                  const int*,
                                  int,
                                  const int*,
                                  int ) override
    {
        return 0u;
    }

    void DestroyInstancedMesh( uint32_t ) override
    {
    }
};
} // namespace SkullbonezTests
