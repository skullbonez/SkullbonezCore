#include "SkullbonezAssetSystem.h"

#include <stdexcept>
#include <utility>

namespace SkullbonezCore
{
namespace Assets
{
namespace
{
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

void AssetSystem::Clear()
{
    m_sourceAssets.clear();
}

size_t AssetSystem::GetSourceAssetCount() const
{
    return m_sourceAssets.size();
}
} // namespace Assets
} // namespace SkullbonezCore
