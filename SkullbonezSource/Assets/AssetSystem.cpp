/*
File: SkullbonezSource/Assets/AssetSystem.cpp
Purpose:
  Loads, owns, and resolves reusable runtime assets for scenes and render code.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Logical asset names are stable scene/runtime contracts; re-registering an
    existing name updates its record and generation instead of creating a second
    identity.
  - Resolved paths are derived from the data root at registration time so
    callers do not mix relative and absolute asset lookup rules.

Related:
  - SkullbonezSource/Assets/AssetSystem.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "AssetSystem.h"
#include "../Core/Config.h"
#include "../Rendering/IRenderResourceFactory.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace SkullbonezCore
{
namespace Assets
{
namespace
{
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
        { "shader.replay_ribbon", "shaders/replay_ribbon" },
        { "shader.launcher_laser", "shaders/launcher_laser" },
        { "shader.tornado_fx", "shaders/tornado_fx" },
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

bool IsAbsolutePath( const std::string& path )
{
    return ( path.size() >= 2 && path[1] == ':' ) || ( !path.empty() && ( path[0] == '/' || path[0] == '\\' ) );
}

bool EndsWithPathSeparator( const std::string& path )
{
    return !path.empty() && ( path.back() == '/' || path.back() == '\\' );
}

ShaderProgramContract
BuiltInShaderContract( bool usesTexture, bool usesLighting, bool usesInstancing, bool depthOnly, bool postProcess )
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


void AssetSystem::RegisterBuiltInSourceAssets( const Basics::EngineConfig& config )
{
    // Concept: built-in runtime assets are source records, not GPU resources.
    // Renderer lifecycle code consumes these records later when backend facets
    // are available.
    RegisterTextureSourceAsset( "texture.terrain", config.terrainTexture.c_str(), TEXTURE_GROUND, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sphere",
                                config.sphereTexture.c_str(),
                                TEXTURE_BOUNDING_SPHERE,
                                true,
                                true,
                                3 );
    RegisterTextureSourceAsset( "texture.sky.left", config.skyLeft.c_str(), TEXTURE_SKY_LEFT, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.right", config.skyRight.c_str(), TEXTURE_SKY_RIGHT, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.front", config.skyFront.c_str(), TEXTURE_SKY_FRONT, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.back", config.skyBack.c_str(), TEXTURE_SKY_BACK, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.up", config.skyUp.c_str(), TEXTURE_SKY_UP, true, true, 3 );
    RegisterTextureSourceAsset( "texture.sky.down", config.skyDown.c_str(), TEXTURE_SKY_DOWN, true, true, 3 );

    RegisterAssetLibrarySourceAsset( "assetlib.low_poly_nature", "assets/low_poly_nature.assets.json" );
    RegisterAssetLibrarySourceAsset( "assetlib.buildings", "assets/buildings.assets.json" );
    RegisterAssetLibrarySourceAsset( "assetlib.physics_props", "assets/physics_props.assets.json" );

    RegisterShaderSourceAsset( "shader.lit_textured",
                               "shaders/lit_textured",
                               ShaderProgramKind::LitTextured,
                               BuiltInShaderContract( true, true, false, false, false ) );
    RegisterShaderSourceAsset( "shader.lit_textured_instanced",
                               "shaders/lit_textured_instanced",
                               ShaderProgramKind::LitTextured,
                               BuiltInShaderContract( true, true, true, false, false ) );
    RegisterShaderSourceAsset( "shader.unlit_textured",
                               "shaders/unlit_textured",
                               ShaderProgramKind::UnlitTextured,
                               BuiltInShaderContract( true, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.shadow_depth",
                               "shaders/shadow_depth",
                               ShaderProgramKind::ShadowDepth,
                               BuiltInShaderContract( false, false, false, true, false ) );
    RegisterShaderSourceAsset( "shader.shadow_depth_instanced",
                               "shaders/shadow_depth_instanced",
                               ShaderProgramKind::ShadowDepth,
                               BuiltInShaderContract( false, false, true, true, false ) );
    RegisterShaderSourceAsset( "shader.post_tonemap",
                               "shaders/post_tonemap",
                               ShaderProgramKind::PostProcess,
                               BuiltInShaderContract( true, false, false, false, true ) );
    RegisterShaderSourceAsset( "shader.post_volumetric_light",
                               "shaders/post_volumetric_light",
                               ShaderProgramKind::PostProcess,
                               BuiltInShaderContract( true, false, false, false, true ) );
    RegisterShaderSourceAsset( "shader.sky_atmosphere",
                               "shaders/sky_atmosphere",
                               ShaderProgramKind::PostProcess,
                               BuiltInShaderContract( false, false, false, false, true ) );
    RegisterShaderSourceAsset( "shader.text",
                               "shaders/text",
                               ShaderProgramKind::Text,
                               BuiltInShaderContract( true, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.solid_color",
                               "shaders/solid_color",
                               ShaderProgramKind::Text,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.solid_color_batch",
                               "shaders/solid_color_batch",
                               ShaderProgramKind::Text,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.water_calm",
                               "shaders/water_calm",
                               ShaderProgramKind::Water,
                               BuiltInShaderContract( true, true, false, false, false ) );
    RegisterShaderSourceAsset( "shader.water_ocean",
                               "shaders/water_ocean",
                               ShaderProgramKind::Water,
                               BuiltInShaderContract( true, true, false, false, false ) );
    RegisterShaderSourceAsset( "shader.collision_visualizer",
                               "shaders/collision_visualizer",
                               ShaderProgramKind::Collision,
                               BuiltInShaderContract( false, true, true, false, false ) );
    RegisterShaderSourceAsset( "shader.grid_line",
                               "shaders/grid_line",
                               ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.replay_ribbon",
                               "shaders/replay_ribbon",
                               ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.launcher_laser",
                               "shaders/launcher_laser",
                               ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.tornado_fx",
                               "shaders/tornado_fx",
                               ShaderProgramKind::DebugLine,
                               BuiltInShaderContract( false, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.ui_backdrop_blur",
                               "shaders/UIBackdropBlur",
                               ShaderProgramKind::UI,
                               BuiltInShaderContract( true, false, false, false, true ) );
    RegisterShaderSourceAsset( "shader.reflect_rt",
                               "shaders/reflect.rt",
                               ShaderProgramKind::RayTracing,
                               BuiltInShaderContract( true, false, false, false, false ) );
    RegisterShaderSourceAsset( "shader.generate_mips",
                               "shaders/generate_mips",
                               ShaderProgramKind::Compute,
                               BuiltInShaderContract( true, false, false, false, false ) );
}

const std::string& AssetSystem::GetDataRoot() const
{
    return m_dataRoot;
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

const SourceAssetRecord&
AssetSystem::RegisterSourceAsset( AssetKind kind, const char* logicalName, const char* relativePath )
{
    if ( !logicalName || logicalName[0] == '\0' )
    {
        throw std::invalid_argument( "AssetSystem::RegisterSourceAsset requires a logical name." );
    }
    if ( !relativePath || relativePath[0] == '\0' )
    {
        throw std::invalid_argument( "AssetSystem::RegisterSourceAsset requires a relative path." );
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

const SourceAssetRecord* AssetSystem::FindSourceAsset( const char* logicalName ) const
{
    if ( !logicalName || logicalName[0] == '\0' )
    {
        return nullptr;
    }

    for ( const SourceAssetRecord& record : m_sourceAssets )
    {
        if ( record.logicalName == logicalName )
        {
            return &record;
        }
    }
    return nullptr;
}

const SourceAssetRecord* AssetSystem::FindSourceAssetById( AssetId id ) const
{
    if ( id == 0 )
    {
        return nullptr;
    }

    for ( const SourceAssetRecord& record : m_sourceAssets )
    {
        if ( record.id == id )
        {
            return &record;
        }
    }
    return nullptr;
}

const TextureSourceAsset& AssetSystem::RegisterTextureSourceAsset( const char* logicalName,
                                                                   const char* relativePath,
                                                                   uint32_t legacyHash,
                                                                   bool generateMips,
                                                                   bool linearFilter,
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

const TextureSourceAsset* AssetSystem::FindTextureSourceAsset( const char* logicalName ) const
{
    if ( !logicalName || logicalName[0] == '\0' )
    {
        return nullptr;
    }

    for ( const TextureSourceAsset& texture : m_textureAssets )
    {
        if ( texture.logicalName == logicalName )
        {
            return &texture;
        }
    }
    return nullptr;
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

const TextureSourceAsset* AssetSystem::FindTextureSourceAssetById( AssetId id ) const
{
    if ( id == 0 )
    {
        return nullptr;
    }

    for ( const TextureSourceAsset& texture : m_textureAssets )
    {
        if ( texture.id == id )
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

const ShaderSourceAsset& AssetSystem::RegisterShaderSourceAsset( const char* logicalName,
                                                                 const char* baseName,
                                                                 ShaderProgramKind kind,
                                                                 ShaderProgramContract contract )
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

const std::vector<ShaderSourceAsset>& AssetSystem::GetShaderSourceAssets() const
{
    return m_shaderAssets;
}

std::unique_ptr<Rendering::IShader> AssetSystem::CreateShader( Rendering::IRenderResourceFactory& renderResources,
                                                               const char* logicalNameOrBaseName ) const
{
    if ( !logicalNameOrBaseName || logicalNameOrBaseName[0] == '\0' )
    {
        throw std::invalid_argument( "AssetSystem::CreateShader requires a logical name or base name." );
    }

    const ShaderSourceAsset* shader = FindShaderSourceAsset( logicalNameOrBaseName );
    const char* fallbackBaseName = BuiltInShaderBaseNameForLogicalName( logicalNameOrBaseName );
    return renderResources.CreateShader( shader ? shader->baseName.c_str()
                                                : ( fallbackBaseName ? fallbackBaseName : logicalNameOrBaseName ) );
}


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

const AssetLibrarySourceAsset* AssetSystem::FindAssetLibrarySourceAssetById( AssetId id ) const
{
    if ( id == 0 )
    {
        return nullptr;
    }

    for ( const AssetLibrarySourceAsset& library : m_assetLibraryAssets )
    {
        if ( library.id == id )
        {
            return &library;
        }
    }
    return nullptr;
}

const std::vector<AssetLibrarySourceAsset>& AssetSystem::GetAssetLibrarySourceAssets() const
{
    return m_assetLibraryAssets;
}

void AssetSystem::Clear()
{
    m_sourceAssets.clear();
    m_textureAssets.clear();
    m_shaderAssets.clear();
    m_assetLibraryAssets.clear();
    m_nextAssetId = 1;
    m_nextGeneration = 1;
}

size_t AssetSystem::GetSourceAssetCount() const
{
    return m_sourceAssets.size();
}

size_t AssetSystem::GetTextureSourceAssetCount() const
{
    return m_textureAssets.size();
}

size_t AssetSystem::GetShaderSourceAssetCount() const
{
    return m_shaderAssets.size();
}

size_t AssetSystem::GetAssetLibrarySourceAssetCount() const
{
    return m_assetLibraryAssets.size();
}

const char* BuiltInShaderBaseName( const char* logicalNameOrBaseName )
{
    return BuiltInShaderBaseNameForLogicalName( logicalNameOrBaseName );
}
} // namespace Assets
} // namespace SkullbonezCore
