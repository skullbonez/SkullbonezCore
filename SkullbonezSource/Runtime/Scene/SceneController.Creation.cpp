// Scene creation reserves an in-memory draft. Only explicit Save writes its path.
#include "SceneController.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/WindowConstants.h"
#include "../../Core/Common.h"
#include "../../Core/Log.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
bool IsSceneNameChar( char value )
{
    return ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) || value == '-' || value == '_';
}

std::string SanitizeSceneFileName( const char* requestedName )
{
    std::string clean;

    if ( requestedName )
    {
        for ( const char* cursor = requestedName; *cursor != '\0' && clean.size() < 48; ++cursor )
        {
            const char value = *cursor;

            if ( IsSceneNameChar( value ) )
            {
                clean.push_back( value );
            }
            else if ( value == ' ' || value == '.' )
            {
                clean.push_back( '_' );
            }
        }
    }

    while ( !clean.empty() && clean.front() == '_' )
    {
        clean.erase( clean.begin() );
    }

    while ( !clean.empty() && clean.back() == '_' )
    {
        clean.pop_back();
    }

    return clean;
}

std::string NormalizeScenePathForCreate( const std::string& path )
{
    std::string normalized = path;

    for ( char& value : normalized )
    {
        if ( value == '\\' )
        {
            value = '/';
        }
    }

    return normalized;
}

std::filesystem::path UniqueScenePath( const std::filesystem::path& sceneDir, const std::string& baseName, std::error_code& error, const SceneController& controller )
{
    // Recoverable error: directory probing is editor-authored IO. Preserve filesystem
    // errors for the caller instead of invoking a throwing overload.
    std::filesystem::path candidate = sceneDir / ( baseName + ".scene.json" );

    if ( !std::filesystem::exists( candidate, error ) && controller.FindNormalizedPath( NormalizeScenePathForCreate( candidate.generic_string() ) ) < 0 )
    {
        return error ? std::filesystem::path() : candidate;
    }

    for ( int suffix = 2; suffix < 1000; ++suffix )
    {
        char numberedName[80] = {};
        std::snprintf( numberedName, sizeof( numberedName ), "%s_%02d.scene.json", baseName.c_str(), suffix );
        candidate = sceneDir / numberedName;

        if ( !std::filesystem::exists( candidate, error ) && controller.FindNormalizedPath( NormalizeScenePathForCreate( candidate.generic_string() ) ) < 0 )
        {
            return error ? std::filesystem::path() : candidate;
        }
    }

    return std::filesystem::path();
}

} // namespace

SceneLoadRequest SceneController::CreateScene( const char* requestedName, const char* heightMap )
{
    // Concept: Creating a scene queues a load action instead of loading
    // directly, keeping filesystem work separate from Run's scene side effects.
    Core::Allocation::RuntimeAllocationScope createScope( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    const std::string cleanName = SanitizeSceneFileName( requestedName );

    if ( cleanName.empty() )
    {
        return SceneLoadRequest::None();
    }

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    std::error_code ec;
    const std::filesystem::path scenePath = UniqueScenePath( sceneDir, cleanName, ec, *this );

    if ( scenePath.empty() || ec )
    {
        SkullbonezCore::Core::Log().WriteEventf( "scene_create_failed name=\"%s\" reason=\"write\"", cleanName.c_str() );

        return SceneLoadRequest::None();
    }

    const std::string normalizedPath = NormalizeScenePathForCreate( scenePath.generic_string() );
    // A failed activation can already own the current queue slot. Retain its
    // metadata until a successful replacement can retire that inactive slot.
    if ( m_pendingDraft.index == CurrentIndex() && HasCurrentEntry() )
    {
        RemoveInactiveEntry( m_activeDraft.index );
        m_activeDraft = std::move( m_pendingDraft );
    }
    else
    {
        RemoveInactiveEntry( m_pendingDraft.index );
    }
    m_pendingDraft.index = Append( normalizedPath );
    m_pendingDraft.heightMap = heightMap && *heightMap ? std::filesystem::absolute( heightMap ).lexically_normal().generic_string() : "";
    return SceneLoadRequest::Load( m_pendingDraft.index, true, true, false, true );
}

bool SceneController::CurrentSceneIsUnsaved() const
{
    return HasCurrentEntry() && ( CurrentIndex() == m_activeDraft.index || CurrentIndex() == m_pendingDraft.index );
}

Core::SbResult SceneController::ReadCurrentDefinition( const Assets::AssetSystem& assets, AuthoredScene& scene ) const
{
    if ( CurrentSceneIsUnsaved() )
    {
        const SceneDraft& draft = CurrentIndex() == m_pendingDraft.index ? m_pendingDraft : m_activeDraft;
        scene = AuthoredScene::CreateEditableStarter( draft.heightMap.c_str() );
        return Core::SbResult::Success();
    }
    return AuthoredScene::TryLoadFromFile( m_resultDiagnostics, CurrentPath()->c_str(), assets, scene );
}

void SceneController::CompleteDraftActivation()
{
    // Abandonment discards only in-memory draft metadata and its inactive queue
    // slot. Stable indices and every user-authored disk file remain unchanged.
    if ( m_activeDraft.index != CurrentIndex() )
    {
        RemoveInactiveEntry( m_activeDraft.index );
        m_activeDraft = {};
    }
    if ( m_pendingDraft.index == CurrentIndex() )
    {
        m_activeDraft = std::move( m_pendingDraft );
        m_pendingDraft = {};
    }
    else
    {
        RemoveInactiveEntry( m_pendingDraft.index );
        m_pendingDraft = {};
    }
}

} // namespace Runtime
} // namespace SkullbonezCore
