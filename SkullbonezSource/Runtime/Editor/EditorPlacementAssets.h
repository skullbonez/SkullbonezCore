/*
File: SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
Purpose:
  Declares editor placeable asset recipes and shared placement-geometry helpers.

Summary:
  Placement preview, tracer ghosts, and placement commit all ask this boundary

  for the same recipe data. The large recipe tables live in the matching .cpp;
  this header exposes only the typed recipe surface and the JSON part visitor
  that must remain template-visible to caller lambdas.

Glossary:
  Placement recipe: Typed editor data that describes a tree, house, building,
    or hull-backed primitive selected from the editor tab.
  Building part visitor: Header-visible template that visits authored JSON
    primitive records without allocating a callback object.
  Authored hull: Baked convex hull asset used for editor-placeable collision
    geometry and preview outlines.

Invariants:
  - Preview, preflight, tracer ghost, and placement commit must all consume the
    same recipe helpers.
  - Asset-library part order is spawn order and must be preserved.
  - The template visitor is the only function body kept in this header.

Related:
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp
  - SkullbonezSource/Runtime/Editor/EditorTracer.cpp
*/
#pragma once

#include "EditorHullAssets.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/RotationMatrix.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Rendering/RenderMaterial.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"

#include <string>

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
namespace Assets
{
class AssetSystem;
}
namespace Runtime
{
using EditorPlacementJson = nlohmann::ordered_json;

inline constexpr float EDITOR_PLACEMENT_SURFACE_EPSILON = 0.02f;
inline constexpr float EDITOR_PLACEMENT_SNAP = 2.0f;

struct EditorTreePartDefinition
{
    Assets::EditorHullAsset hullAsset;
    const char* suffix;
    float offsetX;
    float offsetY;
    float offsetZ;
    float restitution;
    Rendering::RenderMaterialKind materialKind;
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
    Rendering::RenderMaterialKind materialKind;
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


Math::Vector::Vector3 HullAuthoredLocalOffset( const Math::CollisionDetection::ConvexHullShape& hull );
float HullAuthoredBottomOffset( const Math::CollisionDetection::ConvexHullShape& hull );

const EditorPlacementJson* EditorJsonFindMember( const EditorPlacementJson& object, const char* name );
const EditorPlacementJson* CachedEditorBuildingAsset( int objectType, const Assets::AssetSystem& assets );
std::string EditorJsonStringOr( const EditorPlacementJson& object, const char* name, const char* fallback );
float EditorJsonFloatOr( const EditorPlacementJson& object, const char* name, float fallback );
bool EditorJsonBoolOr( const EditorPlacementJson& object, const char* name, bool fallback );
bool TryReadEditorJsonVec3( const EditorPlacementJson& value, Math::Vector::Vector3& out );
Math::Vector::Vector3 EditorJsonVec3Or( const EditorPlacementJson& object, const char* name,
                                        const Math::Vector::Vector3& fallback );
std::string EditorAssetPrimitiveType( const EditorPlacementJson& part );
bool IsEditorAssetPrimitiveType( const std::string& type );
bool TryReadEditorBoxHalfExtents( const EditorPlacementJson& part, Math::Vector::Vector3& outHalfExtents );
bool TryReadEditorSphereRadius( const EditorPlacementJson& part, float& outRadius );
Math::Orientation::Quaternion EditorBuildingPartOrientation( const Math::Orientation::Quaternion& placementOrientation,
                                                             const EditorPlacementJson& part );

const EditorBuildingDefinition* EditorBuildingDefinitionForType( int objectType );
int EditorBuildingPartCount( int objectType, const Assets::AssetSystem& assets );
const Math::CollisionDetection::ConvexHullShape* CachedEditorBuildingHull( Core::SbDiagnosticStore& diagnostics,
                                                                           const std::string& hullPath );
float EditorBuildingVerticalSize( Core::SbDiagnosticStore& diagnostics, int objectType, const Assets::AssetSystem& assets );
bool TryComputeEditorBuildingWorldBounds( Core::SbDiagnosticStore& diagnostics, int objectType,
                                          const Math::Vector::Vector3& terrainPoint,
                                          const Math::Orientation::Quaternion& placementOrientation,
                                          const Assets::AssetSystem& assets, Math::Vector::Vector3& outMin,
                                          Math::Vector::Vector3& outMax );

const EditorTreeDefinition* EditorTreeDefinitionForType( int objectType );
const EditorHouseDefinition* EditorHouseDefinitionForType( int objectType );
bool EditorObjectAlignsToTerrainNormal( int objectType, bool autoTerrainAlign );
Math::Orientation::Quaternion EditorPlacementOrientation( int objectType, Math::Vector::Vector3 terrainNormal,
                                                          bool autoTerrainAlign, float yawRadians );
const Math::CollisionDetection::ConvexHullShape* CachedEditorHullForAsset( Core::SbDiagnosticStore& diagnostics,
                                                                           Assets::EditorHullAsset asset );
bool TryComputeEditorTreeWorldBounds( Core::SbDiagnosticStore& diagnostics, const EditorTreeDefinition& tree,
                                      const Math::Vector::Vector3& terrainPoint,
                                      const Math::Transformation::RotationMatrix& orientation, Math::Vector::Vector3& outMin,
                                      Math::Vector::Vector3& outMax );
bool TryComputeEditorHouseWorldBounds( const EditorHouseDefinition& house, const Math::Vector::Vector3& terrainPoint,
                                       const Math::Transformation::RotationMatrix& orientation,
                                       Math::Vector::Vector3& outMin, Math::Vector::Vector3& outMax );
float EditorTreeVerticalSize( Core::SbDiagnosticStore& diagnostics, int objectType );
float EditorHouseVerticalSize( int objectType );

Rendering::RenderMaterial EditorBuildingPartMaterial( const EditorPlacementJson& part );
Rendering::RenderMaterial EditorTreePartMaterial( const EditorTreePartDefinition& part );
Rendering::RenderMaterial EditorHousePartMaterial( const EditorHousePartDefinition& part );
bool TryEditorRockMaterial( Assets::EditorHullAsset asset, Rendering::RenderMaterial& outMaterial );
bool TryEditorRootMaterial( Assets::EditorHullAsset asset, Rendering::RenderMaterial& outMaterial );
bool TryBuildScaledEditorHullForType( Core::SbDiagnosticStore& diagnostics, int objectType,
                                      const Math::Vector::Vector3& placementScale,
                                      Math::CollisionDetection::ConvexHullShape& outHull );

template <typename Fn> bool ForEachEditorBuildingPart( int objectType, const Assets::AssetSystem& assets, Fn&& fn )
{

    // Invariant: Asset-library part order is the spawn order. Preview bounds,
    // collision hull lookup, material selection, and actual placement must all
    // visit parts through this helper to stay identical.
    const EditorPlacementJson* asset = CachedEditorBuildingAsset( objectType, assets );

    if ( !asset )
    {
        return false;
    }

    const std::string type = EditorJsonStringOr( *asset, "type", "" );

    if ( IsEditorAssetPrimitiveType( type ) )
    {
        fn( *asset );
        return true;
    }

    const EditorPlacementJson* parts = EditorJsonFindMember( *asset, "parts" );

    if ( type != "compound" || !parts || !parts->is_array() )
    {
        return false;
    }

    for ( const EditorPlacementJson& part : *parts )
    {

        if ( !part.is_object() )
        {
            return false;
        }

        fn( part );
    }

    return true;
}
} // namespace Runtime
} // namespace SkullbonezCore
