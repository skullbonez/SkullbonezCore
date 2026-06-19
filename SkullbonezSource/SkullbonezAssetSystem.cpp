/*
File: SkullbonezSource/SkullbonezAssetSystem.cpp
Purpose:
  Loads, owns, and resolves reusable runtime assets for scenes and render code.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezAssetSystem.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezAssetSystem.h"
#include "SkullbonezIRenderBackend.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace SkullbonezCore
{
namespace Assets
{
namespace
{
AssetSystem* g_activeAssetSystem = nullptr;

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
        { "shader.nudge_laser", "shaders/nudge_laser" },
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
    return ( path.size() >= 2 && path[1] == ':' ) ||
           ( !path.empty() && ( path[0] == '/' || path[0] == '\\' ) );
}

bool EndsWithPathSeparator( const std::string& path )
{
    return !path.empty() && ( path.back() == '/' || path.back() == '\\' );
}
} // namespace

AssetSystem::AssetSystem( std::string dataRoot )
    : m_dataRoot( std::move( dataRoot ) )
{
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

const SourceAssetRecord& AssetSystem::RegisterSourceAsset( AssetKind kind, const char* logicalName, const char* relativePath )
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
        if ( texture.id == source.id || texture.logicalName == logicalName || ( legacyHash != 0 && texture.legacyHash == legacyHash ) )
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

std::unique_ptr<Rendering::IShader> AssetSystem::CreateShader( const char* logicalNameOrBaseName ) const
{
    if ( !logicalNameOrBaseName || logicalNameOrBaseName[0] == '\0' )
    {
        throw std::invalid_argument( "AssetSystem::CreateShader requires a logical name or base name." );
    }

    const ShaderSourceAsset* shader = FindShaderSourceAsset( logicalNameOrBaseName );
    const char* fallbackBaseName = BuiltInShaderBaseNameForLogicalName( logicalNameOrBaseName );
    return Rendering::Gfx().CreateShader( shader ? shader->baseName.c_str() : ( fallbackBaseName ? fallbackBaseName : logicalNameOrBaseName ) );
}

const AssetLibrarySourceAsset& AssetSystem::RegisterAssetLibrarySourceAsset( const char* logicalName, const char* relativePath )
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

void BindActiveAssetSystem( AssetSystem* assets )
{
    g_activeAssetSystem = assets;
}

AssetSystem* ActiveAssetSystem()
{
    return g_activeAssetSystem;
}

std::unique_ptr<Rendering::IShader> CreateShaderFromActiveAssets( const char* logicalNameOrBaseName )
{
    if ( g_activeAssetSystem )
    {
        return g_activeAssetSystem->CreateShader( logicalNameOrBaseName );
    }
    const char* fallbackBaseName = BuiltInShaderBaseNameForLogicalName( logicalNameOrBaseName );
    return Rendering::Gfx().CreateShader( fallbackBaseName ? fallbackBaseName : logicalNameOrBaseName );
}
} // namespace Assets
} // namespace SkullbonezCore
