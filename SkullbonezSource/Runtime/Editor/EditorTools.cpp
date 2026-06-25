/*
File: SkullbonezSource/Runtime/Editor/EditorTools.cpp
Purpose:
  Owns editor placement scale math for balls, boxes, hulls, roots, and trees.

Mental model:
  Placement gestures start as mouse deltas and wheel clicks. This file maps
  that input into safe object scale values before RunInput commits the object.

Glossary:
  Placement gesture: Mouse drag and wheel input used to size an editor object
    before placement commits.
  Hull scale: Per-axis size multiplier for convex hull editor assets.
  Uniform scale: One shared size value applied to all axes.
  Scale lock: Tree/root rule that keeps authored part proportions coherent.

Invariants:
  - Object-type classification must stay in sync with UI::EditorTab entries.
  - Scale helpers clamp before objects are committed to the scene.

Related:
  - SkullbonezSource/Runtime/Editor/EditorTools.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#include "EditorTools.h"

#include "../../UI/UITabEditor.h"

#include <algorithm>

using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
namespace
{
constexpr int EDITOR_MOUSE_WHEEL_DELTA = 120;
constexpr float EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT = 16.0f;
constexpr float EDITOR_PLACEMENT_SCALE_WHEEL_UNIT = 1.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT = 160.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_WHEEL_UNIT = 0.05f;

int ClampEditorObjectType( int objectType )
{
    return std::clamp( objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
}

static_assert( UI::EditorTab::OBJECT_TYPE_COUNT == 36,
               "Update editor placement scale classification when adding editor object types." );
} // namespace

int EditorMouseWheelSteps( int wheelDelta )
{
    if ( wheelDelta == 0 )
    {
        return 0;
    }
    return wheelDelta / EDITOR_MOUSE_WHEEL_DELTA;
}

Assets::EditorHullAsset EditorHullAssetForType( int objectType )
{
    switch ( ClampEditorObjectType( objectType ) )
    {
    case UI::EditorTab::OBJECT_HULL_WEDGE:
        return Assets::EditorHullAsset::WEDGE;
    case UI::EditorTab::OBJECT_HULL_TRI_PRISM:
        return Assets::EditorHullAsset::TRI_PRISM;
    case UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        return Assets::EditorHullAsset::TAPERED_BLOCK;
    case UI::EditorTab::OBJECT_HULL_PYRAMID:
        return Assets::EditorHullAsset::PYRAMID;
    case UI::EditorTab::OBJECT_HULL_HEX_PRISM:
        return Assets::EditorHullAsset::HEX_PRISM;
    case UI::EditorTab::OBJECT_HULL_DIAMOND:
        return Assets::EditorHullAsset::DIAMOND;
    case UI::EditorTab::OBJECT_ROCK_SLAB:
        return Assets::EditorHullAsset::ROCK_SLAB_FLAT;
    case UI::EditorTab::OBJECT_ROCK_LUMP:
        return Assets::EditorHullAsset::ROCK_LUMP_LARGE;
    case UI::EditorTab::OBJECT_ROCK_SHARD:
        return Assets::EditorHullAsset::ROCK_SHARD_TALL;
    case UI::EditorTab::OBJECT_ROCK_CHIPPED:
        return Assets::EditorHullAsset::ROCK_CHIPPED_BLOCK;
    case UI::EditorTab::OBJECT_ROOT_SMALL:
        return Assets::EditorHullAsset::TREE_ROOT_SMALL;
    case UI::EditorTab::OBJECT_ROOT_LARGE:
        return Assets::EditorHullAsset::TREE_ROOT_LARGE;
    default:
        return Assets::EditorHullAsset::UNKNOWN;
    }
}

bool EditorPlacementUsesUniformScale( int objectType )
{
    const int type = ClampEditorObjectType( objectType );
    return type == UI::EditorTab::OBJECT_BALL || type == UI::EditorTab::OBJECT_SPHERE ||
           type == UI::EditorTab::OBJECT_RAGDOLL || type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
}

bool EditorPlacementUsesHullScaleFactors( int objectType )
{
    return EditorHullAssetForType( objectType ) != Assets::EditorHullAsset::UNKNOWN;
}

bool EditorPlacementUsesTreeScaleLock( int objectType )
{
    const int type = ClampEditorObjectType( objectType );
    switch ( type )
    {
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
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
        return true;
    default:
        return false;
    }
}

Vector3 EditorDefaultPlacementScale( int objectType )
{
    const int type = ClampEditorObjectType( objectType );
    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        return Vector3( 6.0f, 6.0f, 6.0f );
    case UI::EditorTab::OBJECT_BALL:
        return Vector3( 4.0f, 4.0f, 4.0f );
    case UI::EditorTab::OBJECT_SPHERE:
        return Vector3( 8.0f, 8.0f, 8.0f );
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        return Vector3( 1.0f, 1.0f, 1.0f );
    default:
        return Vector3( 1.0f, 1.0f, 1.0f );
    }
}

Vector3 EditorClampPlacementScale( int objectType, const Vector3& scale )
{
    const int type = ClampEditorObjectType( objectType );
    if ( EditorPlacementUsesTreeScaleLock( type ) )
    {
        return Vector3( 1.0f, 1.0f, 1.0f );
    }

    if ( EditorPlacementUsesUniformScale( type ) )
    {
        const float radius = std::clamp( scale.x, 0.25f, 200.0f );
        return Vector3( radius, radius, radius );
    }

    if ( EditorPlacementUsesHullScaleFactors( type ) )
    {
        return Vector3( std::clamp( scale.x, 0.05f, 20.0f ),
                        std::clamp( scale.y, 0.05f, 20.0f ),
                        std::clamp( scale.z, 0.05f, 20.0f ) );
    }

    return Vector3( std::clamp( scale.x, 0.25f, 200.0f ),
                    std::clamp( scale.y, 0.25f, 200.0f ),
                    std::clamp( scale.z, 0.25f, 200.0f ) );
}

Vector3 EditorPlacementScaleFromGesture( int objectType,
                                         const Vector3& startScale,
                                         float dragPixelsX,
                                         float dragPixelsY,
                                         int wheelSteps )
{
    const int type = ClampEditorObjectType( objectType );
    if ( EditorPlacementUsesUniformScale( type ) )
    {
        const float dragUnits = ( dragPixelsX + dragPixelsY ) / ( EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT * 2.0f );
        const float radius =
            startScale.x + dragUnits + static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_SCALE_WHEEL_UNIT;
        return EditorClampPlacementScale( type, Vector3( radius, radius, radius ) );
    }

    if ( EditorPlacementUsesHullScaleFactors( type ) )
    {
        Vector3 scale = startScale;
        scale.x += dragPixelsX / EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT;
        scale.z += dragPixelsY / EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT;
        scale.y += static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_HULL_SCALE_WHEEL_UNIT;
        return EditorClampPlacementScale( type, scale );
    }

    Vector3 scale = startScale;
    scale.x += dragPixelsX / EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT;
    scale.z += dragPixelsY / EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT;
    scale.y += static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_SCALE_WHEEL_UNIT;
    return EditorClampPlacementScale( type, scale );
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
