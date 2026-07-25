/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp
Purpose:
  Creates starter scene files and returns scene-control actions outside Run.

Summary:
  Creating a scene is scene-runtime policy: sanitize a user-facing name, create
  a deterministic starter scene, append the path to the scene queue, and ask
  the caller to load it interactively. UI refresh is a returned consumer effect.

Glossary:
  Starter scene: Minimal `.scene.json` written for a newly created editable
    scene.
  Sanitized filename: User-provided scene name reduced to a bounded safe file
    stem.
  Scene queue action: Control intent returned so Run loads the new file.

Invariants:
  - Starter scene JSON shape and field names are user-facing compatibility
    surface.
  - Generated filenames stay limited to the existing 48-character sanitized
    base plus numeric suffix behavior.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeCreate.h"
#include "../../Core/WindowConstants.h"
#include "SceneController.h"
#include "../../Core/Common.h"
#include "../../Core/Log.h"

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
using Json = nlohmann::ordered_json;

bool IsSceneNameChar( char value )
{
    return ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) ||
           value == '-' || value == '_';
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

std::filesystem::path
UniqueScenePath( const std::filesystem::path& sceneDir, const std::string& baseName, std::error_code& error )
{
    // Lane R: directory probing is editor-authored IO. Preserve filesystem
    // errors for the caller instead of invoking a throwing overload.
    std::filesystem::path candidate = sceneDir / ( baseName + ".scene.json" );
    if ( !std::filesystem::exists( candidate, error ) )
    {
        return error ? std::filesystem::path() : candidate;
    }

    for ( int suffix = 2; suffix < 1000; ++suffix )
    {
        char numberedName[80] = {};
        std::snprintf( numberedName, sizeof( numberedName ), "%s_%02d.scene.json", baseName.c_str(), suffix );
        candidate = sceneDir / numberedName;
        if ( !std::filesystem::exists( candidate, error ) )
        {
            return error ? std::filesystem::path() : candidate;
        }
    }
    return std::filesystem::path();
}

bool WriteStarterSceneFile( const std::filesystem::path& path, const std::string& displayName )
{
    std::ofstream output( path, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    // Invariant: Starter scene keys are the compatibility surface for newly
    // editable scenes. Keep this shape aligned with AuthoredScene parsing.
    Json scene;
    scene["format"] = "skullbonez.scene.json";
    scene["version"] = 1;
    scene["name"] = displayName;
    scene["simulation"] = {
        { "physics", true },
        { "text", true },
        { "world",
          {
              { "gravity", -9.81f },
              { "fluidHeight", 0.0f },
              { "fluidDensity", 0.0f },
          } },
    };
    scene["editor"] = {
        { "editableScene", true },
    };
    scene["playback"] = {
        { "frames", "unlimited" },
        { "fixedStep", true },
    };
    scene["debug"] = {
        { "waterHidden", true },
    };
    scene["terrain"] = {
        { "flatSlope",
          {
              { "baseY", 30.0f },
              { "slopeX", 0.0f },
              { "slopeZ", 0.0f },
          } },
    };
    scene["cameras"] = Json::array(
        {
            {
                { "name", "main" },
                { "position", Json::array( { 500.0f, 120.0f, 760.0f } ) },
                { "view", Json::array( { 500.0f, 45.0f, 500.0f } ) },
                { "up", Json::array( { 0.0f, 1.0f, 0.0f } ) },
            },
        }
    );
    scene["objects"] = Json::array();
    output << scene.dump( 2 ) << '\n';
    return output.good();
}

} // namespace

SceneLoadRequest CreateSceneFromUI( SceneRuntimeCreateContext context, const char* requestedName )
{
    // Concept: Creating a scene queues a load action instead of loading
    // directly, keeping filesystem work separate from Run's scene side effects.
    const std::string cleanName = SanitizeSceneFileName( requestedName );
    if ( cleanName.empty() )
    {
        return SceneLoadRequest::None();
    }

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    std::error_code ec;
    std::filesystem::create_directories( sceneDir, ec );
    if ( ec )
    {
        SkullbonezCore::Core::Log().WriteEventf(
            "scene_create_failed name=\"%s\" reason=\"mkdir\" message=\"%s\"",
            cleanName.c_str(),
            ec.message().c_str()
        );
        return SceneLoadRequest::None();
    }

    const std::filesystem::path scenePath = UniqueScenePath( sceneDir, cleanName, ec );
    if ( scenePath.empty() || !WriteStarterSceneFile( scenePath, cleanName ) )
    {
        SkullbonezCore::Core::Log().WriteEventf(
            "scene_create_failed name=\"%s\" reason=\"write\"",
            cleanName.c_str()
        );
        return SceneLoadRequest::None();
    }

    const std::string normalizedPath = NormalizeScenePathForCreate( scenePath.generic_string() );
    return SceneLoadRequest::Load( context.controller.Append( normalizedPath ), true, true, false, true );
}

} // namespace Runtime
} // namespace SkullbonezCore
