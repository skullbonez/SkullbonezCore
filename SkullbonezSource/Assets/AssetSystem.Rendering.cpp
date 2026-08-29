// Renderer-facing AssetSystem operations live here so CPU-only registry tests
// can compile AssetSystem.cpp without replacing or copying production behavior.
#include "AssetSystem.h"

#include "../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../Rendering/DX12/ShaderDX12.h"

namespace SkullbonezCore::Assets
{
std::unique_ptr<Rendering::ShaderDX12> AssetSystem::CreateShader( Rendering::Dx12ResourceBuilder& renderResources,
                                                                  const char* logicalNameOrBaseName ) const
{
    const ShaderSourceRequest request = ResolveShaderSourceRequest( logicalNameOrBaseName );
    return renderResources.CreateShaderFromResolvedBasePath( request.resolvedBasePath.c_str(),
                                                             request.contractBaseName.c_str() );
}
} // namespace SkullbonezCore::Assets
