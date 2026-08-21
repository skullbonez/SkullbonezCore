/*
File: SkullbonezSource/Assets/AssetSystem.cpp
Purpose:
  Loads, owns, and resolves reusable runtime assets for scenes and render code.

Summary:
  AssetSystem assigns stable ids and generations to logical asset names,
  resolves their paths against the data root, and publishes source records;
  renderer-facing owners create GPU resources from those records.

Glossary:
  Logical asset name: Stable engine-facing identifier such as
    "shader.lit_textured" that scenes and runtime code can reference without
    knowing a disk path.
  Shader base name: Data-root-relative shader path without the backend-specific
    file extension.

Invariants:
  - Logical asset names are stable scene/runtime contracts; re-registering an
    existing name updates its record and generation instead of creating a second
    identity.
  - Resolved paths are derived from the data root at registration time so
    callers do not mix relative and absolute asset lookup rules.
  - Registry mutation APIs receive engine-authored names and paths. Blank
    values are caller contract failures, not recoverable asset-file failures.

Related:
  - SkullbonezSource/Assets/AssetSystem.h
  - Agentic/Reference/runtime-reference.md
*/
#include "AssetSystem.h"
#include "AssetKeys.h"
#include "../Core/Config.h"
#include "../Core/FatalError.h"

// Why: the standalone CPU test executable compiles source-registry behavior
// without linking DX12 object code. Product builds retain the concrete shader path.
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
#include "../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../Rendering/DX12/ShaderDX12.h"
#endif

