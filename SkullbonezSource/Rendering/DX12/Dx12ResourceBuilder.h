/*
File: SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h
Purpose:
  Declares cold construction of concrete owning DX12 resources.

Summary:
  The builder creates shaders, static meshes, and framebuffers by borrowing the
  exact device-epoch owners those objects need. Texture registry and bounded
  dynamic/instanced geometry operations remain on their own concrete owners.

Glossary:
  Cold construction: Startup, load, or rebuild work allowed to create GPU objects.
  Device epoch: Interval in which one DX12 device and its descriptors remain valid.

Invariants:
  - The builder owns no GPU state and cannot submit ordinary draw work.
  - Every returned object is a concrete DX12 type tied to the borrowed device epoch.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
*/
#pragma once

#include "../RenderResourceTypes.h"

#include <memory>
#include <string>

namespace SkullbonezCore::Rendering
{
class Dx12DescriptorHeaps;
class Dx12Diagnostics;
class Dx12FrameOwner;
class Dx12PipelineOwner;
class Dx12RenderDevice;
class Dx12ShaderDevelopment;
class Dx12TextureOwner;
class FramebufferDX12;
class MeshDX12;
class ShaderDX12;
struct Dx12InitialRasterShaderBytecodePreparationSummary;

class Dx12ResourceBuilder
{
  public:
    Dx12ResourceBuilder( Dx12RenderDevice& device, Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures,
                         Dx12DescriptorHeaps& descriptors, Dx12FrameOwner& frame, Dx12ShaderDevelopment& shaderDevelopment,
                         Dx12Diagnostics& diagnostics )
        : m_device( device ), m_pipeline( pipeline ), m_textures( textures ), m_descriptors( descriptors ), m_frame( frame ),
          m_shaderDevelopment( shaderDevelopment ), m_diagnostics( diagnostics )
    {
    }

    // contractBaseName separates a feature-owned physical asset name from the
    // generic CPU ABI name used by Rendering.
    std::unique_ptr<ShaderDX12> CreateShader( const char* baseName, const char* contractBaseName = nullptr );
    // AssetSystem has already applied its configured root to this path. The
    // builder appends only the HLSL extension and never reapplies DATA_ROOT.
    std::unique_ptr<ShaderDX12> CreateShaderFromResolvedBasePath( const char* resolvedBasePath,
                                                                  const char* contractBaseName = nullptr );
    static std::string DefaultShaderHlslPath( const char* baseName );
    static std::string ResolvedShaderHlslPath( const char* resolvedBasePath );

    // BackendInit-only operation: verify the finite first-gameplay raster set
    // without constructing pass-owned ShaderDX12 objects.
    Dx12InitialRasterShaderBytecodePreparationSummary PrepareInitialRasterShaderBytecode();
    std::unique_ptr<MeshDX12> CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords );
    std::unique_ptr<FramebufferDX12> CreateFramebuffer( int width, int height,
                                                        FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 );

  private:
    std::unique_ptr<ShaderDX12> CreateShaderFromHlslPath( const char* hlslPath, const char* contractBaseName );
    Dx12RenderDevice& m_device;
    Dx12PipelineOwner& m_pipeline;
    Dx12TextureOwner& m_textures;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12FrameOwner& m_frame;
    Dx12ShaderDevelopment& m_shaderDevelopment;
    Dx12Diagnostics& m_diagnostics;
};
} // namespace SkullbonezCore::Rendering
