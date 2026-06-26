/*
File: SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
Purpose:
  Owns runtime editor placement, selection, gizmos, and overlay tracing.

Mental model:
  Input asks for editor actions. This file performs editor math, picking,
  placement, gizmos, and line visualization.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "EditorTools.h"
#include "EditorHullAssets.h"
#include "../../Assets/AssetSystem.h"
#include "../InputController.h"
#include "../RuntimePickService.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UIInput.h"
#include "../../UI/UILayout.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using Json = nlohmann::ordered_json;

namespace
{
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr float EDITOR_PLACEMENT_YAW_STEP_RADIANS = _PI / 12.0f;

bool TransformClipPointToWorld( const Matrix4& inverseViewProjection, float x, float y, float z, Vector3& outWorld )
{
    const float worldX = inverseViewProjection.m[0] * x + inverseViewProjection.m[4] * y +
                         inverseViewProjection.m[8] * z + inverseViewProjection.m[12];
    const float worldY = inverseViewProjection.m[1] * x + inverseViewProjection.m[5] * y +
                         inverseViewProjection.m[9] * z + inverseViewProjection.m[13];
    const float worldZ = inverseViewProjection.m[2] * x + inverseViewProjection.m[6] * y +
                         inverseViewProjection.m[10] * z + inverseViewProjection.m[14];
    const float worldW = inverseViewProjection.m[3] * x + inverseViewProjection.m[7] * y +
                         inverseViewProjection.m[11] * z + inverseViewProjection.m[15];
    if ( fabsf( worldW ) < 1e-6f )
    {
        return false;
    }

    const float invW = 1.0f / worldW;
    outWorld = Vector3( worldX * invW, worldY * invW, worldZ * invW );
    return true;
}

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


bool EditorPositionsDiffer( const Vector3& a, const Vector3& b )
{
    return VectorMagSquared( a - b ) > 1.0e-8f;
}


bool EditorOrientationsDiffer( const Quaternion& a, const Quaternion& b )
{
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float aw = 1.0f;
    float bx = 0.0f;
    float by = 0.0f;
    float bz = 0.0f;
    float bw = 1.0f;
    a.GetComponents( ax, ay, az, aw );
    b.GetComponents( bx, by, bz, bw );

    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    const float dw = aw - bw;
    const float sx = ax + bx;
    const float sy = ay + by;
    const float sz = az + bz;
    const float sw = aw + bw;
    const float directDistanceSq = dx * dx + dy * dy + dz * dz + dw * dw;
    const float flippedDistanceSq = sx * sx + sy * sy + sz * sz + sw * sw;
    return (std::min)( directDistanceSq, flippedDistanceSq ) > 1.0e-10f;
}


float HullVerticalSize( const ConvexHullShape& hull )
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    for ( uint16_t i = 0; i < hull.GetVertexCount(); ++i )
    {
        const float y = hull.GetVertex( i ).y;
        minY = (std::min)( minY, y );
        maxY = (std::max)( maxY, y );
    }
    return minY == FLT_MAX ? 1.0f : (std::max)( 1.0f, maxY - minY );
}


constexpr float EDITOR_TEXTURE_MODE_INVERTED = -2.0f;


void ApplyEditorSpawnMaterial( GameModel& model, bool fixedObject, bool boxObject )
{
    if ( fixedObject )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, 1.0f );
    }
    else if ( boxObject )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
    }
    else
    {
        model.SetRenderTint( 0.42f, 0.50f, 1.0f, -1.0f );
    }
}


constexpr float EDITOR_PLACEMENT_SURFACE_EPSILON = 0.02f;
constexpr float EDITOR_PLACEMENT_SNAP = 2.0f;
struct EditorTreePartDefinition
{
    EditorHullAsset hullAsset;
    const char* suffix;
    float offsetX;
    float offsetY;
    float offsetZ;
    float restitution;
    SkullbonezCore::Rendering::RenderMaterialKind materialKind;
    const char* materialName;
    float colorR;
    float colorG;
    float colorB;
    float roughness;
    float specular;
    float stylization;
    bool startsFixed = false;
    bool contactReleaseOnImpact = false;
    float contactReleaseImpulseThreshold = 1.0f;
};


struct EditorTreeDefinition
{
    const char* label;
    const EditorTreePartDefinition* parts;
    int partCount;
    bool alignToTerrainNormal = false;
    bool forceFixed = false;
    bool seedAsleep = false;
};


struct EditorHousePartDefinition
{
    const char* suffix;
    float offsetX;
    float offsetY;
    float offsetZ;
    float halfX;
    float halfY;
    float halfZ;
    float restitution;
    SkullbonezCore::Rendering::RenderMaterialKind materialKind;
    const char* materialName;
    float colorR;
    float colorG;
    float colorB;
    float roughness;
    float specular;
    float stylization;
};


struct EditorHouseDefinition
{
    const char* label;
    const EditorHousePartDefinition* parts;
    int partCount;
    bool seedAsleep = true;
};


struct EditorBuildingDefinition
{
    int objectType;
    const char* assetName;
    const char* label;
};


constexpr EditorBuildingDefinition EDITOR_BUILDING_ASSETS[] = {
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP, "building.brick_house_low", "bhl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP, "building.brick_house_high", "bhh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP, "building.cute_house_low", "chl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP, "building.cute_house_high", "chh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP, "building.triple_decker_low", "tdl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP, "building.triple_decker_high", "tdh" },
};


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


std::string EditorResolveBuildingAssetLibraryPath()
{
    const SkullbonezCore::Assets::AssetSystem* assets = SkullbonezCore::Assets::ActiveAssetSystem();
    if ( assets )
    {
        if ( const SkullbonezCore::Assets::AssetLibrarySourceAsset* library =
                 assets->FindAssetLibrarySourceAsset( "assetlib.buildings" ) )
        {
            return library->resolvedPath;
        }
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


const Json* CachedEditorBuildingLibrary()
{
    static Json library;
    static bool loaded = false;
    static bool valid = false;
    if ( !loaded )
    {
        loaded = true;
        const std::string path = EditorResolveBuildingAssetLibraryPath();
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


const Json* CachedEditorBuildingAsset( int objectType )
{
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( objectType );
    const Json* library = CachedEditorBuildingLibrary();
    if ( !building || !library )
    {
        return nullptr;
    }

    const Json* assets = EditorJsonFindMember( *library, "assets" );
    if ( !assets || !assets->is_array() )
    {
        return nullptr;
    }
    for ( const Json& asset : *assets )
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
    if ( !value.is_array() || value.size() < 3 || !value[0].is_number() || !value[1].is_number() ||
         !value[2].is_number() )
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


std::string EditorLowercase( std::string value )
{
    std::transform( value.begin(),
                    value.end(),
                    value.begin(),
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
        const SkullbonezCore::Rendering::RenderMaterialKind kind =
            EditorMaterialKindFromAssetToken( modeValue->get<std::string>() );
        return SkullbonezCore::Rendering::RenderMaterialKindLegacyMode( kind );
    }
    return SkullbonezCore::Rendering::RenderMaterialKindLegacyMode(
        SkullbonezCore::Rendering::RenderMaterialKind::Stone );
}


SkullbonezCore::Rendering::RenderMaterial EditorBuildingPartMaterial( const Json& part )
{
    const Json* materialJson = EditorJsonFindMember( part, "material" );
    Vector3 color( 1.0f, 1.0f, 1.0f );
    float mode =
        SkullbonezCore::Rendering::RenderMaterialKindLegacyMode( SkullbonezCore::Rendering::RenderMaterialKind::Stone );
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

    SkullbonezCore::Rendering::RenderMaterial material =
        SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( color.x, color.y, color.z, mode );
    if ( materialJson && materialJson->is_object() )
    {
        material.roughness =
            std::clamp( EditorJsonFloatOr( *materialJson, "roughness", material.roughness ), 0.0f, 1.0f );
        material.metallic = std::clamp( EditorJsonFloatOr( *materialJson, "metallic", material.metallic ), 0.0f, 1.0f );
        material.specular = std::clamp( EditorJsonFloatOr( *materialJson, "specular", material.specular ), 0.0f, 1.0f );
        material.transmission =
            std::clamp( EditorJsonFloatOr( *materialJson, "transmission", material.transmission ), 0.0f, 1.0f );
        material.stylization =
            std::clamp( EditorJsonFloatOr( *materialJson, "stylization", material.stylization ), 0.0f, 1.0f );
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
        material.emissiveStrength =
            (std::max)( 0.0f, EditorJsonFloatOr( *materialJson, "strength", material.emissiveStrength ) );
        material.flags = static_cast<uint32_t>( (std::max)( 0.0f, EditorJsonFloatOr( *materialJson, "flags", 0.0f ) ) );
        const std::string name =
            EditorJsonStringOr( *materialJson,
                                "name",
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


int EditorBuildingPartCount( int objectType )
{
    const Json* asset = CachedEditorBuildingAsset( objectType );
    if ( !asset )
    {
        return 0;
    }
    const std::string type = EditorJsonStringOr( *asset, "type", "" );
    if ( type == "convexHull" )
    {
        return 1;
    }
    const Json* parts = EditorJsonFindMember( *asset, "parts" );
    return type == "compound" && parts && parts->is_array() ? static_cast<int>( parts->size() ) : 0;
}


template <typename Fn> bool ForEachEditorBuildingPart( int objectType, Fn&& fn )
{
    const Json* asset = CachedEditorBuildingAsset( objectType );
    if ( !asset )
    {
        return false;
    }
    const std::string type = EditorJsonStringOr( *asset, "type", "" );
    if ( type == "convexHull" )
    {
        fn( *asset );
        return true;
    }
    const Json* parts = EditorJsonFindMember( *asset, "parts" );
    if ( type != "compound" || !parts || !parts->is_array() )
    {
        return false;
    }
    for ( const Json& part : *parts )
    {
        if ( !part.is_object() )
        {
            return false;
        }
        fn( part );
    }
    return true;
}


const ConvexHullShape* CachedEditorBuildingHull( const std::string& hullPath )
{
    static std::vector<std::pair<std::string, ConvexHullShape>> hulls;
    for ( const auto& entry : hulls )
    {
        if ( entry.first == hullPath )
        {
            return &entry.second;
        }
    }
    hulls.emplace_back( hullPath, ConvexHullShape::LoadFromFile( ResolveEditorHullAssetPath( hullPath.c_str() ) ) );
    return &hulls.back().second;
}


float EditorBuildingVerticalSize( int objectType )
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    const bool ok = ForEachEditorBuildingPart(
        objectType,
        [&]( const Json& part )
        {
            const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
            const ConvexHullShape* hull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
            if ( !hull )
            {
                return;
            }
            const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
            const Quaternion orientation = EditorBuildingPartOrientation( IDENTITY_QUATERNION, part );
            Quaternion orientationCopy = orientation;
            const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
            const Vector3 hullLocalOffset = HullAuthoredLocalOffset( *hull );
            for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount(); ++vertexIndex )
            {
                const float y = offset.y + ( rotation * ( hullLocalOffset + hull->GetVertex( vertexIndex ) ) ).y;
                minY = (std::min)( minY, y );
                maxY = (std::max)( maxY, y );
            }
        } );
    return ok && minY != FLT_MAX ? (std::max)( 1.0f, maxY - minY ) : 1.0f;
}


bool TryComputeEditorBuildingWorldBounds( int objectType,
                                          const Vector3& terrainPoint,
                                          const Quaternion& placementOrientation,
                                          Vector3& outMin,
                                          Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    Quaternion placementCopy = placementOrientation;
    const RotationMatrix placementRotation = placementCopy.GetOrientationMatrix();
    const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
    const bool ok = ForEachEditorBuildingPart(
        objectType,
        [&]( const Json& part )
        {
            const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
            const ConvexHullShape* hull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
            if ( !hull )
            {
                return;
            }
            const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
            const Quaternion partOrientation = EditorBuildingPartOrientation( placementOrientation, part );
            Quaternion partCopy = partOrientation;
            const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
            const Vector3 hullLocalOffset = HullAuthoredLocalOffset( *hull );
            for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount(); ++vertexIndex )
            {
                const Vector3 world = base + placementRotation * offset +
                                      partRotation * ( hullLocalOffset + hull->GetVertex( vertexIndex ) );
                outMin.x = (std::min)( outMin.x, world.x );
                outMin.y = (std::min)( outMin.y, world.y );
                outMin.z = (std::min)( outMin.z, world.z );
                outMax.x = (std::max)( outMax.x, world.x );
                outMax.y = (std::max)( outMax.y, world.y );
                outMax.z = (std::max)( outMax.z, world.z );
            }
        } );
    return ok && outMin.x != FLT_MAX && outMax.x != -FLT_MAX;
}


constexpr EditorTreePartDefinition MakeEditorTreePart( EditorHullAsset hullAsset,
                                                       const char* suffix,
                                                       float offsetX,
                                                       float offsetY,
                                                       float offsetZ,
                                                       float restitution,
                                                       SkullbonezCore::Rendering::RenderMaterialKind materialKind,
                                                       const char* materialName,
                                                       float colorR,
                                                       float colorG,
                                                       float colorB,
                                                       float roughness,
                                                       float specular,
                                                       float stylization,
                                                       bool startsFixed = false,
                                                       bool contactReleaseOnImpact = false,
                                                       float contactReleaseImpulseThreshold = 1.0f )
{
    return { hullAsset,
             suffix,
             offsetX,
             offsetY,
             offsetZ,
             restitution,
             materialKind,
             materialName,
             colorR,
             colorG,
             colorB,
             roughness,
             specular,
             stylization,
             startsFixed,
             contactReleaseOnImpact || EditorHullAssetDefaultsToContactRelease( hullAsset ),
             contactReleaseImpulseThreshold };
}


constexpr EditorHousePartDefinition MakeEditorHousePart( const char* suffix,
                                                         float offsetX,
                                                         float offsetY,
                                                         float offsetZ,
                                                         float halfX,
                                                         float halfY,
                                                         float halfZ,
                                                         float restitution,
                                                         SkullbonezCore::Rendering::RenderMaterialKind materialKind,
                                                         const char* materialName,
                                                         float colorR,
                                                         float colorG,
                                                         float colorB,
                                                         float roughness,
                                                         float specular,
                                                         float stylization )
{
    return { suffix,
             offsetX,
             offsetY,
             offsetZ,
             halfX,
             halfY,
             halfZ,
             restitution,
             materialKind,
             materialName,
             colorR,
             colorG,
             colorB,
             roughness,
             specular,
             stylization };
}


constexpr EditorTreePartDefinition LiftEditorTreePartY( EditorTreePartDefinition part, float liftY )
{
    part.offsetY += liftY;
    return part;
}


constexpr EditorTreePartDefinition SmallRootPart()
{
    return MakeEditorTreePart( EditorHullAsset::TREE_ROOT_SMALL,
                               "root",
                               0.0f,
                               0.55f,
                               0.0f,
                               0.04f,
                               SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                               "editor_small_root",
                               0.24f,
                               0.12f,
                               0.055f,
                               0.96f,
                               0.05f,
                               0.50f );
}


constexpr EditorTreePartDefinition LargeRootPart()
{
    return MakeEditorTreePart( EditorHullAsset::TREE_ROOT_LARGE,
                               "root",
                               0.0f,
                               0.75f,
                               0.0f,
                               0.04f,
                               SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                               "editor_large_root",
                               0.23f,
                               0.115f,
                               0.052f,
                               0.96f,
                               0.05f,
                               0.50f );
}


constexpr EditorTreePartDefinition
PineNeedlePart( const char* suffix, float offsetX, float offsetY, float offsetZ, float shade )
{
    return MakeEditorTreePart( EditorHullAsset::PINE_NEEDLE_CLUSTER,
                               suffix,
                               offsetX,
                               offsetY,
                               offsetZ,
                               0.04f,
                               SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                               "editor_pine_loose_needles",
                               0.035f + shade * 0.025f,
                               0.18f + shade * 0.11f,
                               0.052f + shade * 0.045f,
                               0.90f,
                               0.06f,
                               0.94f,
                               true,
                               true,
                               1.25f );
}


constexpr EditorTreePartDefinition EDITOR_TREE_SMALL_PARTS[] = {
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_SMALL_FACETED,
                        "trunk",
                        0.0f,
                        6.5f,
                        0.0f,
                        0.06f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                        "editor_small_bark",
                        0.30f,
                        0.14f,
                        0.055f,
                        0.94f,
                        0.06f,
                        0.50f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_LOW,
                        "low",
                        0.0f,
                        20.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_small_needles_low",
                        0.055f,
                        0.24f,
                        0.12f,
                        0.89f,
                        0.08f,
                        0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_MID,
                        "mid",
                        0.0f,
                        28.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_small_needles_mid",
                        0.075f,
                        0.30f,
                        0.15f,
                        0.89f,
                        0.08f,
                        0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TOP,
                        "top",
                        0.0f,
                        35.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_small_needles_top",
                        0.10f,
                        0.36f,
                        0.18f,
                        0.89f,
                        0.08f,
                        0.90f ),
};
constexpr int EDITOR_TREE_SMALL_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_SMALL_PARTS ) / sizeof( EDITOR_TREE_SMALL_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL = { "tree_small",
                                                     EDITOR_TREE_SMALL_PARTS,
                                                     EDITOR_TREE_SMALL_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLOPE = { "tree_small_slope",
                                                           EDITOR_TREE_SMALL_PARTS,
                                                           EDITOR_TREE_SMALL_PART_COUNT,
                                                           true,
                                                           false };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLEEP =
    { "tree_small_sleep", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_PARTS[] = {
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_FACETED,
                        "trunk",
                        0.0f,
                        9.0f,
                        0.0f,
                        0.06f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                        "editor_big_bark",
                        0.31f,
                        0.16f,
                        0.07f,
                        0.94f,
                        0.06f,
                        0.48f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_LARGE,
                        "low",
                        0.0f,
                        24.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_big_needles_low",
                        0.045f,
                        0.20f,
                        0.055f,
                        0.88f,
                        0.08f,
                        0.88f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_MID,
                        "mid",
                        0.0f,
                        34.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_big_needles_mid",
                        0.06f,
                        0.25f,
                        0.075f,
                        0.88f,
                        0.08f,
                        0.88f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_TOP,
                        "top",
                        0.0f,
                        43.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_big_needles_top",
                        0.09f,
                        0.31f,
                        0.10f,
                        0.88f,
                        0.08f,
                        0.88f ),
};
constexpr int EDITOR_TREE_BIG_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_BIG_PARTS ) / sizeof( EDITOR_TREE_BIG_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG = { "tree_pine", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLOPE = { "tree_pine_slope",
                                                         EDITOR_TREE_BIG_PARTS,
                                                         EDITOR_TREE_BIG_PART_COUNT,
                                                         true,
                                                         false };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLEEP =
    { "tree_pine_sleep", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_PARTS[] = {
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_FACETED,
                        "trunk",
                        0.0f,
                        9.0f,
                        0.0f,
                        0.06f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                        "editor_cedar_bark",
                        0.28f,
                        0.13f,
                        0.055f,
                        0.94f,
                        0.06f,
                        0.50f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TALL_LOW,
                        "low",
                        0.0f,
                        25.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_cedar_needles_low",
                        0.055f,
                        0.24f,
                        0.12f,
                        0.89f,
                        0.08f,
                        0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TALL_MID,
                        "mid",
                        0.0f,
                        38.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_cedar_needles_mid",
                        0.075f,
                        0.30f,
                        0.15f,
                        0.89f,
                        0.08f,
                        0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TOP,
                        "top",
                        0.0f,
                        48.0f,
                        0.0f,
                        0.05f,
                        SkullbonezCore::Rendering::RenderMaterialKind::Foliage,
                        "editor_cedar_needles_top",
                        0.10f,
                        0.36f,
                        0.18f,
                        0.89f,
                        0.08f,
                        0.90f ),
};
constexpr int EDITOR_TREE_CEDAR_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_CEDAR_PARTS ) / sizeof( EDITOR_TREE_CEDAR_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR = { "tree_cedar",
                                                     EDITOR_TREE_CEDAR_PARTS,
                                                     EDITOR_TREE_CEDAR_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLOPE = { "tree_cedar_slope",
                                                           EDITOR_TREE_CEDAR_PARTS,
                                                           EDITOR_TREE_CEDAR_PART_COUNT,
                                                           true,
                                                           false };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLEEP =
    { "tree_cedar_sleep", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_SMALL_ROOTED_PARTS[] = {
    SmallRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[0],
                         SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[1],
                         SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[2],
                         SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[3],
                         SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_SMALL_ROOTED_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS ) / sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_ROOTED = { "tree_small_rooted",
                                                            EDITOR_TREE_SMALL_ROOTED_PARTS,
                                                            EDITOR_TREE_SMALL_ROOTED_PART_COUNT,
                                                            true,
                                                            true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_BIG_ROOTED_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_BIG_ROOTED_PARTS ) / sizeof( EDITOR_TREE_BIG_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG_ROOTED = { "tree_pine_rooted",
                                                          EDITOR_TREE_BIG_ROOTED_PARTS,
                                                          EDITOR_TREE_BIG_ROOTED_PART_COUNT,
                                                          true,
                                                          true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[0],
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[1],
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[2],
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[3],
                         SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_CEDAR_ROOTED_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS ) / sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_ROOTED = { "tree_cedar_rooted",
                                                            EDITOR_TREE_CEDAR_ROOTED_PARTS,
                                                            EDITOR_TREE_CEDAR_ROOTED_PART_COUNT,
                                                            true,
                                                            true };

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
constexpr int EDITOR_TREE_PINE_SHEDDING_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS ) / sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_PINE_SHEDDING = { "tree_pine_shedding",
                                                             EDITOR_TREE_PINE_SHEDDING_PARTS,
                                                             EDITOR_TREE_PINE_SHEDDING_PART_COUNT,
                                                             true,
                                                             true };


constexpr EditorHousePartDefinition EDITOR_BRICK_HOUSE_PARTS[] = {
    MakeEditorHousePart( "foundation",
                         0.0f,
                         0.38f,
                         0.0f,
                         23.5f,
                         0.38f,
                         16.0f,
                         0.08f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_foundation",
                         0.40f,
                         0.37f,
                         0.32f,
                         0.98f,
                         0.07f,
                         0.58f ),
    MakeEditorHousePart( "upper_floor",
                         0.0f,
                         7.52f,
                         0.0f,
                         22.8f,
                         0.40f,
                         15.2f,
                         0.07f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                         "editor_house_floor",
                         0.30f,
                         0.19f,
                         0.11f,
                         0.90f,
                         0.08f,
                         0.40f ),
    MakeEditorHousePart( "roof",
                         0.0f,
                         14.75f,
                         0.0f,
                         24.6f,
                         0.43f,
                         17.0f,
                         0.06f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_slate_roof",
                         0.13f,
                         0.17f,
                         0.22f,
                         0.94f,
                         0.10f,
                         0.62f ),
    MakeEditorHousePart( "front_lower_left",
                         -14.8f,
                         3.92f,
                         -15.45f,
                         7.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.55f,
                         0.22f,
                         0.17f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "front_lower_right",
                         14.8f,
                         3.92f,
                         -15.45f,
                         7.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.55f,
                         0.22f,
                         0.17f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "front_door_lintel",
                         0.0f,
                         6.37f,
                         -15.45f,
                         6.8f,
                         0.75f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.55f,
                         0.22f,
                         0.17f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "front_upper_left",
                         -15.0f,
                         11.12f,
                         -15.45f,
                         5.6f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "front_upper_center",
                         0.0f,
                         11.12f,
                         -15.45f,
                         4.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "front_upper_right",
                         15.0f,
                         11.12f,
                         -15.45f,
                         5.6f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.72f ),
    MakeEditorHousePart( "back_lower_left",
                         -14.8f,
                         3.92f,
                         15.45f,
                         7.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.50f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "back_lower_center",
                         0.0f,
                         3.92f,
                         15.45f,
                         6.8f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.50f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "back_lower_right",
                         14.8f,
                         3.92f,
                         15.45f,
                         7.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.50f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "back_upper_left",
                         -15.0f,
                         11.12f,
                         15.45f,
                         5.6f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "back_upper_center",
                         0.0f,
                         11.12f,
                         15.45f,
                         4.2f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "back_upper_right",
                         15.0f,
                         11.12f,
                         15.45f,
                         5.6f,
                         3.20f,
                         0.55f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.52f,
                         0.20f,
                         0.16f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "west_lower_front",
                         -22.45f,
                         3.92f,
                         -10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.49f,
                         0.18f,
                         0.14f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "west_lower_back",
                         -22.45f,
                         3.92f,
                         10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.49f,
                         0.18f,
                         0.14f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "east_lower_front",
                         22.45f,
                         3.92f,
                         -10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.49f,
                         0.18f,
                         0.14f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "east_lower_back",
                         22.45f,
                         3.92f,
                         10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.49f,
                         0.18f,
                         0.14f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "west_upper_front",
                         -22.45f,
                         11.12f,
                         -10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.51f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "west_upper_back",
                         -22.45f,
                         11.12f,
                         10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.51f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "east_upper_front",
                         22.45f,
                         11.12f,
                         -10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.51f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "east_upper_back",
                         22.45f,
                         11.12f,
                         10.0f,
                         0.55f,
                         3.20f,
                         5.0f,
                         0.10f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_brick",
                         0.51f,
                         0.19f,
                         0.15f,
                         0.96f,
                         0.07f,
                         0.70f ),
    MakeEditorHousePart( "door",
                         0.0f,
                         3.17f,
                         -16.04f,
                         2.4f,
                         2.45f,
                         0.20f,
                         0.08f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                         "editor_house_front_door",
                         0.28f,
                         0.16f,
                         0.075f,
                         0.86f,
                         0.10f,
                         0.38f ),
    MakeEditorHousePart( "front_window_left",
                         -6.8f,
                         11.10f,
                         -16.05f,
                         1.25f,
                         1.25f,
                         0.16f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.40f,
                         0.68f,
                         0.92f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "front_window_right",
                         6.8f,
                         11.10f,
                         -16.05f,
                         1.25f,
                         1.25f,
                         0.16f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.40f,
                         0.68f,
                         0.92f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "back_window_left",
                         -6.8f,
                         11.10f,
                         16.05f,
                         1.25f,
                         1.25f,
                         0.16f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.38f,
                         0.64f,
                         0.86f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "back_window_right",
                         6.8f,
                         11.10f,
                         16.05f,
                         1.25f,
                         1.25f,
                         0.16f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.38f,
                         0.64f,
                         0.86f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "west_window",
                         -23.05f,
                         3.92f,
                         0.0f,
                         0.16f,
                         1.20f,
                         1.35f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.36f,
                         0.62f,
                         0.84f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "east_window",
                         23.05f,
                         3.92f,
                         0.0f,
                         0.16f,
                         1.20f,
                         1.35f,
                         0.04f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Glass,
                         "editor_house_window",
                         0.36f,
                         0.62f,
                         0.84f,
                         0.08f,
                         0.88f,
                         0.18f ),
    MakeEditorHousePart( "porch",
                         0.0f,
                         0.28f,
                         -20.2f,
                         7.0f,
                         0.28f,
                         3.6f,
                         0.08f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_porch",
                         0.44f,
                         0.40f,
                         0.34f,
                         0.97f,
                         0.07f,
                         0.58f ),
    MakeEditorHousePart( "porch_post_left",
                         -5.2f,
                         2.98f,
                         -20.2f,
                         0.34f,
                         2.42f,
                         0.34f,
                         0.06f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                         "editor_house_porch_post",
                         0.30f,
                         0.18f,
                         0.09f,
                         0.88f,
                         0.08f,
                         0.42f ),
    MakeEditorHousePart( "porch_post_right",
                         5.2f,
                         2.98f,
                         -20.2f,
                         0.34f,
                         2.42f,
                         0.34f,
                         0.06f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Bark,
                         "editor_house_porch_post",
                         0.30f,
                         0.18f,
                         0.09f,
                         0.88f,
                         0.08f,
                         0.42f ),
    MakeEditorHousePart( "chimney",
                         13.0f,
                         16.55f,
                         7.6f,
                         0.82f,
                         1.37f,
                         0.82f,
                         0.08f,
                         SkullbonezCore::Rendering::RenderMaterialKind::Stone,
                         "editor_house_chimney",
                         0.42f,
                         0.18f,
                         0.14f,
                         0.96f,
                         0.07f,
                         0.70f ),
};
constexpr int EDITOR_BRICK_HOUSE_PART_COUNT =
    static_cast<int>( sizeof( EDITOR_BRICK_HOUSE_PARTS ) / sizeof( EDITOR_BRICK_HOUSE_PARTS[0] ) );
constexpr EditorHouseDefinition EDITOR_BRICK_HOUSE_SLEEP = { "brick_house",
                                                             EDITOR_BRICK_HOUSE_PARTS,
                                                             EDITOR_BRICK_HOUSE_PART_COUNT,
                                                             true };


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
    const float dot = std::clamp( up * terrainNormal, -1.0f, 1.0f );
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


const ConvexHullShape* CachedEditorHullForAsset( EditorHullAsset asset )
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
            hulls[i] = ConvexHullShape::LoadFromFile( path );
            loaded[i] = true;
        }
        return &hulls[i];
    }
    return nullptr;
}


const ConvexHullShape* CachedEditorHullForType( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    return CachedEditorHullForAsset( EditorHullAssetForType( type ) );
}


bool TryComputeEditorTreeVerticalBounds( const EditorTreeDefinition& tree, float& outMinY, float& outMaxY )
{
    outMinY = FLT_MAX;
    outMaxY = -FLT_MAX;
    for ( int partIndex = 0; partIndex < tree.partCount; ++partIndex )
    {
        const EditorTreePartDefinition& part = tree.parts[partIndex];
        const ConvexHullShape* hull = CachedEditorHullForAsset( part.hullAsset );
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


bool TryComputeEditorTreeWorldBounds( const EditorTreeDefinition& tree,
                                      const Vector3& terrainPoint,
                                      const RotationMatrix& orientation,
                                      Vector3& outMin,
                                      Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    const Vector3 surfaceLift = orientation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
    for ( int partIndex = 0; partIndex < tree.partCount; ++partIndex )
    {
        const EditorTreePartDefinition& part = tree.parts[partIndex];
        const ConvexHullShape* hull = CachedEditorHullForAsset( part.hullAsset );
        if ( !hull )
        {
            return false;
        }

        const Vector3 localBase =
            Vector3( part.offsetX, part.offsetY, part.offsetZ ) + HullAuthoredLocalOffset( *hull );
        for ( uint16_t vertexIndex = 0; vertexIndex < hull->GetVertexCount(); ++vertexIndex )
        {
            const Vector3 world =
                terrainPoint + surfaceLift + orientation * ( localBase + hull->GetVertex( vertexIndex ) );
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


void ExpandEditorHousePartWorldBounds( const EditorHousePartDefinition& part,
                                       const Vector3& terrainPoint,
                                       const RotationMatrix& orientation,
                                       Vector3& outMin,
                                       Vector3& outMax )
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


bool TryComputeEditorHouseWorldBounds( const EditorHouseDefinition& house,
                                       const Vector3& terrainPoint,
                                       const RotationMatrix& orientation,
                                       Vector3& outMin,
                                       Vector3& outMax )
{
    outMin = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    outMax = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    for ( int partIndex = 0; partIndex < house.partCount; ++partIndex )
    {
        ExpandEditorHousePartWorldBounds( house.parts[partIndex], terrainPoint, orientation, outMin, outMax );
    }
    return outMin.x != FLT_MAX && outMax.x != -FLT_MAX;
}


float EditorTreeVerticalSize( int objectType )
{
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( objectType );
    if ( !tree )
    {
        return 1.0f;
    }
    float minY = 0.0f;
    float maxY = 1.0f;
    return TryComputeEditorTreeVerticalBounds( *tree, minY, maxY ) ? (std::max)( 1.0f, maxY - minY ) : 1.0f;
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
    SkullbonezCore::Rendering::RenderMaterial material =
        SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( part.colorR,
                                                                     part.colorG,
                                                                     part.colorB,
                                                                     static_cast<float>( part.materialKind ) );
    strncpy_s( material.name, sizeof( material.name ), part.materialName, _TRUNCATE );
    material.roughness = part.roughness;
    material.specular = part.specular;
    material.stylization = part.stylization;
    return material;
}


SkullbonezCore::Rendering::RenderMaterial EditorHousePartMaterial( const EditorHousePartDefinition& part )
{
    SkullbonezCore::Rendering::RenderMaterial material =
        SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( part.colorR,
                                                                     part.colorG,
                                                                     part.colorB,
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

    outMaterial = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint(
        r,
        g,
        b,
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
    outMaterial = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint(
        r,
        g,
        b,
        static_cast<float>( SkullbonezCore::Rendering::RenderMaterialKind::Bark ) );
    strncpy_s( outMaterial.name, sizeof( outMaterial.name ), name, _TRUNCATE );
    outMaterial.roughness = 0.96f;
    outMaterial.specular = 0.05f;
    outMaterial.stylization = 0.50f;
    return true;
}

bool TryBuildScaledEditorHullForType( int objectType, const Vector3& placementScale, ConvexHullShape& outHull )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const ConvexHullShape* baseHull = CachedEditorHullForType( type );
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


float EditorPlacementAltitudeStepSize( int objectType, const Vector3& placementScale )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        return scale.y * 2.0f;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        return scale.x * 2.0f;
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL:
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        return scale.x * 18.5f;
    default:
    {
        if ( EditorTreeDefinitionForType( type ) )
        {
            return EditorTreeVerticalSize( type );
        }
        if ( EditorBuildingDefinitionForType( type ) )
        {
            return EditorBuildingVerticalSize( type );
        }
        if ( EditorHouseDefinitionForType( type ) )
        {
            return EditorHouseVerticalSize( type );
        }
        ConvexHullShape hull;
        return TryBuildScaledEditorHullForType( type, scale, hull ) ? HullVerticalSize( hull ) : 1.0f;
    }
    }
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float EditorModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

float EditorShapeAxisExtent( const CollisionShape& shape, int axis )
{
    if ( axis < 0 || axis > 2 )
    {
        return 1.0f;
    }

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        return (std::max)( sphere->GetRadius(), 0.25f );
    }

    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& halfExtents = box->GetHalfExtents();
        if ( axis == 0 )
        {
            return (std::max)( halfExtents.x, 0.25f );
        }
        if ( axis == 1 )
        {
            return (std::max)( halfExtents.y, 0.25f );
        }
        return (std::max)( halfExtents.z, 0.25f );
    }

    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3& halfExtents = hull->GetInertiaHalfExtents();
        if ( axis == 0 )
        {
            return (std::max)( halfExtents.x, 0.25f );
        }
        if ( axis == 1 )
        {
            return (std::max)( halfExtents.y, 0.25f );
        }
        return (std::max)( halfExtents.z, 0.25f );
    }

    return 1.0f;
}


bool TryEditorScaleFactorFromShapes( const CollisionShape& startShape,
                                     const CollisionShape& currentShape,
                                     int axis,
                                     float& outFactor )
{
    const float startExtent = EditorShapeAxisExtent( startShape, axis );
    if ( startExtent <= 0.0f )
    {
        return false;
    }

    const float currentExtent = EditorShapeAxisExtent( currentShape, axis );
    outFactor = currentExtent / startExtent;
    return std::isfinite( outFactor ) && fabsf( outFactor - 1.0f ) > 1.0e-4f;
}


float EditorGizmoAxisLength( float modelRadius )
{
    return (std::max)( 14.0f, modelRadius + 12.0f );
}


float EditorGizmoRotationRadius( float modelRadius )
{
    return (std::max)( 12.0f, modelRadius + 7.0f );
}


using EditorGizmoGroupIndices = std::array<int, RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY>;

bool TryGetEditorRagdollInstancePrefixLength( const GameModel& model, std::size_t& outPrefixLength )
{
    static constexpr const char* RAGDOLL_SUFFIXES[] = { "torso",
                                                        "head",
                                                        "upper_arm_l",
                                                        "lower_arm_l",
                                                        "upper_arm_r",
                                                        "lower_arm_r",
                                                        "upper_leg_l",
                                                        "lower_leg_l",
                                                        "upper_leg_r",
                                                        "lower_leg_r" };

    const char* name = model.GetName();
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    const std::size_t nameLength = std::strlen( name );
    for ( const char* suffix : RAGDOLL_SUFFIXES )
    {
        const std::size_t suffixLength = std::strlen( suffix );
        if ( nameLength <= suffixLength + 1 )
        {
            continue;
        }

        const std::size_t suffixStart = nameLength - suffixLength;
        if ( name[suffixStart - 1] != '_' || std::strncmp( name + suffixStart, suffix, suffixLength ) != 0 )
        {
            continue;
        }

        outPrefixLength = suffixStart - 1;
        return outPrefixLength > 0;
    }
    return false;
}


bool EditorRagdollPrefixMatches( const char* a, std::size_t aLength, const char* b, std::size_t bLength )
{
    return aLength == bLength && std::strncmp( a, b, aLength ) == 0;
}


int GatherSelectedEditorTransformGroup( const std::vector<GameModel>& models,
                                        int selectedIndex,
                                        EditorGizmoGroupIndices& outIndices )
{
    outIndices.fill( -1 );
    if ( selectedIndex < 0 || selectedIndex >= static_cast<int>( models.size() ) )
    {
        return 0;
    }

    std::size_t selectedPrefixLength = 0;
    if ( !TryGetEditorRagdollInstancePrefixLength( models[static_cast<std::size_t>( selectedIndex )],
                                                   selectedPrefixLength ) )
    {
        outIndices[0] = selectedIndex;
        return 1;
    }

    const char* selectedName = models[static_cast<std::size_t>( selectedIndex )].GetName();
    int count = 0;
    for ( int i = 0; i < static_cast<int>( models.size() ) && count < static_cast<int>( outIndices.size() ); ++i )
    {
        const GameModel& candidate = models[static_cast<std::size_t>( i )];
        std::size_t prefixLength = 0;
        if ( TryGetEditorRagdollInstancePrefixLength( candidate, prefixLength ) &&
             EditorRagdollPrefixMatches( selectedName, selectedPrefixLength, candidate.GetName(), prefixLength ) )
        {
            outIndices[static_cast<std::size_t>( count )] = i;
            ++count;
        }
    }

    if ( count <= 0 )
    {
        outIndices[0] = selectedIndex;
        return 1;
    }
    return count;
}


bool TryGetEditorSelectionFrame( const std::vector<GameModel>& models,
                                 int selectedIndex,
                                 Vector3& outOrigin,
                                 float& outRadius,
                                 EditorGizmoGroupIndices* outGroupIndices = nullptr,
                                 int* outGroupCount = nullptr )
{
    EditorGizmoGroupIndices indices = {};
    const int count = GatherSelectedEditorTransformGroup( models, selectedIndex, indices );
    if ( count <= 0 )
    {
        return false;
    }

    Vector3 origin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    for ( int i = 0; i < count; ++i )
    {
        origin += models[static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] )].GetPosition();
    }
    origin /= static_cast<float>( count );

    float radius = 1.0f;
    for ( int i = 0; i < count; ++i )
    {
        const GameModel& model = models[static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] )];
        radius = (std::max)( radius, Distance( model.GetPosition(), origin ) + EditorModelRadius( model ) );
    }

    outOrigin = origin;
    outRadius = radius;
    if ( outGroupIndices )
    {
        *outGroupIndices = indices;
    }
    if ( outGroupCount )
    {
        *outGroupCount = count;
    }
    return true;
}


void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor,
                                       const std::vector<GameModel>& models,
                                       bool allowRagdollGroup )
{
    editor.gizmoDragGroupCount = 0;
    editor.gizmoDragGroupIndices.fill( -1 );
    if ( editor.selectedModelIndex < 0 || editor.selectedModelIndex >= static_cast<int>( models.size() ) )
    {
        return;
    }

    EditorGizmoGroupIndices indices = {};
    int count = 1;
    indices[0] = editor.selectedModelIndex;
    if ( allowRagdollGroup )
    {
        count = GatherSelectedEditorTransformGroup( models, editor.selectedModelIndex, indices );
    }

    editor.gizmoDragGroupCount =
        std::clamp( count, 0, static_cast<int>( RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY ) );
    for ( int i = 0; i < editor.gizmoDragGroupCount; ++i )
    {
        const int index = indices[static_cast<std::size_t>( i )];
        editor.gizmoDragGroupIndices[static_cast<std::size_t>( i )] = index;
        const GameModel& model = models[static_cast<std::size_t>( index )];
        editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( i )] = model.GetPosition();
        editor.gizmoDragGroupStartOrientations[static_cast<std::size_t>( i )] = model.GetOrientation();
    }
}


int ValidCapturedEditorGizmoGroupCount( const RunEditorPlacementState& editor, int modelCount )
{
    const int count = editor.gizmoDragGroupCount;
    if ( count <= 0 || count > static_cast<int>( RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY ) )
    {
        return 0;
    }
    for ( int i = 0; i < count; ++i )
    {
        const int index = editor.gizmoDragGroupIndices[static_cast<std::size_t>( i )];
        if ( index < 0 || index >= modelCount )
        {
            return 0;
        }
    }
    return count;
}


void ResetEditorModelMotionAndWake( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                    SkullbonezCore::Physics::PhysicsEngine& physics,
                                    int index,
                                    GameModel& model )
{
    model.SetLinearVelocity( SkullbonezCore::Math::Vector::ZERO_VECTOR );
    model.SetAngularVelocity( SkullbonezCore::Math::Vector::ZERO_VECTOR );
    if ( !model.IsFixed() )
    {
        physics.WakeBody( collection, index );
    }
}


float ReplayVelocityLinearBaseLength( float modelRadius )
{
    return (std::max)( 10.0f, modelRadius + 7.0f );
}


float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent )
{
    const float sign = velocityComponent < 0.0f ? -1.0f : 1.0f;
    const float t = std::clamp( fabsf( velocityComponent ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
    return sign * ( ReplayVelocityLinearBaseLength( modelRadius ) + t * REPLAY_VELOCITY_EDIT_LINEAR_EXTRA );
}


float ReplayVelocityLinearUnitsPerWorld()
{
    return REPLAY_VELOCITY_EDIT_LINEAR_MAX / REPLAY_VELOCITY_EDIT_LINEAR_EXTRA;
}


float ReplayVelocityAngularBaseRadius( float modelRadius )
{
    return (std::max)( 11.0f, modelRadius + 6.0f );
}


float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent )
{
    const float t = std::clamp( fabsf( angularComponent ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
    return ReplayVelocityAngularBaseRadius( modelRadius ) + t * (std::max)( 5.0f, modelRadius * 0.85f );
}


float ReplayVelocityAxisComponent( const Vector3& value, int axis )
{
    if ( axis == 0 )
    {
        return value.x;
    }
    if ( axis == 1 )
    {
        return value.y;
    }
    return value.z;
}


void ReplayVelocitySetAxisComponent( Vector3& value, int axis, float component )
{
    if ( axis == 0 )
    {
        value.x = component;
    }
    else if ( axis == 1 )
    {
        value.y = component;
    }
    else
    {
        value.z = component;
    }
}


void ReplayVelocityAxisColor( int axis, float heat, bool hot, bool active, float& r, float& g, float& b )
{
    r = axis == 0 ? 1.0f : 0.10f;
    g = axis == 1 ? 0.95f : 0.16f;
    b = axis == 2 ? 1.0f : 0.14f;
    r = std::clamp( r + heat * 0.46f, 0.0f, 1.0f );
    g = std::clamp( g + heat * 0.22f, 0.0f, 1.0f );
    b = std::clamp( b - heat * 0.34f, 0.05f, 1.0f );
    if ( hot || active )
    {
        r = (std::min)( 1.0f, r + 0.34f );
        g = (std::min)( 1.0f, g + 0.34f );
        b = (std::min)( 1.0f, b + 0.20f );
    }
    if ( active )
    {
        r = 1.0f;
        g = 0.96f;
        b = 0.18f;
    }
}


Vector3 EditorRotationRingBasisA( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 2:
        return Vector3( 1.0f, 0.0f, 0.0f );
    default:
        return Vector3( 1.0f, 0.0f, 0.0f );
    }
}


Vector3 EditorRotationRingBasisB( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 1:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 1.0f, 0.0f );
    default:
        return Vector3( 0.0f, 1.0f, 0.0f );
    }
}


float WrapEditorAngleDelta( float delta )
{
    while ( delta > _PI )
    {
        delta -= 2.0f * _PI;
    }
    while ( delta < -_PI )
    {
        delta += 2.0f * _PI;
    }
    return delta;
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = segment * segment;
    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, toPoint * rayDirection );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segmentLenSq;
    const float d = rayDirection * w0;
    const float e = segment * w0;
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}


constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 64;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 12;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
constexpr float MOUSE_PICKUP_DEAD_ZONE = 0.04f;
constexpr float MOUSE_PICKUP_STIFFNESS = 18.0f;
constexpr float MOUSE_PICKUP_DAMPING = 1.35f;
constexpr float MOUSE_PICKUP_MAX_IMPULSE = 260.0f;

} // namespace

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
bool BeginEditorGizmoDragGesture( EditorGizmoContext context, int modelIndex, int axis, bool angular )
{
    if ( context.interaction.PointerCapture() != RuntimePointerCaptureOwner::None ||
         context.interaction.Gesture().kind != RuntimeInteractionGestureKind::None )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = mouse.x;
    gesture.startY = mouse.y;
    gesture.modelIndex = modelIndex;
    gesture.axis = axis;
    gesture.angular = angular;

    context.interaction.BeginGesture( gesture,
                                      RuntimePointerCaptureOwner::ToolGesture,
                                      InteractionExitReason::BeginGesture );
    return context.interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag;
}


void EndEditorGizmoDragGesture( EditorGizmoContext context )
{
    if ( context.interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        context.interaction.EndGesture( InteractionExitReason::EndGesture );
    }
}


void CancelEditorGizmoDragState( EditorGizmoContext context )
{
    EndEditorGizmoDragGesture( context );
    context.editor.gizmoDragActive = false;
    context.editor.gizmoDragIsRotation = false;
    context.editor.gizmoDragIsScale = false;
    context.editor.activeGizmoAxis = -1;
    context.editor.gizmoDragGroupCount = 0;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore


void Run::TickEditorViewportAndPlacementScaleInput( int unhandledWheelDelta )
{
    const bool editorViewportLookNow =
        m_runtimeTools.Editor().editorModeEnabled && Input::IsRightMouseDown() && !m_UI.BlocksCameraMouse();
    if ( editorViewportLookNow != m_runtimeTools.Editor().viewportLookActive )
    {
        InputController::ResetMouseLook( m_camera );
    }
    m_runtimeTools.Editor().viewportLookActive = editorViewportLookNow;
    if ( editorViewportLookNow != ( m_runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook ) )
    {
        UpdateRuntimeInputModeAfterAction( editorViewportLookNow ? RuntimeInputAction::BeginEditorViewportLook
                                                                 : RuntimeInputAction::EndEditorViewportLook,
                                           RuntimeInputActionSource::Mouse );
    }

    const int placementWheelSteps = EditorMouseWheelSteps( unhandledWheelDelta );
    const bool placementLeftMouseNow = Input::IsLeftMouseDown();
    const bool placementYawWheel = placementWheelSteps != 0 && m_runtimeTools.Editor().editorModeEnabled &&
                                   m_runtimeTools.Editor().placementModeEnabled && Input::IsKeyDown( VK_CONTROL ) &&
                                   !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse();
    if ( placementYawWheel )
    {
        EnterInteractiveSceneRun();
        m_runtimeTools.Editor().placementYawRadians =
            WrapEditorAngleDelta( m_runtimeTools.Editor().placementYawRadians +
                                  static_cast<float>( placementWheelSteps ) * EDITOR_PLACEMENT_YAW_STEP_RADIANS );
    }
    if ( m_runtimeTools.Editor().placementScaleActive && placementLeftMouseNow &&
         !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse() )
    {
        if ( placementWheelSteps != 0 && !placementYawWheel )
        {
            EnterInteractiveSceneRun();
            m_runtimeTools.Editor().placementScaleWheelSteps += placementWheelSteps;
        }

        const POINT currentClient = Input::GetClientMouseCoordinates();
        const float dragPixelsX =
            static_cast<float>( currentClient.x - m_runtimeTools.Editor().placementScaleStartClient.x );
        const float dragPixelsY =
            static_cast<float>( currentClient.y - m_runtimeTools.Editor().placementScaleStartClient.y );
        m_runtimeTools.Editor().placementScale =
            EditorPlacementScaleFromGesture( m_runtimeTools.Editor().objectType,
                                             m_runtimeTools.Editor().placementScaleStart,
                                             dragPixelsX,
                                             dragPixelsY,
                                             m_runtimeTools.Editor().placementScaleWheelSteps );
    }
    else if ( placementWheelSteps != 0 && m_runtimeTools.Editor().editorModeEnabled &&
              m_runtimeTools.Editor().placementModeEnabled && !placementYawWheel &&
              !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse() )
    {
        const int nextAltitudeSteps =
            (std::max)( 0, m_runtimeTools.Editor().placementAltitudeSteps + placementWheelSteps );
        if ( nextAltitudeSteps != m_runtimeTools.Editor().placementAltitudeSteps )
        {
            EnterInteractiveSceneRun();
            m_runtimeTools.Editor().placementAltitudeSteps = nextAltitudeSteps;
        }
    }
    ApplyCursorOwnership();
}


bool Run::TickEditorWorldClick( const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    UpdateEditorInteractionPreview();

    const bool leftMouseNow = mouseEdges.leftDown;
    const bool leftPressed = mouseEdges.leftPressed;
    const bool leftReleased = mouseEdges.leftReleased;
    bool consumedWorldClick = false;

    if ( m_runtimeTools.Editor().placementScaleActive )
    {
        consumedWorldClick = true;
        if ( leftReleased || suppressWorldActionThisFrame )
        {
            if ( leftReleased && !suppressWorldActionThisFrame && m_runtimeTools.Editor().placementPreviewVisible )
            {
                EditorObjectPlacementContext placementContext{ m_runtimeTools.Editor(),
                                                               m_cGameModelCollection,
                                                               SceneState(),
                                                               m_cWorldEnvironment,
                                                               m_systems.terrain.get(),
                                                               ActiveGameModelCapacity() };
                EditorObjectPlacementRequest placementRequest{ m_runtimeTools.Editor().objectType,
                                                               m_runtimeTools.Editor().placeStaticObject,
                                                               m_runtimeTools.Editor().placementTerrainPoint };
                EditorObjectPlacementResult placementResult;
                if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
                {
                    EnterInteractiveSceneRun();
                    PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
                    if ( placementResult.placed )
                    {
                        RecordReplayEditorPlaceEvent( placementResult.objectType,
                                                      placementResult.fixedObject,
                                                      placementResult.autoTerrainAlign,
                                                      placementResult.modelCountBefore,
                                                      placementResult.terrainPoint,
                                                      placementResult.placementScale,
                                                      placementResult.placementYawRadians );

                        RuntimeInteractionCommand command;
                        command.type = RuntimeInteractionCommandType::SetEditorSelection;
                        command.modelIndex = placementResult.modelCountAfter - 1;
                        command.claimSelectionOwner = false;
                        ExecuteRuntimeInteractionCommand( command );
                    }
                }
            }
            m_runtimeTools.Editor().placementScaleActive = false;
            m_runtimeTools.Editor().placementScaleWheelSteps = 0;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorPlacementScale,
                                               RuntimeInputActionSource::Mouse );
        }
    }

    if ( !consumedWorldClick && m_runtimeTools.Editor().gizmoDragActive )
    {
        consumedWorldClick = true;
        if ( leftMouseNow && !suppressWorldActionThisFrame )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
            {
                if ( m_runtimeTools.Editor().gizmoDragIsScale )
                {
                    ScaleSelectedEditorObjectAlongAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
                else if ( m_runtimeTools.Editor().gizmoDragIsRotation )
                {
                    RotateSelectedEditorObjectAroundAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
                else
                {
                    MoveSelectedEditorObjectAlongAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
            }
        }
        if ( leftReleased || suppressWorldActionThisFrame )
        {
            if ( leftReleased && !suppressWorldActionThisFrame && m_runtimeTools.Editor().selectedModelIndex >= 0 &&
                 m_runtimeTools.Editor().selectedModelIndex < m_cGameModelCollection.GetModelCount() )
            {
                const GameModel& model =
                    m_cGameModelCollection.GetModelAtIndex( m_runtimeTools.Editor().selectedModelIndex );
                if ( m_runtimeTools.Editor().gizmoDragIsScale )
                {
                    int scaleAxis = m_runtimeTools.Editor().activeGizmoAxis;
                    float scaleFactor = 1.0f;
                    const uint32_t changedFlags =
                        TryEditorScaleFactorFromShapes( m_runtimeTools.Editor().gizmoDragStartShape,
                                                        model.GetCollisionShape(),
                                                        scaleAxis,
                                                        scaleFactor )
                            ? REPLAY_EDITOR_TRANSFORM_SCALE
                            : 0u;
                    RecordReplayEditorTransformEvent( m_runtimeTools.Editor().selectedModelIndex,
                                                      changedFlags,
                                                      model,
                                                      scaleAxis,
                                                      scaleFactor );
                }
                else
                {
                    const int groupCount = ValidCapturedEditorGizmoGroupCount( m_runtimeTools.Editor(),
                                                                               m_cGameModelCollection.GetModelCount() );
                    if ( groupCount > 0 )
                    {
                        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
                        {
                            const int modelIndex =
                                m_runtimeTools.Editor().gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
                            const GameModel& groupModel = m_cGameModelCollection.GetModelAtIndex( modelIndex );
                            uint32_t changedFlags = 0;
                            changedFlags |=
                                EditorPositionsDiffer(
                                    groupModel.GetPosition(),
                                    m_runtimeTools.Editor()
                                        .gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] )
                                    ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                    : 0u;
                            changedFlags |=
                                EditorOrientationsDiffer(
                                    groupModel.GetOrientation(),
                                    m_runtimeTools.Editor()
                                        .gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )] )
                                    ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                    : 0u;
                            RecordReplayEditorTransformEvent( modelIndex, changedFlags, groupModel, -1, 1.0f );
                        }
                    }
                    else
                    {
                        uint32_t changedFlags = 0;
                        changedFlags |=
                            EditorPositionsDiffer( model.GetPosition(), m_runtimeTools.Editor().gizmoDragStartPosition )
                                ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                : 0u;
                        changedFlags |= EditorOrientationsDiffer( model.GetOrientation(),
                                                                  m_runtimeTools.Editor().gizmoDragStartOrientation )
                                            ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                            : 0u;
                        RecordReplayEditorTransformEvent( m_runtimeTools.Editor().selectedModelIndex,
                                                          changedFlags,
                                                          model,
                                                          -1,
                                                          1.0f );
                    }
                }
            }
            CancelEditorGizmoDragState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorGizmoDrag,
                                               RuntimeInputActionSource::Mouse );
        }
    }

    if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame )
    {
        const bool inspectGizmoActive = InspectGizmoInteractionActive();
        const bool transformGizmoActive = ( m_runtimeTools.Editor().editorModeEnabled || inspectGizmoActive ) &&
                                          !m_runtimeTools.Editor().placementModeEnabled;
        const WorldInteractionOwner transformOwner =
            inspectGizmoActive ? WorldInteractionOwner::InspectGizmo : WorldInteractionOwner::EditorGizmo;
        const InteractionExitReason transformReason =
            inspectGizmoActive ? InteractionExitReason::EnterInspect : InteractionExitReason::EnterEdit;
        const bool canCaptureGizmoGesture = m_interaction.PointerCapture() == RuntimePointerCaptureOwner::None &&
                                            m_interaction.Gesture().kind == RuntimeInteractionGestureKind::None;
        const bool editorScaleMode = transformGizmoActive && Input::IsKeyDown( VK_CONTROL );
        if ( canCaptureGizmoGesture && editorScaleMode && m_runtimeTools.Editor().selectedModelIndex >= 0 &&
             m_runtimeTools.Editor().selectedModelIndex < m_cGameModelCollection.GetModelCount() &&
             m_runtimeTools.Editor().hotGizmoAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float axisT = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryEditorAxisRayParameter( gizmoContext,
                                            m_runtimeTools.Editor().hotGizmoAxis,
                                            rayOrigin,
                                            rayDirection,
                                            axisT ) )
            {
                EnterInteractiveSceneRun();
                SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                   m_runtimeTools.Editor().selectedModelIndex,
                                                   m_runtimeTools.Editor().hotGizmoAxis,
                                                   false ) )
                {
                    return consumedWorldClick;
                }
                GameModel& model = m_cGameModelCollection.GetModelAtIndex( m_runtimeTools.Editor().selectedModelIndex );
                m_runtimeTools.Editor().gizmoDragActive = true;
                m_runtimeTools.Editor().gizmoDragIsRotation = false;
                m_runtimeTools.Editor().gizmoDragIsScale = true;
                m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotGizmoAxis;
                m_runtimeTools.Editor().gizmoDragStartAxisT = axisT;
                m_runtimeTools.Editor().gizmoDragStartShape = model.GetCollisionShape();
                m_runtimeTools.Editor().gizmoDragStartPosition = model.GetPosition();
                m_runtimeTools.Editor().gizmoDragStartOrientation = model.GetOrientation();
                CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), m_cGameModelCollection.Models(), false );
                consumedWorldClick = true;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoScale,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( canCaptureGizmoGesture && transformGizmoActive && !editorScaleMode &&
             m_runtimeTools.Editor().selectedModelIndex >= 0 && m_runtimeTools.Editor().hotRotationAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float startAngle = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryEditorRotationRayAngle( gizmoContext,
                                            m_runtimeTools.Editor().hotRotationAxis,
                                            rayOrigin,
                                            rayDirection,
                                            startAngle ) )
            {
                EnterInteractiveSceneRun();
                SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                   m_runtimeTools.Editor().selectedModelIndex,
                                                   m_runtimeTools.Editor().hotRotationAxis,
                                                   true ) )
                {
                    return consumedWorldClick;
                }
                m_runtimeTools.Editor().gizmoDragActive = true;
                m_runtimeTools.Editor().gizmoDragIsRotation = true;
                m_runtimeTools.Editor().gizmoDragIsScale = false;
                m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotRotationAxis;
                m_runtimeTools.Editor().gizmoDragStartRotationAngle = startAngle;
                const std::vector<GameModel>& models = m_cGameModelCollection.Models();
                Vector3 selectionOrigin =
                    models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetPosition();
                float selectionRadius = 1.0f;
                TryGetEditorSelectionFrame( models,
                                            m_runtimeTools.Editor().selectedModelIndex,
                                            selectionOrigin,
                                            selectionRadius );
                m_runtimeTools.Editor().gizmoDragStartPosition = selectionOrigin;
                m_runtimeTools.Editor().gizmoDragStartOrientation =
                    models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetOrientation();
                CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), models, true );
                consumedWorldClick = true;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoRotate,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && canCaptureGizmoGesture && transformGizmoActive && !editorScaleMode &&
             m_runtimeTools.Editor().selectedModelIndex >= 0 && m_runtimeTools.Editor().hotGizmoAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float axisT = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryEditorAxisRayParameter( gizmoContext,
                                            m_runtimeTools.Editor().hotGizmoAxis,
                                            rayOrigin,
                                            rayDirection,
                                            axisT ) )
            {
                EnterInteractiveSceneRun();
                SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                   m_runtimeTools.Editor().selectedModelIndex,
                                                   m_runtimeTools.Editor().hotGizmoAxis,
                                                   false ) )
                {
                    return consumedWorldClick;
                }
                m_runtimeTools.Editor().gizmoDragActive = true;
                m_runtimeTools.Editor().gizmoDragIsRotation = false;
                m_runtimeTools.Editor().gizmoDragIsScale = false;
                m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotGizmoAxis;
                m_runtimeTools.Editor().gizmoDragStartAxisT = axisT;
                const std::vector<GameModel>& models = m_cGameModelCollection.Models();
                float selectionRadius = 1.0f;
                TryGetEditorSelectionFrame( models,
                                            m_runtimeTools.Editor().selectedModelIndex,
                                            m_runtimeTools.Editor().gizmoDragStartPosition,
                                            selectionRadius );
                m_runtimeTools.Editor().gizmoDragStartOrientation =
                    models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetOrientation();
                CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), models, true );
                consumedWorldClick = true;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoTranslate,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && ( m_runtimeTools.Editor().editorModeEnabled || inspectGizmoActive ) )
        {
            if ( m_runtimeTools.Editor().placementModeEnabled )
            {
                consumedWorldClick = true;
                if ( m_runtimeTools.Editor().placementPreviewVisible )
                {
                    m_runtimeTools.Editor().placementScaleActive = true;
                    m_runtimeTools.Editor().placementScaleWheelSteps = 0;
                    m_runtimeTools.Editor().placementScaleStart =
                        EditorClampPlacementScale( m_runtimeTools.Editor().objectType,
                                                   m_runtimeTools.Editor().placementScale );
                    m_runtimeTools.Editor().placementScale = m_runtimeTools.Editor().placementScaleStart;
                    m_runtimeTools.Editor().placementScaleStartClient = Input::GetClientMouseCoordinates();
                    m_runtimeTools.Editor().placementScaleTerrainPoint = m_runtimeTools.Editor().placementTerrainPoint;
                    m_runtimeTools.Editor().placementScaleRayOrigin = m_runtimeTools.Editor().placementRayOrigin;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorPlacementScale,
                                                       RuntimeInputActionSource::Mouse );
                }
            }
            else
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                RuntimePickResult result;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    RuntimePickRequest request;
                    request.purpose = RuntimePickPurpose::EditorSelection;
                    request.models = &m_cGameModelCollection.Models();
                    request.rayOrigin = rayOrigin;
                    request.rayDirection = rayDirection;
                    RuntimePickService::TryPickModel( request, result );
                }

                if ( result.modelIndex >= 0 )
                {
                    RuntimeInteractionCommand command;
                    command.type = RuntimeInteractionCommandType::SetEditorSelection;
                    command.modelIndex = result.modelIndex;
                    command.selectionScope = inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect
                                                                : RuntimeInteractionSelectionScope::Editor;
                    consumedWorldClick = ExecuteRuntimeInteractionCommand( command );
                }
                else
                {
                    RuntimeInteractionCommand command;
                    command.type = RuntimeInteractionCommandType::SetEditorSelection;
                    command.modelIndex = -1;
                    command.selectionScope = inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect
                                                                : RuntimeInteractionSelectionScope::Editor;
                    consumedWorldClick = ExecuteRuntimeInteractionCommand( command );
                }
            }
        }
    }

    return consumedWorldClick;
}


RunEditorTracer::RunEditorTracer()
{
    m_lineData.reserve( 4096 );
}


void RunEditorTracer::Clear()
{
    m_lineData.clear();
}


void RunEditorTracer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    m_lineData.insert( m_lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}


void RunEditorTracer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLine( a, b, r, g, bl );

    Vector3 dir = b - a;
    const float len = VectorMag( dir );
    if ( len <= TOLERANCE )
    {
        return;
    }
    dir /= len;

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) )
                                         : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
    const float sideLen = VectorMag( side );
    if ( sideLen <= TOLERANCE )
    {
        return;
    }
    side /= sideLen;

    const float head = (std::min)( len * 0.25f, 2.0f );
    const Vector3 base = b - dir * head;
    EmitLine( b, base + side * ( head * 0.45f ), r, g, bl );
    EmitLine( b, base - side * ( head * 0.45f ), r, g, bl );
}


void RunEditorTracer::EmitRing( const Vector3& center, int axis, float radius, float r, float g, float bl )
{
    constexpr int segments = 64;
    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    Vector3 previous = center + basisA * radius;
    for ( int i = 1; i <= segments; ++i )
    {
        const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
        const Vector3 next = center + basisA * ( cosf( theta ) * radius ) + basisB * ( sinf( theta ) * radius );
        EmitLine( previous, next, r, g, bl );
        previous = next;
    }
}


void RunEditorTracer::EmitSphere( const Vector3& center, float radius, float r, float g, float bl )
{
    constexpr int segments = 32;
    for ( int plane = 0; plane < 3; ++plane )
    {
        Vector3 previous;
        for ( int i = 0; i <= segments; ++i )
        {
            const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
            const float c = cosf( theta ) * radius;
            const float s = sinf( theta ) * radius;
            Vector3 next = center;
            if ( plane == 0 )
            {
                next.x += c;
                next.z += s;
            }
            else if ( plane == 1 )
            {
                next.x += c;
                next.y += s;
            }
            else
            {
                next.y += c;
                next.z += s;
            }

            if ( i > 0 )
            {
                EmitLine( previous, next, r, g, bl );
            }
            previous = next;
        }
    }
}


void RunEditorTracer::EmitBox( const Vector3& center,
                               const Vector3& xAxis,
                               const Vector3& yAxis,
                               const Vector3& zAxis,
                               float r,
                               float g,
                               float bl )
{
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis,
        center + xAxis - yAxis - zAxis,
        center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis,
        center - xAxis - yAxis + zAxis,
        center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis,
        center - xAxis + yAxis + zAxis,
    };

    static constexpr int kEdges[12][2] = {
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 },
    };
    for ( const auto& edge : kEdges )
    {
        EmitLine( corners[edge[0]], corners[edge[1]], r, g, bl );
    }
}


void RunEditorTracer::AddPlacementRay( const Vector3& rayOrigin, const Vector3& hitPoint )
{
    EmitLine( rayOrigin, hitPoint, 0.25f, 0.80f, 1.0f );
}


void RunEditorTracer::AddPlacementGhost( int objectType,
                                         const Vector3& center,
                                         const Vector3& terrainPoint,
                                         const Vector3& placementScale,
                                         const Quaternion& orientation )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    Quaternion orientationCopy = orientation;
    const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
    constexpr float ghostR = 0.25f;
    constexpr float ghostG = 1.0f;
    constexpr float ghostB = 0.85f;

    if ( const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < tree->partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = tree->parts[partIndex];
            const ConvexHullShape* hull = CachedEditorHullForAsset( part.hullAsset );
            if ( !hull )
            {
                continue;
            }
            const Vector3 hullCenter = base + rotation * ( Vector3( part.offsetX, part.offsetY, part.offsetZ ) +
                                                           HullAuthoredLocalOffset( *hull ) );
            for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
            {
                const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                EmitLine( hullCenter + rotation * hull->GetVertex( edge.vertexA ),
                          hullCenter + rotation * hull->GetVertex( edge.vertexB ),
                          ghostR,
                          ghostG,
                          ghostB );
            }
        }
        return;
    }
    if ( EditorBuildingDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        ForEachEditorBuildingPart(
            type,
            [&]( const Json& part )
            {
                const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                const ConvexHullShape* hull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
                if ( !hull )
                {
                    return;
                }
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( orientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 bodyCenter = base + rotation * offset + partRotation * hull->GetAuthoredCenterOfMass();
                const Vector3 hullCenter = bodyCenter + partRotation * hull->GetPosition();
                for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
                {
                    const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                    EmitLine( hullCenter + partRotation * hull->GetVertex( edge.vertexA ),
                              hullCenter + partRotation * hull->GetVertex( edge.vertexB ),
                              ghostR,
                              ghostG,
                              ghostB );
                }
            } );
        return;
    }
    if ( const EditorHouseDefinition* house = EditorHouseDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < house->partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = house->parts[partIndex];
            const Vector3 partCenter = base + rotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            EmitBox( partCenter,
                     rotation * Vector3( part.halfX, 0.0f, 0.0f ),
                     rotation * Vector3( 0.0f, part.halfY, 0.0f ),
                     rotation * Vector3( 0.0f, 0.0f, part.halfZ ),
                     ghostR,
                     ghostG,
                     ghostB );
        }
        return;
    }

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        EmitBox( center,
                 rotation * Vector3( scale.x, 0.0f, 0.0f ),
                 rotation * Vector3( 0.0f, scale.y, 0.0f ),
                 rotation * Vector3( 0.0f, 0.0f, scale.z ),
                 ghostR,
                 ghostG,
                 ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL:
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        Ragdoll::AddPreviewLines( m_lineData, terrainPoint, scale.x, orientation, ghostR, ghostG, ghostB );
        break;
    default:
    {
        ConvexHullShape hull;
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return;
        }
        const Vector3 hullCenter = center + rotation * hull.GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
            EmitLine( hullCenter + rotation * hull.GetVertex( edge.vertexA ),
                      hullCenter + rotation * hull.GetVertex( edge.vertexB ),
                      ghostR,
                      ghostG,
                      ghostB );
        }
        break;
    }
    }
}


void RunEditorTracer::AddRayCastTestLine( const Vector3& start, const Vector3& end, float alpha, bool hit )
{
    alpha = std::clamp( alpha, 0.0f, 1.0f );
    if ( alpha <= 0.0f )
    {
        return;
    }

    const float r = hit ? 1.0f : 0.35f;
    const float g = hit ? 0.34f : 0.72f;
    const float b = hit ? 0.12f : 1.0f;
    EmitLine( start, end, r * alpha, g * alpha, b * alpha );
}

void RunEditorTracer::AddReplayPathSegment( const Vector3& start, const Vector3& end, float r, float g, float b )
{
    EmitLine( start, end, r, g, b );
}


void RunEditorTracer::AddReplayContactMarker( const Vector3& point, const Vector3& normal, float r, float g, float b )
{
    constexpr float crossSize = 0.55f;
    EmitLine( point - Vector3( crossSize, 0.0f, 0.0f ), point + Vector3( crossSize, 0.0f, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, crossSize, 0.0f ), point + Vector3( 0.0f, crossSize, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, 0.0f, crossSize ), point + Vector3( 0.0f, 0.0f, crossSize ), r, g, b );
    if ( VectorMagSquared( normal ) > TOLERANCE * TOLERANCE )
    {
        EmitArrow( point, point + normal * 1.8f, r, g, b );
    }
}


void RunEditorTracer::AddReplayImpulseVector( const Vector3& point, const Vector3& impulse, float r, float g, float b )
{
    const float magSq = VectorMagSquared( impulse );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    Vector3 direction = impulse;
    const float magnitude = sqrtf( magSq );
    direction /= magnitude;
    const float length = std::clamp( sqrtf( magnitude ) * 3.0f, 1.8f, 12.0f );
    EmitArrow( point, point + direction * length, r, g, b );
}


void RunEditorTracer::AddReplayFutureTargetMarker( const Vector3& center, float radius, int depth )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.34f );
    const float r = std::clamp( 0.98f - depthFade * 0.55f, 0.52f, 1.0f );
    const float g = std::clamp( 0.72f - depthFade * 0.22f, 0.42f, 0.82f );
    const float b = std::clamp( 0.22f - depthFade * 0.12f, 0.10f, 0.32f );
    radius = (std::max)( 0.75f, radius );
    EmitRing( center, 1, radius, r, g, b );
    EmitRing( center, 0, radius * 0.72f, r * 0.84f, g * 0.88f, b );
}


void RunEditorTracer::AddReplayTargetMarker( const GameModel& model )
{
    AddSelectionOutline( model );
    EmitRing( model.GetPosition(), 1, (std::max)( 1.0f, EditorModelRadius( model ) * 1.18f ), 1.0f, 1.0f, 1.0f );
}


void RunEditorTracer::AddAttachedCameraTargetMarker( const GameModel& model, bool activeFollow )
{
    AddSelectionOutline( model );
    const float radius = (std::max)( 1.0f, EditorModelRadius( model ) * 1.24f );
    const float r = activeFollow ? 0.16f : 1.0f;
    const float g = activeFollow ? 1.0f : 0.72f;
    const float b = activeFollow ? 0.92f : 0.24f;
    EmitRing( model.GetPosition(), 1, radius, r, g, b );
    EmitRing( model.GetPosition(), 0, radius * 0.68f, r, g, b );
}


void RunEditorTracer::AddSelectionOutline( const GameModel& model )
{
    Quaternion orientation = model.GetOrientation();
    const RotationMatrix rot = orientation.GetOrientationMatrix();
    constexpr float outlineR = 1.0f;
    constexpr float outlineG = 1.0f;
    constexpr float outlineB = 0.55f;

    const CollisionShape& shape = model.GetCollisionShape();
    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        EmitSphere( model.GetPosition() + rot * sphere->GetPosition(),
                    sphere->GetBoundingRadius(),
                    outlineR,
                    outlineG,
                    outlineB );
        return;
    }
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = model.GetPosition() + rot * box->GetPosition();
        EmitBox( center,
                 rot * Vector3( he.x, 0.0f, 0.0f ),
                 rot * Vector3( 0.0f, he.y, 0.0f ),
                 rot * Vector3( 0.0f, 0.0f, he.z ),
                 outlineR,
                 outlineG,
                 outlineB );
        return;
    }
    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = model.GetPosition() + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLine( hullCenter + rot * hull->GetVertex( edge.vertexA ),
                      hullCenter + rot * hull->GetVertex( edge.vertexB ),
                      outlineR,
                      outlineG,
                      outlineB );
        }
    }
}


void RunEditorTracer::AddGizmo( const Vector3& origin,
                                float radius,
                                int hotTranslateAxis,
                                int hotRotationAxis,
                                int activeAxis,
                                bool activeRotation,
                                bool scaleMode,
                                bool activeScale )
{
    const float length = EditorGizmoAxisLength( radius );
    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;
        if ( ( activeScale || ( !scaleMode && !activeRotation ) ) && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotTranslateAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }

        const Vector3 axisVector = EditorAxisVector( axis );
        const Vector3 endpoint = origin + axisVector * length;
        if ( scaleMode || activeScale )
        {
            const float handle = (std::max)( 0.75f, length * 0.045f );
            EmitLine( origin, endpoint, r, g, b );
            EmitBox( endpoint,
                     Vector3( handle, 0.0f, 0.0f ),
                     Vector3( 0.0f, handle, 0.0f ),
                     Vector3( 0.0f, 0.0f, handle ),
                     r,
                     g,
                     b );
        }
        else
        {
            EmitArrow( origin, endpoint, r, g, b );
        }
    }

    if ( scaleMode || activeScale )
    {
        return;
    }

    const float ringRadius = EditorGizmoRotationRadius( radius );
    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;
        if ( activeRotation && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotRotationAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }
        EmitRing( origin, axis, ringRadius, r, g, b );
    }
}


void RunEditorTracer::AddReplayVelocityGizmo( const GameModel& model,
                                              int hotLinearAxis,
                                              int hotAngularAxis,
                                              int activeAxis,
                                              bool activeAngular )
{
    AddSelectionOutline( model );

    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const Vector3 linearVelocity = model.GetVelocity();
    const Vector3 angularVelocity = model.GetAngularVelocity();
    const float baseLength = ReplayVelocityLinearBaseLength( radius );

    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( linearVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
        const bool hot = hotLinearAxis == axis;
        const bool active = !activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );

        const float axisT = ReplayVelocityLinearVisualAxisT( radius, component );
        const Vector3 endpoint = origin + axisVector * axisT;
        EmitLine( origin - axisVector * ( baseLength * 0.24f ),
                  origin + axisVector * ( baseLength * 0.24f ),
                  r * 0.34f,
                  g * 0.34f,
                  b * 0.34f );
        EmitArrow( origin, endpoint, r, g, b );
    }

    for ( int axis = 0; axis < 3; ++axis )
    {
        const float component = ReplayVelocityAxisComponent( angularVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
        const bool hot = hotAngularAxis == axis;
        const bool active = activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );
        EmitRing( origin, axis, ReplayVelocityAngularVisualRadius( radius, component ), r, g, b );
    }
}


void RunEditorTracer::Render( const Matrix4& viewProjection )
{
    if ( m_lineData.empty() || !IsGfxReady() )
    {
        return;
    }
    Gfx().DrawLinesColored( m_lineData.data(), static_cast<int>( m_lineData.size() / 6 ), viewProjection.Data() );
}


bool Run::TryBuildMouseWorldRay( Vector3& outOrigin, Vector3& outDirection ) const
{
    if ( !m_systems.window || !m_systems.cameras )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    const int screenW = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.x ) );
    const int screenH = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.y ) );
    if ( mouse.x < 0 || mouse.y < 0 || mouse.x >= screenW || mouse.y >= screenH )
    {
        return false;
    }

    const float ndcX = ( static_cast<float>( mouse.x ) / static_cast<float>( screenW ) ) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ( static_cast<float>( mouse.y ) / static_cast<float>( screenH ) ) * 2.0f;

    const Vector3 eye = m_systems.cameras->GetCameraTranslation();
    const Vector3 view = m_systems.cameras->GetCameraView();
    const Vector3 up = m_systems.cameras->GetCameraUp();
    const Matrix4 viewMatrix = Matrix4::LookAt( eye, view, up );
    const Matrix4 inverseViewProjection = ( m_systems.window->GetProjectionMatrix() * viewMatrix ).Inverse();

    Vector3 rayNear;
    Vector3 rayFar;
    if ( !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 0.0f, rayNear ) ||
         !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 1.0f, rayFar ) )
    {
        return false;
    }

    Vector3 rayDirection = rayFar - rayNear;
    const float dirLenSq = VectorMagSquared( rayDirection );
    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    outOrigin = rayNear;
    outDirection = rayDirection * ( 1.0f / sqrtf( dirLenSq ) );
    return true;
}


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
bool TryGetEditorTerrainPlacement( Geometry::Terrain* terrain,
                                   const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   EditorTerrainPlacement& outPlacement )
{
    if ( !terrain )
    {
        return false;
    }

    outPlacement.rayOrigin = rayOrigin;
    outPlacement.rayDirection = rayDirection;
    constexpr float MAX_RAY_DISTANCE = 5000.0f;
    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = MAX_RAY_DISTANCE * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Vector3 sample = rayOrigin + rayDirection * t;
        if ( !terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outPlacement.position = Vector3( sample.x, terrainY, sample.z );
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;
            Vector3 hit = sample;
            float hitY = terrainY;
            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Vector3 mid = rayOrigin + rayDirection * midT;
                if ( !terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;
                hit = mid;
                hitY = midTerrainY;
                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }
            outPlacement.position = Vector3( hit.x, hitY, hit.z );
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}


bool TryComputeEditorObjectCenter( int objectType,
                                   const Vector3& terrainPoint,
                                   const Vector3& placementScale,
                                   const Quaternion& orientation,
                                   Vector3& outCenter )
{
    const int type = std::clamp( objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    Quaternion orientationCopy = orientation;
    const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        outCenter = terrainPoint + rotation * Vector3( 0.0f, scale.y + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        return true;
    case UI::EditorTab::OBJECT_BALL:
    case UI::EditorTab::OBJECT_SPHERE:
        outCenter =
            Vector3( terrainPoint.x, terrainPoint.y + scale.x + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        outCenter = Ragdoll::DefaultPreviewCenter( terrainPoint, scale.x, orientation );
        return true;
    case UI::EditorTab::OBJECT_TREE_SMALL:
    case UI::EditorTab::OBJECT_TREE_BIG:
    case UI::EditorTab::OBJECT_TREE_CEDAR:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
    case UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
    case UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
    case UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
    case UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
    case UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
    case UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
    {
        const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
        Vector3 minV;
        Vector3 maxV;
        if ( !tree || !TryComputeEditorTreeWorldBounds( *tree, terrainPoint, rotation, minV, maxV ) )
        {
            return false;
        }
        outCenter = ( minV + maxV ) * 0.5f;
        return true;
    }
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
    {
        Vector3 minV;
        Vector3 maxV;
        if ( !TryComputeEditorBuildingWorldBounds( type, terrainPoint, orientation, minV, maxV ) )
        {
            return false;
        }
        outCenter = ( minV + maxV ) * 0.5f;
        return true;
    }
    default:
    {
        ConvexHullShape hull;
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return false;
        }
        const Vector3 authoredOrigin =
            terrainPoint +
            rotation * Vector3( 0.0f, HullAuthoredBottomOffset( hull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        outCenter = authoredOrigin + rotation * hull.GetAuthoredCenterOfMass();
        return true;
    }
    }
}


bool TryUpdateEditorPlacementPreview( EditorPlacementPreviewContext context,
                                      int objectType,
                                      const EditorTerrainPlacement* mousePlacement )
{
    Vector3 terrainPoint;
    Vector3 rayOrigin;
    bool terrainAlreadyIncludesAltitude = false;
    if ( context.editor.placementScaleActive )
    {
        terrainPoint = context.editor.placementScaleTerrainPoint;
        rayOrigin = context.editor.placementScaleRayOrigin;
        terrainAlreadyIncludesAltitude = true;
    }
    else
    {
        if ( !mousePlacement )
        {
            return false;
        }
        terrainPoint = mousePlacement->position;
        rayOrigin = mousePlacement->rayOrigin;

        if ( context.terrain && EDITOR_PLACEMENT_SNAP > 0.0f )
        {
            const float snappedX = roundf( terrainPoint.x / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            const float snappedZ = roundf( terrainPoint.z / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            if ( context.terrain->IsInBounds( snappedX, snappedZ ) )
            {
                terrainPoint.x = snappedX;
                terrainPoint.z = snappedZ;
                terrainPoint.y = context.terrain->GetTerrainHeightAt( snappedX, snappedZ );
            }
        }
    }

    if ( !terrainAlreadyIncludesAltitude )
    {
        terrainPoint.y += static_cast<float>( context.editor.placementAltitudeSteps ) *
                          EditorPlacementAltitudeStepSize( objectType, context.editor.placementScale );
    }

    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        context.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const Quaternion placementOrientation = EditorPlacementOrientation( objectType,
                                                                        terrainNormal,
                                                                        context.editor.autoTerrainAlign,
                                                                        context.editor.placementYawRadians );

    Vector3 center;
    if ( !TryComputeEditorObjectCenter( objectType,
                                        terrainPoint,
                                        context.editor.placementScale,
                                        placementOrientation,
                                        center ) )
    {
        return false;
    }

    context.editor.placementTerrainPoint = terrainPoint;
    context.editor.placementCenter = center;
    context.editor.placementOrientation = placementOrientation;
    context.editor.placementRayOrigin = rayOrigin;
    context.editor.placementRayHit = terrainPoint;
    return true;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore


void Run::CancelMousePickup()
{
    if ( m_runtimeTools.MousePickup().mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
    }
    if ( m_interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        m_interaction.EndGesture( InteractionExitReason::EndGesture );
    }
    m_runtimeTools.MousePickup() = RunMousePickupState{};
}


bool Run::TickMousePickupInput( HWND hwnd, const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    if ( !IsManipulatorCameraMode() || m_runtimeTools.Editor().editorModeEnabled || ReplayInspectionActive() )
    {
        CancelMousePickup();
        return false;
    }

    const auto UpdatePickupTarget = [&]() -> bool
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
        {
            return false;
        }

        const float denom = rayDirection * m_runtimeTools.MousePickup().planeNormal;
        if ( fabsf( denom ) <= 1.0e-5f )
        {
            return false;
        }

        const float planeT =
            ( ( m_runtimeTools.MousePickup().planePoint - rayOrigin ) * m_runtimeTools.MousePickup().planeNormal ) /
            denom;
        if ( planeT < 0.0f )
        {
            return false;
        }

        m_runtimeTools.MousePickup().targetPoint = rayOrigin + rayDirection * planeT;
        return true;
    };

    if ( m_runtimeTools.MousePickup().active )
    {
        if ( mouseEdges.leftReleased || !mouseEdges.leftDown )
        {
            CancelMousePickup();
            return true;
        }
        UpdatePickupTarget();
        return true;
    }

    if ( !mouseEdges.leftPressed )
    {
        return false;
    }
    if ( suppressWorldActionThisFrame || m_UI.WantsNativeMouseCursor() )
    {
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        return true;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ManipulatorPickup;
    request.models = &m_cGameModelCollection.Models();
    request.rayOrigin = rayOrigin;
    request.rayDirection = rayDirection;

    RuntimePickResult result;
    if ( !RuntimePickService::TryPickModel( request, result ) )
    {
        return true;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int pickedIndex = result.modelIndex;
    if ( pickedIndex < 0 || pickedIndex >= static_cast<int>( models.size() ) )
    {
        return true;
    }

    const GameModel& picked = models[static_cast<size_t>( pickedIndex )];
    Vector3 cameraNormal = m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
    const float normalLenSq = VectorMagSquared( cameraNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return true;
    }
    cameraNormal *= 1.0f / sqrtf( normalLenSq );

    const Vector3 grabPoint = rayOrigin + rayDirection * result.rayT;
    m_runtimeTools.MousePickup().active = true;
    m_runtimeTools.MousePickup().mouseCaptured = true;
    m_runtimeTools.MousePickup().modelIndex = pickedIndex;
    m_runtimeTools.MousePickup().planePoint = grabPoint;
    m_runtimeTools.MousePickup().planeNormal = cameraNormal;
    m_runtimeTools.MousePickup().grabOffset = grabPoint - picked.GetPosition();
    m_runtimeTools.MousePickup().targetPoint = grabPoint;
    m_runtimeTools.MousePickup().preservedAngularVelocity = picked.GetAngularVelocity();
    m_runtimeTools.MousePickup().lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    UI::InputControl::BeginMouseCapture( hwnd );
    const POINT mouse = Input::GetClientMouseCoordinates();
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = mouse.x;
    gesture.startY = mouse.y;
    gesture.modelIndex = pickedIndex;
    m_interaction.BeginGesture( gesture,
                                RuntimePointerCaptureOwner::ToolGesture,
                                InteractionExitReason::EnterManipulator );
    EnterInteractiveSceneRun();
    UpdatePickupTarget();
    return true;
}


void Run::ApplyMousePickupPhysicsStep()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    if ( m_runtimeTools.MousePickup().modelIndex < 0 ||
         m_runtimeTools.MousePickup().modelIndex >= static_cast<int>( models.size() ) )
    {
        CancelMousePickup();
        return;
    }

    GameModel& model = models[static_cast<size_t>( m_runtimeTools.MousePickup().modelIndex )];
    if ( model.IsFixed() )
    {
        CancelMousePickup();
        return;
    }
    model.SetAngularVelocity( m_runtimeTools.MousePickup().preservedAngularVelocity );

    const Vector3 grabPoint = model.GetPosition() + m_runtimeTools.MousePickup().grabOffset;
    const Vector3 pull = m_runtimeTools.MousePickup().targetPoint - grabPoint;
    const float pullLenSq = VectorMagSquared( pull );
    if ( pullLenSq <= MOUSE_PICKUP_DEAD_ZONE * MOUSE_PICKUP_DEAD_ZONE )
    {
        m_runtimeTools.MousePickup().lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        return;
    }

    Vector3 impulse = pull * MOUSE_PICKUP_STIFFNESS - model.GetVelocity() * MOUSE_PICKUP_DAMPING;
    const float impulseLenSq = VectorMagSquared( impulse );
    if ( impulseLenSq > MOUSE_PICKUP_MAX_IMPULSE * MOUSE_PICKUP_MAX_IMPULSE )
    {
        impulse *= MOUSE_PICKUP_MAX_IMPULSE / sqrtf( impulseLenSq );
    }

    m_cGameModelCollection.GetPhysicsEngine().ApplyBodyImpulse( m_cGameModelCollection,
                                                                m_runtimeTools.MousePickup().modelIndex,
                                                                impulse,
                                                                SkullbonezCore::Math::Vector::ZERO_VECTOR );
    m_cGameModelCollection.InvalidatePhysicsStreams();
    m_runtimeTools.MousePickup().lastImpulse = impulse;
}


void Run::RestoreMousePickupAngularVelocity()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    if ( m_runtimeTools.MousePickup().modelIndex < 0 ||
         m_runtimeTools.MousePickup().modelIndex >= static_cast<int>( models.size() ) )
    {
        CancelMousePickup();
        return;
    }

    GameModel& model = models[static_cast<size_t>( m_runtimeTools.MousePickup().modelIndex )];
    if ( model.IsFixed() )
    {
        CancelMousePickup();
        return;
    }

    model.SetAngularVelocity( m_runtimeTools.MousePickup().preservedAngularVelocity );
    m_cGameModelCollection.InvalidatePhysicsStreams();
}


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
int HitEditorGizmoAxis( EditorGizmoContext context, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( context.editor.selectedModelIndex < 0 || context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = context.models.Models();
    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( models, context.editor.selectedModelIndex, origin, radius ) )
    {
        return -1;
    }
    const float length = EditorGizmoAxisLength( radius );
    const float threshold = (std::max)( 1.25f, length * 0.06f );
    const float thresholdSq = threshold * threshold;

    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float distanceSq =
            DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, origin + axisVector * length );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int HitEditorRotationGizmoAxis( EditorGizmoContext context, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( context.editor.selectedModelIndex < 0 || context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = context.models.Models();
    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( models, context.editor.selectedModelIndex, origin, radius ) )
    {
        return -1;
    }
    const float ringRadius = EditorGizmoRotationRadius( radius );
    const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );

    int bestAxis = -1;
    float bestDiff = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = normal * rayDirection;
        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
        if ( rayT < 0.0f )
        {
            continue;
        }

        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( radial * normal ) );
        const float diff = fabsf( radialDistance - ringRadius );
        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool TryEditorAxisRayParameter( EditorGizmoContext context,
                                int axis,
                                const Vector3& rayOrigin,
                                const Vector3& rayDirection,
                                float& outAxisT )
{
    if ( axis < 0 || axis > 2 || context.editor.selectedModelIndex < 0 ||
         context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return false;
    }

    Vector3 axisOrigin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( context.models.Models(), context.editor.selectedModelIndex, axisOrigin, radius ) )
    {
        return false;
    }
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


bool TryEditorRotationRayAngle( EditorGizmoContext context,
                                int axis,
                                const Vector3& rayOrigin,
                                const Vector3& rayDirection,
                                float& outAngle )
{
    if ( axis < 0 || axis > 2 || context.editor.selectedModelIndex < 0 ||
         context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return false;
    }

    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( context.models.Models(), context.editor.selectedModelIndex, origin, radius ) )
    {
        return false;
    }
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = normal * rayDirection;
    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( radial * normal );
    const float radialLenSq = radial * radial;
    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( radial * basisB, radial * basisA );
    return true;
}


void MoveSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                        const Vector3& rayOrigin,
                                        const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( context, context.editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    GameModel& model = context.models.GetModelAtIndex( index );
    const Vector3 axisVector = EditorAxisVector( context.editor.activeGizmoAxis );
    const Vector3 delta = axisVector * ( axisT - context.editor.gizmoDragStartAxisT );
    const int groupCount = ValidCapturedEditorGizmoGroupCount( context.editor, context.models.GetModelCount() );
    if ( groupCount > 0 )
    {
        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const int modelIndex = context.editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
            GameModel& groupModel = context.models.GetModelAtIndex( modelIndex );
            groupModel.SetPosition(
                context.editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] + delta );
            ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), modelIndex, groupModel );
        }
    }
    else
    {
        const Vector3 newPosition = context.editor.gizmoDragStartPosition + delta;
        model.SetPosition( newPosition );
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model );
    }
}


void ScaleSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                         const Vector3& rayOrigin,
                                         const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || !context.editor.gizmoDragIsScale || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( context, context.editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    const float startExtent =
        EditorShapeAxisExtent( context.editor.gizmoDragStartShape, context.editor.activeGizmoAxis );
    const float targetExtent = (std::max)( 0.25f, startExtent + axisT - context.editor.gizmoDragStartAxisT );
    const float factor = targetExtent / startExtent;

    GameModel& model = context.models.GetModelAtIndex( index );
    if ( model.ScaleCollisionShapeAxisFromBase( context.editor.gizmoDragStartShape,
                                                context.editor.activeGizmoAxis,
                                                factor ) )
    {
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model );
    }
}


void RotateSelectedEditorObjectAroundAxis( EditorGizmoContext context,
                                           const Vector3& rayOrigin,
                                           const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || !context.editor.gizmoDragIsRotation || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float currentAngle = 0.0f;
    if ( !TryEditorRotationRayAngle( context, context.editor.activeGizmoAxis, rayOrigin, rayDirection, currentAngle ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    const Vector3 axisVector = EditorAxisVector( context.editor.activeGizmoAxis );
    const float angleDelta = WrapEditorAngleDelta( currentAngle - context.editor.gizmoDragStartRotationAngle );
    const int groupCount = ValidCapturedEditorGizmoGroupCount( context.editor, context.models.GetModelCount() );
    if ( groupCount > 0 )
    {
        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const int modelIndex = context.editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
            GameModel& groupModel = context.models.GetModelAtIndex( modelIndex );
            const Vector3 startOffset =
                context.editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] -
                context.editor.gizmoDragStartPosition;
            Quaternion orientation =
                context.editor.gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )];
            orientation.RotateAboutAxis( axisVector, angleDelta );
            groupModel.SetPosition( context.editor.gizmoDragStartPosition +
                                    RotatePointAboutArbitrary( angleDelta, axisVector, startOffset ) );
            groupModel.SetOrientation( orientation );
            ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), modelIndex, groupModel );
        }
    }
    else
    {
        Quaternion orientation = context.editor.gizmoDragStartOrientation;
        orientation.RotateAboutAxis( axisVector, angleDelta );
        GameModel& model = context.models.GetModelAtIndex( index );
        model.SetOrientation( orientation );
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model );
    }
}


void UpdateEditorGizmoHotAxes( EditorGizmoContext context,
                               const Vector3& rayOrigin,
                               const Vector3& rayDirection,
                               bool scaleMode )
{
    if ( scaleMode )
    {
        context.editor.hotGizmoAxis = HitEditorGizmoAxis( context, rayOrigin, rayDirection );
        return;
    }

    context.editor.hotRotationAxis = HitEditorRotationGizmoAxis( context, rayOrigin, rayDirection );
    context.editor.hotGizmoAxis =
        context.editor.hotRotationAxis < 0 ? HitEditorGizmoAxis( context, rayOrigin, rayDirection ) : -1;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore


void Run::UpdateEditorInteractionPreview()
{
    m_runtimeTools.Editor().placementPreviewVisible = false;
    m_runtimeTools.Editor().hotGizmoAxis = -1;
    m_runtimeTools.Editor().hotRotationAxis = -1;

    if ( m_UI.BlocksCameraMouse() || m_runtimeTools.Editor().viewportLookActive )
    {
        return;
    }

    const bool inspectGizmoActive = InspectGizmoInteractionActive();
    if ( !m_runtimeTools.Editor().editorModeEnabled && !inspectGizmoActive )
    {
        return;
    }

    if ( m_runtimeTools.Editor().editorModeEnabled && m_runtimeTools.Editor().placementModeEnabled )
    {
        EditorTerrainPlacement terrainPlacement;
        const EditorTerrainPlacement* terrainPlacementForPreview = nullptr;
        if ( !m_runtimeTools.Editor().placementScaleActive )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryGetEditorTerrainPlacement( m_systems.terrain.get(), rayOrigin, rayDirection, terrainPlacement ) )
            {
                terrainPlacementForPreview = &terrainPlacement;
            }
        }
        m_runtimeTools.Editor().placementPreviewVisible =
            TryUpdateEditorPlacementPreview( { m_runtimeTools.Editor(), m_systems.terrain.get() },
                                             m_runtimeTools.Editor().objectType,
                                             terrainPlacementForPreview );
    }

    if ( m_runtimeTools.Editor().selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.modelIndex = -1;
        command.selectionScope =
            inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect : RuntimeInteractionSelectionScope::Editor;
        command.claimSelectionOwner = false;
        ExecuteRuntimeInteractionCommand( command );
        CancelEditorGizmoDragState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
    }

    if ( m_runtimeTools.Editor().selectedModelIndex >= 0 && !m_runtimeTools.Editor().gizmoDragActive &&
         !m_runtimeTools.Editor().placementModeEnabled )
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
        {
            UpdateEditorGizmoHotAxes( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                                      rayOrigin,
                                      rayDirection,
                                      Input::IsKeyDown( VK_CONTROL ) );
        }
    }
}


void Run::RenderEditorOverlay( const Matrix4& viewProjection, const Vector3& cameraEye, const Vector3& cameraUp )
{
    m_runtimeTools.EditorTracer().Clear();
    const float rayLinger = (std::max)( 0.0f, m_debug.physicsDebugContactLinger );
    if ( rayLinger > 0.0f )
    {
        for ( const RunRayCastTestLine& line : m_runtimeTools.RayCastTest().lines )
        {
            if ( line.active && line.ageSeconds < rayLinger )
            {
                m_runtimeTools.EditorTracer().AddRayCastTestLine( line.start,
                                                                  line.end,
                                                                  1.0f - line.ageSeconds / rayLinger,
                                                                  line.hit );
            }
        }
    }

    if ( m_runtimeTools.Editor().editorModeEnabled && m_runtimeTools.Editor().placementModeEnabled &&
         m_runtimeTools.Editor().placementPreviewVisible )
    {
        m_runtimeTools.EditorTracer().AddPlacementRay( m_runtimeTools.Editor().placementRayOrigin,
                                                       m_runtimeTools.Editor().placementRayHit );
        m_runtimeTools.EditorTracer().AddPlacementGhost( m_runtimeTools.Editor().objectType,
                                                         m_runtimeTools.Editor().placementCenter,
                                                         m_runtimeTools.Editor().placementTerrainPoint,
                                                         m_runtimeTools.Editor().placementScale,
                                                         m_runtimeTools.Editor().placementOrientation );
    }

    if ( ( m_runtimeTools.Editor().editorModeEnabled || InspectGizmoInteractionActive() ) &&
         !m_runtimeTools.Editor().placementModeEnabled && m_runtimeTools.Editor().selectedModelIndex >= 0 &&
         m_runtimeTools.Editor().selectedModelIndex < m_cGameModelCollection.GetModelCount() )
    {
        const std::vector<GameModel>& models = m_cGameModelCollection.Models();
        Vector3 gizmoOrigin;
        float radius = 1.0f;
        EditorGizmoGroupIndices groupIndices = {};
        int groupCount = 0;
        const bool scaleMode = m_runtimeTools.Editor().gizmoDragIsScale || Input::IsKeyDown( VK_CONTROL );
        if ( TryGetEditorSelectionFrame( models,
                                         m_runtimeTools.Editor().selectedModelIndex,
                                         gizmoOrigin,
                                         radius,
                                         &groupIndices,
                                         &groupCount ) )
        {
            for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
            {
                m_runtimeTools.EditorTracer().AddSelectionOutline(
                    models[static_cast<std::size_t>( groupIndices[static_cast<std::size_t>( groupIndex )] )] );
            }
            m_runtimeTools.EditorTracer().AddGizmo( gizmoOrigin,
                                                    radius,
                                                    m_runtimeTools.Editor().hotGizmoAxis,
                                                    m_runtimeTools.Editor().hotRotationAxis,
                                                    m_runtimeTools.Editor().activeGizmoAxis,
                                                    m_runtimeTools.Editor().gizmoDragIsRotation,
                                                    scaleMode,
                                                    m_runtimeTools.Editor().gizmoDragIsScale );
        }
    }
    if ( m_runtimeTools.MousePickup().active && m_runtimeTools.MousePickup().modelIndex >= 0 &&
         m_runtimeTools.MousePickup().modelIndex < m_cGameModelCollection.GetModelCount() )
    {
        const GameModel& grabbed =
            m_cGameModelCollection.Models()[static_cast<size_t>( m_runtimeTools.MousePickup().modelIndex )];
        const Vector3 grabPoint = grabbed.GetPosition() + m_runtimeTools.MousePickup().grabOffset;
        m_runtimeTools.EditorTracer().AddSelectionOutline( grabbed );
        m_runtimeTools.EditorTracer().AddReplayPathSegment( grabPoint,
                                                            m_runtimeTools.MousePickup().targetPoint,
                                                            0.1f,
                                                            0.95f,
                                                            1.0f );
        m_runtimeTools.EditorTracer().AddReplayContactMarker( m_runtimeTools.MousePickup().targetPoint,
                                                              m_runtimeTools.MousePickup().planeNormal,
                                                              0.1f,
                                                              0.95f,
                                                              1.0f );
        m_runtimeTools.EditorTracer().AddReplayImpulseVector( grabPoint,
                                                              m_runtimeTools.MousePickup().lastImpulse,
                                                              0.1f,
                                                              0.95f,
                                                              1.0f );
    }
    if ( IsAttachedCameraMode() )
    {
        int targetIndex = -1;
        if ( TryResolveAttachedCameraTarget( targetIndex ) && targetIndex >= 0 &&
             targetIndex < m_cGameModelCollection.GetModelCount() )
        {
            const GameModel& target = m_cGameModelCollection.Models()[static_cast<size_t>( targetIndex )];
            m_runtimeTools.EditorTracer().AddAttachedCameraTargetMarker( target, m_attachedCamera.activeFollow );
        }
    }
    RenderReplayPathVisualizer( m_runtimeTools.EditorTracer() );
    RenderReplayCauseFocusOverlay( m_runtimeTools.EditorTracer() );
    RenderReplayVelocityEditOverlay( m_runtimeTools.EditorTracer() );
    m_runtimeTools.EditorTracer().Render( viewProjection );
    m_runtimeTools.Laser().Render( viewProjection, cameraEye, cameraUp );
}


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
static bool TryResolveEditorObjectPlacementPreflight( EditorObjectPlacementContext context,
                                                      EditorObjectPlacementRequest request,
                                                      int& outType,
                                                      bool reportErrors )
{
    const int modelCount = context.models.GetModelCount();
    const int type = std::clamp( request.objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const int buildingPartCount = building ? EditorBuildingPartCount( type ) : 0;
    const bool isRagdollType = type == UI::EditorTab::OBJECT_RAGDOLL || type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    if ( building && buildingPartCount <= 0 )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s is missing or empty.\n", building->assetName );
        }
        return false;
    }
    const int requiredModelCount =
        isRagdollType
            ? Ragdoll::SIMPLE_PART_COUNT
            : ( building ? buildingPartCount : ( house ? house->partCount : ( tree ? tree->partCount : 1 ) ) );
    if ( modelCount + requiredModelCount > context.activeModelCapacity )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place object: model capacity reached.\n" );
        }
        return false;
    }
    outType = type;
    return true;
}


bool CanPlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context, EditorObjectPlacementRequest request )
{
    int type = 0;
    return TryResolveEditorObjectPlacementPreflight( context, request, type, true );
}


bool PlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context,
                                      EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult )
{
    int type = 0;
    if ( !TryResolveEditorObjectPlacementPreflight( context, request, type, false ) )
    {
        outResult = EditorObjectPlacementResult{};
        return false;
    }

    const int modelCount = context.models.GetModelCount();
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const Vector3& terrainPoint = request.terrainPoint;
    const bool fixedObject = request.fixedObject;
    const Vector3 placementScale = EditorClampPlacementScale( type, context.editor.placementScale );
    const int serial = context.editor.placedObjectSerial++;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        context.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const bool alignToTerrain = EditorObjectAlignsToTerrainNormal( type, context.editor.autoTerrainAlign );
    const Quaternion placementOrientation = EditorPlacementOrientation( type,
                                                                        terrainNormal,
                                                                        context.editor.autoTerrainAlign,
                                                                        context.editor.placementYawRadians );
    Quaternion placementOrientationCopy = placementOrientation;
    const RotationMatrix placementRotation = placementOrientationCopy.GetOrientationMatrix();
    const bool placementFixed = tree && tree->forceFixed ? true : fixedObject;
    const bool ragdollStartsAsleep = type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    const char* modePrefix = placementFixed ? "static"
                                            : ( ( tree && tree->seedAsleep ) || ( house && house->seedAsleep ) ||
                                                        building || ragdollStartsAsleep
                                                    ? "sleeping"
                                                    : "dynamic" );

    auto addModel = [&]( GameModel model, bool modelFixed, bool modelStartsAsleep = false )
    {
        model.SetFixed( modelFixed );
        const int index = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        if ( !modelFixed )
        {
            if ( modelStartsAsleep )
            {
                context.models.GetPhysicsEngine().SeedBodyAsleep( context.models, index );
            }
            else
            {
                context.models.GetPhysicsEngine().WakeBody( context.models, index );
            }
        }
    };

    auto addSphere = [&]( const char* label, float radius, float restitution )
    {
        const float mass = CalculateSphereMass( radius );
        const Vector3 inertia = CalculateSphereInertia( radius, mass );
        const Vector3 center( terrainPoint.x,
                              terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON,
                              terrainPoint.z );
        GameModel model( &context.world, center, inertia, mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( restitution );
        model.AddBoundingSphere( radius );
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addBox = [&]()
    {
        const Vector3 halfExtents = placementScale;
        const float mass = CalculateBoxMass( halfExtents );
        Vector3 center;
        if ( !TryComputeEditorObjectCenter( type, terrainPoint, placementScale, placementOrientation, center ) )
        {
            return;
        }
        GameModel model( &context.world, center, CalculateBoxInertiaForHalfExtents( halfExtents, mass ), mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( 0.25f );
        model.AddBoundingBox( halfExtents );
        if ( alignToTerrain )
        {
            model.SetOrientation( placementOrientation );
        }
        ApplyEditorSpawnMaterial( model, fixedObject, true );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_box_%03d", modePrefix, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addHull = [&]( EditorHullAsset asset )
    {
        const char* label = EditorHullAssetToken( asset );
        const char* path = EditorHullAssetPath( asset );
        if ( !path )
        {
            return;
        }
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( path );
        ConvexHullShape scaledHull = hull;
        scaledHull.ScaleAxis( 0, placementScale.x );
        scaledHull.ScaleAxis( 1, placementScale.y );
        scaledHull.ScaleAxis( 2, placementScale.z );
        const float mass = scaledHull.GetDefaultMass();
        const bool alignHull = alignToTerrain;
        const RotationMatrix hullRotation = alignHull ? placementRotation : IDENTITY_MATRIX;
        const Quaternion hullOrientation = alignHull ? placementOrientation : IDENTITY_QUATERNION;
        const Vector3 authoredOrigin =
            terrainPoint +
            hullRotation *
                Vector3( 0.0f, HullAuthoredBottomOffset( scaledHull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const Vector3 center = authoredOrigin + hullRotation * scaledHull.GetAuthoredCenterOfMass();
        GameModel model( &context.world, center, scaledHull.ComputeBoxApproxInertia( mass ), mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( 0.25f );
        model.AddConvexHull( scaledHull );
        model.SetOrientation( hullOrientation );
        SkullbonezCore::Rendering::RenderMaterial rockMaterial;
        if ( TryEditorRockMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else if ( TryEditorRootMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else
        {
            ApplyEditorSpawnMaterial( model, fixedObject, false );
        }
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addTree = [&]( const EditorTreeDefinition& treeDefinition )
    {
        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            if ( !CachedEditorHullForAsset( part.hullAsset ) )
            {
                fprintf( stderr,
                         "[editor] Cannot place tree: missing hull asset %s.\n",
                         EditorHullAssetToken( part.hullAsset ) );
                return;
            }
        }

        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            const ConvexHullShape* sourceHull = CachedEditorHullForAsset( part.hullAsset );
            if ( !sourceHull )
            {
                continue;
            }
            ConvexHullShape hull = *sourceHull;
            const Vector3 localOffset( part.offsetX, part.offsetY, part.offsetZ );
            const Vector3 authoredOrigin =
                terrainPoint +
                placementRotation * ( localOffset + Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f ) );
            const Vector3 center = authoredOrigin + placementRotation * hull.GetAuthoredCenterOfMass();
            const float mass = hull.GetDefaultMass();
            GameModel model( &context.world, center, hull.ComputeBoxApproxInertia( mass ), mass );
            model.SetTerrain( context.terrain );
            model.SetCoefficientRestitution( part.restitution );
            model.AddConvexHull( hull );
            model.SetOrientation( placementOrientation );
            model.SetContactReleaseOnImpact( part.contactReleaseOnImpact, part.contactReleaseImpulseThreshold );
            model.SetRenderMaterial( EditorTreePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, treeDefinition.label, serial, part.suffix );
            model.SetName( name );
            const bool partFixed = treeDefinition.forceFixed || part.startsFixed || placementFixed;
            addModel( std::move( model ), partFixed, treeDefinition.seedAsleep && !partFixed );
        }
    };

    auto addHouse = [&]( const EditorHouseDefinition& houseDefinition )
    {
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < houseDefinition.partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = houseDefinition.parts[partIndex];
            const Vector3 halfExtents( part.halfX, part.halfY, part.halfZ );
            const float mass = CalculateBoxMass( halfExtents );
            const Vector3 center = base + placementRotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            GameModel model( &context.world, center, CalculateBoxInertiaForHalfExtents( halfExtents, mass ), mass );
            model.SetTerrain( context.terrain );
            model.SetCoefficientRestitution( part.restitution );
            model.AddBoundingBox( halfExtents );
            model.SetOrientation( placementOrientation );
            model.SetRenderMaterial( EditorHousePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, houseDefinition.label, serial, part.suffix );
            model.SetName( name );
            addModel( std::move( model ), placementFixed, houseDefinition.seedAsleep && !placementFixed );
        }
    };

    auto addBuilding = [&]( const EditorBuildingDefinition& buildingDefinition )
    {
        bool failed = false;
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const bool ok = ForEachEditorBuildingPart(
            type,
            [&]( const Json& part )
            {
                if ( failed )
                {
                    return;
                }
                const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                const ConvexHullShape* sourceHull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
                if ( !sourceHull )
                {
                    failed = true;
                    return;
                }

                ConvexHullShape hull = *sourceHull;
                const float mass = EditorJsonFloatOr( part, "mass", hull.GetDefaultMass() );
                const float restitution = EditorJsonFloatOr( part, "restitution", 0.08f );
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( placementOrientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 authoredOrigin = base + placementRotation * offset;
                const Vector3 center = authoredOrigin + partRotation * hull.GetAuthoredCenterOfMass();
                GameModel model( &context.world, center, hull.ComputeBoxApproxInertia( mass ), mass );
                model.SetTerrain( context.terrain );
                model.SetCoefficientRestitution( restitution );
                model.SetContactReleaseOnImpact(
                    EditorJsonBoolOr( part, "contactReleaseOnImpact", false ),
                    (std::max)( 0.0f, EditorJsonFloatOr( part, "contactReleaseImpulseThreshold", 1.0f ) ) );
                model.AddConvexHull( hull );
                model.SetOrientation( partOrientation );
                model.SetRenderMaterial( EditorBuildingPartMaterial( part ) );
                if ( const Json* velocity = EditorJsonFindMember( part, "velocity" ) )
                {
                    Vector3 authoredVelocity;
                    if ( TryReadEditorJsonVec3( *velocity, authoredVelocity ) )
                    {
                        model.SetLinearVelocity( authoredVelocity );
                    }
                }

                char name[64];
                const std::string partName = EditorJsonStringOr( part, "name", "part" );
                snprintf( name,
                          sizeof( name ),
                          "%s_%s_%03d_%s",
                          modePrefix,
                          buildingDefinition.label,
                          serial,
                          partName.c_str() );
                name[sizeof( name ) - 1] = '\0';
                model.SetName( name );
                const bool partFixed = placementFixed || EditorJsonBoolOr( part, "fixed", false );
                const bool partSleeping = EditorJsonBoolOr( part, "sleeping", true );
                addModel( std::move( model ), partFixed, partSleeping && !partFixed );
            } );
        if ( failed || !ok )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s.\n", buildingDefinition.assetName );
        }
    };

    auto addRagdoll = [&]()
    {
        RagdollBuildOptions options;
        char prefix[64];
        sprintf_s( prefix, sizeof( prefix ), "%s_ragdoll_%03d", modePrefix, serial );
        options.namePrefix = prefix;
        options.terrainPoint = terrainPoint;
        options.orientation = placementOrientation;
        options.scale = placementScale.x;
        options.fixed = placementFixed;
        options.startsAsleep = ragdollStartsAsleep && !placementFixed;
        Ragdoll::AddSimpleHumanoid( context.models,
                                    context.models.GetPhysicsEngine(),
                                    context.world,
                                    context.terrain,
                                    options );
    };

    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        addBox();
        break;
    case UI::EditorTab::OBJECT_BALL:
        addSphere( "ball", placementScale.x, 0.45f );
        break;
    case UI::EditorTab::OBJECT_SPHERE:
        addSphere( "sphere", placementScale.x, 0.35f );
        break;
    case UI::EditorTab::OBJECT_HULL_WEDGE:
        addHull( EditorHullAsset::WEDGE );
        break;
    case UI::EditorTab::OBJECT_HULL_TRI_PRISM:
        addHull( EditorHullAsset::TRI_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        addHull( EditorHullAsset::TAPERED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_HULL_PYRAMID:
        addHull( EditorHullAsset::PYRAMID );
        break;
    case UI::EditorTab::OBJECT_HULL_HEX_PRISM:
        addHull( EditorHullAsset::HEX_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_DIAMOND:
        addHull( EditorHullAsset::DIAMOND );
        break;
    case UI::EditorTab::OBJECT_ROCK_SLAB:
        addHull( EditorHullAsset::ROCK_SLAB_FLAT );
        break;
    case UI::EditorTab::OBJECT_ROCK_LUMP:
        addHull( EditorHullAsset::ROCK_LUMP_LARGE );
        break;
    case UI::EditorTab::OBJECT_ROCK_SHARD:
        addHull( EditorHullAsset::ROCK_SHARD_TALL );
        break;
    case UI::EditorTab::OBJECT_ROCK_CHIPPED:
        addHull( EditorHullAsset::ROCK_CHIPPED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_ROOT_SMALL:
        addHull( EditorHullAsset::TREE_ROOT_SMALL );
        break;
    case UI::EditorTab::OBJECT_ROOT_LARGE:
        addHull( EditorHullAsset::TREE_ROOT_LARGE );
        break;
    case UI::EditorTab::OBJECT_TREE_SMALL:
        if ( tree )
        {
            addTree( *tree );
        }
        break;
    case UI::EditorTab::OBJECT_TREE_BIG:
    case UI::EditorTab::OBJECT_TREE_CEDAR:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
    case UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
    case UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
    case UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
    case UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
    case UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
    case UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
        if ( tree )
        {
            addTree( *tree );
        }
        break;
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
        if ( building )
        {
            addBuilding( *building );
        }
        break;
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        addRagdoll();
        break;
    default:
        break;
    }

    context.scene.modelCount = context.models.GetModelCount();
    const bool placed = context.scene.modelCount > modelCount;
    outResult.placed = placed;
    outResult.modelCountBefore = modelCount;
    outResult.modelCountAfter = context.scene.modelCount;
    outResult.objectType = type;
    outResult.fixedObject = fixedObject;
    outResult.autoTerrainAlign = context.editor.autoTerrainAlign;
    outResult.terrainPoint = terrainPoint;
    outResult.placementScale = placementScale;
    outResult.placementYawRadians = context.editor.placementYawRadians;
    return placed;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
