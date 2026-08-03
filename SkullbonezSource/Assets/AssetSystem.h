/*
File: SkullbonezSource/Assets/AssetSystem.h
Purpose:
  Loads, owns, and resolves reusable runtime assets for scenes and render code.

Summary:
  AssetSystem owns the process asset registry and identity sequence. Scenes and
  render setup borrow its source records while backend owners retain GPU
  resource lifetime.

Glossary:
  Asset-system borrow: Frame- or operation-local pointer/reference that lets
    parsing or tool code resolve records without a process-global bridge.

Invariants:
  - AssetId values are AssetSystem-owned identities and must not be invented by
    callers.
  - Shader and texture source records describe load intent; backend GPU resource
    lifetime is owned by renderer-facing systems.

Related:
  - SkullbonezSource/Assets/AssetSystem.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "../Core/Common.h"
#include "../Core/WindowConstants.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core
namespace Runtime
{
}
namespace Rendering
{
class ShaderDX12;
class Dx12ResourceBuilder;
} // namespace Rendering

namespace Assets
{
using AssetId = uint32_t;

enum class AssetKind
{
    Unknown,
    Texture2D,
    ShaderProgram,
    MeshSource,
    MaterialPreset,
    StyleFile,
    SceneFile,
    AssetLibrary,
    Terrain,
    Font,
    Texture = Texture2D,
    Shader = ShaderProgram,
    Mesh = MeshSource,
    Scene = SceneFile
};

enum class ShaderProgramKind
{
    Unknown,
    LitTextured,
    UnlitTextured,
    ShadowDepth,
    PostProcess,
    DebugLine,
    Text,
    Water,
    UI,
    Collision,
    RayTracing,
    Compute
};

struct ShaderProgramContract
{
    bool usesTexture = false;
    bool usesLighting = false;
    bool usesInstancing = false;
    bool depthOnly = false;
    bool postProcess = false;
};

struct SourceAssetRecord
{
    AssetId id = 0;
    AssetKind kind = AssetKind::Unknown;
    std::string logicalName;
    std::string relativePath;
    std::string resolvedPath;
    uint32_t generation = 0;
};

struct TextureSourceAsset
{
    AssetId id = 0;
    std::string logicalName;
    std::string relativePath;
    std::string resolvedPath;
    uint32_t legacyHash = 0;
    bool generateMips = true;
    bool linearFilter = true;
    int channelsHint = 3;
};

struct ShaderSourceAsset
{
    AssetId id = 0;
    std::string logicalName;
    std::string baseName;
    std::string resolvedBasePath;
    ShaderProgramKind kind = ShaderProgramKind::Unknown;
    ShaderProgramContract contract;
};

struct AssetLibrarySourceAsset
{
    AssetId id = 0;
    std::string logicalName;
    std::string relativePath;
    std::string resolvedPath;
};

class AssetSystem
{
  public:
    explicit AssetSystem( std::string dataRoot = DATA_ROOT );

    void RegisterBuiltInSourceAssets( const SkullbonezCore::Core::EngineConfig& config );
    std::string ResolvePath( const char* relativePath ) const;

    const SourceAssetRecord& RegisterSourceAsset( AssetKind kind, const char* logicalName, const char* relativePath );
    std::string RegisterSourceAssetPath( AssetKind kind, const char* logicalName, const char* relativePath );

    const TextureSourceAsset& RegisterTextureSourceAsset( const char* logicalName, const char* relativePath,
                                                          uint32_t legacyHash, bool generateMips = true,
                                                          bool linearFilter = true, int channelsHint = 3 );
    const TextureSourceAsset* FindTextureSourceAssetByLegacyHash( uint32_t legacyHash ) const;
    const std::vector<TextureSourceAsset>& GetTextureSourceAssets() const;

    const ShaderSourceAsset& RegisterShaderSourceAsset( const char* logicalName, const char* baseName,
                                                        ShaderProgramKind kind = ShaderProgramKind::Unknown,
                                                        ShaderProgramContract contract = {} );
    const ShaderSourceAsset* FindShaderSourceAsset( const char* logicalNameOrBaseName ) const;
    std::unique_ptr<Rendering::ShaderDX12> CreateShader( Rendering::Dx12ResourceBuilder& renderResources,
                                                         const char* logicalNameOrBaseName ) const;

    const AssetLibrarySourceAsset& RegisterAssetLibrarySourceAsset( const char* logicalName, const char* relativePath );
    const AssetLibrarySourceAsset* FindAssetLibrarySourceAsset( const char* logicalName ) const;


  private:
    std::string m_dataRoot;
    std::vector<SourceAssetRecord> m_sourceAssets;
    std::vector<TextureSourceAsset> m_textureAssets;
    std::vector<ShaderSourceAsset> m_shaderAssets;
    std::vector<AssetLibrarySourceAsset> m_assetLibraryAssets;
    AssetId m_nextAssetId = 1;
    uint32_t m_nextGeneration = 1;
};

} // namespace Assets
} // namespace SkullbonezCore
