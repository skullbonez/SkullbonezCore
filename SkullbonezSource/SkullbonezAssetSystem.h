#pragma once

#include "SkullbonezCommon.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Assets
{
enum class AssetKind
{
    Unknown,
    Texture,
    Shader,
    Mesh,
    Terrain,
    Font,
    Scene
};

struct SourceAssetRecord
{
    AssetKind kind = AssetKind::Unknown;
    std::string logicalName;
    std::string relativePath;
    std::string resolvedPath;
    uint32_t generation = 0;
};

class AssetSystem
{
  public:
    explicit AssetSystem( std::string dataRoot = DATA_ROOT );

    const std::string& GetDataRoot() const;
    std::string ResolvePath( const char* relativePath ) const;

    const SourceAssetRecord& RegisterSourceAsset( AssetKind kind, const char* logicalName, const char* relativePath );
    const SourceAssetRecord* FindSourceAsset( const char* logicalName ) const;

    void Clear();
    size_t GetSourceAssetCount() const;

  private:
    std::string m_dataRoot;
    std::vector<SourceAssetRecord> m_sourceAssets;
    uint32_t m_nextGeneration = 1;
};
} // namespace Assets
} // namespace SkullbonezCore
