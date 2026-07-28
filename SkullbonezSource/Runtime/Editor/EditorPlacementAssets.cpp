/*
File: SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
Purpose:
  Owns editor placeable asset recipes, authored hull/material lookup, and placement bounds helpers.

Summary:
  This file compiles the editor recipe tables once and exposes typed helpers
  through EditorPlacementAssets.h. Input routing, tracer ghosts, and placement
  commit all use this same source of recipe truth.

Glossary:
  Asset system: Runtime-owned registry used to resolve editor asset-library
    names before falling back to conventional data paths.
  Placement recipe: Data and helper logic used to preview or spawn one editor object type.
  Asset primitive: Single collision shape in a placeable asset recipe, such as a
    box, sphere, or convex hull.
  Authored hull: Baked convex hull asset used for editor-placeable collision geometry.

Invariants:
  - Preview bounds and placement commit must read the same asset recipe helpers.
  - Building-library path lookup borrows the runtime asset system; the parsed
    catalog cache is read-only after first load.
  - Header-visible declarations are the shared editor asset boundary; recipe
    tables and caches stay local to this TU.

Related:
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorHullAssets.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "EditorPlacementAssets.h"
#include "EditorTools.h"
#include "../../Assets/AssetSystem.h"
#include "../../UI/UITabEditor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::Math::Vector::Vector3;
using Json = SkullbonezCore::Runtime::EditorPlacementJson;

namespace SkullbonezCore
{
namespace Runtime
{

constexpr EditorBuildingDefinition EDITOR_BUILDING_ASSETS[] = {
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP, "building.brick_house_low", "bhl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP, "building.brick_house_high", "bhh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP, "building.cute_house_low", "chl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP, "building.cute_house_high", "chh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP, "building.triple_decker_low", "tdl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP, "building.triple_decker_high", "tdh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP, "building.brick_wall_200", "bw200" },
};


Vector3 HullAuthoredLocalOffset( const ConvexHullShape& hull )
{
    return hull.GetPosition() + hull.GetAuthoredCenterOfMass();
}


float HullAuthoredBottomOffset( const ConvexHullShape& hull )
{
    float minY = FLT_MAX;
    const Vector3 authoredOffset = HullAuthoredLocalOffset( hull );

    for ( uint16_t i = 0; i < hull.GetVertexCount(); ++i )
    {
        minY = (std::min)( minY, authoredOffset.y + hull.GetVertex( i ).y );
    }

    return minY == FLT_MAX ? 0.0f : -minY;
}


const EditorBuildingDefinition* EditorBuildingDefinitionForType( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );

    for ( const EditorBuildingDefinition& building : EDITOR_BUILDING_ASSETS )
    {

        if ( building.objectType == type )
        {
            return &building;
        }
    }

    return nullptr;
}


std::string EditorResolveBuildingAssetLibraryPath( const SkullbonezCore::Assets::AssetSystem& assets )
{

    if ( const SkullbonezCore::Assets::AssetLibrarySourceAsset* library = assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) )
    {
        return library->resolvedPath;
    }

    return "SkullbonezData/assets/buildings.assets.json";
}


const Json* EditorJsonFindMember( const Json& object, const char* name )
{

    if ( !object.is_object() )
    {
        return nullptr;
    }

    const auto it = object.find( name );
    return it == object.end() ? nullptr : &*it;
}


const Json* CachedEditorBuildingLibrary( const SkullbonezCore::Assets::AssetSystem& assets )
{

    // Lifetime: The building asset catalog is process-static editor data.
    // It is read-only after the first successful parse, so placement preview and
    // commit paths share one stable recipe source without repeated disk IO. The
    // first caller resolves the path through the borrowed runtime AssetSystem.
    static Json library;
    static bool loaded = false;
    static bool valid = false;

    if ( !loaded )
    {
        loaded = true;
        const std::string path = EditorResolveBuildingAssetLibraryPath( assets );
        std::ifstream file( path );

        if ( file )
        {
            library = Json::parse( file, nullptr, false );
            valid = library.is_object() && !library.is_discarded();
        }

        if ( !valid )
        {
            fprintf( stderr, "[editor] Cannot load building asset library: %s\n", path.c_str() );
        }
    }

    return valid ? &library : nullptr;
}


const Json* CachedEditorBuildingAsset( int objectType, const SkullbonezCore::Assets::AssetSystem& assets )
{
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( objectType );
    const Json* library = CachedEditorBuildingLibrary( assets );

    if ( !building || !library )
    {
        return nullptr;
    }

    const Json* assetArray = EditorJsonFindMember( *library, "assets" );

    if ( !assetArray || !assetArray->is_array() )
    {
        return nullptr;
    }

    for ( const Json& asset : *assetArray )
    {
        const Json* name = EditorJsonFindMember( asset, "name" );

        if ( name && name->is_string() && name->get<std::string>() == building->assetName )
        {
            return &asset;
        }
    }

    return nullptr;
}


std::string EditorJsonStringOr( const Json& object, const char* name, const char* fallback )
{
    const Json* value = EditorJsonFindMember( object, name );
    return value && value->is_string() ? value->get<std::string>() : std::string( fallback );
}


float EditorJsonFloatOr( const Json& object, const char* name, float fallback )
{
    const Json* value = EditorJsonFindMember( object, name );
    return value && value->is_number() ? value->get<float>() : fallback;
}


bool EditorJsonBoolOr( const Json& object, const char* name, bool fallback )
{
    const Json* value = EditorJsonFindMember( object, name );
    return value && value->is_boolean() ? value->get<bool>() : fallback;
}


bool TryReadEditorJsonVec3( const Json& value, Vector3& out )
{

    if ( !value.is_array() || value.size() < 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number() )
    {
        return false;
    }

    out = Vector3( value[0].get<float>(), value[1].get<float>(), value[2].get<float>() );
    return true;
}


Vector3 EditorJsonVec3Or( const Json& object, const char* name, const Vector3& fallback )
{
    const Json* value = EditorJsonFindMember( object, name );
    Vector3 result = fallback;
    return value && TryReadEditorJsonVec3( *value, result ) ? result : fallback;
}


std::string EditorAssetPrimitiveType( const Json& part )
{
    const std::string type = EditorJsonStringOr( part, "type", "" );

    if ( type.empty() && EditorJsonFindMember( part, "hull" ) )
    {
        return "convexHull";
    }

    return type;
}


bool IsEditorAssetPrimitiveType( const std::string& type )
{
    return type == "convexHull" || type == "box" || type == "sphere";
}


bool TryReadEditorBoxHalfExtents( const Json& part, Vector3& outHalfExtents )
{
    const Json* halfExtents = EditorJsonFindMember( part, "halfExtents" );

    if ( !halfExtents || !TryReadEditorJsonVec3( *halfExtents, outHalfExtents ) )
    {
        return false;
    }

    return outHalfExtents.x > 0.0f && outHalfExtents.y > 0.0f && outHalfExtents.z > 0.0f;
}


bool TryReadEditorSphereRadius( const Json& part, float& outRadius )
{
    outRadius = EditorJsonFloatOr( part, "radius", 0.0f );
    return outRadius > 0.0f;
}


void IncludeEditorBoundsPoint( const Vector3& point, Vector3& inOutMin, Vector3& inOutMax )
{
    inOutMin.x = (std::min)( inOutMin.x, point.x );
    inOutMin.y = (std::min)( inOutMin.y, point.y );
    inOutMin.z = (std::min)( inOutMin.z, point.z );
    inOutMax.x = (std::max)( inOutMax.x, point.x );
    inOutMax.y = (std::max)( inOutMax.y, point.y );
    inOutMax.z = (std::max)( inOutMax.z, point.z );
}


std::string EditorLowercase( std::string value )
{
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

    return value;
}


SkullbonezCore::Rendering::RenderMaterialKind EditorMaterialKindFromAssetToken( const std::string& token )
{
    const std::string mode = EditorLowercase( token );

    if ( mode == "matte" || mode == "solid" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Matte;
    }

    if ( mode == "metal" || mode == "chrome" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Metal;
    }

    if ( mode == "emissive" || mode == "neon" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Emissive;
    }

    if ( mode == "glass" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Glass;
    }

    if ( mode == "toon" || mode == "pixar" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Toon;
    }

    if ( mode == "lowpoly" || mode == "low_poly" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::LowPoly;
    }

    if ( mode == "shadow" || mode == "black" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Shadow;
    }

    if ( mode == "foliage" || mode == "leaf" || mode == "leaves" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Foliage;
    }

    if ( mode == "bark" || mode == "trunk" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Bark;
    }

    if ( mode == "stone" || mode == "rock" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Stone;
    }

    if ( mode == "ridge" || mode == "distant" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Ridge;
    }

    if ( mode == "shore" || mode == "sand" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Shore;
    }

    if ( mode == "pine" || mode == "conifer" )
    {
        return SkullbonezCore::Rendering::RenderMaterialKind::Pine;
    }

    return SkullbonezCore::Rendering::RenderMaterialKind::Textured;
}


float EditorMaterialLegacyModeFromAsset( const Json& material )
{
    const Json* modeValue = EditorJsonFindMember( material, "mode" );

    if ( !modeValue )
    {
        modeValue = EditorJsonFindMember( material, "kind" );
    }

    if ( modeValue && modeValue->is_number() )
    {
        return modeValue->get<float>();
    }

    if ( modeValue && modeValue->is_string() )
    {
        const SkullbonezCore::Rendering::RenderMaterialKind kind = EditorMaterialKindFromAssetToken( modeValue->get<std::string>() );

        return SkullbonezCore::Rendering::RenderMaterialKindLegacyMode( kind );
    }

    return SkullbonezCore::Rendering::RenderMaterialKindLegacyMode( SkullbonezCore::Rendering::RenderMaterialKind::Stone );
}


SkullbonezCore::Rendering::RenderMaterial EditorBuildingPartMaterial( const Json& part )
{
    const Json* materialJson = EditorJsonFindMember( part, "material" );
    Vector3 color( 1.0f, 1.0f, 1.0f );
    float mode = SkullbonezCore::Rendering::RenderMaterialKindLegacyMode( SkullbonezCore::Rendering::RenderMaterialKind::Stone );

    if ( materialJson && materialJson->is_object() )
    {

        if ( const Json* colorValue = EditorJsonFindMember( *materialJson, "color" ) )
        {
            TryReadEditorJsonVec3( *colorValue, color );
        }
        else if ( const Json* colourValue = EditorJsonFindMember( *materialJson, "colour" ) )
        {
            TryReadEditorJsonVec3( *colourValue, color );
        }
        else if ( const Json* tintValue = EditorJsonFindMember( *materialJson, "tint" ) )
        {
            TryReadEditorJsonVec3( *tintValue, color );
        }

        mode = EditorMaterialLegacyModeFromAsset( *materialJson );
    }

    SkullbonezCore::Rendering::RenderMaterial
        material = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( color.x, color.y, color.z, mode );

    if ( materialJson && materialJson->is_object() )
    {
        material.roughness = std::clamp( EditorJsonFloatOr( *materialJson, "roughness", material.roughness ), 0.0f, 1.0f );

        material.metallic = std::clamp( EditorJsonFloatOr( *materialJson, "metallic", material.metallic ), 0.0f, 1.0f );
        material.specular = std::clamp( EditorJsonFloatOr( *materialJson, "specular", material.specular ), 0.0f, 1.0f );
        material.transmission = std::clamp( EditorJsonFloatOr( *materialJson, "transmission", material.transmission ), 0.0f,
                                            1.0f );

        material.stylization = std::clamp( EditorJsonFloatOr( *materialJson, "stylization", material.stylization ), 0.0f,
                                           1.0f );

        if ( const Json* emissive = EditorJsonFindMember( *materialJson, "emissive" ) )
        {
            Vector3 emissiveColor;

            if ( TryReadEditorJsonVec3( *emissive, emissiveColor ) )
            {
                material.emissiveColor[0] = emissiveColor.x;
                material.emissiveColor[1] = emissiveColor.y;
                material.emissiveColor[2] = emissiveColor.z;
            }
        }

        material.emissiveStrength = (std::max)( 0.0f,
                                                EditorJsonFloatOr( *materialJson, "strength", material.emissiveStrength ) );

        material.flags = static_cast<uint32_t>( (std::max)( 0.0f, EditorJsonFloatOr( *materialJson, "flags", 0.0f ) ) );
        const std::string name = EditorJsonStringOr( *materialJson, "name",
                                                     SkullbonezCore::Rendering::RenderMaterialKindName( material.kind ) );

        strncpy_s( material.name, sizeof( material.name ), name.c_str(), _TRUNCATE );
    }

    return material;
}


Quaternion EditorQuaternionFromEulerDegrees( const Vector3& eulerDegrees )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    const float xHalf = eulerDegrees.x * DEG2RAD * 0.5f;
    const float yHalf = eulerDegrees.y * DEG2RAD * 0.5f;
    const float zHalf = eulerDegrees.z * DEG2RAD * 0.5f;
    const Quaternion xRotation( sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, sinf( zHalf ), cosf( zHalf ) );
    Quaternion q;
    q *= xRotation * yRotation * zRotation;
    q.Normalise();
    return q;
}


Quaternion EditorBuildingPartOrientation( const Quaternion& placementOrientation, const Json& part )
{
    Quaternion result = placementOrientation;
    const Vector3 euler = EditorJsonVec3Or( part, "euler", Vector3( 0.0f, 0.0f, 0.0f ) );

    if ( fabsf( euler.x ) > 1.0e-5f || fabsf( euler.y ) > 1.0e-5f || fabsf( euler.z ) > 1.0e-5f )
    {
        result *= EditorQuaternionFromEulerDegrees( euler );
        result.Normalise();
    }

    return result;
}


int EditorBuildingPartCount( int objectType, const SkullbonezCore::Assets::AssetSystem& assets )
{
    const Json* asset = CachedEditorBuildingAsset( objectType, assets );

    if ( !asset )
    {
        return 0;
    }

    const std::string type = EditorJsonStringOr( *asset, "type", "" );

    if ( IsEditorAssetPrimitiveType( type ) )
    {
        return 1;
    }

    const Json* parts = EditorJsonFindMember( *asset, "parts" );
    return type == "compound" && parts && parts->is_array() ? static_cast<int>( parts->size() ) : 0;
}


const ConvexHullShape* CachedEditorBuildingHull( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                 const std::string& hullPath )
{
    static std::vector<std::pair<std::string, ConvexHullShape>> hulls;

    for ( const auto& entry : hulls )
    {

        if ( entry.first == hullPath )
        {
            return &entry.second;
        }
    }

    ConvexHullShape hull;
    const SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( diagnostics,
                                                                                      ResolveEditorHullAssetPath( hullPath.c_str() ),
                                                                                      hull );

    if ( !hullLoad.Ok() )
    {
        fprintf( stderr, "[editor] Cannot cache building hull %s: %s\n", hullPath.c_str(), hullLoad.ErrorMessage() );
        return nullptr;
    }

    hulls.emplace_back( hullPath, hull );
    return &hulls.back().second;
}


float EditorBuildingVerticalSize( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                  const SkullbonezCore::Assets::AssetSystem& assets )
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    const bool
        ok = ForEachEditorBuildingPart( objectType, assets,
                                        [&]( const Json& part )
                                        {
                                            const Vector3 offset = EditorJsonVec3Or( part, "offset",
                                                                                     Vector3( 0.0f, 0.0f, 0.0f ) );

                                            const Quaternion
                                                orientation = EditorBuildingPartOrientation( IDENTITY_QUATERNION, part );

                                            Quaternion orientationCopy = orientation;
                                            const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
                                            const std::string primitiveType = EditorAssetPrimitiveType( part );

                                            if ( primitiveType == "convexHull" )
                                            {
                                                const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                                                const ConvexHullShape* hull = hullPath.empty()
                                                                                  ? nullptr
                                                                                  : CachedEditorBuildingHull( diagnostics,
                                                                                                              hullPath );

                                                if ( !hull )
                                                {
                                                    return;
                                                }

                                                const Vector3 hullLocalOffset = HullAuthoredLocalOffset( *hull );

                                                for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount();
                                                      ++vertexIndex )
                                                {
                                                    const float y = offset.y +
                                                                    ( rotation *
                                                                      ( hullLocalOffset + hull->GetVertex( vertexIndex ) ) )
                                                                        .y;

                                                    minY = (std::min)( minY, y );
                                                    maxY = (std::max)( maxY, y );
                                                }

                                                return;
                                            }

                                            if ( primitiveType == "box" )
                                            {
                                                Vector3 halfExtents;

                                                if ( !TryReadEditorBoxHalfExtents( part, halfExtents ) )
                                                {
                                                    return;
                                                }

                                                for ( int xSign = -1; xSign <= 1; xSign += 2 )
                                                {

                                                    for ( int ySign = -1; ySign <= 1; ySign += 2 )
                                                    {

                                                        for ( int zSign = -1; zSign <= 1; zSign += 2 )
                                                        {
                                                            const Vector3 corner( halfExtents.x *
                                                                                      static_cast<float>( xSign ),
                                                                                  halfExtents.y *
                                                                                      static_cast<float>( ySign ),
                                                                                  halfExtents.z *
                                                                                      static_cast<float>( zSign ) );
                                                            const float y = offset.y + ( rotation * corner ).y;
                                                            minY = (std::min)( minY, y );
                                                            maxY = (std::max)( maxY, y );
                                                        }
                                                    }
                                                }

                                                return;
                                            }

                                            if ( primitiveType == "sphere" )
                                            {
                                                float radius = 0.0f;

                                                if ( TryReadEditorSphereRadius( part, radius ) )
                                                {
                                                    minY = (std::min)( minY, offset.y - radius );
                                                    maxY = (std::max)( maxY, offset.y + radius );
                                                }
                                            }
                                        } );

    return ok && minY != FLT_MAX ? (std::max)( 1.0f, maxY - minY ) : 1.0f;
}


bool TryComputeEditorBuildingWorldBounds( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                          const Vector3& terrainPoint, const Quaternion& placementOrientation,
                                          const SkullbonezCore::Assets::AssetSystem& assets, Vector3& outMin,
                                          Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    Quaternion placementCopy = placementOrientation;
    const RotationMatrix placementRotation = placementCopy.GetOrientationMatrix();
    const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
    const bool
        ok = ForEachEditorBuildingPart( objectType, assets,
                                        [&]( const Json& part )
                                        {
                                            const Vector3 offset = EditorJsonVec3Or( part, "offset",
                                                                                     Vector3( 0.0f, 0.0f, 0.0f ) );

                                            const Quaternion
                                                partOrientation = EditorBuildingPartOrientation( placementOrientation,
                                                                                                 part );

                                            Quaternion partCopy = partOrientation;
                                            const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                                            const Vector3 partCenter = base + placementRotation * offset;
                                            const std::string primitiveType = EditorAssetPrimitiveType( part );

                                            if ( primitiveType == "convexHull" )
                                            {
                                                const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                                                const ConvexHullShape* hull = hullPath.empty()
                                                                                  ? nullptr
                                                                                  : CachedEditorBuildingHull( diagnostics,
                                                                                                              hullPath );

                                                if ( !hull )
                                                {
                                                    return;
                                                }

                                                const Vector3 hullLocalOffset = HullAuthoredLocalOffset( *hull );

                                                for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount();
                                                      ++vertexIndex )
                                                {
                                                    IncludeEditorBoundsPoint( partCenter +
                                                                                  partRotation *
                                                                                      ( hullLocalOffset +
                                                                                        hull->GetVertex( vertexIndex ) ),
                                                                              outMin, outMax );
                                                }

                                                return;
                                            }

                                            if ( primitiveType == "box" )
                                            {
                                                Vector3 halfExtents;

                                                if ( !TryReadEditorBoxHalfExtents( part, halfExtents ) )
                                                {
                                                    return;
                                                }

                                                for ( int xSign = -1; xSign <= 1; xSign += 2 )
                                                {

                                                    for ( int ySign = -1; ySign <= 1; ySign += 2 )
                                                    {

                                                        for ( int zSign = -1; zSign <= 1; zSign += 2 )
                                                        {
                                                            const Vector3 corner( halfExtents.x *
                                                                                      static_cast<float>( xSign ),
                                                                                  halfExtents.y *
                                                                                      static_cast<float>( ySign ),
                                                                                  halfExtents.z *
                                                                                      static_cast<float>( zSign ) );
                                                            IncludeEditorBoundsPoint( partCenter + partRotation * corner,
                                                                                      outMin, outMax );
                                                        }
                                                    }
                                                }

                                                return;
                                            }

                                            if ( primitiveType == "sphere" )
                                            {
                                                float radius = 0.0f;

                                                if ( !TryReadEditorSphereRadius( part, radius ) )
                                                {
                                                    return;
                                                }

                                                IncludeEditorBoundsPoint( partCenter + Vector3( -radius, -radius, -radius ),
                                                                          outMin, outMax );

                                                IncludeEditorBoundsPoint( partCenter + Vector3( radius, radius, radius ),
                                                                          outMin, outMax );
                                            }
                                        } );

    return ok && outMin.x != FLT_MAX && outMax.x != -FLT_MAX;
}


constexpr EditorTreePartDefinition NormalizeEditorTreePart( EditorTreePartDefinition part )
{

    // Why: authored tree parts are already real recipe values. Normalization
    // adds the hull policy directly instead of copying the recipe through a
    // second descriptor with the same fields.
    part.contactReleaseOnImpact = part.contactReleaseOnImpact || EditorHullAssetDefaultsToContactRelease( part.hullAsset );

    return part;
}


constexpr EditorTreePartDefinition LiftEditorTreePartY( EditorTreePartDefinition part, float liftY )
{
    part.offsetY += liftY;
    return part;
}


constexpr EditorTreePartDefinition SmallRootPart()
{
    return NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::TREE_ROOT_SMALL,
                                                               .suffix = "root",
                                                               .offsetX = 0.0f,
                                                               .offsetY = 0.55f,
                                                               .offsetZ = 0.0f,
                                                               .restitution = 0.04f,
                                                               .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                                               .materialName = "editor_small_root",
                                                               .colorR = 0.24f,
                                                               .colorG = 0.12f,
                                                               .colorB = 0.055f,
                                                               .roughness = 0.96f,
                                                               .specular = 0.05f,
                                                               .stylization = 0.50f } );
}


constexpr EditorTreePartDefinition LargeRootPart()
{
    return NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::TREE_ROOT_LARGE,
                                                               .suffix = "root",
                                                               .offsetX = 0.0f,
                                                               .offsetY = 0.75f,
                                                               .offsetZ = 0.0f,
                                                               .restitution = 0.04f,
                                                               .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                                               .materialName = "editor_large_root",
                                                               .colorR = 0.23f,
                                                               .colorG = 0.115f,
                                                               .colorB = 0.052f,
                                                               .roughness = 0.96f,
                                                               .specular = 0.05f,
                                                               .stylization = 0.50f } );
}


constexpr EditorTreePartDefinition PineNeedlePart( const char* suffix, float offsetX, float offsetY, float offsetZ,
                                                   float shade )
{
    return NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::PINE_NEEDLE_CLUSTER,
                                                               .suffix = suffix,
                                                               .offsetX = offsetX,
                                                               .offsetY = offsetY,
                                                               .offsetZ = offsetZ,
                                                               .restitution = 0.04f,
                                                               .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                               .materialName = "editor_pine_loose_needles",
                                                               .colorR = 0.035f + shade * 0.025f,
                                                               .colorG = 0.18f + shade * 0.11f,
                                                               .colorB = 0.052f + shade * 0.045f,
                                                               .roughness = 0.90f,
                                                               .specular = 0.06f,
                                                               .stylization = 0.94f,
                                                               .startsFixed = true,
                                                               .contactReleaseOnImpact = true,
                                                               .contactReleaseImpulseThreshold = 1.25f } );
}


constexpr EditorTreePartDefinition EDITOR_TREE_SMALL_PARTS[] = {
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::TREE_TRUNK_SMALL_FACETED,
                                                        .suffix = "trunk",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 6.5f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.06f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                                        .materialName = "editor_small_bark",
                                                        .colorR = 0.30f,
                                                        .colorG = 0.14f,
                                                        .colorB = 0.055f,
                                                        .roughness = 0.94f,
                                                        .specular = 0.06f,
                                                        .stylization = 0.50f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_LOW,
                                                        .suffix = "low",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 20.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_small_needles_low",
                                                        .colorR = 0.055f,
                                                        .colorG = 0.24f,
                                                        .colorB = 0.12f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_MID,
                                                        .suffix = "mid",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 28.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_small_needles_mid",
                                                        .colorR = 0.075f,
                                                        .colorG = 0.30f,
                                                        .colorB = 0.15f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_TOP,
                                                        .suffix = "top",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 35.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_small_needles_top",
                                                        .colorR = 0.10f,
                                                        .colorG = 0.36f,
                                                        .colorB = 0.18f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
};
constexpr int EDITOR_TREE_SMALL_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_SMALL_PARTS ) /
                                                               sizeof( EDITOR_TREE_SMALL_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL = { "tree_small", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLOPE = { "tree_small_slope", EDITOR_TREE_SMALL_PARTS,
                                                           EDITOR_TREE_SMALL_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLEEP = { "tree_small_sleep", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_PARTS[] = {
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::TREE_TRUNK_FACETED,
                                                        .suffix = "trunk",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 9.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.06f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                                        .materialName = "editor_big_bark",
                                                        .colorR = 0.31f,
                                                        .colorG = 0.16f,
                                                        .colorB = 0.07f,
                                                        .roughness = 0.94f,
                                                        .specular = 0.06f,
                                                        .stylization = 0.48f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::PINE_TIER_LARGE,
                                                        .suffix = "low",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 24.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_big_needles_low",
                                                        .colorR = 0.045f,
                                                        .colorG = 0.20f,
                                                        .colorB = 0.055f,
                                                        .roughness = 0.88f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.88f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::PINE_TIER_MID,
                                                        .suffix = "mid",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 34.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_big_needles_mid",
                                                        .colorR = 0.06f,
                                                        .colorG = 0.25f,
                                                        .colorB = 0.075f,
                                                        .roughness = 0.88f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.88f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::PINE_TIER_TOP,
                                                        .suffix = "top",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 43.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_big_needles_top",
                                                        .colorR = 0.09f,
                                                        .colorG = 0.31f,
                                                        .colorB = 0.10f,
                                                        .roughness = 0.88f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.88f } ),
};
constexpr int EDITOR_TREE_BIG_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_BIG_PARTS ) /
                                                             sizeof( EDITOR_TREE_BIG_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG = { "tree_pine", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLOPE = { "tree_pine_slope", EDITOR_TREE_BIG_PARTS,
                                                         EDITOR_TREE_BIG_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLEEP = { "tree_pine_sleep", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_PARTS[] = {
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::TREE_TRUNK_FACETED,
                                                        .suffix = "trunk",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 9.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.06f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                                        .materialName = "editor_cedar_bark",
                                                        .colorR = 0.28f,
                                                        .colorG = 0.13f,
                                                        .colorB = 0.055f,
                                                        .roughness = 0.94f,
                                                        .specular = 0.06f,
                                                        .stylization = 0.50f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_TALL_LOW,
                                                        .suffix = "low",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 25.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_cedar_needles_low",
                                                        .colorR = 0.055f,
                                                        .colorG = 0.24f,
                                                        .colorB = 0.12f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_TALL_MID,
                                                        .suffix = "mid",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 38.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_cedar_needles_mid",
                                                        .colorR = 0.075f,
                                                        .colorG = 0.30f,
                                                        .colorB = 0.15f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
    NormalizeEditorTreePart( EditorTreePartDefinition { .hullAsset = EditorHullAsset::CEDAR_TIER_TOP,
                                                        .suffix = "top",
                                                        .offsetX = 0.0f,
                                                        .offsetY = 48.0f,
                                                        .offsetZ = 0.0f,
                                                        .restitution = 0.05f,
                                                        .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                                                        .materialName = "editor_cedar_needles_top",
                                                        .colorR = 0.10f,
                                                        .colorG = 0.36f,
                                                        .colorB = 0.18f,
                                                        .roughness = 0.89f,
                                                        .specular = 0.08f,
                                                        .stylization = 0.90f } ),
};
constexpr int EDITOR_TREE_CEDAR_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_CEDAR_PARTS ) /
                                                               sizeof( EDITOR_TREE_CEDAR_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR = { "tree_cedar", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLOPE = { "tree_cedar_slope", EDITOR_TREE_CEDAR_PARTS,
                                                           EDITOR_TREE_CEDAR_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLEEP = { "tree_cedar_sleep", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_SMALL_ROOTED_PARTS[] = {
    SmallRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_SMALL_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS ) /
                                                                      sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_ROOTED = { "tree_small_rooted", EDITOR_TREE_SMALL_ROOTED_PARTS,
                                                            EDITOR_TREE_SMALL_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_BIG_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_BIG_ROOTED_PARTS ) /
                                                                    sizeof( EDITOR_TREE_BIG_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG_ROOTED = { "tree_pine_rooted", EDITOR_TREE_BIG_ROOTED_PARTS,
                                                          EDITOR_TREE_BIG_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_CEDAR_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS ) /
                                                                      sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_ROOTED = { "tree_cedar_rooted", EDITOR_TREE_CEDAR_ROOTED_PARTS,
                                                            EDITOR_TREE_CEDAR_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_PINE_SHEDDING_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_00", -12.0f, 22.0f, -12.0f, 0.10f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_01", -4.0f, 21.5f, -16.0f, 0.18f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_02", 6.0f, 22.5f, -15.0f, 0.30f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_03", 14.0f, 23.0f, -7.0f, 0.24f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_04", 15.0f, 23.5f, 5.0f, 0.14f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_05", 8.0f, 22.0f, 14.0f, 0.34f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_06", -4.0f, 22.5f, 16.0f, 0.22f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_07", -15.0f, 23.0f, 7.0f, 0.28f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_08", -9.0f, 31.0f, -10.0f, 0.38f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_09", 1.0f, 31.5f, -12.0f, 0.26f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_10", 10.0f, 32.0f, -5.0f, 0.16f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_11", 11.0f, 32.5f, 6.0f, 0.32f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_12", 2.0f, 31.0f, 12.0f, 0.20f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_13", -10.0f, 32.0f, 4.0f, 0.36f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_14", -6.0f, 40.0f, -6.0f, 0.18f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_15", 4.0f, 40.5f, -7.0f, 0.28f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_16", 7.0f, 41.0f, 3.0f, 0.12f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_17", -3.0f, 41.0f, 7.0f, 0.34f ),
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_PINE_SHEDDING_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS ) /
                                                                       sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_PINE_SHEDDING = { "tree_pine_shedding", EDITOR_TREE_PINE_SHEDDING_PARTS,
                                                             EDITOR_TREE_PINE_SHEDDING_PART_COUNT, true, true };


constexpr EditorHousePartDefinition EDITOR_BRICK_HOUSE_PARTS[] = {
    EditorHousePartDefinition { .suffix = "foundation",
                                .offsetX = 0.0f,
                                .offsetY = 0.38f,
                                .offsetZ = 0.0f,
                                .halfX = 23.5f,
                                .halfY = 0.38f,
                                .halfZ = 16.0f,
                                .restitution = 0.08f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_foundation",
                                .colorR = 0.40f,
                                .colorG = 0.37f,
                                .colorB = 0.32f,
                                .roughness = 0.98f,
                                .specular = 0.07f,
                                .stylization = 0.58f },
    EditorHousePartDefinition { .suffix = "upper_floor",
                                .offsetX = 0.0f,
                                .offsetY = 7.52f,
                                .offsetZ = 0.0f,
                                .halfX = 22.8f,
                                .halfY = 0.40f,
                                .halfZ = 15.2f,
                                .restitution = 0.07f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                .materialName = "editor_house_floor",
                                .colorR = 0.30f,
                                .colorG = 0.19f,
                                .colorB = 0.11f,
                                .roughness = 0.90f,
                                .specular = 0.08f,
                                .stylization = 0.40f },
    EditorHousePartDefinition { .suffix = "roof",
                                .offsetX = 0.0f,
                                .offsetY = 14.75f,
                                .offsetZ = 0.0f,
                                .halfX = 24.6f,
                                .halfY = 0.43f,
                                .halfZ = 17.0f,
                                .restitution = 0.06f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_slate_roof",
                                .colorR = 0.13f,
                                .colorG = 0.17f,
                                .colorB = 0.22f,
                                .roughness = 0.94f,
                                .specular = 0.10f,
                                .stylization = 0.62f },
    EditorHousePartDefinition { .suffix = "front_lower_left",
                                .offsetX = -14.8f,
                                .offsetY = 3.92f,
                                .offsetZ = -15.45f,
                                .halfX = 7.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.55f,
                                .colorG = 0.22f,
                                .colorB = 0.17f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "front_lower_right",
                                .offsetX = 14.8f,
                                .offsetY = 3.92f,
                                .offsetZ = -15.45f,
                                .halfX = 7.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.55f,
                                .colorG = 0.22f,
                                .colorB = 0.17f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "front_door_lintel",
                                .offsetX = 0.0f,
                                .offsetY = 6.37f,
                                .offsetZ = -15.45f,
                                .halfX = 6.8f,
                                .halfY = 0.75f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.55f,
                                .colorG = 0.22f,
                                .colorB = 0.17f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "front_upper_left",
                                .offsetX = -15.0f,
                                .offsetY = 11.12f,
                                .offsetZ = -15.45f,
                                .halfX = 5.6f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "front_upper_center",
                                .offsetX = 0.0f,
                                .offsetY = 11.12f,
                                .offsetZ = -15.45f,
                                .halfX = 4.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "front_upper_right",
                                .offsetX = 15.0f,
                                .offsetY = 11.12f,
                                .offsetZ = -15.45f,
                                .halfX = 5.6f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.72f },
    EditorHousePartDefinition { .suffix = "back_lower_left",
                                .offsetX = -14.8f,
                                .offsetY = 3.92f,
                                .offsetZ = 15.45f,
                                .halfX = 7.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.50f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "back_lower_center",
                                .offsetX = 0.0f,
                                .offsetY = 3.92f,
                                .offsetZ = 15.45f,
                                .halfX = 6.8f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.50f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "back_lower_right",
                                .offsetX = 14.8f,
                                .offsetY = 3.92f,
                                .offsetZ = 15.45f,
                                .halfX = 7.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.50f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "back_upper_left",
                                .offsetX = -15.0f,
                                .offsetY = 11.12f,
                                .offsetZ = 15.45f,
                                .halfX = 5.6f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "back_upper_center",
                                .offsetX = 0.0f,
                                .offsetY = 11.12f,
                                .offsetZ = 15.45f,
                                .halfX = 4.2f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "back_upper_right",
                                .offsetX = 15.0f,
                                .offsetY = 11.12f,
                                .offsetZ = 15.45f,
                                .halfX = 5.6f,
                                .halfY = 3.20f,
                                .halfZ = 0.55f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.52f,
                                .colorG = 0.20f,
                                .colorB = 0.16f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "west_lower_front",
                                .offsetX = -22.45f,
                                .offsetY = 3.92f,
                                .offsetZ = -10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.49f,
                                .colorG = 0.18f,
                                .colorB = 0.14f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "west_lower_back",
                                .offsetX = -22.45f,
                                .offsetY = 3.92f,
                                .offsetZ = 10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.49f,
                                .colorG = 0.18f,
                                .colorB = 0.14f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "east_lower_front",
                                .offsetX = 22.45f,
                                .offsetY = 3.92f,
                                .offsetZ = -10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.49f,
                                .colorG = 0.18f,
                                .colorB = 0.14f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "east_lower_back",
                                .offsetX = 22.45f,
                                .offsetY = 3.92f,
                                .offsetZ = 10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.49f,
                                .colorG = 0.18f,
                                .colorB = 0.14f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "west_upper_front",
                                .offsetX = -22.45f,
                                .offsetY = 11.12f,
                                .offsetZ = -10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.51f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "west_upper_back",
                                .offsetX = -22.45f,
                                .offsetY = 11.12f,
                                .offsetZ = 10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.51f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "east_upper_front",
                                .offsetX = 22.45f,
                                .offsetY = 11.12f,
                                .offsetZ = -10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.51f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "east_upper_back",
                                .offsetX = 22.45f,
                                .offsetY = 11.12f,
                                .offsetZ = 10.0f,
                                .halfX = 0.55f,
                                .halfY = 3.20f,
                                .halfZ = 5.0f,
                                .restitution = 0.10f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_brick",
                                .colorR = 0.51f,
                                .colorG = 0.19f,
                                .colorB = 0.15f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
    EditorHousePartDefinition { .suffix = "door",
                                .offsetX = 0.0f,
                                .offsetY = 3.17f,
                                .offsetZ = -16.04f,
                                .halfX = 2.4f,
                                .halfY = 2.45f,
                                .halfZ = 0.20f,
                                .restitution = 0.08f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                .materialName = "editor_house_front_door",
                                .colorR = 0.28f,
                                .colorG = 0.16f,
                                .colorB = 0.075f,
                                .roughness = 0.86f,
                                .specular = 0.10f,
                                .stylization = 0.38f },
    EditorHousePartDefinition { .suffix = "front_window_left",
                                .offsetX = -6.8f,
                                .offsetY = 11.10f,
                                .offsetZ = -16.05f,
                                .halfX = 1.25f,
                                .halfY = 1.25f,
                                .halfZ = 0.16f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.40f,
                                .colorG = 0.68f,
                                .colorB = 0.92f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "front_window_right",
                                .offsetX = 6.8f,
                                .offsetY = 11.10f,
                                .offsetZ = -16.05f,
                                .halfX = 1.25f,
                                .halfY = 1.25f,
                                .halfZ = 0.16f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.40f,
                                .colorG = 0.68f,
                                .colorB = 0.92f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "back_window_left",
                                .offsetX = -6.8f,
                                .offsetY = 11.10f,
                                .offsetZ = 16.05f,
                                .halfX = 1.25f,
                                .halfY = 1.25f,
                                .halfZ = 0.16f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.38f,
                                .colorG = 0.64f,
                                .colorB = 0.86f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "back_window_right",
                                .offsetX = 6.8f,
                                .offsetY = 11.10f,
                                .offsetZ = 16.05f,
                                .halfX = 1.25f,
                                .halfY = 1.25f,
                                .halfZ = 0.16f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.38f,
                                .colorG = 0.64f,
                                .colorB = 0.86f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "west_window",
                                .offsetX = -23.05f,
                                .offsetY = 3.92f,
                                .offsetZ = 0.0f,
                                .halfX = 0.16f,
                                .halfY = 1.20f,
                                .halfZ = 1.35f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.36f,
                                .colorG = 0.62f,
                                .colorB = 0.84f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "east_window",
                                .offsetX = 23.05f,
                                .offsetY = 3.92f,
                                .offsetZ = 0.0f,
                                .halfX = 0.16f,
                                .halfY = 1.20f,
                                .halfZ = 1.35f,
                                .restitution = 0.04f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                                .materialName = "editor_house_window",
                                .colorR = 0.36f,
                                .colorG = 0.62f,
                                .colorB = 0.84f,
                                .roughness = 0.08f,
                                .specular = 0.88f,
                                .stylization = 0.18f },
    EditorHousePartDefinition { .suffix = "porch",
                                .offsetX = 0.0f,
                                .offsetY = 0.28f,
                                .offsetZ = -20.2f,
                                .halfX = 7.0f,
                                .halfY = 0.28f,
                                .halfZ = 3.6f,
                                .restitution = 0.08f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_porch",
                                .colorR = 0.44f,
                                .colorG = 0.40f,
                                .colorB = 0.34f,
                                .roughness = 0.97f,
                                .specular = 0.07f,
                                .stylization = 0.58f },
    EditorHousePartDefinition { .suffix = "porch_post_left",
                                .offsetX = -5.2f,
                                .offsetY = 2.98f,
                                .offsetZ = -20.2f,
                                .halfX = 0.34f,
                                .halfY = 2.42f,
                                .halfZ = 0.34f,
                                .restitution = 0.06f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                .materialName = "editor_house_porch_post",
                                .colorR = 0.30f,
                                .colorG = 0.18f,
                                .colorB = 0.09f,
                                .roughness = 0.88f,
                                .specular = 0.08f,
                                .stylization = 0.42f },
    EditorHousePartDefinition { .suffix = "porch_post_right",
                                .offsetX = 5.2f,
                                .offsetY = 2.98f,
                                .offsetZ = -20.2f,
                                .halfX = 0.34f,
                                .halfY = 2.42f,
                                .halfZ = 0.34f,
                                .restitution = 0.06f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                                .materialName = "editor_house_porch_post",
                                .colorR = 0.30f,
                                .colorG = 0.18f,
                                .colorB = 0.09f,
                                .roughness = 0.88f,
                                .specular = 0.08f,
                                .stylization = 0.42f },
    EditorHousePartDefinition { .suffix = "chimney",
                                .offsetX = 13.0f,
                                .offsetY = 16.55f,
                                .offsetZ = 7.6f,
                                .halfX = 0.82f,
                                .halfY = 1.37f,
                                .halfZ = 0.82f,
                                .restitution = 0.08f,
                                .materialKind = SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                                .materialName = "editor_house_chimney",
                                .colorR = 0.42f,
                                .colorG = 0.18f,
                                .colorB = 0.14f,
                                .roughness = 0.96f,
                                .specular = 0.07f,
                                .stylization = 0.70f },
};
constexpr int EDITOR_BRICK_HOUSE_PART_COUNT = static_cast<int>( sizeof( EDITOR_BRICK_HOUSE_PARTS ) /
                                                                sizeof( EDITOR_BRICK_HOUSE_PARTS[0] ) );
constexpr EditorHouseDefinition EDITOR_BRICK_HOUSE_SLEEP = { "brick_house", EDITOR_BRICK_HOUSE_PARTS,
                                                             EDITOR_BRICK_HOUSE_PART_COUNT, true };


const EditorTreeDefinition* EditorTreeDefinitionForType( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL:
        return &EDITOR_TREE_SMALL;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG:
        return &EDITOR_TREE_BIG;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR:
        return &EDITOR_TREE_CEDAR;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
        return &EDITOR_TREE_SMALL_SLOPE;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
        return &EDITOR_TREE_BIG_SLOPE;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
        return &EDITOR_TREE_CEDAR_SLOPE;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
        return &EDITOR_TREE_SMALL_SLEEP;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
        return &EDITOR_TREE_BIG_SLEEP;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
        return &EDITOR_TREE_CEDAR_SLEEP;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
        return &EDITOR_TREE_SMALL_ROOTED;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
        return &EDITOR_TREE_BIG_ROOTED;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
        return &EDITOR_TREE_CEDAR_ROOTED;
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
        return &EDITOR_TREE_PINE_SHEDDING;
    default:
        return nullptr;
    }
}


const EditorHouseDefinition* EditorHouseDefinitionForType( int objectType )
{
    (void)objectType;
    return nullptr;
}


bool EditorObjectAlignsToTerrainNormal( int objectType, bool autoTerrainAlign )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );

    if ( autoTerrainAlign )
    {
        return true;
    }

    if ( const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type ) )
    {
        return tree->alignToTerrainNormal;
    }

    return type == SkullbonezCore::UI::EditorTab::OBJECT_ROOT_SMALL ||
           type == SkullbonezCore::UI::EditorTab::OBJECT_ROOT_LARGE;
}


Quaternion EditorOrientationFromTerrainNormal( int objectType, Vector3 terrainNormal, bool autoTerrainAlign )
{

    if ( !EditorObjectAlignsToTerrainNormal( objectType, autoTerrainAlign ) )
    {
        return IDENTITY_QUATERNION;
    }

    const float normalMag = VectorMag( terrainNormal );

    if ( normalMag <= TOLERANCE )
    {
        return IDENTITY_QUATERNION;
    }

    terrainNormal /= normalMag;

    const Vector3 up( 0.0f, 1.0f, 0.0f );
    const float dot = std::clamp( Dot( up, terrainNormal ), -1.0f, 1.0f );
    Quaternion orientation = IDENTITY_QUATERNION;

    if ( dot > 0.9995f )
    {
        return orientation;
    }

    if ( dot < -0.9995f )
    {
        orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), _PI );
        return orientation;
    }

    Vector3 axis = CrossProduct( up, terrainNormal );
    const float axisMag = VectorMag( axis );

    if ( axisMag <= TOLERANCE )
    {
        return orientation;
    }

    axis /= axisMag;
    orientation.RotateAboutAxis( axis, acosf( dot ) );
    return orientation;
}


Quaternion EditorPlacementOrientation( int objectType, Vector3 terrainNormal, bool autoTerrainAlign, float yawRadians )
{
    Quaternion orientation = EditorOrientationFromTerrainNormal( objectType, terrainNormal, autoTerrainAlign );

    if ( fabsf( yawRadians ) > 1.0e-5f )
    {
        orientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), yawRadians );
    }

    return orientation;
}


const ConvexHullShape* CachedEditorHullForAsset( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                 EditorHullAsset asset )
{
    const char* path = EditorHullAssetPath( asset );

    if ( !path )
    {
        return nullptr;
    }

    static std::array<ConvexHullShape, EDITOR_HULL_ASSET_COUNT> hulls = {};
    static std::array<bool, EDITOR_HULL_ASSET_COUNT> loaded = {};

    for ( std::size_t i = 0; i < EDITOR_HULL_ASSET_COUNT; ++i )
    {

        if ( EDITOR_HULL_ASSETS[i].asset != asset )
        {
            continue;
        }

        if ( !loaded[i] )
        {
            ConvexHullShape hull;
            const SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( diagnostics, path, hull );

            if ( !hullLoad.Ok() )
            {
                fprintf( stderr, "[editor] Cannot cache hull asset %s: %s\n", EditorHullAssetToken( asset ),
                         hullLoad.ErrorMessage() );

                return nullptr;
            }

            hulls[i] = hull;
            loaded[i] = true;
        }

        return &hulls[i];
    }

    return nullptr;
}


const ConvexHullShape* CachedEditorHullForType( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    return CachedEditorHullForAsset( diagnostics, EditorHullAssetForType( type ) );
}


bool TryComputeEditorTreeVerticalBounds( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                         const EditorTreeDefinition& tree, float& outMinY, float& outMaxY )
{
    outMinY = FLT_MAX;
    outMaxY = -FLT_MAX;

    for ( int partIndex = 0; partIndex < tree.partCount; ++partIndex )
    {
        const EditorTreePartDefinition& part = tree.parts[partIndex];
        const ConvexHullShape* hull = CachedEditorHullForAsset( diagnostics, part.hullAsset );

        if ( !hull )
        {
            return false;
        }

        for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount(); ++vertexIndex )
        {
            const Vector3 authoredOffset = HullAuthoredLocalOffset( *hull );
            const float y = part.offsetY + authoredOffset.y + hull->GetVertex( vertexIndex ).y;
            outMinY = (std::min)( outMinY, y );
            outMaxY = (std::max)( outMaxY, y );
        }
    }

    return outMinY != FLT_MAX && outMaxY != -FLT_MAX;
}


bool TryComputeEditorTreeWorldBounds( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const EditorTreeDefinition& tree,
                                      const Vector3& terrainPoint, const RotationMatrix& orientation, Vector3& outMin,
                                      Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    const Vector3 surfaceLift = orientation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

    for ( int partIndex = 0; partIndex < tree.partCount; ++partIndex )
    {
        const EditorTreePartDefinition& part = tree.parts[partIndex];
        const ConvexHullShape* hull = CachedEditorHullForAsset( diagnostics, part.hullAsset );

        if ( !hull )
        {
            return false;
        }

        const Vector3 localBase = Vector3( part.offsetX, part.offsetY, part.offsetZ ) + HullAuthoredLocalOffset( *hull );

        for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount(); ++vertexIndex )
        {
            const Vector3 world = terrainPoint + surfaceLift + orientation * ( localBase + hull->GetVertex( vertexIndex ) );

            outMin.x = (std::min)( outMin.x, world.x );
            outMin.y = (std::min)( outMin.y, world.y );
            outMin.z = (std::min)( outMin.z, world.z );
            outMax.x = (std::max)( outMax.x, world.x );
            outMax.y = (std::max)( outMax.y, world.y );
            outMax.z = (std::max)( outMax.z, world.z );
        }
    }

    return outMin.x != FLT_MAX && outMax.x != -FLT_MAX;
}


void ExpandEditorHousePartWorldBounds( const EditorHousePartDefinition& part, const Vector3& terrainPoint,
                                       const RotationMatrix& orientation, Vector3& outMin, Vector3& outMax )
{
    const Vector3 base = terrainPoint + orientation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
    const Vector3 center = base + orientation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
    const Vector3 halfExtents( part.halfX, part.halfY, part.halfZ );

    for ( int x = -1; x <= 1; x += 2 )
    {

        for ( int y = -1; y <= 1; y += 2 )
        {

            for ( int z = -1; z <= 1; z += 2 )
            {
                const Vector3 corner = center + orientation * Vector3( halfExtents.x * static_cast<float>( x ),
                                                                       halfExtents.y * static_cast<float>( y ),
                                                                       halfExtents.z * static_cast<float>( z ) );

                outMin.x = (std::min)( outMin.x, corner.x );
                outMin.y = (std::min)( outMin.y, corner.y );
                outMin.z = (std::min)( outMin.z, corner.z );
                outMax.x = (std::max)( outMax.x, corner.x );
                outMax.y = (std::max)( outMax.y, corner.y );
                outMax.z = (std::max)( outMax.z, corner.z );
            }
        }
    }
}


bool TryComputeEditorHouseWorldBounds( const EditorHouseDefinition& house, const Vector3& terrainPoint,
                                       const RotationMatrix& orientation, Vector3& outMin, Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );

    for ( int partIndex = 0; partIndex < house.partCount; ++partIndex )
    {
        ExpandEditorHousePartWorldBounds( house.parts[partIndex], terrainPoint, orientation, outMin, outMax );
    }

    return outMin.x != FLT_MAX && outMax.x != -FLT_MAX;
}


float EditorTreeVerticalSize( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType )
{
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( objectType );

    if ( !tree )
    {
        return 1.0f;
    }

    float minY = 0.0f;
    float maxY = 1.0f;
    return TryComputeEditorTreeVerticalBounds( diagnostics, *tree, minY, maxY ) ? (std::max)( 1.0f, maxY - minY ) : 1.0f;
}


float EditorHouseVerticalSize( int objectType )
{
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( objectType );

    if ( !house )
    {
        return 1.0f;
    }

    float minY = FLT_MAX;
    float maxY = -FLT_MAX;

    for ( int partIndex = 0; partIndex < house->partCount; ++partIndex )
    {
        const EditorHousePartDefinition& part = house->parts[partIndex];
        minY = (std::min)( minY, part.offsetY - part.halfY );
        maxY = (std::max)( maxY, part.offsetY + part.halfY );
    }

    return minY == FLT_MAX ? 1.0f : (std::max)( 1.0f, maxY - minY );
}


SkullbonezCore::Rendering::RenderMaterial EditorTreePartMaterial( const EditorTreePartDefinition& part )
{
    SkullbonezCore::Rendering::RenderMaterial
        material = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( part.colorR, part.colorG, part.colorB,
                                                                                static_cast<float>( part.materialKind ) );

    strncpy_s( material.name, sizeof( material.name ), part.materialName, _TRUNCATE );
    material.roughness = part.roughness;
    material.specular = part.specular;
    material.stylization = part.stylization;
    return material;
}


SkullbonezCore::Rendering::RenderMaterial EditorHousePartMaterial( const EditorHousePartDefinition& part )
{
    SkullbonezCore::Rendering::RenderMaterial
        material = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( part.colorR, part.colorG, part.colorB,
                                                                                static_cast<float>( part.materialKind ) );

    strncpy_s( material.name, sizeof( material.name ), part.materialName, _TRUNCATE );
    material.roughness = part.roughness;
    material.specular = part.specular;
    material.stylization = part.stylization;
    return material;
}


bool TryEditorRockMaterial( EditorHullAsset asset, SkullbonezCore::Rendering::RenderMaterial& outMaterial )
{
    const char* name = nullptr;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float roughness = 0.96f;
    float specular = 0.08f;
    float stylization = 0.66f;

    switch ( asset )
    {
    case EditorHullAsset::ROCK_SLAB_FLAT:
        name = "editor_cool_blue_stone";
        r = 0.34f;
        g = 0.42f;
        b = 0.58f;
        stylization = 0.72f;
        break;
    case EditorHullAsset::ROCK_LUMP_LARGE:
        name = "editor_moss_grey_stone";
        r = 0.31f;
        g = 0.39f;
        b = 0.35f;
        roughness = 0.98f;
        specular = 0.07f;
        break;
    case EditorHullAsset::ROCK_SHARD_TALL:
        name = "editor_violet_granite";
        r = 0.44f;
        g = 0.38f;
        b = 0.55f;
        specular = 0.11f;
        stylization = 0.70f;
        break;
    case EditorHullAsset::ROCK_CHIPPED_BLOCK:
        name = "editor_warm_chipped_stone";
        r = 0.52f;
        g = 0.45f;
        b = 0.40f;
        roughness = 0.97f;
        stylization = 0.62f;
        break;
    default:
        return false;
    }

    outMaterial = SkullbonezCore::Rendering::
        MakeRenderMaterialFromLegacyTint( r, g, b,
                                          static_cast<float>( SkullbonezCore::Rendering::RenderMaterialKind::Stone ) );

    strncpy_s( outMaterial.name, sizeof( outMaterial.name ), name, _TRUNCATE );
    outMaterial.roughness = roughness;
    outMaterial.specular = specular;
    outMaterial.stylization = stylization;
    return true;
}


bool TryEditorRootMaterial( EditorHullAsset asset, SkullbonezCore::Rendering::RenderMaterial& outMaterial )
{
    const bool smallRoot = asset == EditorHullAsset::TREE_ROOT_SMALL;
    const bool largeRoot = asset == EditorHullAsset::TREE_ROOT_LARGE;

    if ( !smallRoot && !largeRoot )
    {
        return false;
    }

    const char* name = smallRoot ? "editor_small_root" : "editor_large_root";
    const float r = smallRoot ? 0.24f : 0.23f;
    const float g = smallRoot ? 0.12f : 0.115f;
    const float b = smallRoot ? 0.055f : 0.052f;
    outMaterial = SkullbonezCore::Rendering::
        MakeRenderMaterialFromLegacyTint( r, g, b,
                                          static_cast<float>( SkullbonezCore::Rendering::RenderMaterialKind::Bark ) );

    strncpy_s( outMaterial.name, sizeof( outMaterial.name ), name, _TRUNCATE );
    outMaterial.roughness = 0.96f;
    outMaterial.specular = 0.05f;
    outMaterial.stylization = 0.50f;
    return true;
}

bool TryBuildScaledEditorHullForType( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                      const Vector3& placementScale, ConvexHullShape& outHull )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const ConvexHullShape* baseHull = CachedEditorHullForType( diagnostics, type );

    if ( !baseHull )
    {
        return false;
    }

    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    outHull = *baseHull;
    outHull.ScaleAxis( 0, scale.x );
    outHull.ScaleAxis( 1, scale.y );
    outHull.ScaleAxis( 2, scale.z );
    return true;
}
} // namespace Runtime
} // namespace SkullbonezCore
