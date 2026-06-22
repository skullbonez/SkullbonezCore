/*
File: SkullbonezSource/Assets/AssetSystem.h
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
  - SkullbonezSource/Assets/AssetSystem.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Common.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class IShader;
}

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

    const std::string& GetDataRoot() const;
    std::string ResolvePath( const char* relativePath ) const;

    const SourceAssetRecord& RegisterSourceAsset( AssetKind kind, const char* logicalName, const char* relativePath );
    const SourceAssetRecord* FindSourceAsset( const char* logicalName ) const;
    const SourceAssetRecord* FindSourceAssetById( AssetId id ) const;

    const TextureSourceAsset& RegisterTextureSourceAsset( const char* logicalName,
                                                          const char* relativePath,
                                                          uint32_t legacyHash,
                                                          bool generateMips = true,
                                                          bool linearFilter = true,
                                                          int channelsHint = 3 );
    const TextureSourceAsset* FindTextureSourceAsset( const char* logicalName ) const;
    const TextureSourceAsset* FindTextureSourceAssetByLegacyHash( uint32_t legacyHash ) const;
    const TextureSourceAsset* FindTextureSourceAssetById( AssetId id ) const;
    const std::vector<TextureSourceAsset>& GetTextureSourceAssets() const;

    const ShaderSourceAsset& RegisterShaderSourceAsset( const char* logicalName,
                                                        const char* baseName,
                                                        ShaderProgramKind kind = ShaderProgramKind::Unknown,
                                                        ShaderProgramContract contract = {} );
    const ShaderSourceAsset* FindShaderSourceAsset( const char* logicalNameOrBaseName ) const;
    const std::vector<ShaderSourceAsset>& GetShaderSourceAssets() const;
    std::unique_ptr<Rendering::IShader> CreateShader( const char* logicalNameOrBaseName ) const;

    const AssetLibrarySourceAsset& RegisterAssetLibrarySourceAsset( const char* logicalName, const char* relativePath );
    const AssetLibrarySourceAsset* FindAssetLibrarySourceAsset( const char* logicalName ) const;
    const AssetLibrarySourceAsset* FindAssetLibrarySourceAssetById( AssetId id ) const;
    const std::vector<AssetLibrarySourceAsset>& GetAssetLibrarySourceAssets() const;

    void Clear();
    size_t GetSourceAssetCount() const;
    size_t GetTextureSourceAssetCount() const;
    size_t GetShaderSourceAssetCount() const;
    size_t GetAssetLibrarySourceAssetCount() const;

  private:
    std::string m_dataRoot;
    std::vector<SourceAssetRecord> m_sourceAssets;
    std::vector<TextureSourceAsset> m_textureAssets;
    std::vector<ShaderSourceAsset> m_shaderAssets;
    std::vector<AssetLibrarySourceAsset> m_assetLibraryAssets;
    AssetId m_nextAssetId = 1;
    uint32_t m_nextGeneration = 1;
};

// Transitional bridge for legacy singleton-style render helpers. The run loop
// owns the real AssetSystem, while helpers still own their GPU shader handles.
void BindActiveAssetSystem( AssetSystem* assets );
AssetSystem* ActiveAssetSystem();
std::unique_ptr<Rendering::IShader> CreateShaderFromActiveAssets( const char* logicalNameOrBaseName );
} // namespace Assets
} // namespace SkullbonezCore