#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Assets
{
namespace
{
// Why: renderer-free tests omit CreateShader, the only consumer of this map.
// Compile both in lockstep so portable warning-clean builds do not retain an
// unused internal helper.
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
const char* BuiltInShaderBaseNameForLogicalName( const char* logicalName )
{
    struct BuiltInShaderName
    {
        const char* logicalName;
        const char* baseName;
    };

    static constexpr BuiltInShaderName builtInShaders[] = {
        { "shader.lit_textured", "shaders/lit_textured" },
        { "shader.lit_textured_instanced", "shaders/lit_textured_instanced" },
        { "shader.unlit_textured", "shaders/unlit_textured" },
        { "shader.shadow_depth", "shaders/shadow_depth" },
        { "shader.shadow_depth_instanced", "shaders/shadow_depth_instanced" },
        { "shader.post_tonemap", "shaders/post_tonemap" },
        { "shader.post_volumetric_light", "shaders/post_volumetric_light" },
        { "shader.sky_atmosphere", "shaders/sky_atmosphere" },
        { "shader.text", "shaders/text" },
        { "shader.solid_color", "shaders/solid_color" },
        { "shader.solid_color_batch", "shaders/solid_color_batch" },
        { "shader.water_calm", "shaders/water_calm" },
        { "shader.water_ocean", "shaders/water_ocean" },
        { "shader.collision_visualizer", "shaders/collision_visualizer" },
        { "shader.grid_line", "shaders/grid_line" },
        { "shader.soft_additive_ribbon", "shaders/soft_additive_ribbon" },
        { "shader.retained_ribbon", "shaders/trajectory_ribbon" },
        { "shader.launcher_laser", "shaders/launcher_laser" },
        { "shader.transient_colored_triangles", "shaders/transient_colored_triangles" },
        { "shader.ui_backdrop_blur", "shaders/UIBackdropBlur" },
        { "shader.ui_render_target_preview", "shaders/ui_render_target_preview" },
        { "shader.reflect_rt", "shaders/reflect.rt" },
        { "shader.generate_mips", "shaders/generate_mips" },
    };

    if ( !logicalName || logicalName[0] == '\0' )
    {
        return nullptr;
    }

    for ( const BuiltInShaderName& shader : builtInShaders )
    {
        if ( std::strcmp( shader.logicalName, logicalName ) == 0 )
        {
            return shader.baseName;
        }
    }

    return nullptr;
}
#endif

bool IsAbsolutePath( const std::string& path )
{
    return ( path.size() >= 2 && path[1] == ':' ) || ( !path.empty() && ( path[0] == '/' || path[0] == '\\' ) );
}

bool EndsWithPathSeparator( const std::string& path )
{
    return !path.empty() && ( path.back() == '/' || path.back() == '\\' );
}

ShaderProgramContract BuiltInShaderContract( bool usesTexture, bool usesLighting, bool usesInstancing, bool depthOnly,
                                             bool postProcess )
{
    ShaderProgramContract result;
    result.usesTexture = usesTexture;
    result.usesLighting = usesLighting;
    result.usesInstancing = usesInstancing;
    result.depthOnly = depthOnly;
    result.postProcess = postProcess;
    return result;
}
} // namespace

AssetSystem::AssetSystem( std::string dataRoot ) : m_dataRoot( std::move( dataRoot ) )
{
}


void AssetSystem::RegisterBuiltInSourceAssets( const SkullbonezCore::Core::EngineConfig& config )
{
    // Concept: built-in runtime assets are source records, not GPU resources.
    // Renderer lifecycle code consumes these records later when backend facets
    // are available.
    RegisterTextureSourceAsset( "texture.terrain", config.assetPaths.terrainTexture.c_str(), TEXTURE_GROUND, true, true, 3 );

    RegisterTextureSourceAsset( "texture.sphere", config.assetPaths.sphereTexture.c_str(), TEXTURE_BOUNDING_SPHERE, true,
                                true, 3 );

    RegisterTextureSourceAsset( "texture.sky.left", config.assetPaths.skyLeft.c_str(), TEXTURE_SKY_LEFT, true, true, 3 );

    RegisterTextureSourceAsset( "texture.sky.right", config.assetPaths.skyRight.c_str(), TEXTURE_SKY_RIGHT, true, true, 3 );

    RegisterTextureSourceAsset( "texture.sky.front", config.assetPaths.skyFront.c_str(), TEXTURE_SKY_FRONT, true, true, 3 );

    RegisterTextureSourceAsset( "texture.sky.back", config.assetPaths.skyBack.c_str(), TEXTURE_SKY_BACK, true, true, 3 );

    RegisterTextureSourceAsset( "texture.sky.up", config.assetPaths.skyUp.c_str(), TEXTURE_SKY_UP, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.down", config.assetPaths.skyDown.c_str(), TEXTURE_SKY_DOWN, true, true, 3 );

    RegisterAssetLibrarySourceAsset( "assetlib.low_poly_nature", "assets/low_poly_nature.assets.json" );
    RegisterAssetLibrarySourceAsset( "assetlib.buildings", "assets/buildings.assets.json" );
    RegisterAssetLibrarySourceAsset( "assetlib.physics_props", "assets/physics_props.assets.json" );

    RegisterShaderSourceAsset( "shader.lit_textured", "shaders/lit_textured", ShaderProgramKind::LitTextured,
                               BuiltInShaderContract( true, true, false, false, false ) );

    RegisterShaderSourceAsset( "shader.lit_textured_instanced", "shaders/lit_textured_instanced",
                               ShaderProgramKind::LitTextured, BuiltInShaderContract( true, true, true, false, false ) );

    RegisterShaderSourceAsset( "shader.unlit_textured", "shaders/unlit_textured", ShaderProgramKind::UnlitTextured,
                               BuiltInShaderContract( true, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.shadow_depth", "shaders/shadow_depth", ShaderProgramKind::ShadowDepth,
                               BuiltInShaderContract( false, false, false, true, false ) );

    RegisterShaderSourceAsset( "shader.shadow_depth_instanced", "shaders/shadow_depth_instanced",
                               ShaderProgramKind::ShadowDepth, BuiltInShaderContract( false, false, true, true, false ) );

    RegisterShaderSourceAsset( "shader.post_tonemap", "shaders/post_tonemap", ShaderProgramKind::PostProcess,
                               BuiltInShaderContract( true, false, false, false, true ) );

    RegisterShaderSourceAsset( "shader.post_volumetric_light", "shaders/post_volumetric_light",
                               ShaderProgramKind::PostProcess, BuiltInShaderContract( true, false, false, false, true ) );

    RegisterShaderSourceAsset( "shader.sky_atmosphere", "shaders/sky_atmosphere", ShaderProgramKind::PostProcess,
                               BuiltInShaderContract( false, false, false, false, true ) );

    RegisterShaderSourceAsset( "shader.text", "shaders/text", ShaderProgramKind::Text,
                               BuiltInShaderContract( true, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.solid_color", "shaders/solid_color", ShaderProgramKind::Text,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.solid_color_batch", "shaders/solid_color_batch", ShaderProgramKind::Text,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.water_calm", "shaders/water_calm", ShaderProgramKind::Water,
                               BuiltInShaderContract( true, true, false, false, false ) );

    RegisterShaderSourceAsset( "shader.water_ocean", "shaders/water_ocean", ShaderProgramKind::Water,
                               BuiltInShaderContract( true, true, false, false, false ) );

    RegisterShaderSourceAsset( "shader.collision_visualizer", "shaders/collision_visualizer", ShaderProgramKind::Collision,
                               BuiltInShaderContract( false, true, true, false, false ) );

    RegisterShaderSourceAsset( "shader.grid_line", "shaders/grid_line", ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.soft_additive_ribbon", "shaders/soft_additive_ribbon", ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.retained_ribbon", "shaders/trajectory_ribbon", ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.launcher_laser", "shaders/launcher_laser", ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.transient_colored_triangles", "shaders/transient_colored_triangles",
                               ShaderProgramKind::DebugLine, BuiltInShaderContract( false, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.ui_backdrop_blur", "shaders/UIBackdropBlur", ShaderProgramKind::UI,
                               BuiltInShaderContract( true, false, false, false, true ) );

    RegisterShaderSourceAsset( "shader.reflect_rt", "shaders/reflect.rt", ShaderProgramKind::RayTracing,
                               BuiltInShaderContract( true, false, false, false, false ) );

    RegisterShaderSourceAsset( "shader.generate_mips", "shaders/generate_mips", ShaderProgramKind::Compute,
                               BuiltInShaderContract( true, false, false, false, false ) );
}


std::string AssetSystem::ResolvePath( const char* relativePath ) const
{
    if ( !relativePath || relativePath[0] == '\0' )
    {
        return m_dataRoot;
    }

    std::string path( relativePath );

    if ( IsAbsolutePath( path ) || m_dataRoot.empty() )
    {
        return path;
    }

    return EndsWithPathSeparator( m_dataRoot ) ? m_dataRoot + path : m_dataRoot + "/" + path;
}

const SourceAssetRecord& AssetSystem::RegisterSourceAsset( AssetKind kind, const char* logicalName,
                                                           const char* relativePath )
{
    // Invariant: registration is an engine-owned setup path. Authored asset
    // file failures are recoverable error elsewhere; a blank registry key means the caller
    // violated the AssetSystem API contract.
    if ( !logicalName || logicalName[0] == '\0' )
    {
        SB_FATAL( "AssetSystem", "RegisterSourceAsset requires a logical name." );
    }

    if ( !relativePath || relativePath[0] == '\0' )
    {
        SB_FATAL( "AssetSystem", "RegisterSourceAsset requires a relative path. logical=%s", logicalName );
    }

    for ( SourceAssetRecord& record : m_sourceAssets )
    {
        if ( record.logicalName == logicalName )
        {
            record.kind = kind;
            record.relativePath = relativePath;
            record.resolvedPath = ResolvePath( relativePath );
            record.generation = m_nextGeneration++;
            return record;
        }
    }

    SourceAssetRecord record;
    record.id = m_nextAssetId++;
    record.kind = kind;
    record.logicalName = logicalName;
    record.relativePath = relativePath;
    record.resolvedPath = ResolvePath( relativePath );
    record.generation = m_nextGeneration++;
    m_sourceAssets.push_back( std::move( record ) );
    return m_sourceAssets.back();
}

std::string AssetSystem::RegisterSourceAssetPath( AssetKind kind, const char* logicalName, const char* relativePath )
{
    // Why: callers that only need a load path should not depend on the registry
    // record layout; AssetSystem still owns source identity and resolution.
    return RegisterSourceAsset( kind, logicalName, relativePath ).resolvedPath;
}


const TextureSourceAsset& AssetSystem::RegisterTextureSourceAsset( const char* logicalName, const char* relativePath,
                                                                   uint32_t legacyHash, bool generateMips, bool linearFilter,
                                                                   int channelsHint )
{
    const SourceAssetRecord& source = RegisterSourceAsset( AssetKind::Texture2D, logicalName, relativePath );

    for ( TextureSourceAsset& texture : m_textureAssets )
    {
        if ( texture.id == source.id || texture.logicalName == logicalName ||
             ( legacyHash != 0 && texture.legacyHash == legacyHash ) )
        {
            texture.id = source.id;
            texture.logicalName = logicalName;
            texture.relativePath = relativePath;
            texture.resolvedPath = source.resolvedPath;
            texture.legacyHash = legacyHash;
            texture.generateMips = generateMips;
            texture.linearFilter = linearFilter;
            texture.channelsHint = channelsHint;
            return texture;
        }
    }

    TextureSourceAsset texture;
    texture.id = source.id;
    texture.logicalName = logicalName;
    texture.relativePath = relativePath;
    texture.resolvedPath = source.resolvedPath;
    texture.legacyHash = legacyHash;
    texture.generateMips = generateMips;
    texture.linearFilter = linearFilter;
    texture.channelsHint = channelsHint;
    m_textureAssets.push_back( std::move( texture ) );
    return m_textureAssets.back();
}


const TextureSourceAsset* AssetSystem::FindTextureSourceAssetByLegacyHash( uint32_t legacyHash ) const
{
    if ( legacyHash == 0 )
    {
        return nullptr;
    }

    for ( const TextureSourceAsset& texture : m_textureAssets )
    {
        if ( texture.legacyHash == legacyHash )
        {
            return &texture;
        }
    }

    return nullptr;
}


const std::vector<TextureSourceAsset>& AssetSystem::GetTextureSourceAssets() const
{
    return m_textureAssets;
}

const ShaderSourceAsset& AssetSystem::RegisterShaderSourceAsset( const char* logicalName, const char* baseName,
                                                                 ShaderProgramKind kind, ShaderProgramContract contract )
{
    const SourceAssetRecord& source = RegisterSourceAsset( AssetKind::ShaderProgram, logicalName, baseName );

    for ( ShaderSourceAsset& shader : m_shaderAssets )
    {
        if ( shader.id == source.id || shader.logicalName == logicalName || shader.baseName == baseName )
        {
            shader.id = source.id;
            shader.logicalName = logicalName;
            shader.baseName = baseName;
            shader.resolvedBasePath = source.resolvedPath;
            shader.kind = kind;
            shader.contract = contract;
            return shader;
        }
    }

    ShaderSourceAsset shader;
    shader.id = source.id;
    shader.logicalName = logicalName;
    shader.baseName = baseName;
    shader.resolvedBasePath = source.resolvedPath;
    shader.kind = kind;
    shader.contract = contract;
    m_shaderAssets.push_back( std::move( shader ) );
    return m_shaderAssets.back();
}

const ShaderSourceAsset* AssetSystem::FindShaderSourceAsset( const char* logicalNameOrBaseName ) const
{
    if ( !logicalNameOrBaseName || logicalNameOrBaseName[0] == '\0' )
    {
        return nullptr;
    }

    for ( const ShaderSourceAsset& shader : m_shaderAssets )
    {
        if ( shader.logicalName == logicalNameOrBaseName || shader.baseName == logicalNameOrBaseName )
        {
            return &shader;
        }
    }

    return nullptr;
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
std::unique_ptr<Rendering::ShaderDX12> AssetSystem::CreateShader( Rendering::Dx12ResourceBuilder& renderResources,
                                                                  const char* logicalNameOrBaseName ) const
{
    // Invariant: empty shader keys are owner API violations. A non-empty miss
    // can still use the built-in compatibility map or explicit base-name path.
    if ( !logicalNameOrBaseName || logicalNameOrBaseName[0] == '\0' )
    {
        SB_FATAL( "AssetSystem", "CreateShader requires a logical name or base name." );
    }

    const ShaderSourceAsset* shader = FindShaderSourceAsset( logicalNameOrBaseName );
    const char* fallbackBaseName = BuiltInShaderBaseNameForLogicalName( logicalNameOrBaseName );
    return renderResources.CreateShader( shader ? shader->baseName.c_str()
                                                : ( fallbackBaseName ? fallbackBaseName : logicalNameOrBaseName ) );
}
#endif


const AssetLibrarySourceAsset& AssetSystem::RegisterAssetLibrarySourceAsset( const char* logicalName,
                                                                             const char* relativePath )
{
    const SourceAssetRecord& source = RegisterSourceAsset( AssetKind::AssetLibrary, logicalName, relativePath );

    for ( AssetLibrarySourceAsset& library : m_assetLibraryAssets )
    {
        if ( library.id == source.id || library.logicalName == logicalName )
        {
            library.id = source.id;
            library.logicalName = logicalName;
            library.relativePath = relativePath;
            library.resolvedPath = source.resolvedPath;
            return library;
        }
    }

    AssetLibrarySourceAsset library;
    library.id = source.id;
    library.logicalName = logicalName;
    library.relativePath = relativePath;
    library.resolvedPath = source.resolvedPath;
    m_assetLibraryAssets.push_back( std::move( library ) );
    return m_assetLibraryAssets.back();
}

const AssetLibrarySourceAsset* AssetSystem::FindAssetLibrarySourceAsset( const char* logicalName ) const
{
    if ( !logicalName || logicalName[0] == '\0' )
    {
        return nullptr;
    }

    for ( const AssetLibrarySourceAsset& library : m_assetLibraryAssets )
    {
        if ( library.logicalName == logicalName )
        {
            return &library;
        }
    }

    return nullptr;
}


} // namespace Assets
} // namespace SkullbonezCore
