/*
File: SkullbonezSource/SkullbonezRunInput.cpp
Purpose:
  Translates input and UI commands into runtime state changes.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"
#include "SkullbonezEditorHullAssets.h"
#include "SkullbonezInputController.h"
#include "SkullbonezPhysicsMass.h"
#include "SkullbonezRuntimeFileWriter.h"
#include "SkullbonezWorkerPool.h"
#include "UI/UIInput.h"
#include "UI/UILayout.h"

#include <chrono>
#include <cfloat>
#include <cstddef>
#include <cstring>

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

namespace
{
RuntimeInputModeState BuildRuntimeInputModeState( const RunCameraState& camera, const RunEditorPlacementState& editor )
{
    RuntimeInputModeState state;
    state.flyCamera = camera.isFlyMode;
    state.launcher = camera.isLauncherMode;
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = editor.placementScaleActive;
    state.editorGizmoDrag = editor.gizmoDragActive;
    state.editorGizmoRotation = editor.gizmoDragIsRotation;
    state.editorGizmoScale = editor.gizmoDragIsScale;
    return state;
}

struct RuntimeInputKeyBinding
{
    RuntimeInputAction action;
    int virtualKey;
};

void AdvanceTakeInputKeyboardActionMemories( RuntimeInputContext& input )
{
    static const RuntimeInputKeyBinding kBindings[] = {
        { RuntimeInputAction::ToggleFlyCamera, 'F' },
        { RuntimeInputAction::ToggleLauncher, 'N' },
        { RuntimeInputAction::ToggleEditor, VK_OEM_3 },
        { RuntimeInputAction::ToggleEditorTool, VK_MENU },
        { RuntimeInputAction::CycleEditorPlacementType, VK_TAB },
        { RuntimeInputAction::CycleLauncherFireMode, 'M' },
        { RuntimeInputAction::WriteLauncherReproSnapshot, VK_RETURN },
        { RuntimeInputAction::ToggleWaterFreeze, '1' },
        { RuntimeInputAction::CycleWaterReflection, '2' },
        { RuntimeInputAction::ToggleWaterFlat, '3' },
        { RuntimeInputAction::ToggleTerrainHidden, '4' },
        { RuntimeInputAction::ToggleWaterHidden, '5' },
        { RuntimeInputAction::ToggleCollisionVisualizer, 'V' },
        { RuntimeInputAction::CyclePhysicsDebugOverlay, 'C' },
        { RuntimeInputAction::ToggleTerrainContactProbe, 'O' },
        { RuntimeInputAction::StepPhysicsPipelinePrevious, VK_F7 },
        { RuntimeInputAction::StepPhysicsPipelineNext, VK_F8 },
        { RuntimeInputAction::TogglePhysicsDebugTransparent, '6' },
        { RuntimeInputAction::ReportRendererRuntimeRetired, 'Q' },
        { RuntimeInputAction::ToggleBroadphaseOverlay, 'G' },
        { RuntimeInputAction::ToggleUIVisibility, '0' },
        { RuntimeInputAction::NavigateScenePrevious, VK_LEFT },
        { RuntimeInputAction::NavigateSceneNext, VK_RIGHT },
        { RuntimeInputAction::DismissOrExitUI, VK_ESCAPE },
        { RuntimeInputAction::SaveSceneSnapshot, VK_F2 },
        { RuntimeInputAction::SaveScreenshot, VK_F3 },
        { RuntimeInputAction::ResetScene, 'R' },
        { RuntimeInputAction::ResetSceneFromBackspace, VK_BACK } };

    for ( std::size_t i = 0; i < sizeof( kBindings ) / sizeof( kBindings[0] ); ++i )
    {
        input.SetActionDown( kBindings[i].action, Input::IsKeyDown( kBindings[i].virtualKey ) );
    }
}

bool TransformClipPointToWorld( const Matrix4& inverseViewProjection, float x, float y, float z, Vector3& outWorld )
{
    const float worldX = inverseViewProjection.m[0] * x + inverseViewProjection.m[4] * y + inverseViewProjection.m[8] * z + inverseViewProjection.m[12];
    const float worldY = inverseViewProjection.m[1] * x + inverseViewProjection.m[5] * y + inverseViewProjection.m[9] * z + inverseViewProjection.m[13];
    const float worldZ = inverseViewProjection.m[2] * x + inverseViewProjection.m[6] * y + inverseViewProjection.m[10] * z + inverseViewProjection.m[14];
    const float worldW = inverseViewProjection.m[3] * x + inverseViewProjection.m[7] * y + inverseViewProjection.m[11] * z + inverseViewProjection.m[15];
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
constexpr float EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT = 16.0f;
constexpr float EDITOR_PLACEMENT_SCALE_WHEEL_UNIT = 1.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT = 160.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_WHEEL_UNIT = 0.05f;
constexpr float RAY_CAST_TEST_MAX_DISTANCE = 5000.0f;
constexpr float RAY_CAST_TEST_VISUAL_MISS_DISTANCE = 360.0f;
constexpr float LAUNCHER_PROJECTILE_RADIUS = 0.85f;
constexpr float LAUNCHER_PROJECTILE_MASS = 6.0f;
constexpr float LAUNCHER_PROJECTILE_RESTITUTION = 0.42f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_LEAD = 3.2f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET = 0.28f;


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


constexpr EditorTreePartDefinition LiftEditorTreePartY( EditorTreePartDefinition part, float liftY )
{
    part.offsetY += liftY;
    return part;
}


constexpr EditorTreePartDefinition SmallRootPart()
{
    return MakeEditorTreePart( EditorHullAsset::TREE_ROOT_SMALL, "root", 0.0f, 0.55f, 0.0f, 0.04f, SkullbonezCore::Rendering::RenderMaterialKind::Bark, "editor_small_root", 0.24f, 0.12f, 0.055f, 0.96f, 0.05f, 0.50f );
}


constexpr EditorTreePartDefinition LargeRootPart()
{
    return MakeEditorTreePart( EditorHullAsset::TREE_ROOT_LARGE, "root", 0.0f, 0.75f, 0.0f, 0.04f, SkullbonezCore::Rendering::RenderMaterialKind::Bark, "editor_large_root", 0.23f, 0.115f, 0.052f, 0.96f, 0.05f, 0.50f );
}


constexpr EditorTreePartDefinition PineNeedlePart( const char* suffix, float offsetX, float offsetY, float offsetZ, float shade )
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
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_SMALL_FACETED, "trunk", 0.0f, 6.5f, 0.0f, 0.06f, SkullbonezCore::Rendering::RenderMaterialKind::Bark, "editor_small_bark", 0.30f, 0.14f, 0.055f, 0.94f, 0.06f, 0.50f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_LOW, "low", 0.0f, 20.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_small_needles_low", 0.055f, 0.24f, 0.12f, 0.89f, 0.08f, 0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_MID, "mid", 0.0f, 28.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_small_needles_mid", 0.075f, 0.30f, 0.15f, 0.89f, 0.08f, 0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TOP, "top", 0.0f, 35.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_small_needles_top", 0.10f, 0.36f, 0.18f, 0.89f, 0.08f, 0.90f ),
};
constexpr int EDITOR_TREE_SMALL_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_SMALL_PARTS ) / sizeof( EDITOR_TREE_SMALL_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL = { "tree_small", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLOPE = { "tree_small_slope", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_SLEEP = { "tree_small_sleep", EDITOR_TREE_SMALL_PARTS, EDITOR_TREE_SMALL_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_PARTS[] = {
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_FACETED, "trunk", 0.0f, 9.0f, 0.0f, 0.06f, SkullbonezCore::Rendering::RenderMaterialKind::Bark, "editor_big_bark", 0.31f, 0.16f, 0.07f, 0.94f, 0.06f, 0.48f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_LARGE, "low", 0.0f, 24.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_big_needles_low", 0.045f, 0.20f, 0.055f, 0.88f, 0.08f, 0.88f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_MID, "mid", 0.0f, 34.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_big_needles_mid", 0.06f, 0.25f, 0.075f, 0.88f, 0.08f, 0.88f ),
    MakeEditorTreePart( EditorHullAsset::PINE_TIER_TOP, "top", 0.0f, 43.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_big_needles_top", 0.09f, 0.31f, 0.10f, 0.88f, 0.08f, 0.88f ),
};
constexpr int EDITOR_TREE_BIG_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_BIG_PARTS ) / sizeof( EDITOR_TREE_BIG_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG = { "tree_pine", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLOPE = { "tree_pine_slope", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_BIG_SLEEP = { "tree_pine_sleep", EDITOR_TREE_BIG_PARTS, EDITOR_TREE_BIG_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_PARTS[] = {
    MakeEditorTreePart( EditorHullAsset::TREE_TRUNK_FACETED, "trunk", 0.0f, 9.0f, 0.0f, 0.06f, SkullbonezCore::Rendering::RenderMaterialKind::Bark, "editor_cedar_bark", 0.28f, 0.13f, 0.055f, 0.94f, 0.06f, 0.50f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TALL_LOW, "low", 0.0f, 25.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_cedar_needles_low", 0.055f, 0.24f, 0.12f, 0.89f, 0.08f, 0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TALL_MID, "mid", 0.0f, 38.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_cedar_needles_mid", 0.075f, 0.30f, 0.15f, 0.89f, 0.08f, 0.90f ),
    MakeEditorTreePart( EditorHullAsset::CEDAR_TIER_TOP, "top", 0.0f, 48.0f, 0.0f, 0.05f, SkullbonezCore::Rendering::RenderMaterialKind::Foliage, "editor_cedar_needles_top", 0.10f, 0.36f, 0.18f, 0.89f, 0.08f, 0.90f ),
};
constexpr int EDITOR_TREE_CEDAR_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_CEDAR_PARTS ) / sizeof( EDITOR_TREE_CEDAR_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR = { "tree_cedar", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLOPE = { "tree_cedar_slope", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT, true, false };
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_SLEEP = { "tree_cedar_sleep", EDITOR_TREE_CEDAR_PARTS, EDITOR_TREE_CEDAR_PART_COUNT, false, false, true };

constexpr EditorTreePartDefinition EDITOR_TREE_SMALL_ROOTED_PARTS[] = {
    SmallRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_SMALL_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_SMALL_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS ) / sizeof( EDITOR_TREE_SMALL_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_SMALL_ROOTED = { "tree_small_rooted", EDITOR_TREE_SMALL_ROOTED_PARTS, EDITOR_TREE_SMALL_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_BIG_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_BIG_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_BIG_ROOTED_PARTS ) / sizeof( EDITOR_TREE_BIG_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_BIG_ROOTED = { "tree_pine_rooted", EDITOR_TREE_BIG_ROOTED_PARTS, EDITOR_TREE_BIG_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_CEDAR_ROOTED_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_CEDAR_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_CEDAR_ROOTED_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS ) / sizeof( EDITOR_TREE_CEDAR_ROOTED_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_CEDAR_ROOTED = { "tree_cedar_rooted", EDITOR_TREE_CEDAR_ROOTED_PARTS, EDITOR_TREE_CEDAR_ROOTED_PART_COUNT, true, true };

constexpr EditorTreePartDefinition EDITOR_TREE_PINE_SHEDDING_PARTS[] = {
    LargeRootPart(),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[0], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[1], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[2], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( EDITOR_TREE_BIG_PARTS[3], SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_00", -12.0f, 22.0f, -12.0f, 0.10f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_01", -4.0f, 21.5f, -16.0f, 0.18f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_02", 6.0f, 22.5f, -15.0f, 0.30f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_03", 14.0f, 23.0f, -7.0f, 0.24f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_04", 15.0f, 23.5f, 5.0f, 0.14f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_05", 8.0f, 22.0f, 14.0f, 0.34f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_06", -4.0f, 22.5f, 16.0f, 0.22f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_07", -15.0f, 23.0f, 7.0f, 0.28f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_08", -9.0f, 31.0f, -10.0f, 0.38f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_09", 1.0f, 31.5f, -12.0f, 0.26f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_10", 10.0f, 32.0f, -5.0f, 0.16f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_11", 11.0f, 32.5f, 6.0f, 0.32f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_12", 2.0f, 31.0f, 12.0f, 0.20f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_13", -10.0f, 32.0f, 4.0f, 0.36f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_14", -6.0f, 40.0f, -6.0f, 0.18f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_15", 4.0f, 40.5f, -7.0f, 0.28f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_16", 7.0f, 41.0f, 3.0f, 0.12f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
    LiftEditorTreePartY( PineNeedlePart( "needle_17", -3.0f, 41.0f, 7.0f, 0.34f ), SkullbonezCore::Assets::EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y ),
};
constexpr int EDITOR_TREE_PINE_SHEDDING_PART_COUNT = static_cast<int>( sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS ) / sizeof( EDITOR_TREE_PINE_SHEDDING_PARTS[0] ) );
constexpr EditorTreeDefinition EDITOR_TREE_PINE_SHEDDING = { "tree_pine_shedding", EDITOR_TREE_PINE_SHEDDING_PARTS, EDITOR_TREE_PINE_SHEDDING_PART_COUNT, true, true };


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


EditorHullAsset EditorHullAssetForType( int objectType )
{
    switch ( objectType )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_WEDGE:
        return EditorHullAsset::WEDGE;
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_TRI_PRISM:
        return EditorHullAsset::TRI_PRISM;
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        return EditorHullAsset::TAPERED_BLOCK;
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_PYRAMID:
        return EditorHullAsset::PYRAMID;
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_HEX_PRISM:
        return EditorHullAsset::HEX_PRISM;
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_DIAMOND:
        return EditorHullAsset::DIAMOND;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_SLAB:
        return EditorHullAsset::ROCK_SLAB_FLAT;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_LUMP:
        return EditorHullAsset::ROCK_LUMP_LARGE;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_SHARD:
        return EditorHullAsset::ROCK_SHARD_TALL;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_CHIPPED:
        return EditorHullAsset::ROCK_CHIPPED_BLOCK;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROOT_SMALL:
        return EditorHullAsset::TREE_ROOT_SMALL;
    case SkullbonezCore::UI::EditorTab::OBJECT_ROOT_LARGE:
        return EditorHullAsset::TREE_ROOT_LARGE;
    default:
        return EditorHullAsset::UNKNOWN;
    }
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


bool TryComputeEditorTreeWorldBounds( const EditorTreeDefinition& tree, const Vector3& terrainPoint, const RotationMatrix& orientation, Vector3& outMin, Vector3& outMax )
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


SkullbonezCore::Rendering::RenderMaterial EditorTreePartMaterial( const EditorTreePartDefinition& part )
{
    SkullbonezCore::Rendering::RenderMaterial material =
        SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( part.colorR, part.colorG, part.colorB, static_cast<float>( part.materialKind ) );
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

    outMaterial = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( r, g, b, static_cast<float>( SkullbonezCore::Rendering::RenderMaterialKind::Stone ) );
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
    outMaterial = SkullbonezCore::Rendering::MakeRenderMaterialFromLegacyTint( r, g, b, static_cast<float>( SkullbonezCore::Rendering::RenderMaterialKind::Bark ) );
    strncpy_s( outMaterial.name, sizeof( outMaterial.name ), name, _TRUNCATE );
    outMaterial.roughness = 0.96f;
    outMaterial.specular = 0.05f;
    outMaterial.stylization = 0.50f;
    return true;
}


int EditorMouseWheelSteps( int wheelDelta )
{
    if ( wheelDelta == 0 )
    {
        return 0;
    }
    return wheelDelta / WHEEL_DELTA;
}


bool EditorPlacementUsesUniformScale( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    return type == SkullbonezCore::UI::EditorTab::OBJECT_BALL ||
           type == SkullbonezCore::UI::EditorTab::OBJECT_SPHERE;
}


bool EditorPlacementUsesHullScaleFactors( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    return EditorHullAssetForType( type ) != EditorHullAsset::UNKNOWN;
}


Vector3 EditorDefaultPlacementScale( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        return Vector3( 6.0f, 6.0f, 6.0f );
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        return Vector3( 4.0f, 4.0f, 4.0f );
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        return Vector3( 8.0f, 8.0f, 8.0f );
    default:
        return Vector3( 1.0f, 1.0f, 1.0f );
    }
}


Vector3 EditorClampPlacementScale( int objectType, const Vector3& scale )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( EditorTreeDefinitionForType( type ) )
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


Vector3 EditorPlacementScaleFromGesture( int objectType, const Vector3& startScale, float dragPixelsX, float dragPixelsY, int wheelSteps )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( EditorPlacementUsesUniformScale( type ) )
    {
        const float dragUnits = ( dragPixelsX + dragPixelsY ) / ( EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT * 2.0f );
        const float radius = startScale.x + dragUnits + static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_SCALE_WHEEL_UNIT;
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
    default:
    {
        if ( EditorTreeDefinitionForType( type ) )
        {
            return EditorTreeVerticalSize( type );
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


float EditorGizmoAxisLength( float modelRadius )
{
    return (std::max)( 14.0f, modelRadius + 12.0f );
}


float EditorGizmoRotationRadius( float modelRadius )
{
    return (std::max)( 12.0f, modelRadius + 7.0f );
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


bool IntersectRaySphere( const Vector3& rayOrigin,
                         const Vector3& rayDirection,
                         const Vector3& center,
                         float radius,
                         float& outT )
{
    const Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );
    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }
    return true;
}

constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 64;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 12;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }
    return id;
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame, ReplayBodyId id )
{
    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

ReplayBodyId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }
    return id;
}

float ReplayPathFrameT( ReplayFrameIndex frame, ReplayFrameIndex start, ReplayFrameIndex end )
{
    if ( end <= start || frame <= start )
    {
        return 0.0f;
    }
    if ( frame >= end )
    {
        return 1.0f;
    }
    const double numerator = static_cast<double>( frame - start );
    const double denominator = static_cast<double>( end - start );
    return static_cast<float>( std::clamp( numerator / denominator, 0.0, 1.0 ) );
}

std::size_t ReplayPathStrideForSampleCount( std::size_t sampleCount )
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }
    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

struct ReplayPathBoundsContext
{
    bool hasSample = false;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

void CaptureReplayPathBounds( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathBoundsContext& context = *static_cast<ReplayPathBoundsContext*>( userData );
    if ( !context.hasSample )
    {
        context.hasSample = true;
        context.firstFrame = sample.frameIndex;
    }
    context.lastFrame = sample.frameIndex;
}

struct ReplayPathFutureContext
{
    RunReplayPathVisualizerState* visualizer = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex presentFrame = 0;
};

bool TryGetReplayFutureDepth( const ReplayPathFutureContext& context, ReplayBodyId id, ReplayFrameIndex frame, int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }
    if ( id.value == context.rootId.value )
    {
        outDepth = 0;
        return frame >= context.presentFrame;
    }

    for ( const RunReplayPathTraceNode& node : context.visualizer->futureNodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

bool ReplayFutureNodeExists( const RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : visualizer.futureNodes )
    {
        if ( node.id.value == id.value )
        {
            return true;
        }
    }
    return false;
}

RunReplayPathTarget* FindReplayPathTarget( RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( RunReplayPathTarget& target : visualizer.targets )
    {
        if ( target.id.value == id.value )
        {
            return &target;
        }
    }
    return nullptr;
}

void ApplyPrimaryReplayPathTarget( RunReplayPathVisualizerState& visualizer,
                                   ReplayBodyId id,
                                   int modelIndex,
                                   const char* name )
{
    visualizer.hasTarget = id.value != 0;
    visualizer.targetId = id;
    visualizer.targetModelIndex = modelIndex;
    visualizer.targetName[0] = '\0';
    if ( name && name[0] != '\0' )
    {
        strncpy_s( visualizer.targetName, sizeof( visualizer.targetName ), name, _TRUNCATE );
    }
}

void AddReplayFutureNode( ReplayPathFutureContext& context,
                          ReplayBodyId parentId,
                          ReplayBodyId id,
                          ReplayFrameIndex firstFrame,
                          const Vector3& contactPoint,
                          const Vector3& contactNormal,
                          int depth )
{
    if ( id.value == 0 ||
         id.value == context.rootId.value ||
         ReplayFutureNodeExists( *context.visualizer, id ) ||
         context.visualizer->futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    node.id = id;
    node.parentId = parentId;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    context.visualizer->futureNodes.push_back( node );
}

void BuildReplayFutureNodes( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathFutureContext& context = *static_cast<ReplayPathFutureContext*>( userData );
    if ( !context.visualizer || sample.frameIndex < context.presentFrame )
    {
        return;
    }

    for ( const PhysicsDebugContact& contact : sample.worldSnapshot.debugContacts )
    {
        const ReplayBodyId idA = ReplayBodyIdForModelIndex( sample, contact.bodyA );
        const ReplayBodyId idB = ReplayBodyIdForModelIndex( sample, contact.bodyB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayFutureDepth( context, idA, sample.frameIndex, depthA );
        const bool activeB = TryGetReplayFutureDepth( context, idB, sample.frameIndex, depthB );
        if ( activeA && !activeB )
        {
            AddReplayFutureNode( context, idA, idB, sample.frameIndex, contact.point, contact.normal, depthA + 1 );
        }
        else if ( activeB && !activeA )
        {
            AddReplayFutureNode( context, idB, idA, sample.frameIndex, contact.point, contact.normal * -1.0f, depthB + 1 );
        }
    }
}

bool ShouldDrawReplayPathSample( std::size_t ordinal, std::size_t stride )
{
    return stride <= 1 || ( ordinal % stride ) == 0;
}

struct ReplayPathRootDrawContext
{
    RunEditorTracer* tracer = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
    bool hasPastPrevious = false;
    bool hasFuturePrevious = false;
    Vector3 pastPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 futurePrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

void DrawReplayRootPath( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathRootDrawContext& context = *static_cast<ReplayPathRootDrawContext*>( userData );
    const std::size_t ordinal = context.sampleOrdinal++;
    if ( sample.frameIndex != context.presentFrame &&
         sample.frameIndex != context.lastFrame &&
         !ShouldDrawReplayPathSample( ordinal, context.sampleStride ) )
    {
        return;
    }

    const ReplaySolverBodySample* body = FindReplayBodyById( sample, context.rootId );
    if ( !body )
    {
        return;
    }

    if ( sample.frameIndex <= context.presentFrame )
    {
        if ( context.hasPastPrevious && VectorMagSquared( body->position - context.pastPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.firstFrame, context.presentFrame );
            context.tracer->AddReplayPathSegment( context.pastPrevious, body->position, 1.0f, t, t );
        }
        context.pastPrevious = body->position;
        context.hasPastPrevious = true;
    }

    if ( sample.frameIndex >= context.presentFrame )
    {
        if ( context.hasFuturePrevious && VectorMagSquared( body->position - context.futurePrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, context.lastFrame );
            context.tracer->AddReplayPathSegment( context.futurePrevious, body->position, 1.0f - t, 1.0f, 1.0f - t );
        }
        context.futurePrevious = body->position;
        context.hasFuturePrevious = true;
    }
}

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool hasIncomingPrevious = false;
    bool hasPrevious = false;
    bool markerDrawn = false;
    Vector3 incomingPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

struct ReplayPathChildDrawContext
{
    RunEditorTracer* tracer = nullptr;
    const std::vector<GameModel>* models = nullptr;
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
};

float ReplayFutureMarkerRadiusForModelIndex( const std::vector<GameModel>* models, int modelIndex )
{
    if ( models &&
         modelIndex >= 0 &&
         modelIndex < static_cast<int>( models->size() ) )
    {
        return EditorModelRadius( ( *models )[static_cast<std::size_t>( modelIndex )] ) * 1.18f;
    }
    return 1.25f;
}

void ReplayChildIncomingColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.36f );
    r = std::clamp( 0.96f - depthFade * 0.55f, 0.44f, 1.0f );
    g = std::clamp( 0.48f + t * 0.34f - depthFade * 0.36f, 0.28f, 0.88f );
    b = std::clamp( 0.16f + t * 0.20f - depthFade * 0.18f, 0.10f, 0.52f );
}

void ReplayChildFutureColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.08f, 0.0f, 0.30f );
    const float shade = std::clamp( 0.48f + t * 0.28f - depthFade, 0.25f, 0.78f );
    r = shade;
    g = shade;
    b = shade + 0.06f;
}

void DrawReplayChildPaths( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathChildDrawContext& context = *static_cast<ReplayPathChildDrawContext*>( userData );
    const std::size_t ordinal = context.sampleOrdinal++;
    bool importantChildFrame = sample.frameIndex == context.presentFrame;
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        if ( sample.frameIndex == context.nodes[i].node.firstFrame )
        {
            importantChildFrame = true;
            break;
        }
    }
    const bool skipSample = sample.frameIndex < context.presentFrame ||
                            ( sample.frameIndex != context.lastFrame &&
                              !importantChildFrame &&
                              !ShouldDrawReplayPathSample( ordinal, context.sampleStride ) );
    if ( skipSample )
    {
        return;
    }

    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        ReplayPathChildDrawState& drawState = context.nodes[i];
        const ReplaySolverBodySample* body = FindReplayBodyById( sample, drawState.node.id );
        if ( !body )
        {
            continue;
        }

        if ( sample.frameIndex <= drawState.node.firstFrame )
        {
            if ( !drawState.markerDrawn )
            {
                const float radius = ReplayFutureMarkerRadiusForModelIndex( context.models, body->modelIndex );
                context.tracer->AddReplayFutureTargetMarker( body->position, radius, drawState.node.depth );
                drawState.markerDrawn = true;
            }
            if ( drawState.hasIncomingPrevious && VectorMagSquared( body->position - drawState.incomingPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, drawState.node.firstFrame );
                float r = 0.92f;
                float g = 0.54f;
                float b = 0.18f;
                ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                context.tracer->AddReplayPathSegment( drawState.incomingPrevious, body->position, r, g, b );
            }
            drawState.incomingPrevious = body->position;
            drawState.hasIncomingPrevious = true;
        }

        if ( sample.frameIndex >= drawState.node.firstFrame &&
             drawState.hasPrevious &&
             VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, drawState.node.firstFrame, context.lastFrame );
            float r = 0.5f;
            float g = 0.5f;
            float b = 0.56f;
            ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
            context.tracer->AddReplayPathSegment( drawState.previous, body->position, r, g, b );
        }
        if ( sample.frameIndex >= drawState.node.firstFrame )
        {
            drawState.previous = body->position;
            drawState.hasPrevious = true;
        }
    }
}

void AddReplayFutureContactMarkers( const RunReplayPathVisualizerState& visualizer, RunEditorTracer& tracer )
{
    for ( const RunReplayPathTraceNode& node : visualizer.futureNodes )
    {
        float r = 0.58f;
        float g = 0.62f;
        float b = 0.70f;
        if ( node.depth <= 1 )
        {
            r = 0.72f;
            g = 0.78f;
            b = 0.86f;
        }
        tracer.AddReplayContactMarker( node.contactPoint, node.contactNormal, r, g, b );
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    ReplayBodyId rootId;
};

bool TryGetReplayPredictionFutureDepth( const ReplayPredictionFutureContext& context, ReplayBodyId id, ReplayFrameIndex frame, int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }
    if ( id.value == context.rootId.value )
    {
        outDepth = 0;
        return true;
    }

    for ( const RunReplayPathTraceNode& node : context.prediction->futureNodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

bool ReplayPredictionFutureNodeExists( const RunReplayPredictionState& prediction, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : prediction.futureNodes )
    {
        if ( node.id.value == id.value )
        {
            return true;
        }
    }
    return false;
}

void AddReplayPredictionFutureNode( ReplayPredictionFutureContext& context,
                                    ReplayBodyId parentId,
                                    ReplayBodyId id,
                                    ReplayFrameIndex firstFrame,
                                    const Vector3& contactPoint,
                                    const Vector3& contactNormal,
                                    int depth )
{
    if ( id.value == 0 ||
         id.value == context.rootId.value ||
         ReplayPredictionFutureNodeExists( *context.prediction, id ) ||
         context.prediction->futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    node.id = id;
    node.parentId = parentId;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    context.prediction->futureNodes.push_back( node );
}

void BuildReplayPredictionFutureNodes( const RunReplayPredictionFrame& frame, ReplayPredictionFutureContext& context )
{
    for ( const PhysicsDebugContact& contact : frame.debugContacts )
    {
        const ReplayBodyId idA = ReplayPredictionBodyIdForModelIndex( frame, contact.bodyA );
        const ReplayBodyId idB = ReplayPredictionBodyIdForModelIndex( frame, contact.bodyB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayPredictionFutureDepth( context, idA, frame.frameIndex, depthA );
        const bool activeB = TryGetReplayPredictionFutureDepth( context, idB, frame.frameIndex, depthB );
        if ( activeA && !activeB )
        {
            AddReplayPredictionFutureNode( context, idA, idB, frame.frameIndex, contact.point, contact.normal, depthA + 1 );
        }
        else if ( activeB && !activeA )
        {
            AddReplayPredictionFutureNode( context, idB, idA, frame.frameIndex, contact.point, contact.normal * -1.0f, depthB + 1 );
        }
    }
}


uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return SCENE_CINE_EXPOSURE;
    case UICinematicParam::Gamma:
        return SCENE_CINE_GAMMA;
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
    case UICinematicParam::WaterMode:
        return SCENE_CINE_STYLE_MODES;
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
    case UICinematicParam::StyleVignette:
        return SCENE_CINE_STYLE_GRADE;
    case UICinematicParam::SunX:
        return SCENE_CINE_SUN_SCREEN_X;
    case UICinematicParam::SunY:
        return SCENE_CINE_SUN_SCREEN_Y;
    case UICinematicParam::SunBrightness:
        return SCENE_CINE_SUN_INTENSITY;
    case UICinematicParam::SunRed:
        return SCENE_CINE_SUN_COLOR_R;
    case UICinematicParam::SunGreen:
        return SCENE_CINE_SUN_COLOR_G;
    case UICinematicParam::SunBlue:
        return SCENE_CINE_SUN_COLOR_B;
    case UICinematicParam::SkyGlow:
        return SCENE_CINE_SKY_GLOW_STRENGTH;
    case UICinematicParam::HorizonRed:
        return SCENE_CINE_SKY_HORIZON_R;
    case UICinematicParam::HorizonGreen:
        return SCENE_CINE_SKY_HORIZON_G;
    case UICinematicParam::HorizonBlue:
        return SCENE_CINE_SKY_HORIZON_B;
    case UICinematicParam::ZenithRed:
        return SCENE_CINE_SKY_ZENITH_R;
    case UICinematicParam::ZenithGreen:
        return SCENE_CINE_SKY_ZENITH_G;
    case UICinematicParam::ZenithBlue:
        return SCENE_CINE_SKY_ZENITH_B;
    case UICinematicParam::CloudCoverage:
        return SCENE_CINE_CLOUD_COVERAGE;
    case UICinematicParam::CloudSoftness:
        return SCENE_CINE_CLOUD_SOFTNESS;
    case UICinematicParam::CloudScale:
        return SCENE_CINE_CLOUD_SCALE;
    case UICinematicParam::CloudIntensity:
        return SCENE_CINE_CLOUD_INTENSITY;
    case UICinematicParam::ShaftStrength:
        return SCENE_CINE_SUN_SHAFT_STRENGTH;
    case UICinematicParam::ShaftFalloff:
        return SCENE_CINE_SUN_SHAFT_FALLOFF;
    case UICinematicParam::VolumetricStrength:
        return SCENE_CINE_VOLUMETRIC_STRENGTH;
    case UICinematicParam::VolumetricDensity:
        return SCENE_CINE_VOLUMETRIC_DENSITY;
    case UICinematicParam::VolumetricDecay:
        return SCENE_CINE_VOLUMETRIC_DECAY;
    case UICinematicParam::BloomThreshold:
        return SCENE_CINE_BLOOM_THRESHOLD;
    case UICinematicParam::BloomKnee:
        return SCENE_CINE_BLOOM_KNEE;
    case UICinematicParam::BloomStrength:
        return SCENE_CINE_BLOOM_STRENGTH;
    case UICinematicParam::BloomRadius:
        return SCENE_CINE_BLOOM_RADIUS;
    case UICinematicParam::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF;
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
        return SCENE_CINE_TERRAIN_TINT;
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
        return SCENE_CINE_TERRAIN_ACCENT;
    case UICinematicParam::TerrainGridScale:
    case UICinematicParam::TerrainGridStrength:
        return SCENE_CINE_TERRAIN_GRID;
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
        return SCENE_CINE_WATER_TINT;
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
    case UICinematicParam::WaterGlint:
        return SCENE_CINE_WATER_PROFILE;
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
    case UICinematicParam::BasinFeather:
        return SCENE_CINE_BASIN_MASK;
    case UICinematicParam::BasinDepth:
        return SCENE_CINE_BASIN_DEPTH;
    case UICinematicParam::BasinRimLift:
        return SCENE_CINE_BASIN_RIM_LIFT;
    case UICinematicParam::FogDensity:
        return SCENE_CINE_FOG_DENSITY;
    case UICinematicParam::FogOpacity:
        return SCENE_CINE_FOG_MAX_OPACITY;
    case UICinematicParam::FogStart:
        return SCENE_CINE_FOG_START;
    case UICinematicParam::FogEnd:
        return SCENE_CINE_FOG_END;
    case UICinematicParam::FogRed:
        return SCENE_CINE_FOG_COLOR_R;
    case UICinematicParam::FogGreen:
        return SCENE_CINE_FOG_COLOR_G;
    case UICinematicParam::FogBlue:
        return SCENE_CINE_FOG_COLOR_B;
    default:
        return 0;
    }
}


uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return SCENE_CINE_SKY_ATMOSPHERE;
    case UICinematicFeature::Clouds:
        return SCENE_CINE_CLOUDS;
    case UICinematicFeature::GodRays:
        return SCENE_CINE_GOD_RAYS;
    case UICinematicFeature::VolumetricLight:
        return SCENE_CINE_VOLUMETRIC_LIGHTING;
    case UICinematicFeature::Bloom:
        return SCENE_CINE_BLOOM;
    case UICinematicFeature::Fog:
        return SCENE_CINE_FOG;
    case UICinematicFeature::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF_ENABLED;
    case UICinematicFeature::Shadows:
        return SCENE_CINE_SHADOWS;
    default:
        return 0;
    }
}


void ApplyWorkerThreadCountOverride( int requestedWorkerThreads )
{
    const int clampedWorkerThreads = requestedWorkerThreads < 0 ? -1 : std::clamp( requestedWorkerThreads, 0, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}


void ApplyCinematicUIParam( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicParam param, float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    {
        return std::clamp( value, minValue, maxValue );
    };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    {
        return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue );
    };

    switch ( param )
    {
    case UICinematicParam::Exposure:
        cinematic.exposure = clampValue( rawValue, 0.05f, 3.00f );
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = cinematic.exposure;
        scene.cinematicOverrideMask |= SCENE_CINE_EXPOSURE;
        break;
    case UICinematicParam::Gamma:
        cinematic.gamma = clampValue( rawValue, 1.00f, 3.00f );
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = cinematic.gamma;
        scene.cinematicOverrideMask |= SCENE_CINE_GAMMA;
        break;
    case UICinematicParam::SkyMode:
        cinematic.skyMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::TerrainMode:
        cinematic.terrainMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::ObjectStyle:
        cinematic.objectStyle = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::WaterMode:
        cinematic.waterMode = clampIntValue( rawValue, 0, 4 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::StyleSaturation:
        cinematic.styleSaturation = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleContrast:
        cinematic.styleContrast = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleVignette:
        cinematic.styleVignette = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::SunX:
        cinematic.sunScreenX = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_X;
        break;
    case UICinematicParam::SunY:
        cinematic.sunScreenY = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_Y;
        break;
    case UICinematicParam::SunBrightness:
        cinematic.sunIntensity = clampValue( rawValue, 0.00f, 40.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_INTENSITY;
        break;
    case UICinematicParam::SunRed:
        cinematic.sunColorR = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_R;
        break;
    case UICinematicParam::SunGreen:
        cinematic.sunColorG = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_G;
        break;
    case UICinematicParam::SunBlue:
        cinematic.sunColorB = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_B;
        break;
    case UICinematicParam::SkyGlow:
        cinematic.skyGlowStrength = clampValue( rawValue, 0.00f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_GLOW_STRENGTH;
        break;
    case UICinematicParam::HorizonRed:
        cinematic.skyHorizonR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_R;
        break;
    case UICinematicParam::HorizonGreen:
        cinematic.skyHorizonG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_G;
        break;
    case UICinematicParam::HorizonBlue:
        cinematic.skyHorizonB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_B;
        break;
    case UICinematicParam::ZenithRed:
        cinematic.skyZenithR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_R;
        break;
    case UICinematicParam::ZenithGreen:
        cinematic.skyZenithG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_G;
        break;
    case UICinematicParam::ZenithBlue:
        cinematic.skyZenithB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_B;
        break;
    case UICinematicParam::CloudCoverage:
        cinematic.cloudCoverage = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_COVERAGE;
        break;
    case UICinematicParam::CloudSoftness:
        cinematic.cloudSoftness = clampValue( rawValue, 0.01f, 0.65f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SOFTNESS;
        break;
    case UICinematicParam::CloudScale:
        cinematic.cloudScale = clampValue( rawValue, 0.50f, 12.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SCALE;
        break;
    case UICinematicParam::CloudIntensity:
        cinematic.cloudIntensity = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_INTENSITY;
        break;
    case UICinematicParam::ShaftStrength:
        cinematic.sunShaftStrength = clampValue( rawValue, 0.00f, 3.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_STRENGTH;
        break;
    case UICinematicParam::ShaftFalloff:
        cinematic.sunShaftFalloff = clampValue( rawValue, 0.25f, 5.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_FALLOFF;
        break;
    case UICinematicParam::VolumetricStrength:
        cinematic.volumetricStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_STRENGTH;
        break;
    case UICinematicParam::VolumetricDensity:
        cinematic.volumetricDensity = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DENSITY;
        break;
    case UICinematicParam::VolumetricDecay:
        cinematic.volumetricDecay = clampValue( rawValue, 0.800f, 0.995f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DECAY;
        break;
    case UICinematicParam::BloomThreshold:
        cinematic.bloomThreshold = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_THRESHOLD;
        break;
    case UICinematicParam::BloomKnee:
        cinematic.bloomKnee = clampValue( rawValue, 0.01f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_KNEE;
        break;
    case UICinematicParam::BloomStrength:
        cinematic.bloomStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_STRENGTH;
        break;
    case UICinematicParam::BloomRadius:
        cinematic.bloomRadius = clampValue( rawValue, 0.25f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_RADIUS;
        break;
    case UICinematicParam::TerrainRelief:
        cinematic.terrainRelief = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF;
        break;
    case UICinematicParam::TerrainTintRed:
        cinematic.terrainTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintGreen:
        cinematic.terrainTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintBlue:
        cinematic.terrainTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainAccentRed:
        cinematic.terrainAccentR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentGreen:
        cinematic.terrainAccentG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentBlue:
        cinematic.terrainAccentB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainGridScale:
        cinematic.terrainGridScale = clampValue( rawValue, 0.10f, 120.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::TerrainGridStrength:
        cinematic.terrainGridStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::WaterTintRed:
        cinematic.waterTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintGreen:
        cinematic.waterTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintBlue:
        cinematic.waterTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterAlpha:
        cinematic.waterAlpha = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterReflection:
        cinematic.waterReflectionStrength = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterGlint:
        cinematic.waterGlintStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::BasinCenterX:
        cinematic.basinCenterX = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinCenterZ:
        cinematic.basinCenterZ = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusX:
        cinematic.basinRadiusX = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusZ:
        cinematic.basinRadiusZ = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinFeather:
        cinematic.basinFeather = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinDepth:
        cinematic.basinDepth = clampValue( rawValue, 0.00f, 80.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_DEPTH;
        break;
    case UICinematicParam::BasinRimLift:
        cinematic.basinRimLift = clampValue( rawValue, 0.00f, 60.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_RIM_LIFT;
        break;
    case UICinematicParam::FogDensity:
        cinematic.fogDensity = clampValue( rawValue, 0.00000f, 0.00600f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_DENSITY;
        break;
    case UICinematicParam::FogOpacity:
        cinematic.fogMaxOpacity = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_MAX_OPACITY;
        break;
    case UICinematicParam::FogStart:
        cinematic.fogStart = clampValue( rawValue, 0.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_START;
        break;
    case UICinematicParam::FogEnd:
        cinematic.fogEnd = clampValue( rawValue, 100.00f, 4000.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_END;
        break;
    case UICinematicParam::FogRed:
        cinematic.fogColorR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_R;
        break;
    case UICinematicParam::FogGreen:
        cinematic.fogColorG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_G;
        break;
    case UICinematicParam::FogBlue:
        cinematic.fogColorB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_B;
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIParam( param );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}


void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled )
{
    // Shadow maps are configured next to the cinematic controls because the
    // original implementation grew from that renderer work, but the depth pass
    // now feeds normal rendering too. Toggling shadows from either the Options
    // tab or the Cine tab must therefore only touch the shadow flag and scene
    // override bits; it must not silently enable the HDR/post-processing stack.
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
}

void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        ordinary.sunIntensity = std::clamp( rawValue, 0.0f, 4.0f );
        break;
    case UIRenderParam::SunRed:
        ordinary.sunColorR = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunGreen:
        ordinary.sunColorG = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunBlue:
        ordinary.sunColorB = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::AmbientStrength:
        ordinary.ambientStrength = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyRed:
        ordinary.skyAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyGreen:
        ordinary.skyAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyBlue:
        ordinary.skyAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundRed:
        ordinary.groundAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundGreen:
        ordinary.groundAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundBlue:
        ordinary.groundAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::ShadowStrength:
        ordinary.shadowStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::ShadowSoftness:
        ordinary.shadowSoftness = std::clamp( rawValue, 0.25f, 4.0f );
        break;
    case UIRenderParam::ShadowDepthBias:
        ordinary.shadowDepthBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::ShadowSlopeBias:
        ordinary.shadowSlopeBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::WaterRed:
        ordinary.waterTintR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterGreen:
        ordinary.waterTintG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterBlue:
        ordinary.waterTintB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterAlpha:
        ordinary.waterAlpha = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterReflection:
        ordinary.waterReflectionStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterFresnel:
        ordinary.waterFresnelF0 = std::clamp( rawValue, 0.0f, 0.12f );
        break;
    case UIRenderParam::BallRoughness:
        ordinary.ballRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BallSpecular:
        ordinary.ballSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::BoxRoughness:
        ordinary.boxRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BoxSpecular:
        ordinary.boxSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    default:
        break;
    }
}


void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature )
{
    // Feature toggles are boolean pass switches: sky on/off, bloom on/off, etc.
    // Each toggle also marks the matching override bit for scene persistence.
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        cinematic.skyAtmosphereEnabled = !cinematic.skyAtmosphereEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ATMOSPHERE;
        break;
    case UICinematicFeature::Clouds:
        cinematic.cloudsEnabled = !cinematic.cloudsEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUDS;
        break;
    case UICinematicFeature::GodRays:
        cinematic.godRaysEnabled = !cinematic.godRaysEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_GOD_RAYS;
        break;
    case UICinematicFeature::VolumetricLight:
        cinematic.volumetricLightingEnabled = !cinematic.volumetricLightingEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_LIGHTING;
        break;
    case UICinematicFeature::Bloom:
        cinematic.bloomEnabled = !cinematic.bloomEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM;
        break;
    case UICinematicFeature::Fog:
        cinematic.fogEnabled = !cinematic.fogEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_FOG;
        break;
    case UICinematicFeature::TerrainRelief:
        cinematic.terrainReliefEnabled = !cinematic.terrainReliefEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF_ENABLED;
        break;
    case UICinematicFeature::Shadows:
        SetCinematicShadowsEnabledFromUI( cinematic, scene, !cinematic.shadowsEnabled );
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIFeature( feature );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}
} // namespace

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

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) ) : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
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


void RunEditorTracer::EmitBox( const Vector3& center, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis, float r, float g, float bl )
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


void RunEditorTracer::AddPlacementGhost( int objectType, const Vector3& center, const Vector3& terrainPoint, const Vector3& placementScale, const Quaternion& orientation )
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
            const Vector3 hullCenter = base + rotation * ( Vector3( part.offsetX, part.offsetY, part.offsetZ ) + HullAuthoredLocalOffset( *hull ) );
            for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
            {
                const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                EmitLine( hullCenter + rotation * hull->GetVertex( edge.vertexA ), hullCenter + rotation * hull->GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
            }
        }
        return;
    }

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        EmitBox( center, rotation * Vector3( scale.x, 0.0f, 0.0f ), rotation * Vector3( 0.0f, scale.y, 0.0f ), rotation * Vector3( 0.0f, 0.0f, scale.z ), ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
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
            EmitLine( hullCenter + rotation * hull.GetVertex( edge.vertexA ), hullCenter + rotation * hull.GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
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
        EmitSphere( model.GetPosition() + rot * sphere->GetPosition(), sphere->GetBoundingRadius(), outlineR, outlineG, outlineB );
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
            EmitLine( hullCenter + rot * hull->GetVertex( edge.vertexA ), hullCenter + rot * hull->GetVertex( edge.vertexB ), outlineR, outlineG, outlineB );
        }
    }
}


void RunEditorTracer::AddGizmo( const Vector3& origin, float radius, int hotTranslateAxis, int hotRotationAxis, int activeAxis, bool activeRotation, bool scaleMode, bool activeScale )
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


void RunEditorTracer::AddReplayVelocityGizmo( const GameModel& model, int hotLinearAxis, int hotAngularAxis, int activeAxis, bool activeAngular )
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
        EmitLine( origin - axisVector * ( baseLength * 0.24f ), origin + axisVector * ( baseLength * 0.24f ), r * 0.34f, g * 0.34f, b * 0.34f );
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


void SkullbonezRun::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void SkullbonezRun::SetReplaySimulationPaused( bool paused )
{
    if ( m_replayScrubber.simulationPaused == paused )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/SimulationPause" );

    if ( paused )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.simulationPaused = true;
        UpdateReplayInspectionCamera();
        return;
    }

    m_replayScrubber.simulationPaused = false;
    UpdateReplayInspectionCamera();
}


void SkullbonezRun::EnterReplayInspectionCamera()
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const bool enteringInspectionCamera = !m_replayScrubber.inspectionCameraActive;
    if ( !m_replayScrubber.inspectionCameraActive )
    {
        m_replayScrubber.inspectionRestoreFlyMode = m_camera.isFlyMode;
        m_replayScrubber.inspectionRestoreLauncherMode = m_camera.isLauncherMode;
        m_replayScrubber.inspectionRestoreCameraHash = m_systems.cameras->GetSelectedCameraName();

        const Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
        const Vector3 view = m_systems.cameras->GetRenderCameraView();
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        m_systems.cameras->SetPrimaryPosition( eye );
        m_systems.cameras->SetViewCoordinates( view );
        m_replayScrubber.inspectionCameraActive = true;
    }

    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    m_systems.cameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
    m_camera.cameraTime = 0.0f;
    m_camera.isFlyMode = true;
    m_camera.isLauncherMode = false;
    if ( enteringInspectionCamera )
    {
        Input::SetSystemCursorVisible( true );
        InputController::ResetMouseLook( m_camera );
    }
}


void SkullbonezRun::ExitReplayInspectionCamera()
{
    if ( !m_replayScrubber.inspectionCameraActive )
    {
        return;
    }

    m_replayScrubber.inspectionCameraActive = false;
    m_camera.isLauncherMode = m_replayScrubber.inspectionRestoreLauncherMode;
    m_camera.isFlyMode = m_replayScrubber.inspectionRestoreFlyMode || m_camera.isLauncherMode;
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( m_replayScrubber.inspectionRestoreCameraHash, false );
        if ( m_systems.terrain )
        {
            const uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            if ( m_camera.isFlyMode )
            {
                XZBounds unbounded;
                unbounded.m_xMin = -99999.9f;
                unbounded.m_xMax = 99999.9f;
                unbounded.m_zMin = -99999.9f;
                unbounded.m_zMax = 99999.9f;
                m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            }
            else
            {
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
            }
        }
    }
    Input::SetSystemCursorVisible( true );
    InputController::ResetMouseLook( m_camera );
}


void SkullbonezRun::UpdateReplayInspectionCamera()
{
    if ( m_replayScrubber.paused || m_replayScrubber.simulationPaused )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
}


bool SkullbonezRun::TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberInput" );
    m_replayScrubber.restoreConsumedThisFrame = false;
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayScrubber.leftWasDown;
    const bool leftReleased = !leftDown && m_replayScrubber.leftWasDown;
    m_replayScrubber.leftWasDown = leftDown;
    const bool restoreDown = Input::IsKeyDown( VK_RETURN );
    const bool restorePressed = restoreDown && !m_replayScrubber.restoreWasDown;
    m_replayScrubber.restoreWasDown = restoreDown;

    const bool scrubberAllowed = !m_editor.editorModeEnabled && m_UI.IsVisible() && m_UI.IsMinimized();
    const ReplayRecorderStats replayStats = m_replay.GetStats();
    const ReplayRecorderStats solverReplayStats = m_solverReplay.GetStats();
    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    if ( !scrubberAllowed || !replayStats.enabled || !solverReplayStats.enabled || replayStats.sampleCount < 2 || solverReplayStats.sampleCount < 2 || screenW <= 0 || screenH <= 0 )
    {
        if ( m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
        }
        ResetReplayScrubber();
        m_replayPrediction.checkboxHovered = false;
        m_replayPrediction.decreaseHovered = false;
        m_replayPrediction.increaseHovered = false;
        m_replayPrediction.horizonHovered = false;
        m_replayPrediction.horizonDragging = false;
        m_replayVelocityEdit.toggleHovered = false;
        m_replayScrubber.leftWasDown = leftDown;
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    m_replayScrubber.mouseX = mouse.x;
    m_replayScrubber.mouseY = mouse.y;

    const UI::UIRect hotZone = ReplayScrubberHotZoneRect( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect presentationTrack = ReplayScrubberTrackRect( screenW, screenH, RunReplayTrack::Presentation );
    const UI::UIRect solverTrack = ReplayScrubberTrackRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect presentationSaveButton = ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Presentation );
    const UI::UIRect solverSaveButton = ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
    const UI::UIRect velocityEditToggle = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    const UI::UIRect predictControl = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predictDecrease = ReplayScrubberPredictDecreaseRect( screenW, screenH );
    const UI::UIRect predictIncrease = ReplayScrubberPredictIncreaseRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const bool inHotZone = hotZone.Contains( mouse.x, mouse.y );
    const bool overPanel = panel.Contains( mouse.x, mouse.y );
    const bool overPresentationSaveButton = presentationSaveButton.Contains( mouse.x, mouse.y );
    const bool overSolverSaveButton = solverSaveButton.Contains( mouse.x, mouse.y );
    const bool overSaveButton = overPresentationSaveButton || overSolverSaveButton;
    const bool overPauseButton = pauseButton.Contains( mouse.x, mouse.y );
    const bool overVelocityEditToggle = velocityEditToggle.Contains( mouse.x, mouse.y );
    const bool overPredictControl = predictControl.Contains( mouse.x, mouse.y );
    const bool overPredictToggle = predictToggle.Contains( mouse.x, mouse.y );
    const bool overPredictUi = overPredictControl || overPredictToggle;
    const bool overPredictDecrease = predictDecrease.Contains( mouse.x, mouse.y );
    const bool overPredictIncrease = predictIncrease.Contains( mouse.x, mouse.y );
    const bool overPredictHorizon = predictHorizon.Contains( mouse.x, mouse.y ) ||
                                    ( predictControl.Contains( mouse.x, mouse.y ) && mouse.x >= predictHorizon.x && mouse.x <= predictHorizon.x + predictHorizon.w );
    const bool overSolverRow = solverTrack.Contains( mouse.x, mouse.y ) || overSolverSaveButton ||
                               ( overPanel && mouse.y >= ( presentationTrack.y + solverTrack.y ) * 0.5f );
    const RunReplayTrack hoveredTrack = overSolverRow ? RunReplayTrack::Solver : RunReplayTrack::Presentation;
    const bool canTakeMouse = !uiBlocksMouse || m_replayScrubber.dragging || m_replayPrediction.horizonDragging;
    const double now = m_timers.simulationTimer.GetTotalTime();

    if ( inHotZone ||
         overPanel ||
         overSaveButton ||
         overPauseButton ||
         overVelocityEditToggle ||
         overPredictUi ||
         m_replayScrubber.dragging ||
         m_replayPrediction.horizonDragging ||
         m_replayScrubber.paused ||
         m_replayScrubber.simulationPaused )
    {
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    }
    m_replayScrubber.saveHovered = overSaveButton &&
                                   ( m_replayScrubber.visibleUntil >= now ||
                                     m_replayScrubber.dragging ||
                                     m_replayScrubber.paused );
    m_replayScrubber.saveHoveredTrack = hoveredTrack;
    const bool predictionControlVisible = m_replayScrubber.visibleUntil >= now ||
                                          m_replayScrubber.dragging ||
                                          m_replayPrediction.horizonDragging ||
                                          m_replayScrubber.paused ||
                                          m_replayScrubber.simulationPaused;
    m_replayScrubber.pauseHovered = overPauseButton && predictionControlVisible;
    m_replayVelocityEdit.toggleHovered = overVelocityEditToggle && predictionControlVisible;
    m_replayPrediction.checkboxHovered = overPredictToggle && predictionControlVisible;
    m_replayPrediction.decreaseHovered = overPredictDecrease && predictionControlVisible;
    m_replayPrediction.increaseHovered = overPredictIncrease && predictionControlVisible;
    m_replayPrediction.horizonHovered = overPredictHorizon && predictionControlVisible;

    bool consumesMouse = canTakeMouse &&
                         ( m_replayScrubber.dragging ||
                           m_replayPrediction.horizonDragging ||
                           ( m_replayScrubber.visibleUntil >= now && ( inHotZone || overSaveButton || overPauseButton || overVelocityEditToggle || overPredictUi ) ) );

    if ( restorePressed && m_replayScrubber.paused && m_replayScrubber.activeTrack == RunReplayTrack::Solver )
    {
        EnterInteractiveSceneRun();
        char reason[96] = {};
        const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample();
        const bool restored = sample && RestoreReplaySolverSampleAsLive( *sample, reason, sizeof( reason ) );
        m_replayScrubber.restoreConsumedThisFrame = true;
        m_replayScrubber.saveMessageTrack = RunReplayTrack::Solver;
        sprintf_s( m_replayScrubber.saveMessage,
                   sizeof( m_replayScrubber.saveMessage ),
                   restored ? "SOLVER RESTORED" : "RESTORE FAILED" );
        m_replayScrubber.saveMessageUntil = now + 2.5;
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 reason[0] != '\0' ? ": " : "",
                 reason );
        return true;
    }

    auto changePredictionHorizon = [&]( float deltaSeconds )
    {
        EnterInteractiveSceneRun();
        const float nextSeconds = std::clamp( m_replayPrediction.horizonSeconds + deltaSeconds,
                                              REPLAY_PREDICTION_MIN_SECONDS,
                                              REPLAY_PREDICTION_MAX_SECONDS );
        if ( nextSeconds != m_replayPrediction.horizonSeconds )
        {
            m_replayPrediction.horizonSeconds = nextSeconds;
            MarkReplayPredictionDirty();
        }
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    };

    auto setPredictionHorizonFromMouse = [&]()
    {
        EnterInteractiveSceneRun();
        const float nextSeconds = ReplayPredictionHorizonFromMouse( mouse.x, predictHorizon );
        if ( nextSeconds != m_replayPrediction.horizonSeconds )
        {
            m_replayPrediction.horizonSeconds = nextSeconds;
            MarkReplayPredictionDirty();
        }
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    };

    if ( leftPressed && canTakeMouse && overPauseButton && m_replayScrubber.visibleUntil >= now )
    {
        SetReplaySimulationPaused( !m_replayScrubber.simulationPaused );
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overVelocityEditToggle && m_replayScrubber.visibleUntil >= now )
    {
        SetReplayVelocityEditEnabled( !m_replayVelocityEdit.enabled );
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overPredictHorizon && m_replayScrubber.visibleUntil >= now )
    {
        m_replayPrediction.horizonDragging = true;
        setPredictionHorizonFromMouse();
        if ( !m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayScrubber.mouseCaptured = true;
        }
    }
    else if ( leftPressed && canTakeMouse && overPredictDecrease && m_replayScrubber.visibleUntil >= now )
    {
        changePredictionHorizon( -REPLAY_PREDICTION_STEP_SECONDS );
    }
    else if ( leftPressed && canTakeMouse && overPredictIncrease && m_replayScrubber.visibleUntil >= now )
    {
        changePredictionHorizon( REPLAY_PREDICTION_STEP_SECONDS );
    }
    else if ( leftPressed && canTakeMouse && overPredictToggle && m_replayScrubber.visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        m_replayPrediction.enabled = !m_replayPrediction.enabled;
        m_replayPrediction.horizonSeconds = std::clamp( m_replayPrediction.horizonSeconds,
                                                        REPLAY_PREDICTION_MIN_SECONDS,
                                                        REPLAY_PREDICTION_MAX_SECONDS );
        if ( !m_replayPrediction.enabled )
        {
            ClearReplayPredictionCache();
        }
        MarkReplayPredictionDirty();
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overSaveButton && m_replayScrubber.visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.activeTrack = hoveredTrack;
        ReplayScrubberSyncActivePosition( m_replayScrubber );
        SaveReplayBufferFromScrubber( hoveredTrack );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && !overPauseButton && !overPredictUi && ( inHotZone || overPanel || m_replayScrubber.paused ) )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.activeTrack = hoveredTrack;
        ReplayScrubberSyncActivePosition( m_replayScrubber );
        m_replayScrubber.dragging = true;
        if ( !m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayScrubber.mouseCaptured = true;
        }
    }

    if ( m_replayScrubber.dragging )
    {
        ReplayScrubberSetTrackPosition( m_replayScrubber,
                                        m_replayScrubber.activeTrack,
                                        ReplayScrubberPositionFromMouse( mouse.x, screenW, screenH, m_replayScrubber.activeTrack ) );
        if ( m_replayScrubber.position >= REPLAY_SCRUBBER_LIVE_THRESHOLD )
        {
            ReplayScrubberSetTrackPosition( m_replayScrubber, m_replayScrubber.activeTrack, 1.0f );
            m_replayScrubber.paused = false;
        }
        else
        {
            m_replayScrubber.paused = true;
        }

        if ( leftReleased )
        {
            m_replayScrubber.dragging = false;
            if ( m_replayScrubber.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayScrubber.mouseCaptured = false;
            }
        }
    }
    else if ( m_replayPrediction.horizonDragging )
    {
        setPredictionHorizonFromMouse();
        if ( leftReleased )
        {
            m_replayPrediction.horizonDragging = false;
            if ( m_replayScrubber.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayScrubber.mouseCaptured = false;
            }
        }
    }
    else if ( !m_replayScrubber.paused )
    {
        ReplayScrubberSetAllTrackPositions( m_replayScrubber, 1.0f );
    }

    m_replayScrubber.visible = m_replayScrubber.dragging ||
                               m_replayPrediction.horizonDragging ||
                               m_replayScrubber.paused ||
                               m_replayScrubber.simulationPaused ||
                               m_replayScrubber.visibleUntil >= now;
    UpdateReplayInspectionCamera();
    return consumesMouse;
}


void SkullbonezRun::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        if ( m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
        }
        ResetReplayScrubber();
        m_replayPrediction.checkboxHovered = false;
        m_replayPrediction.decreaseHovered = false;
        m_replayPrediction.increaseHovered = false;
        m_replayPrediction.horizonHovered = false;
        m_replayPrediction.horizonDragging = false;
        m_replayVelocityEdit.toggleHovered = false;
        m_replayVelocityEdit.keyboardAltWasDown = false;
        m_replayVelocityEdit.dragging = false;
        m_replayVelocityEdit.draggingAngular = false;
        m_replayVelocityEdit.activeAxis = -1;
        m_replayVelocityEdit.hotLinearAxis = -1;
        m_replayVelocityEdit.hotAngularAxis = -1;
        if ( m_replayVelocityEdit.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayVelocityEdit.mouseCaptured = false;
        }
        m_editor.viewportLookActive = false;
        m_editor.altShortcutWasDown = false;
        m_editor.tabShortcutWasDown = false;
        m_editor.tildeShortcutWasDown = false;
        m_editor.placementScaleActive = false;
        m_editor.placementScaleWheelSteps = 0;
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
        m_editor.gizmoDragStartAxisT = 0.0f;
        m_editor.gizmoDragStartRotationAngle = 0.0f;
        m_editor.gizmoDragStartPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        m_editor.gizmoDragStartOrientation = IDENTITY_QUATERNION;
        InputController::ResetUnfocusedInput( m_camera, m_leftSceneCycleWasDown, m_rightSceneCycleWasDown );
        m_runtimeInput.ResetEdges();
        InputController::BeginFrame( m_runtimeInput, BuildRuntimeInputModeState( m_camera, m_editor ), false, true, true );
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    const auto ReplayInspectionActive = [&]() -> bool
    {
        return m_replayScrubber.inspectionCameraActive ||
               m_replayScrubber.paused ||
               m_replayScrubber.simulationPaused;
    };
    const auto ReplayInspectionMouseLookActive = [&]() -> bool
    {
        return ReplayInspectionActive() &&
               Input::IsRightMouseDown() &&
               !m_UI.WantsNativeMouseCursor() &&
               !m_UI.BlocksCameraMouse();
    };
    const auto MouseLookOwnsCursor = [&]() -> bool
    {
        if ( m_UI.WantsNativeMouseCursor() || m_UI.BlocksCameraMouse() )
        {
            return false;
        }

        if ( m_editor.editorModeEnabled )
        {
            return m_editor.viewportLookActive;
        }

        if ( ReplayInspectionActive() )
        {
            return ReplayInspectionMouseLookActive();
        }

        return m_camera.isFlyMode;
    };
    const auto ShouldHideNativeCursor = [&]() -> bool
    {
        if ( MouseLookOwnsCursor() )
        {
            return true;
        }

        return m_editor.editorModeEnabled &&
               m_editor.placementModeEnabled &&
               m_editor.placementPreviewVisible &&
               !m_UI.WantsNativeMouseCursor() &&
               !m_UI.BlocksCameraMouse();
    };
    const auto ApplyCursorOwnership = [&]() -> void
    {
        Input::SetSystemCursorVisible( !ShouldHideNativeCursor() );
    };
    const auto ReleaseMouseToUI = [&]() -> void
    {
        if ( !MouseLookOwnsCursor() )
        {
            ReleaseCapture();
            InputController::ResetMouseLook( m_camera );
        }
    };
    const auto UpdateRuntimeInputModeAfterAction = [&]( RuntimeInputAction action, RuntimeInputActionSource source ) -> void
    {
        InputController::ApplyModeAction( m_runtimeInput,
                                          InputController::ResolveMode( BuildRuntimeInputModeState( m_camera, m_editor ) ),
                                          action,
                                          source );
    };
    const auto ClearEditorManipulationState = [&]() -> void
    {
        m_editor.placementPreviewVisible = false;
        m_editor.placementScaleActive = false;
        m_editor.placementScaleWheelSteps = 0;
        m_editor.placementScale = EditorDefaultPlacementScale( m_editor.objectType );
        m_editor.placementScaleStart = m_editor.placementScale;
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
        m_editor.placementAltitudeSteps = 0;
    };
    const auto ToggleEditorPlacementMode = [&]( RuntimeInputActionSource source ) -> void
    {
        EnterInteractiveSceneRun();
        m_editor.placementModeEnabled = m_editor.editorModeEnabled && !m_editor.placementModeEnabled;
        m_editor.viewportLookActive = false;
        ClearEditorManipulationState();
        ReleaseMouseToUI();
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    const auto EnterFlyModeCamera = [&]() -> void
    {
        // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
        // on the current camera so fly controls work without requiring CAMERA_FREE
        if ( !SceneState().isSceneMode )
        {
            m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        }
        m_camera.cameraTime = 0.0f;
        XZBounds unbounded;
        unbounded.m_xMin = -99999.9f;
        unbounded.m_xMax = 99999.9f;
        unbounded.m_zMin = -99999.9f;
        unbounded.m_zMax = 99999.9f;
        uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
        m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
        if ( ShouldHideNativeCursor() )
        {
            Input::SetSystemCursorVisible( false );
        }
        else
        {
            ReleaseMouseToUI();
            Input::SetSystemCursorVisible( true );
        }
        InputController::ResetMouseLook( m_camera );
    };
    const auto ExitFlyModeCamera = [&]() -> void
    {
        // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
        // the stock Windows cursor.
        uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
        m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
        Input::SetSystemCursorVisible( true );
        m_camera.cameraTime = 0.0f;
        // Exiting fly mode also exits launcher mode.
        m_camera.isLauncherMode = false;
        InputController::ResetMouseLook( m_camera );
    };

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera, m_editor ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
    if ( !UIBlocksKeyboardBeforeInput )
    {
        keyboardToggleEditorMode = InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleEditor, VK_OEM_3 );

        // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
        bool prevFlyMode = m_camera.isFlyMode;
        bool keyboardModeAction = false;
        RuntimeInputAction keyboardModeActionName = RuntimeInputAction::None;
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleFlyCamera, 'F' ) )
        {
            m_camera.isFlyMode = !m_camera.isFlyMode;
            m_camera.isLauncherMode = false; // F-key fly never implies launcher mode.
            keyboardModeAction = true;
            keyboardModeActionName = RuntimeInputAction::ToggleFlyCamera;
        }

        // N key: toggle launcher mode with live simulation (edge-detected).
        // Entering also enters fly mode; exiting also exits fly mode.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleLauncher, 'N' ) )
            {
                m_camera.isLauncherMode = !m_camera.isLauncherMode;
                m_camera.isFlyMode = m_camera.isLauncherMode;
                keyboardModeAction = true;
                keyboardModeActionName = RuntimeInputAction::ToggleLauncher;
            }
        }

        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::CycleLauncherFireMode, 'M' ) && m_camera.isLauncherMode )
            {
                m_rayCastTest.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Laser ? RunLauncherFireMode::Projectile : RunLauncherFireMode::Laser;
            }
        }

#ifdef _DEBUG
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::WriteLauncherReproSnapshot, VK_RETURN ) &&
                 m_camera.isLauncherMode &&
                 !m_replayScrubber.restoreConsumedThisFrame )
            {
                WriteLauncherReproSnapshot();
            }
        }
#endif

        if ( m_editor.editorModeEnabled )
        {
            m_camera.isFlyMode = true;
            m_camera.isLauncherMode = false;
            m_replayVelocityEdit.keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleEditorTool, VK_MENU ) )
            {
                ToggleEditorPlacementMode( RuntimeInputActionSource::Keyboard );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::CycleEditorPlacementType, VK_TAB ) )
            {
                if ( Input::IsKeyDown( VK_CONTROL ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.placeStaticObject = !m_editor.placeStaticObject;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement, RuntimeInputActionSource::Keyboard );
                }
                else
                {
                    EnterInteractiveSceneRun();
                    m_editor.objectType = ( m_editor.objectType + 1 ) % UI::EditorTab::OBJECT_TYPE_COUNT;
                    ClearEditorManipulationState();
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleEditorPlacementType, RuntimeInputActionSource::Keyboard );
                }
            }
        }
        else
        {
            const bool altDown = Input::IsKeyDown( VK_MENU );
            if ( altDown && !m_replayVelocityEdit.keyboardAltWasDown )
            {
                SetReplayVelocityEditEnabled( !m_replayVelocityEdit.enabled );
            }
            m_replayVelocityEdit.keyboardAltWasDown = altDown;
            m_runtimeInput.SetActionDown( RuntimeInputAction::ToggleEditorTool, altDown );
            m_runtimeInput.SetActionDown( RuntimeInputAction::CycleEditorPlacementType, Input::IsKeyDown( VK_TAB ) );
            m_editor.altShortcutWasDown = altDown;
            m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        }

        if ( m_camera.isFlyMode != prevFlyMode )
        {
            if ( m_camera.isFlyMode )
            {
                EnterFlyModeCamera();
            }
            else
            {
                ExitFlyModeCamera();
            }
        }
        if ( keyboardModeAction )
        {
            UpdateRuntimeInputModeAfterAction( keyboardModeActionName, RuntimeInputActionSource::Keyboard );
        }

        // Water m_shader debug toggles
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterFreeze, '1' ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        // Key '2' cycles water reflection modes in a predictable loop:
        // FBO mirror rendering, then DXR raytraced reflection when supported,
        // then no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::CycleWaterReflection, '2' ) )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true;
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true;
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true;
                }
                else
                {
                    m_debug.isWaterNoReflect = false;
                }
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterFlat, '3' ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleTerrainHidden, '4' ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterHidden, '5' ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
        }
        // V key: collision visualizer for balls and boxes as solid debug colours.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleCollisionVisualizer, 'V' ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::CyclePhysicsDebugOverlay, 'C' ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
        }

        // O key: toggle the terrain polygon/contact probe. It is independent of
        // the C-key debug cycle so it can be layered over any other physics view.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleTerrainContactProbe, 'O' ) )
            {
                m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            }
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::StepPhysicsPipelinePrevious, VK_F7 ) )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::StepPhysicsPipelineNext, VK_F8 ) )
            {
                StepPhysicsPipelineStage( 1 );
            }
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::TogglePhysicsDebugTransparent, '6' ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
        }

        // Q key used to cycle legacy renderers; it now reports that DX12 is the only runtime renderer.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ReportRendererRuntimeRetired, 'Q' ) )
            {
                fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
            }
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleBroadphaseOverlay, 'G' ) )
        {
            if ( SceneState().isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleUIVisibility, '0' ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleUIVisibility, RuntimeInputActionSource::Keyboard );
            }
        }

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::NavigateScenePrevious, VK_LEFT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( -1 ) )
            {
                LoadAdjacentSceneFromBrowser( -1 );
            }
        }
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::NavigateSceneNext, VK_RIGHT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( 1 ) )
            {
                LoadAdjacentSceneFromBrowser( 1 );
            }
        }
    }
    else
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
        m_replayVelocityEdit.keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.altShortcutWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        m_editor.tildeShortcutWasDown = Input::IsKeyDown( VK_OEM_3 );
    }

    bool suppressWorldActionThisFrame = UIBlocksKeyboardBeforeInput;
    int editorUnhandledWheelDelta = 0;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult = m_UI.UpdateInput( m_systems.window->m_sWindow,
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                                                         m_timers.simulationTimer.GetTotalTime(),
                                                         m_editor.editorModeEnabled,
                                                         m_editor.placementModeEnabled,
                                                         m_editor.placeStaticObject,
                                                         m_editor.autoTerrainAlign,
                                                         m_editor.objectType,
                                                         m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                                                         static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                                                         selectedSceneBrowserIndex );
        editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || uiCommands.ui.userInteracted || m_UI.BlocksCameraMouse();
        const bool replayScrubberOwnsMouse = TickReplayScrubberInput( m_systems.window->m_sWindow, m_UI.BlocksCameraMouse() );
        const bool replayCauseTreeOwnsMouse = TickReplayCauseTreeInput( m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse );
        const bool replayVelocityEditOwnsMouse = TickReplayVelocityEditInput( m_systems.window->m_sWindow, m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse );
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse;
        m_runtimeInput.BeginFrame( true, m_UI.BlocksKeyboard(), m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse );

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::DismissOrExitUI, VK_ESCAPE ) && !uiCommands.ui.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleVsync, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.requestedObjectType >= 0 )
        {
            const int requestedObjectType = std::clamp( uiCommands.editor.requestedObjectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
            if ( requestedObjectType != m_editor.objectType )
            {
                m_editor.objectType = requestedObjectType;
                ClearEditorManipulationState();
            }
            else if ( uiCommands.editor.enterPlacementMode )
            {
                ClearEditorManipulationState();
            }
            if ( uiCommands.editor.enterPlacementMode && m_editor.editorModeEnabled )
            {
                EnterInteractiveSceneRun();
                m_editor.placementModeEnabled = true;
                m_editor.viewportLookActive = false;
                ReleaseMouseToUI();
                ApplyCursorOwnership();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleEditorPlacementType, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleEditorMode || keyboardToggleEditorMode )
        {
            EnterInteractiveSceneRun();
            const RuntimeInputActionSource toggleEditorSource = keyboardToggleEditorMode ? RuntimeInputActionSource::Keyboard : RuntimeInputActionSource::UI;
            m_editor.editorModeEnabled = !m_editor.editorModeEnabled;
            if ( m_editor.editorModeEnabled )
            {
                const bool wasFlyMode = m_camera.isFlyMode;
                m_editor.placementModeEnabled = true;
                m_editor.viewportLookActive = false;
                ClearEditorManipulationState();
                m_editor.restoreFlyModeAfterEditor = m_camera.isFlyMode;
                m_editor.restoreRayTestModeAfterEditor = m_camera.isLauncherMode;
                m_camera.isFlyMode = true;
                m_camera.isLauncherMode = false;
                if ( !wasFlyMode )
                {
                    EnterFlyModeCamera();
                }
                else
                {
                    InputController::ResetMouseLook( m_camera );
                }
                ApplyCursorOwnership();
            }
            else
            {
                const bool wasFlyMode = m_camera.isFlyMode;
                m_editor.viewportLookActive = false;
                m_editor.placementPreviewVisible = false;
                m_editor.placementModeEnabled = false;
                m_editor.gizmoDragActive = false;
                m_editor.gizmoDragIsRotation = false;
                m_editor.gizmoDragIsScale = false;
                m_editor.activeGizmoAxis = -1;
                m_editor.placementScaleActive = false;
                m_editor.placementScaleWheelSteps = 0;
                m_editor.placementScale = EditorDefaultPlacementScale( m_editor.objectType );
                m_editor.placementScaleStart = m_editor.placementScale;
                m_editor.placementAltitudeSteps = 0;
                m_camera.isFlyMode = m_editor.restoreFlyModeAfterEditor || m_editor.restoreRayTestModeAfterEditor;
                m_camera.isLauncherMode = m_editor.restoreRayTestModeAfterEditor;
                m_editor.restoreFlyModeAfterEditor = false;
                m_editor.restoreRayTestModeAfterEditor = false;
                if ( wasFlyMode && !m_camera.isFlyMode )
                {
                    ExitFlyModeCamera();
                }
                else
                {
                    InputController::ResetMouseLook( m_camera );
                }
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditor, toggleEditorSource );
        }
        if ( uiCommands.editor.togglePlacementMode )
        {
            ToggleEditorPlacementMode( RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.togglePlaceStatic )
        {
            EnterInteractiveSceneRun();
            m_editor.placeStaticObject = !m_editor.placeStaticObject;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleTerrainAlign )
        {
            EnterInteractiveSceneRun();
            m_editor.autoTerrainAlign = !m_editor.autoTerrainAlign;
            m_editor.placementPreviewVisible = false;
            m_editor.placementScaleActive = false;
            m_editor.placementScaleWheelSteps = 0;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTerrainAlign, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCollisionVisualizer, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsSleepPolicy, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugFlags, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelinePrevious, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelineNext, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugTransparent, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleBroadphaseOverlay, RuntimeInputActionSource::UI );
        }
        bool tornadoFieldChanged = false;
        if ( uiCommands.physics.toggleTornado )
        {
            m_runtimeSettings.tornadoField.enabled = !m_runtimeSettings.tornadoField.enabled;
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornado, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoFieldVectors )
        {
            m_runtimeSettings.tornadoField.visualizeVelocityField = !m_runtimeSettings.tornadoField.visualizeVelocityField;
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoFieldVectors, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleRayCastVisualization )
        {
            m_rayCastTest.visualizeRays = !m_rayCastTest.visualizeRays;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRayCastVisualization, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoRadius )
        {
            m_runtimeSettings.tornadoField.radius = std::clamp( uiCommands.physics.requestedTornadoRadius, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoHeight )
        {
            m_runtimeSettings.tornadoField.height = std::clamp( uiCommands.physics.requestedTornadoHeight, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoInward )
        {
            m_runtimeSettings.tornadoField.inwardAcceleration = std::clamp( uiCommands.physics.requestedTornadoInward, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoSwirl )
        {
            m_runtimeSettings.tornadoField.swirlAcceleration = std::clamp( uiCommands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoLift )
        {
            m_runtimeSettings.tornadoField.liftAcceleration = std::clamp( uiCommands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( tornadoFieldChanged )
        {
            SyncTornadoFieldToPhysics();
        }
        if ( uiCommands.physics.toggleTerrainContactProbe )
        {
            m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainContactProbe, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTextOnly, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleFixedStep, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFreeze, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFlat, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            if ( IsCinematicRenderingEnabled() )
            {
                const bool shadowsActive = ActiveCinematicConfig().shadowsEnabled;
                m_cmdHasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), SceneState(), !shadowsActive );
            }
            else
            {
                Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRenderShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            SaveRenderDefaults();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveRenderDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( Cfg().ordinaryRender, uiCommands.renderTuning.requestedParam, uiCommands.renderTuning.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyRenderTuning, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterReflection, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWaterReflectionMode, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_UITimeScaleOverride;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetTimeScale, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            SceneState().rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            SceneState().rngState = SceneState().rngSeed;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRunSeed, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugAlpha, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger = std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugContactLinger, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestRayCastImpulseStrength )
        {
            m_rayCastTest.impulseStrength = std::clamp( uiCommands.physics.requestedRayCastImpulseStrength, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRayCastImpulseStrength, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestLauncherProjectileSpeed )
        {
            m_rayCastTest.projectileSpeed = std::clamp( uiCommands.physics.requestedLauncherProjectileSpeed, UI_LAUNCHER_PROJECTILE_SPEED_MIN, UI_LAUNCHER_PROJECTILE_SPEED_MAX );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetLauncherProjectileSpeed, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( uiCommands.sceneOptions.requestedModelCount );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetModelCount, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.profiler.requestedWorkerThreads >= -1 )
        {
            ApplyWorkerThreadCountOverride( uiCommands.profiler.requestedWorkerThreads );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int boxes = m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : SceneState().solverBoxCount;
            ApplyUISolverObjectCounts( std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ), boxes );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int balls = m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : SceneState().solverBallCount;
            ApplyUISolverObjectCounts( balls, std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, modelCapacity - balls ) ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight || uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight ? uiCommands.water.requestedWorldFluidHeight : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity ? uiCommands.water.requestedWorldFluidDensity : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyWorldWaterSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_cmdHasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            const bool currentlyEnabled = m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_cmdHasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicRendering, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            ApplyCinematicModeFromBrowserIndex( uiCommands.cinematic.requestedModeSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectCinematicScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_cmdHasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), uiCommands.cinematic.requestedFeature );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicFeature, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            ApplyCinematicUIParam( cinematic, SceneState(), uiCommands.cinematic.requestedParam, uiCommands.cinematic.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyCinematicParam, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetScene )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( false, true, false );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            LoadDemoSceneFromUI();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::LoadDemoScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.createScene )
        {
            CreateSceneFromUI( uiCommands.scene.requestedSceneName );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CreateScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            LoadSceneFromBrowserIndex( uiCommands.scene.requestedSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectScene, RuntimeInputActionSource::UI );
        }

        RunUIStressActions();

        const bool editorViewportLookNow = m_editor.editorModeEnabled && Input::IsRightMouseDown() && !m_UI.BlocksCameraMouse();
        if ( editorViewportLookNow != m_editor.viewportLookActive )
        {
            InputController::ResetMouseLook( m_camera );
        }
        m_editor.viewportLookActive = editorViewportLookNow;
        if ( editorViewportLookNow != ( m_runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook ) )
        {
            UpdateRuntimeInputModeAfterAction( editorViewportLookNow ? RuntimeInputAction::BeginEditorViewportLook : RuntimeInputAction::EndEditorViewportLook,
                                               RuntimeInputActionSource::Mouse );
        }

        const int placementWheelSteps = EditorMouseWheelSteps( editorUnhandledWheelDelta );
        const bool placementLeftMouseNow = Input::IsLeftMouseDown();
        if ( m_editor.placementScaleActive &&
             placementLeftMouseNow &&
             !m_editor.viewportLookActive &&
             !m_UI.BlocksCameraMouse() )
        {
            if ( placementWheelSteps != 0 )
            {
                EnterInteractiveSceneRun();
                m_editor.placementScaleWheelSteps += placementWheelSteps;
            }

            const POINT currentClient = Input::GetClientMouseCoordinates();
            const float dragPixelsX = static_cast<float>( currentClient.x - m_editor.placementScaleStartClient.x );
            const float dragPixelsY = static_cast<float>( currentClient.y - m_editor.placementScaleStartClient.y );
            m_editor.placementScale = EditorPlacementScaleFromGesture( m_editor.objectType,
                                                                       m_editor.placementScaleStart,
                                                                       dragPixelsX,
                                                                       dragPixelsY,
                                                                       m_editor.placementScaleWheelSteps );
        }
        else if ( placementWheelSteps != 0 &&
                  m_editor.editorModeEnabled &&
                  m_editor.placementModeEnabled &&
                  !m_editor.viewportLookActive &&
                  !m_UI.BlocksCameraMouse() )
        {
            const int nextAltitudeSteps = (std::max)( 0, m_editor.placementAltitudeSteps + placementWheelSteps );
            if ( nextAltitudeSteps != m_editor.placementAltitudeSteps )
            {
                EnterInteractiveSceneRun();
                m_editor.placementAltitudeSteps = nextAltitudeSteps;
            }
        }
        ApplyCursorOwnership();
    }

    UpdateEditorInteractionPreview();

    // Editor and launcher actions share world clicks. UI hover/capture
    // suppresses both so panel interaction never mutates the scene.
    {
        const RuntimeMouseEdges mouseEdges = m_runtimeInput.CaptureMouseButtons( Input::IsLeftMouseDown(), Input::IsRightMouseDown() );
        const bool leftMouseNow = mouseEdges.leftDown;
        const bool leftPressed = mouseEdges.leftPressed;
        const bool leftReleased = mouseEdges.leftReleased;
        bool consumedWorldClick = false;

        if ( m_editor.placementScaleActive )
        {
            consumedWorldClick = true;
            if ( leftReleased || suppressWorldActionThisFrame )
            {
                if ( leftReleased && !suppressWorldActionThisFrame && m_editor.placementPreviewVisible )
                {
                    const int previousModelCount = m_cGameModelCollection.GetModelCount();
                    PlaceEditorObjectAtTerrainPoint( m_editor.objectType, m_editor.placeStaticObject, m_editor.placementTerrainPoint );
                    if ( m_cGameModelCollection.GetModelCount() > previousModelCount )
                    {
                        m_editor.selectedModelIndex = m_cGameModelCollection.GetModelCount() - 1;
                    }
                }
                m_editor.placementScaleActive = false;
                m_editor.placementScaleWheelSteps = 0;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorPlacementScale, RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && m_editor.gizmoDragActive )
        {
            consumedWorldClick = true;
            if ( leftMouseNow && !suppressWorldActionThisFrame )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    if ( m_editor.gizmoDragIsScale )
                    {
                        ScaleSelectedEditorObjectAlongAxis( rayOrigin, rayDirection );
                    }
                    else if ( m_editor.gizmoDragIsRotation )
                    {
                        RotateSelectedEditorObjectAroundAxis( rayOrigin, rayDirection );
                    }
                    else
                    {
                        MoveSelectedEditorObjectAlongAxis( rayOrigin, rayDirection );
                    }
                }
            }
            if ( leftReleased || suppressWorldActionThisFrame )
            {
                m_editor.gizmoDragActive = false;
                m_editor.gizmoDragIsRotation = false;
                m_editor.gizmoDragIsScale = false;
                m_editor.activeGizmoAxis = -1;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorGizmoDrag, RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame )
        {
            const bool editorScaleMode = m_editor.editorModeEnabled &&
                                         !m_editor.placementModeEnabled &&
                                         Input::IsKeyDown( VK_CONTROL );
            if ( editorScaleMode &&
                 m_editor.selectedModelIndex >= 0 &&
                 m_editor.selectedModelIndex < m_cGameModelCollection.GetModelCount() &&
                 m_editor.hotGizmoAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float axisT = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorAxisRayParameter( m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    GameModel& model = m_cGameModelCollection.GetModelAtIndex( m_editor.selectedModelIndex );
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = false;
                    m_editor.gizmoDragIsScale = true;
                    m_editor.activeGizmoAxis = m_editor.hotGizmoAxis;
                    m_editor.gizmoDragStartAxisT = axisT;
                    m_editor.gizmoDragStartShape = model.GetCollisionShape();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoScale, RuntimeInputActionSource::Mouse );
                }
            }

            if ( m_editor.editorModeEnabled &&
                 !m_editor.placementModeEnabled &&
                 !editorScaleMode &&
                 m_editor.selectedModelIndex >= 0 &&
                 m_editor.hotRotationAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float startAngle = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorRotationRayAngle( m_editor.hotRotationAxis, rayOrigin, rayDirection, startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = true;
                    m_editor.gizmoDragIsScale = false;
                    m_editor.activeGizmoAxis = m_editor.hotRotationAxis;
                    m_editor.gizmoDragStartRotationAngle = startAngle;
                    m_editor.gizmoDragStartOrientation = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetOrientation();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoRotate, RuntimeInputActionSource::Mouse );
                }
            }

            if ( !consumedWorldClick &&
                 m_editor.editorModeEnabled &&
                 !m_editor.placementModeEnabled &&
                 !editorScaleMode &&
                 m_editor.selectedModelIndex >= 0 &&
                 m_editor.hotGizmoAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float axisT = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorAxisRayParameter( m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = false;
                    m_editor.gizmoDragIsScale = false;
                    m_editor.activeGizmoAxis = m_editor.hotGizmoAxis;
                    m_editor.gizmoDragStartAxisT = axisT;
                    m_editor.gizmoDragStartPosition = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetPosition();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoTranslate, RuntimeInputActionSource::Mouse );
                }
            }

            if ( !consumedWorldClick && m_editor.editorModeEnabled )
            {
                if ( m_editor.placementModeEnabled )
                {
                    if ( m_editor.placementPreviewVisible )
                    {
                        m_editor.placementScaleActive = true;
                        m_editor.placementScaleWheelSteps = 0;
                        m_editor.placementScaleStart = EditorClampPlacementScale( m_editor.objectType, m_editor.placementScale );
                        m_editor.placementScale = m_editor.placementScaleStart;
                        m_editor.placementScaleStartClient = Input::GetClientMouseCoordinates();
                        m_editor.placementScaleTerrainPoint = m_editor.placementTerrainPoint;
                        m_editor.placementScaleRayOrigin = m_editor.placementRayOrigin;
                        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorPlacementScale, RuntimeInputActionSource::Mouse );
                    }
                }
                else
                {
                    Vector3 rayOrigin;
                    Vector3 rayDirection;
                    int pickedIndex = -1;
                    if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                         TryPickEditorModel( rayOrigin, rayDirection, pickedIndex ) )
                    {
                        m_editor.selectedModelIndex = pickedIndex;
                    }
                    else
                    {
                        m_editor.selectedModelIndex = -1;
                    }
                }
                consumedWorldClick = true;
            }
        }

        if ( !consumedWorldClick &&
             leftPressed &&
             !suppressWorldActionThisFrame &&
             !m_editor.editorModeEnabled &&
             !m_UI.WantsNativeMouseCursor() &&
             ( Input::IsKeyDown( VK_CONTROL ) || !m_camera.isLauncherMode ) )
        {
            const bool additiveReplayPick = Input::IsKeyDown( VK_SHIFT );
            TryPickReplayPathTargetFromMouse( additiveReplayPick, !additiveReplayPick );
            consumedWorldClick = true;
        }

        if ( !consumedWorldClick &&
             m_camera.isLauncherMode &&
             leftPressed &&
             !suppressWorldActionThisFrame &&
             !m_UI.WantsNativeMouseCursor() )
        {
            EnterInteractiveSceneRun();
            FireRayCastTest();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::FireLauncher, RuntimeInputActionSource::Mouse );
        }
    }

    if ( m_UI.BlocksKeyboard() )
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    // F2: Save scene snapshot to Scenes/
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::SaveSceneSnapshot, VK_F2 ) )
        {
            static int sSnapshotSeq = 0;
            char path[256] = {};
            if ( RuntimeFileWriter::NextNumberedPath( path, sizeof( path ), "Scenes", "snapshot_", ".scene.json", sSnapshotSeq, 100 ) )
            {
                m_cGameModelCollection.SaveSceneSnapshot(
                    path,
                    SceneState().isScenePhysics,
                    SceneState().isSceneText,
                    m_cWorldEnvironment,
                    m_systems.cameras->GetCameraTranslation(),
                    m_systems.cameras->GetCameraView(),
                    m_systems.cameras->GetCameraUp() );
            }
        }
    }

    // F3: Save screenshot to Screenshots/
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::SaveScreenshot, VK_F3 ) )
        {
            static int sScreenshotSeq = 0;
            char path[256] = {};
            if ( RuntimeFileWriter::NextNumberedPath( path, sizeof( path ), "Screenshots", "screenshot_", ".bmp", sScreenshotSeq, 100 ) )
            {
                SaveScreenshot( path );
            }
        }
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ResetScene, 'R' ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }
    if ( SceneState().isSceneMode )
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ResetSceneFromBackspace, VK_BACK ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }

    const bool cameraMouseLookActive = ( !m_editor.editorModeEnabled &&
                                         m_camera.isFlyMode &&
                                         ( !ReplayInspectionActive() || ReplayInspectionMouseLookActive() ) ) ||
                                       m_editor.viewportLookActive;
    const bool cameraKeyboardControlsActive = m_camera.isFlyMode || m_editor.viewportLookActive;
    if ( cameraMouseLookActive )
    {
        // Diagnostics UI owns the native cursor; mouse-look hides it while
        // consuming raw Win32 deltas, with cursor-position deltas as a
        // remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            InputController::ResetMouseLook( m_camera );
        }
        else if ( !MouseLookOwnsCursor() )
        {
            ApplyCursorOwnership();
            InputController::ResetMouseLook( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                InputController::SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                InputController::SetMouseLookDelta( m_camera,
                                                    currentClient.x - m_camera.mouseLookLastClient.x,
                                                    currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }

    if ( cameraKeyboardControlsActive )
    {
        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }
}


void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode || m_editor.viewportLookActive )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( ( !m_editor.editorModeEnabled || m_editor.viewportLookActive ) &&
             ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 ) )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_editor.viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}


void SkullbonezRun::ClearRayCastTestLines()
{
    m_rayCastTest.lines = {};
    m_rayCastTest.nextLine = 0;
}


void SkullbonezRun::AddRayCastTestLine( const Vector3& start, const Vector3& end, bool hit )
{
    if ( !m_rayCastTest.visualizeRays )
    {
        return;
    }

    RunRayCastTestLine& line = m_rayCastTest.lines[static_cast<std::size_t>( m_rayCastTest.nextLine ) % RunRayCastTestState::MAX_LINES];
    line.start = start;
    line.end = end;
    line.ageSeconds = 0.0f;
    line.active = true;
    line.hit = hit;
    m_rayCastTest.nextLine = ( m_rayCastTest.nextLine + 1 ) % static_cast<int>( RunRayCastTestState::MAX_LINES );
}


void SkullbonezRun::TickRayCastTestLines( float dt )
{
    if ( dt <= 0.0f )
    {
        return;
    }

    for ( RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active )
        {
            line.ageSeconds += dt;
        }
    }
}


bool SkullbonezRun::TryRayCastTestHit( const Vector3& rayOrigin, const Vector3& rayDirection, float maxDistance, int& outIndex, float& outT )
{
    outIndex = -1;
    outT = maxDistance;

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameModel& model = models[static_cast<size_t>( i )];
        const float radius = EditorModelRadius( model );
        float rayT = 0.0f;
        if ( IntersectRaySphere( rayOrigin, rayDirection, model.GetPosition(), radius, rayT ) &&
             rayT <= maxDistance &&
             rayT < outT )
        {
            outIndex = i;
            outT = rayT;
        }
    }

    return outIndex >= 0;
}


bool SkullbonezRun::TryLauncherTerrainHit( const Vector3& rayOrigin, const Vector3& rayDirection, float maxDistance, float& outT ) const
{
    outT = maxDistance;
    if ( !m_systems.terrain )
    {
        return false;
    }

    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = maxDistance * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Vector3 sample = rayOrigin + rayDirection * t;
        if ( !m_systems.terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = m_systems.terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outT = t;
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;
            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Vector3 mid = rayOrigin + rayDirection * midT;
                if ( !m_systems.terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = m_systems.terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;
                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }
            outT = highT;
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}


void SkullbonezRun::FireRayCastTest()
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const Vector3 rayOrigin = m_systems.cameras->GetCameraTranslation();
    Vector3 rayDirection = m_systems.cameras->GetCameraView() - rayOrigin;
    const float dirLenSq = VectorMagSquared( rayDirection );
    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return;
    }
    rayDirection = rayDirection * ( 1.0f / sqrtf( dirLenSq ) );

    if ( m_rayCastTest.fireMode == RunLauncherFireMode::Projectile )
    {
        FireLauncherProjectile( rayOrigin, rayDirection );
        return;
    }

    FireLauncherLaser( rayOrigin, rayDirection );
}


void SkullbonezRun::FireLauncherLaser( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, modelHitIndex, modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit = TryLauncherTerrainHit( rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

    const bool terrainIsClosest = terrainHit && ( !modelHit || terrainHitT < modelHitT );
    const bool hit = modelHit || terrainHit;
    const float hitT = terrainIsClosest ? terrainHitT : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Vector3 visualEnd = rayOrigin + rayDirection * hitT;
    m_launcherLaser.Fire( rayOrigin,
                          rayDirection,
                          m_systems.cameras->GetCameraUp(),
                          hitT,
                          hit );
    AddRayCastTestLine( rayOrigin, visualEnd, hit );

    if ( terrainIsClosest || !modelHit || modelHitIndex < 0 || modelHitIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelHitIndex );
    bool releasedFromFixed = false;
    if ( model.IsFixed() )
    {
        if ( !model.ReleasesFromFixedOnContact() ||
             m_rayCastTest.impulseStrength < model.GetContactReleaseImpulseThreshold() )
        {
            return;
        }
        model.SetFixed( false );
        releasedFromFixed = true;
    }

    const Vector3 hitPoint = rayOrigin + rayDirection * hitT;
    model.SetImpulseForce( rayDirection * m_rayCastTest.impulseStrength, hitPoint - model.GetPosition() );
    m_cGameModelCollection.WakeModel( modelHitIndex );
    if ( releasedFromFixed )
    {
        const float mass = (std::max)( 0.001f, model.GetMass() );
        const float releaseSpeed = std::clamp( m_rayCastTest.impulseStrength / mass, 1.5f, 36.0f );
        m_cGameModelCollection.ReleaseAttachedFixedTreeParts( modelHitIndex, rayDirection * releaseSpeed, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    }
}


void SkullbonezRun::FireLauncherProjectile( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( !m_systems.terrain || m_cGameModelCollection.GetModelCount() >= ActiveGameModelCapacity() )
    {
        return;
    }

    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, modelHitIndex, modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit = TryLauncherTerrainHit( rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

    const float hitT = terrainHit && ( !modelHit || terrainHitT < modelHitT ) ? terrainHitT : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Vector3 aimPoint = rayOrigin + rayDirection * hitT;
    const Vector3 cameraUp = m_systems.cameras ? m_systems.cameras->GetCameraUp() : Vector3( 0.0f, 1.0f, 0.0f );
    Vector3 up = cameraUp;
    const float upLenSq = VectorMagSquared( up );
    up = upLenSq > TOLERANCE * TOLERANCE ? up * ( 1.0f / sqrtf( upLenSq ) ) : Vector3( 0.0f, 1.0f, 0.0f );
    const Vector3 spawn = rayOrigin + rayDirection * LAUNCHER_PROJECTILE_SPAWN_LEAD - up * LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET;
    Vector3 velocityDir = aimPoint - spawn;
    const float velocityDirLenSq = VectorMagSquared( velocityDir );
    if ( velocityDirLenSq <= TOLERANCE * TOLERANCE )
    {
        velocityDir = rayDirection;
    }
    else
    {
        velocityDir = velocityDir * ( 1.0f / sqrtf( velocityDirLenSq ) );
    }

    const float moment = 0.4f * LAUNCHER_PROJECTILE_MASS * LAUNCHER_PROJECTILE_RADIUS * LAUNCHER_PROJECTILE_RADIUS;
    GameModel projectile( &m_cWorldEnvironment, spawn, Vector3( moment, moment, moment ), LAUNCHER_PROJECTILE_MASS );
    projectile.SetTerrain( m_systems.terrain.get() );
    projectile.SetCoefficientRestitution( LAUNCHER_PROJECTILE_RESTITUTION );
    projectile.AddBoundingSphere( LAUNCHER_PROJECTILE_RADIUS );
    projectile.SetLinearVelocity( velocityDir * m_rayCastTest.projectileSpeed );
    projectile.SetRenderTint( 0.72f, 0.88f, 1.0f, 1.0f );
    projectile.SetName( "launcher_projectile" );

    const int projectileIndex = m_cGameModelCollection.GetModelCount();
    m_cGameModelCollection.AddGameModel( std::move( projectile ) );
    m_cGameModelCollection.WakeModel( projectileIndex );
    SceneState().modelCount = m_cGameModelCollection.GetModelCount();
}


bool SkullbonezRun::TryBuildMouseWorldRay( Vector3& outOrigin, Vector3& outDirection ) const
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


bool SkullbonezRun::TryGetMouseTerrainPlacement( Vector3& outPosition ) const
{
    return TryGetMouseTerrainPlacement( outPosition, nullptr, nullptr );
}


bool SkullbonezRun::TryGetMouseTerrainPlacement( Vector3& outPosition, Vector3* outRayOrigin, Vector3* outRayDirection ) const
{
    if ( !m_systems.terrain )
    {
        return false;
    }

    Vector3 rayNear;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayNear, rayDirection ) )
    {
        return false;
    }
    if ( outRayOrigin )
    {
        *outRayOrigin = rayNear;
    }
    if ( outRayDirection )
    {
        *outRayDirection = rayDirection;
    }

    constexpr float MAX_RAY_DISTANCE = 5000.0f;
    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = MAX_RAY_DISTANCE * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Vector3 sample = rayNear + rayDirection * t;
        if ( !m_systems.terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = m_systems.terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outPosition = Vector3( sample.x, terrainY, sample.z );
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
                const Vector3 mid = rayNear + rayDirection * midT;
                if ( !m_systems.terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = m_systems.terrain->GetTerrainHeightAt( mid.x, mid.z );
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
            outPosition = Vector3( hit.x, hitY, hit.z );
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}


bool SkullbonezRun::TryComputeEditorObjectCenter( int objectType, const Vector3& terrainPoint, const Vector3& placementScale, const Quaternion& orientation, Vector3& outCenter ) const
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
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + scale.x + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
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
    default:
    {
        ConvexHullShape hull;
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return false;
        }
        const Vector3 authoredOrigin = terrainPoint + rotation * Vector3( 0.0f, HullAuthoredBottomOffset( hull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        outCenter = authoredOrigin + rotation * hull.GetAuthoredCenterOfMass();
        return true;
    }
    }
}


bool SkullbonezRun::TryComputeEditorPlacementPreview( int objectType )
{
    Vector3 terrainPoint;
    Vector3 rayOrigin;
    Vector3 rayDirection;
    bool terrainAlreadyIncludesAltitude = false;
    if ( m_editor.placementScaleActive )
    {
        terrainPoint = m_editor.placementScaleTerrainPoint;
        rayOrigin = m_editor.placementScaleRayOrigin;
        terrainAlreadyIncludesAltitude = true;
    }
    else
    {
        if ( !TryGetMouseTerrainPlacement( terrainPoint, &rayOrigin, &rayDirection ) )
        {
            return false;
        }

        if ( m_systems.terrain && EDITOR_PLACEMENT_SNAP > 0.0f )
        {
            const float snappedX = roundf( terrainPoint.x / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            const float snappedZ = roundf( terrainPoint.z / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            if ( m_systems.terrain->IsInBounds( snappedX, snappedZ ) )
            {
                terrainPoint.x = snappedX;
                terrainPoint.z = snappedZ;
                terrainPoint.y = m_systems.terrain->GetTerrainHeightAt( snappedX, snappedZ );
            }
        }
    }

    if ( !terrainAlreadyIncludesAltitude )
    {
        terrainPoint.y += static_cast<float>( m_editor.placementAltitudeSteps ) *
                          EditorPlacementAltitudeStepSize( objectType, m_editor.placementScale );
    }

    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( m_systems.terrain && m_systems.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        m_systems.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const Quaternion placementOrientation = EditorOrientationFromTerrainNormal( objectType, terrainNormal, m_editor.autoTerrainAlign );

    Vector3 center;
    if ( !TryComputeEditorObjectCenter( objectType, terrainPoint, m_editor.placementScale, placementOrientation, center ) )
    {
        return false;
    }

    m_editor.placementTerrainPoint = terrainPoint;
    m_editor.placementCenter = center;
    m_editor.placementOrientation = placementOrientation;
    m_editor.placementRayOrigin = rayOrigin;
    m_editor.placementRayHit = terrainPoint;
    return true;
}


bool SkullbonezRun::TryPickEditorModel( const Vector3& rayOrigin, const Vector3& rayDirection, int& outIndex ) const
{
    outIndex = -1;
    float bestT = FLT_MAX;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameModel& model = models[i];
        const float radius = EditorModelRadius( model ) + 1.0f;
        const Vector3 toCenter = model.GetPosition() - rayOrigin;
        const float rayT = toCenter * rayDirection;
        if ( rayT < 0.0f || rayT >= bestT )
        {
            continue;
        }

        const Vector3 closest = rayOrigin + rayDirection * rayT;
        if ( VectorMagSquared( model.GetPosition() - closest ) <= radius * radius )
        {
            bestT = rayT;
            outIndex = i;
        }
    }
    return outIndex >= 0;
}


void SkullbonezRun::ClearReplayPathVisualizer()
{
    m_replayPathVisualizer.hasTarget = false;
    m_replayPathVisualizer.targetId = ReplayBodyId{};
    m_replayPathVisualizer.targetModelIndex = -1;
    m_replayPathVisualizer.targetName[0] = '\0';
    m_replayPathVisualizer.futureNodes.clear();
    m_replayPathVisualizer.targets.clear();
    ClearReplayPredictionCache();
    MarkReplayPredictionDirty();
}


void SkullbonezRun::MarkReplayPredictionDirty()
{
    CancelReplayPredictionJob( true );
    m_replayPrediction.dirty = true;
}


void SkullbonezRun::ClearReplayPredictionCache()
{
    CancelReplayPredictionJob( true );
    m_replayPrediction.targetId = ReplayBodyId{};
    m_replayPrediction.sourceFrameIndex = 0;
    m_replayPrediction.sourceSolverHash = 0;
    m_replayPrediction.lastBuildTime = 0.0;
}


bool SkullbonezRun::BuildReplayCauseTreeRows()
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_replayCauseTree.rowCount = 0;

    if ( !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 )
    {
        return false;
    }

    const bool usePrediction = m_replayPrediction.enabled &&
                               m_replayPrediction.frames.size() >= 2 &&
                               m_replayPrediction.targetId.value == m_replayPathVisualizer.targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes = usePrediction ? m_replayPrediction.futureNodes : m_replayPathVisualizer.futureNodes;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();

    auto modelIndexForId = [&]( ReplayBodyId id ) -> int
    {
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
            {
                return i;
            }
        }
        return -1;
    };

    auto writeName = [&]( ReplayBodyId id, int modelIndex, const char* fallback, char* out, std::size_t outSize ) -> void
    {
        out[0] = '\0';
        if ( fallback && fallback[0] != '\0' )
        {
            strncpy_s( out, outSize, fallback, _TRUNCATE );
            return;
        }
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( models.size() ) )
        {
            const char* modelName = models[static_cast<std::size_t>( modelIndex )].GetName();
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( out, outSize, modelName, _TRUNCATE );
                return;
            }
        }
        if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
            {
                if ( body->name[0] != '\0' )
                {
                    strncpy_s( out, outSize, body->name, _TRUNCATE );
                    return;
                }
            }
        }
        sprintf_s( out, outSize, "body_%u", id.value );
    };

    auto addRow = [&]( ReplayBodyId id, ReplayBodyId parentId, ReplayFrameIndex firstFrame, int depth, int modelIndex, const char* fallbackName ) -> bool
    {
        if ( id.value == 0 || m_replayCauseTree.rowCount >= REPLAY_CAUSE_TREE_MAX_ROWS )
        {
            return false;
        }

        RunReplayCauseTreeRow& row = m_replayCauseTree.rows[static_cast<std::size_t>( m_replayCauseTree.rowCount++ )];
        row = RunReplayCauseTreeRow{};
        row.id = id;
        row.parentId = parentId;
        row.firstFrame = firstFrame;
        row.depth = depth;
        row.modelIndex = modelIndex >= 0 ? modelIndex : modelIndexForId( id );
        row.prediction = usePrediction;
        writeName( id, row.modelIndex, fallbackName, row.name, sizeof( row.name ) );
        return true;
    };

    addRow( m_replayPathVisualizer.targetId,
            ReplayBodyId{},
            0,
            0,
            m_replayPathVisualizer.targetModelIndex,
            m_replayPathVisualizer.targetName );

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addRow( node.id, parentId, node.firstFrame, depth, modelIndexForId( node.id ), nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_replayPathVisualizer.targetId, 1 );

    return m_replayCauseTree.rowCount > 0;
}


bool SkullbonezRun::TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id, Vector3& outPosition ) const
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( m_replayPrediction.enabled &&
         !m_replayPrediction.frames.empty() &&
         m_replayPrediction.targetId.value == m_replayPathVisualizer.targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body = FindReplayPredictionBodyById( m_replayPrediction.frames.front(), id ) )
        {
            outPosition = body->position;
            return true;
        }
    }

    if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
    {
        if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
        {
            outPosition = body->position;
            return true;
        }
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( const GameModel& model : models )
    {
        if ( model.GetReplayBodyId() == id.value )
        {
            outPosition = model.GetPosition();
            return true;
        }
    }
    return false;
}


bool SkullbonezRun::FocusReplayCauseTreeBody( ReplayBodyId id )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
    Vector3 targetPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    if ( !TryResolveReplayCauseTreeBodyPosition( id, targetPosition ) )
    {
        return false;
    }

    EnterInteractiveSceneRun();
    if ( !m_replayScrubber.simulationPaused )
    {
        SetReplaySimulationPaused( true );
    }
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SetViewCoordinates( targetPosition );
        m_systems.cameras->ResetRelativity();
    }
    m_replayCauseTree.focusedId = id;
    InputController::ResetMouseLook( m_camera );
    Input::SetSystemCursorVisible( true );
    return true;
}


bool SkullbonezRun::TickReplayCauseTreeInput( bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayCauseTree.leftWasDown;
    m_replayCauseTree.leftWasDown = leftDown;
    m_replayCauseTree.hoveredRow = -1;

    if ( uiBlocksMouse ||
         m_editor.editorModeEnabled ||
         !m_UI.IsVisible() ||
         !m_UI.IsMinimized() ||
         WindowScreenWidth() <= 0 ||
         WindowScreenHeight() <= 0 ||
         !BuildReplayCauseTreeRows() )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    const UI::UIRect panel = ReplayCauseTreePanelRect( WindowScreenWidth(), WindowScreenHeight() );
    if ( !panel.Contains( mouse.x, mouse.y ) )
    {
        return false;
    }

    const int visibleRows = (std::min)( m_replayCauseTree.rowCount, ReplayCauseTreeVisibleRowCapacity( panel ) );
    for ( int rowIndex = 0; rowIndex < visibleRows; ++rowIndex )
    {
        const UI::UIRect rowRect = ReplayCauseTreeRowRect( panel, rowIndex );
        if ( rowRect.Contains( mouse.x, mouse.y ) )
        {
            m_replayCauseTree.hoveredRow = rowIndex;
            if ( leftPressed )
            {
                FocusReplayCauseTreeBody( m_replayCauseTree.rows[static_cast<std::size_t>( rowIndex )].id );
            }
            break;
        }
    }

    return true;
}


void SkullbonezRun::SetReplayVelocityEditEnabled( bool enabled )
{
    if ( m_replayVelocityEdit.enabled == enabled )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );
    m_replayVelocityEdit.enabled = enabled;
    m_replayVelocityEdit.hotLinearAxis = -1;
    m_replayVelocityEdit.hotAngularAxis = -1;
    m_replayVelocityEdit.activeAxis = -1;
    m_replayVelocityEdit.dragging = false;
    m_replayVelocityEdit.draggingAngular = false;
    if ( m_replayVelocityEdit.mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
        m_replayVelocityEdit.mouseCaptured = false;
    }

    if ( enabled )
    {
        EnterInteractiveSceneRun();
        SetReplaySimulationPaused( true );
        m_replayPrediction.enabled = true;
        m_replayPrediction.horizonSeconds = std::clamp( m_replayPrediction.horizonSeconds,
                                                        REPLAY_PREDICTION_MIN_SECONDS,
                                                        REPLAY_PREDICTION_MAX_SECONDS );
        MarkReplayPredictionDirty();
        m_replayScrubber.visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
    }
}


int SkullbonezRun::ResolveReplayVelocityEditModelIndex() const
{
    if ( !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int cachedIndex = m_replayPathVisualizer.targetModelIndex;
    if ( cachedIndex >= 0 &&
         cachedIndex < static_cast<int>( models.size() ) &&
         models[static_cast<std::size_t>( cachedIndex )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
    {
        return cachedIndex;
    }

    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
        {
            return i;
        }
    }
    return -1;
}


int SkullbonezRun::HitReplayVelocityLinearAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( model.GetVelocity(), axis );
        const Vector3 endpoint = origin + axisVector * ReplayVelocityLinearVisualAxisT( radius, component );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, endpoint );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int SkullbonezRun::HitReplayVelocityAngularAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float modelRadius = EditorModelRadius( model );
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

        const float ringRadius = ReplayVelocityAngularVisualRadius( modelRadius, ReplayVelocityAxisComponent( model.GetAngularVelocity(), axis ) );
        const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );
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


bool SkullbonezRun::TryReplayVelocityAxisRayParameter( int axis, const Vector3& rayOrigin, const Vector3& rayDirection, float& outAxisT ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 axisOrigin = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )].GetPosition();
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


bool SkullbonezRun::TryReplayVelocityAngularRayAngle( int axis, const Vector3& rayOrigin, const Vector3& rayDirection, float& outAngle ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 origin = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )].GetPosition();
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


void SkullbonezRun::ApplyReplayVelocityEditToModel( int modelIndex, const Vector3& linearVelocity, const Vector3& angularVelocity )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Apply" );
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    Vector3 clampedLinear = linearVelocity;
    Vector3 clampedAngular = angularVelocity;
    clampedLinear.x = std::clamp( clampedLinear.x, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.y = std::clamp( clampedLinear.y, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.z = std::clamp( clampedLinear.z, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedAngular.x = std::clamp( clampedAngular.x, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.y = std::clamp( clampedAngular.y, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.z = std::clamp( clampedAngular.z, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
    if ( model.IsFixed() )
    {
        return;
    }

    model.SetLinearVelocity( clampedLinear );
    model.SetAngularVelocity( clampedAngular );
    if ( VectorMagSquared( clampedLinear ) > TOLERANCE * TOLERANCE ||
         VectorMagSquared( clampedAngular ) > TOLERANCE * TOLERANCE )
    {
        m_cGameModelCollection.WakeModel( modelIndex );
    }
    m_cGameModelCollection.InvalidatePhysicsStreams();
    MarkReplayPredictionDirty();
    m_replayScrubber.visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayScrubber.visible = true;
}


void SkullbonezRun::ApplyReplayVelocityEditDrag( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() || m_replayVelocityEdit.activeAxis < 0 )
    {
        m_replayVelocityEdit.dragging = false;
        m_replayVelocityEdit.activeAxis = -1;
        return;
    }

    Vector3 linearVelocity = m_replayVelocityEdit.dragStartLinearVelocity;
    Vector3 angularVelocity = m_replayVelocityEdit.dragStartAngularVelocity;
    if ( m_replayVelocityEdit.draggingAngular )
    {
        float currentAngle = 0.0f;
        if ( !TryReplayVelocityAngularRayAngle( m_replayVelocityEdit.activeAxis, rayOrigin, rayDirection, currentAngle ) )
        {
            return;
        }
        const float angleDelta = WrapEditorAngleDelta( currentAngle - m_replayVelocityEdit.dragStartAngle );
        const float component = ReplayVelocityAxisComponent( m_replayVelocityEdit.dragStartAngularVelocity, m_replayVelocityEdit.activeAxis ) +
                                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
        ReplayVelocitySetAxisComponent( angularVelocity,
                                        m_replayVelocityEdit.activeAxis,
                                        std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
    }
    else
    {
        float axisT = 0.0f;
        if ( !TryReplayVelocityAxisRayParameter( m_replayVelocityEdit.activeAxis, rayOrigin, rayDirection, axisT ) )
        {
            return;
        }
        const float component = ReplayVelocityAxisComponent( m_replayVelocityEdit.dragStartLinearVelocity, m_replayVelocityEdit.activeAxis ) +
                                ( axisT - m_replayVelocityEdit.dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
        ReplayVelocitySetAxisComponent( linearVelocity,
                                        m_replayVelocityEdit.activeAxis,
                                        std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
    }

    ApplyReplayVelocityEditToModel( modelIndex, linearVelocity, angularVelocity );
}


bool SkullbonezRun::TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayVelocityEdit.leftWasDown;
    const bool leftReleased = !leftDown && m_replayVelocityEdit.leftWasDown;
    m_replayVelocityEdit.leftWasDown = leftDown;

    if ( !m_replayVelocityEdit.enabled ||
         m_editor.editorModeEnabled ||
         !SceneState().isScenePhysics ||
         WindowScreenWidth() <= 0 ||
         WindowScreenHeight() <= 0 )
    {
        m_replayVelocityEdit.hotLinearAxis = -1;
        m_replayVelocityEdit.hotAngularAxis = -1;
        if ( m_replayVelocityEdit.mouseCaptured && !leftDown )
        {
            UI::InputControl::EndMouseCapture();
            m_replayVelocityEdit.mouseCaptured = false;
        }
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        return m_replayVelocityEdit.dragging;
    }

    if ( m_replayVelocityEdit.dragging )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            ApplyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            m_replayVelocityEdit.dragging = false;
            m_replayVelocityEdit.draggingAngular = false;
            m_replayVelocityEdit.activeAxis = -1;
            if ( m_replayVelocityEdit.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayVelocityEdit.mouseCaptured = false;
            }
        }
        return true;
    }

    m_replayVelocityEdit.hotAngularAxis = uiBlocksMouse ? -1 : HitReplayVelocityAngularAxis( rayOrigin, rayDirection );
    m_replayVelocityEdit.hotLinearAxis = ( uiBlocksMouse || m_replayVelocityEdit.hotAngularAxis >= 0 ) ? -1 : HitReplayVelocityLinearAxis( rayOrigin, rayDirection );

    if ( !uiBlocksMouse && leftPressed )
    {
        const int modelIndex = ResolveReplayVelocityEditModelIndex();
        if ( modelIndex >= 0 && modelIndex < m_cGameModelCollection.GetModelCount() )
        {
            const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
            if ( m_replayVelocityEdit.hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( m_replayVelocityEdit.hotAngularAxis, rayOrigin, rayDirection, startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplaySimulationPaused( true );
                    m_replayPrediction.enabled = true;
                    m_replayVelocityEdit.dragging = true;
                    m_replayVelocityEdit.draggingAngular = true;
                    m_replayVelocityEdit.activeAxis = m_replayVelocityEdit.hotAngularAxis;
                    m_replayVelocityEdit.dragStartAngle = startAngle;
                    m_replayVelocityEdit.dragStartLinearVelocity = model.GetVelocity();
                    m_replayVelocityEdit.dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayVelocityEdit.mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayVelocityEdit.mouseCaptured = true;
                    }
                    return true;
                }
            }
            else if ( m_replayVelocityEdit.hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;
                if ( TryReplayVelocityAxisRayParameter( m_replayVelocityEdit.hotLinearAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplaySimulationPaused( true );
                    m_replayPrediction.enabled = true;
                    m_replayVelocityEdit.dragging = true;
                    m_replayVelocityEdit.draggingAngular = false;
                    m_replayVelocityEdit.activeAxis = m_replayVelocityEdit.hotLinearAxis;
                    m_replayVelocityEdit.dragStartAxisT = axisT;
                    m_replayVelocityEdit.dragStartLinearVelocity = model.GetVelocity();
                    m_replayVelocityEdit.dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayVelocityEdit.mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayVelocityEdit.mouseCaptured = true;
                    }
                    return true;
                }
            }
        }
    }

    return m_replayVelocityEdit.hotLinearAxis >= 0 || m_replayVelocityEdit.hotAngularAxis >= 0;
}


void SkullbonezRun::RenderReplayVelocityEditOverlay( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !m_replayVelocityEdit.enabled || m_editor.editorModeEnabled )
    {
        return;
    }

    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo( model,
                                   m_replayVelocityEdit.hotLinearAxis,
                                   m_replayVelocityEdit.hotAngularAxis,
                                   m_replayVelocityEdit.activeAxis,
                                   m_replayVelocityEdit.draggingAngular );
}


bool SkullbonezRun::TryPickReplayPathTargetFromMouse( bool additive, bool clearOnMiss )
{
    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( clearOnMiss )
        {
            ClearReplayPathVisualizer();
        }
        return false;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    ReplayBodyId pickedId;
    int pickedIndex = -1;
    char pickedName[64] = {};
    if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
    {
        float bestT = FLT_MAX;
        for ( const ReplaySolverBodySample& body : sample->bodies )
        {
            float radius = 1.0f;
            if ( body.modelIndex >= 0 && body.modelIndex < static_cast<int>( models.size() ) )
            {
                radius = EditorModelRadius( models[static_cast<std::size_t>( body.modelIndex )] ) + 1.0f;
            }
            float rayT = 0.0f;
            if ( IntersectRaySphere( rayOrigin, rayDirection, body.position, radius, rayT ) && rayT < bestT )
            {
                bestT = rayT;
                pickedId = body.id;
                pickedIndex = body.modelIndex;
                pickedName[0] = '\0';
                if ( body.name[0] != '\0' )
                {
                    strncpy_s( pickedName, sizeof( pickedName ), body.name, _TRUNCATE );
                }
            }
        }
    }
    else if ( TryPickEditorModel( rayOrigin, rayDirection, pickedIndex ) &&
              pickedIndex >= 0 &&
              pickedIndex < m_cGameModelCollection.GetModelCount() )
    {
        const GameModel& model = models[static_cast<std::size_t>( pickedIndex )];
        pickedId.value = model.GetReplayBodyId();
        const char* modelName = model.GetName();
        if ( modelName && modelName[0] != '\0' )
        {
            strncpy_s( pickedName, sizeof( pickedName ), modelName, _TRUNCATE );
        }
    }

    if ( pickedId.value != 0 )
    {
        if ( !additive )
        {
            m_replayPathVisualizer.targets.clear();
        }

        RunReplayPathTarget* target = FindReplayPathTarget( m_replayPathVisualizer, pickedId );
        if ( !target )
        {
            if ( m_replayPathVisualizer.targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                m_replayPathVisualizer.targets.erase( m_replayPathVisualizer.targets.begin() );
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            m_replayPathVisualizer.targets.push_back( nextTarget );
            target = &m_replayPathVisualizer.targets.back();
        }

        target->modelIndex = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyPrimaryReplayPathTarget( m_replayPathVisualizer, pickedId, pickedIndex, target->name );
        m_replayPathVisualizer.futureNodes.clear();
        ClearReplayPredictionCache();
        MarkReplayPredictionDirty();
        return true;
    }

    if ( clearOnMiss )
    {
        ClearReplayPathVisualizer();
    }
    return false;
}


void SkullbonezRun::CancelReplayPredictionJob( bool clearSamples )
{
    m_replayPrediction.building = false;
    m_replayPrediction.complete = false;
    m_replayPrediction.targetModelIndex = -1;
    m_replayPrediction.nextTick = 1;
    m_replayPrediction.targetTickCount = 0;
    m_replayPrediction.predictionBodies.clear();
    m_replayPrediction.liveRestoreBodies.clear();
    m_replayPrediction.predictionWorld = ReplaySolverWorldSnapshot();
    m_replayPrediction.liveRestoreWorld = ReplaySolverWorldSnapshot();
    if ( clearSamples )
    {
        m_replayPrediction.frames.clear();
        m_replayPrediction.futureNodes.clear();
    }
}


bool SkullbonezRun::CaptureReplayPredictionBodyState( std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    outBodies.clear();
    outBodies.reserve( models.size() );
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = model.GetReplayBodyId();
        backup.modelIndex = i;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        backup.linearVelocity = model.GetVelocity();
        backup.angularVelocity = model.GetAngularVelocity();
        backup.fixedContactHighlightSeconds = model.GetFixedContactHighlightSeconds();
        backup.fixed = model.IsFixed();
        outBodies.push_back( backup );
    }
    return true;
}


bool SkullbonezRun::ApplyReplayPredictionBodyState( const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyBodyState" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    if ( bodies.size() != models.size() )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        if ( backup.modelIndex < 0 || backup.modelIndex >= static_cast<int>( models.size() ) )
        {
            return false;
        }

        GameModel& model = models[static_cast<std::size_t>( backup.modelIndex )];
        if ( model.GetReplayBodyId() != backup.id.value )
        {
            return false;
        }

        model.SetFixed( backup.fixed );
        model.SetPosition( backup.position );
        model.SetOrientation( backup.orientation );
        model.SetLinearVelocity( backup.linearVelocity );
        model.SetAngularVelocity( backup.angularVelocity );
        model.SetFixedContactHighlightSeconds( backup.fixedContactHighlightSeconds );
    }
    return true;
}


void SkullbonezRun::CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    frame.bodies.reserve( models.size() );
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = i;
        body.position = model.GetPosition();
        frame.bodies.push_back( body );
    }
    frame.debugContacts = m_cGameModelCollection.GetPhysicsDebugContacts();
    m_replayPrediction.frames.push_back( std::move( frame ) );
}


bool SkullbonezRun::BeginReplayPredictionJob( ReplayFrameIndex sourceFrameIndex, uint64_t sourceSolverHash )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );
    CancelReplayPredictionJob( true );
    m_replayPrediction.targetId = m_replayPathVisualizer.targetId;
    m_replayPrediction.dirty = false;

    if ( !m_replayPrediction.enabled ||
         !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 ||
         !SceneState().isScenePhysics )
    {
        return false;
    }

    m_replayPrediction.sourceFrameIndex = sourceFrameIndex;
    m_replayPrediction.sourceSolverHash = sourceSolverHash;
    m_replayPrediction.lastBuildTime = m_timers.simulationTimer.GetTotalTime();

    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    int targetIndex = -1;
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
        {
            targetIndex = i;
            break;
        }
    }
    if ( targetIndex < 0 )
    {
        return false;
    }
    m_replayPrediction.targetModelIndex = targetIndex;
    m_replayPathVisualizer.targetModelIndex = targetIndex;

    m_replayPrediction.horizonSeconds = std::clamp( m_replayPrediction.horizonSeconds,
                                                    REPLAY_PREDICTION_MIN_SECONDS,
                                                    REPLAY_PREDICTION_MAX_SECONDS );
    const int predictionTicks = (std::max)( 1, static_cast<int>( std::ceil( m_replayPrediction.horizonSeconds / PHYSICS_FIXED_DT ) ) );
    m_replayPrediction.targetTickCount = predictionTicks;
    m_replayPrediction.nextTick = 1;
    m_replayPrediction.frames.reserve( static_cast<std::size_t>( predictionTicks + 1 ) );

    if ( !CaptureReplayPredictionBodyState( m_replayPrediction.predictionBodies ) )
    {
        CancelReplayPredictionJob( true );
        return false;
    }

    m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
    CaptureReplayPredictionFrame( 0 );
    m_replayPrediction.building = true;

    return !m_replayPrediction.frames.empty();
}


bool SkullbonezRun::StepReplayPredictionJob( double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );
    if ( !m_replayPrediction.building )
    {
        return m_replayPrediction.complete;
    }

    const auto sliceStart = std::chrono::steady_clock::now();
    if ( !CaptureReplayPredictionBodyState( m_replayPrediction.liveRestoreBodies ) )
    {
        CancelReplayPredictionJob( true );
        m_replayPrediction.dirty = true;
        return false;
    }
    m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.liveRestoreWorld );

#ifdef _DEBUG
    const bool previousDiagnosticsSuppressed = m_cGameModelCollection.SetPhysicsDiagnosticsSuppressed( true );
#endif

    bool jobApplied = false;
    bool jobStateCaptured = false;
    bool progressed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyJobState" );
        jobApplied = ApplyReplayPredictionBodyState( m_replayPrediction.predictionBodies ) &&
                     m_cGameModelCollection.RestoreReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( jobApplied )
    {
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
            while ( m_replayPrediction.nextTick <= m_replayPrediction.targetTickCount )
            {
                {
                    PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                    m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                }
                CaptureReplayPredictionFrame( static_cast<ReplayFrameIndex>( m_replayPrediction.nextTick ) );
                ++m_replayPrediction.nextTick;
                progressed = true;

                const double elapsedMilliseconds =
                    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - sliceStart ).count();
                if ( elapsedMilliseconds >= budgetMilliseconds )
                {
                    break;
                }
            }
        }

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
            jobStateCaptured = CaptureReplayPredictionBodyState( m_replayPrediction.predictionBodies );
            if ( jobStateCaptured )
            {
                m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
            }
        }
    }

#ifdef _DEBUG
    m_cGameModelCollection.SetPhysicsDiagnosticsSuppressed( previousDiagnosticsSuppressed );
#endif

    bool liveRestored = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/RestoreLive" );
        liveRestored = ApplyReplayPredictionBodyState( m_replayPrediction.liveRestoreBodies ) &&
                       m_cGameModelCollection.RestoreReplaySolverWorldSnapshot( m_replayPrediction.liveRestoreWorld );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( !jobApplied || !jobStateCaptured || !liveRestored )
    {
        CancelReplayPredictionJob( true );
        m_replayPrediction.dirty = true;
        return false;
    }

    if ( m_replayPrediction.nextTick > m_replayPrediction.targetTickCount )
    {
        m_replayPrediction.building = false;
        m_replayPrediction.complete = true;
        m_replayPrediction.lastBuildTime = m_timers.simulationTimer.GetTotalTime();
    }

    return progressed || m_replayPrediction.complete;
}


bool SkullbonezRun::BuildReplayFocusModelMask()
{
    PROFILE_SCOPED( "Frame/Replay/FocusMask" );
    const int modelCount = m_cGameModelCollection.GetModelCount();
    if ( !m_replayPathVisualizer.hasTarget || m_replayPathVisualizer.targetId.value == 0 || modelCount <= 0 )
    {
        m_replayFocusModelMask.clear();
        return false;
    }

    m_replayFocusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    int markedCount = 0;
    const auto markByReplayId = [&]( ReplayBodyId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        int resolvedIndex = -1;
        if ( preferredModelIndex >= 0 &&
             preferredModelIndex < modelCount &&
             models[static_cast<std::size_t>( preferredModelIndex )].GetReplayBodyId() == id.value )
        {
            resolvedIndex = preferredModelIndex;
        }
        else
        {
            for ( int i = 0; i < modelCount; ++i )
            {
                if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
                {
                    resolvedIndex = i;
                    break;
                }
            }
        }

        if ( resolvedIndex >= 0 )
        {
            uint8_t& mask = m_replayFocusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_replayPathVisualizer.targets.empty() )
    {
        markByReplayId( m_replayPathVisualizer.targetId, m_replayPathVisualizer.targetModelIndex );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_replayPathVisualizer.targets )
        {
            markByReplayId( target.id, target.modelIndex );
        }
    }

    const std::vector<RunReplayPathTraceNode>& futureNodes = m_replayPrediction.enabled ? m_replayPrediction.futureNodes : m_replayPathVisualizer.futureNodes;
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, -1 );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_replayFocusModelMask.clear();
        return false;
    }
    return true;
}


void SkullbonezRun::RenderReplayPredictionVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    if ( !m_replayPrediction.enabled ||
         !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 )
    {
        if ( m_replayPrediction.building )
        {
            CancelReplayPredictionJob( true );
        }
        return;
    }

    const ReplaySolverFrameSample* latest = m_solverReplay.LatestSample();
    const ReplayFrameIndex latestFrame = latest ? latest->frameIndex : 0;
    const uint64_t latestHash = latest ? latest->solverHash : 0;
    const double now = m_timers.simulationTimer.GetTotalTime();
    const bool sourceChanged = m_replayPrediction.targetId.value != m_replayPathVisualizer.targetId.value ||
                               m_replayPrediction.sourceFrameIndex != latestFrame ||
                               m_replayPrediction.sourceSolverHash != latestHash;
    const bool refreshDue = ( now - m_replayPrediction.lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool allowAutomaticRefresh = !m_replayScrubber.simulationPaused;
    if ( m_replayPrediction.dirty || ( allowAutomaticRefresh && !m_replayPrediction.building && sourceChanged && refreshDue ) )
    {
        BeginReplayPredictionJob( latestFrame, latestHash );
    }
    if ( m_replayPrediction.building )
    {
        StepReplayPredictionJob( REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    }

    if ( m_replayPrediction.frames.size() < 2 )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        m_replayPrediction.futureNodes.clear();
        ReplayPredictionFutureContext futureContext;
        futureContext.prediction = &m_replayPrediction;
        futureContext.rootId = m_replayPathVisualizer.targetId;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
            BuildReplayPredictionFutureNodes( frame, futureContext );
        }
    }

    const ReplayFrameIndex lastFrame = m_replayPrediction.frames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( m_replayPrediction.frames.size() );
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }
            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyById( frame, m_replayPathVisualizer.targetId );
            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
                tracer.AddReplayPathSegment( previous, body->position, 1.0f - t * 0.85f, 1.0f, 1.0f - t * 0.72f );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.presentFrame = 0;
        childDraw.lastFrame = lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( m_replayPrediction.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = m_replayPrediction.futureNodes[i];
        }

        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
            const std::size_t currentOrdinal = ordinal++;
            bool importantChildFrame = frame.frameIndex == 0 || frame.frameIndex == lastFrame;
            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                if ( frame.frameIndex == childDraw.nodes[i].node.firstFrame )
                {
                    importantChildFrame = true;
                    break;
                }
            }
            if ( !importantChildFrame &&
                 !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }

            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                ReplayPathChildDrawState& drawState = childDraw.nodes[i];
                const RunReplayPredictionBodySample* body = FindReplayPredictionBodyById( frame, drawState.node.id );
                if ( !body )
                {
                    continue;
                }

                if ( frame.frameIndex <= drawState.node.firstFrame )
                {
                    if ( !drawState.markerDrawn )
                    {
                        const float radius = ReplayFutureMarkerRadiusForModelIndex( childDraw.models, body->modelIndex );
                        tracer.AddReplayFutureTargetMarker( body->position, radius, drawState.node.depth );
                        drawState.markerDrawn = true;
                    }
                    if ( drawState.hasIncomingPrevious && VectorMagSquared( body->position - drawState.incomingPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                    {
                        const float t = ReplayPathFrameT( frame.frameIndex, 0, drawState.node.firstFrame );
                        float r = 0.92f;
                        float g = 0.54f;
                        float b = 0.18f;
                        ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                        tracer.AddReplayPathSegment( drawState.incomingPrevious, body->position, r, g, b );
                    }
                    drawState.incomingPrevious = body->position;
                    drawState.hasIncomingPrevious = true;
                }

                if ( frame.frameIndex >= drawState.node.firstFrame &&
                     drawState.hasPrevious &&
                     VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                {
                    const float t = ReplayPathFrameT( frame.frameIndex, drawState.node.firstFrame, lastFrame );
                    float r = 0.5f;
                    float g = 0.5f;
                    float b = 0.56f;
                    ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
                    tracer.AddReplayPathSegment( drawState.previous, body->position, r, g, b );
                }
                if ( frame.frameIndex >= drawState.node.firstFrame )
                {
                    drawState.previous = body->position;
                    drawState.hasPrevious = true;
                }
            }
        }

        for ( const RunReplayPathTraceNode& node : m_replayPrediction.futureNodes )
        {
            float r = 0.58f;
            float g = 0.64f;
            float b = 0.68f;
            if ( node.depth <= 1 )
            {
                r = 0.68f;
                g = 0.78f;
                b = 0.76f;
            }
            tracer.AddReplayContactMarker( node.contactPoint, node.contactNormal, r, g, b );
        }
    }
}


void SkullbonezRun::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    if ( !m_replayPathVisualizer.hasTarget )
    {
        return;
    }

    RenderReplayPredictionVisualizer( tracer );

    if ( !m_solverReplay.IsEnabled() )
    {
        return;
    }

    if ( m_replayPathVisualizer.targets.empty() && m_replayPathVisualizer.targetId.value != 0 )
    {
        RunReplayPathTarget target;
        target.id = m_replayPathVisualizer.targetId;
        target.modelIndex = m_replayPathVisualizer.targetModelIndex;
        if ( m_replayPathVisualizer.targetName[0] != '\0' )
        {
            strncpy_s( target.name, sizeof( target.name ), m_replayPathVisualizer.targetName, _TRUNCATE );
        }
        m_replayPathVisualizer.targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = CurrentReplaySolverScrubSample();
    if ( !presentSample )
    {
        presentSample = m_solverReplay.LatestSample();
    }
    if ( !presentSample )
    {
        return;
    }

    ReplayPathBoundsContext bounds;
    m_solverReplay.ForEachSampleChronological( CaptureReplayPathBounds, &bounds );
    if ( !bounds.hasSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = std::clamp( presentSample->frameIndex, bounds.firstFrame, bounds.lastFrame );
    const ReplayRecorderStats stats = m_solverReplay.GetStats();
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( stats.sampleCount );

    m_replayPathVisualizer.futureNodes.clear();
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( RunReplayPathTarget& target : m_replayPathVisualizer.targets )
    {
        if ( target.id.value == 0 )
        {
            continue;
        }

        PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget" );
        RunReplayPathVisualizerState targetVisualizer;
        ApplyPrimaryReplayPathTarget( targetVisualizer, target.id, target.modelIndex, target.name );

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/BuildTree" );
            ReplayPathFutureContext futureContext;
            futureContext.visualizer = &targetVisualizer;
            futureContext.rootId = target.id;
            futureContext.presentFrame = presentFrame;
            m_solverReplay.ForEachSampleChronological( BuildReplayFutureNodes, &futureContext );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            ReplayPathRootDrawContext rootDraw;
            rootDraw.tracer = &tracer;
            rootDraw.rootId = target.id;
            rootDraw.firstFrame = bounds.firstFrame;
            rootDraw.presentFrame = presentFrame;
            rootDraw.lastFrame = bounds.lastFrame;
            rootDraw.sampleStride = sampleStride;
            m_solverReplay.ForEachSampleChronological( DrawReplayRootPath, &rootDraw );
        }

        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.presentFrame = presentFrame;
        childDraw.lastFrame = bounds.lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( targetVisualizer.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = targetVisualizer.futureNodes[i];
        }
        if ( childDraw.nodeCount > 0 )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawChildren" );
            m_solverReplay.ForEachSampleChronological( DrawReplayChildPaths, &childDraw );
            AddReplayFutureContactMarkers( targetVisualizer, tracer );
        }

        if ( target.id.value == m_replayPathVisualizer.targetId.value )
        {
            m_replayPathVisualizer.futureNodes = targetVisualizer.futureNodes;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            int markerIndex = target.modelIndex;
            if ( markerIndex < 0 ||
                 markerIndex >= static_cast<int>( models.size() ) ||
                 models[static_cast<std::size_t>( markerIndex )].GetReplayBodyId() != target.id.value )
            {
                markerIndex = -1;
                for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
                {
                    if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == target.id.value )
                    {
                        markerIndex = i;
                        target.modelIndex = i;
                        if ( target.id.value == m_replayPathVisualizer.targetId.value )
                        {
                            m_replayPathVisualizer.targetModelIndex = i;
                        }
                        break;
                    }
                }
            }
            if ( markerIndex >= 0 && markerIndex < static_cast<int>( models.size() ) )
            {
                tracer.AddReplayTargetMarker( models[static_cast<std::size_t>( markerIndex )] );
            }
        }
    }
}


int SkullbonezRun::HitEditorGizmoAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    if ( m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const GameModel& model = models[static_cast<size_t>( m_editor.selectedModelIndex )];
    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const float length = EditorGizmoAxisLength( radius );
    const float threshold = (std::max)( 1.25f, length * 0.06f );
    const float thresholdSq = threshold * threshold;

    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, origin + axisVector * length );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int SkullbonezRun::HitEditorRotationGizmoAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    if ( m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const GameModel& model = models[static_cast<size_t>( m_editor.selectedModelIndex )];
    const Vector3 origin = model.GetPosition();
    const float ringRadius = EditorGizmoRotationRadius( EditorModelRadius( model ) );
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


bool SkullbonezRun::TryEditorAxisRayParameter( int axis, const Vector3& rayOrigin, const Vector3& rayDirection, float& outAxisT ) const
{
    if ( axis < 0 || axis > 2 || m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 axisOrigin = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetPosition();
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


bool SkullbonezRun::TryEditorRotationRayAngle( int axis, const Vector3& rayOrigin, const Vector3& rayDirection, float& outAngle ) const
{
    if ( axis < 0 || axis > 2 || m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 origin = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetPosition();
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


void SkullbonezRun::MoveSelectedEditorObjectAlongAxis( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( !m_editor.gizmoDragActive || m_editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( m_editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = m_editor.selectedModelIndex;
    if ( index < 0 || index >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.gizmoDragActive = false;
        m_editor.activeGizmoAxis = -1;
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( index );
    const Vector3 axisVector = EditorAxisVector( m_editor.activeGizmoAxis );
    const Vector3 newPosition = m_editor.gizmoDragStartPosition + axisVector * ( axisT - m_editor.gizmoDragStartAxisT );
    model.SetPosition( newPosition );
    model.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    if ( !model.IsFixed() )
    {
        m_cGameModelCollection.WakeModel( index );
    }
}


void SkullbonezRun::ScaleSelectedEditorObjectAlongAxis( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( !m_editor.gizmoDragActive || !m_editor.gizmoDragIsScale || m_editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( m_editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = m_editor.selectedModelIndex;
    if ( index < 0 || index >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
        return;
    }

    const float startExtent = EditorShapeAxisExtent( m_editor.gizmoDragStartShape, m_editor.activeGizmoAxis );
    const float targetExtent = (std::max)( 0.25f, startExtent + axisT - m_editor.gizmoDragStartAxisT );
    const float factor = targetExtent / startExtent;

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( index );
    if ( model.ScaleCollisionShapeAxisFromBase( m_editor.gizmoDragStartShape, m_editor.activeGizmoAxis, factor ) )
    {
        model.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
        model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
        if ( !model.IsFixed() )
        {
            m_cGameModelCollection.WakeModel( index );
        }
    }
}


void SkullbonezRun::RotateSelectedEditorObjectAroundAxis( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( !m_editor.gizmoDragActive || !m_editor.gizmoDragIsRotation || m_editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float currentAngle = 0.0f;
    if ( !TryEditorRotationRayAngle( m_editor.activeGizmoAxis, rayOrigin, rayDirection, currentAngle ) )
    {
        return;
    }

    const int index = m_editor.selectedModelIndex;
    if ( index < 0 || index >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.activeGizmoAxis = -1;
        return;
    }

    Quaternion orientation = m_editor.gizmoDragStartOrientation;
    orientation.RotateAboutAxis( EditorAxisVector( m_editor.activeGizmoAxis ), WrapEditorAngleDelta( currentAngle - m_editor.gizmoDragStartRotationAngle ) );

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( index );
    model.SetOrientation( orientation );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    if ( !model.IsFixed() )
    {
        model.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
        m_cGameModelCollection.WakeModel( index );
    }
}


void SkullbonezRun::UpdateEditorInteractionPreview()
{
    m_editor.placementPreviewVisible = false;
    m_editor.hotGizmoAxis = -1;
    m_editor.hotRotationAxis = -1;

    if ( m_UI.BlocksCameraMouse() || m_editor.viewportLookActive )
    {
        return;
    }

    if ( !m_editor.editorModeEnabled )
    {
        return;
    }

    if ( m_editor.placementModeEnabled )
    {
        m_editor.placementPreviewVisible = TryComputeEditorPlacementPreview( m_editor.objectType );
    }

    if ( m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.selectedModelIndex = -1;
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
    }

    if ( m_editor.selectedModelIndex >= 0 && !m_editor.gizmoDragActive && !m_editor.placementModeEnabled )
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
        {
            if ( Input::IsKeyDown( VK_CONTROL ) )
            {
                m_editor.hotGizmoAxis = HitEditorGizmoAxis( rayOrigin, rayDirection );
            }
            else
            {
                m_editor.hotRotationAxis = HitEditorRotationGizmoAxis( rayOrigin, rayDirection );
                m_editor.hotGizmoAxis = m_editor.hotRotationAxis < 0 ? HitEditorGizmoAxis( rayOrigin, rayDirection ) : -1;
            }
        }
    }
}


void SkullbonezRun::RenderEditorOverlay( const Matrix4& viewProjection, const Vector3& cameraEye, const Vector3& cameraUp )
{
    m_editorTracer.Clear();
    const float rayLinger = (std::max)( 0.0f, m_debug.physicsDebugContactLinger );
    if ( rayLinger > 0.0f )
    {
        for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
        {
            if ( line.active && line.ageSeconds < rayLinger )
            {
                m_editorTracer.AddRayCastTestLine( line.start, line.end, 1.0f - line.ageSeconds / rayLinger, line.hit );
            }
        }
    }

    if ( m_editor.editorModeEnabled &&
         m_editor.placementModeEnabled &&
         m_editor.placementPreviewVisible )
    {
        m_editorTracer.AddPlacementRay( m_editor.placementRayOrigin, m_editor.placementRayHit );
        m_editorTracer.AddPlacementGhost( m_editor.objectType, m_editor.placementCenter, m_editor.placementTerrainPoint, m_editor.placementScale, m_editor.placementOrientation );
    }

    if ( m_editor.editorModeEnabled &&
         !m_editor.placementModeEnabled &&
         m_editor.selectedModelIndex >= 0 &&
         m_editor.selectedModelIndex < m_cGameModelCollection.GetModelCount() )
    {
        const GameModel& selected = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )];
        const float radius = EditorModelRadius( selected );
        const bool scaleMode = m_editor.gizmoDragIsScale || Input::IsKeyDown( VK_CONTROL );
        m_editorTracer.AddSelectionOutline( selected );
        m_editorTracer.AddGizmo( selected.GetPosition(), radius, m_editor.hotGizmoAxis, m_editor.hotRotationAxis, m_editor.activeGizmoAxis, m_editor.gizmoDragIsRotation, scaleMode, m_editor.gizmoDragIsScale );
    }
    RenderReplayPathVisualizer( m_editorTracer );
    RenderReplayVelocityEditOverlay( m_editorTracer );
    m_editorTracer.Render( viewProjection );
    m_launcherLaser.Render( viewProjection, cameraEye, cameraUp );
}


void SkullbonezRun::PlaceEditorObjectAtMouse( int objectType, bool fixedObject )
{
    Vector3 terrainPoint;
    if ( !TryGetMouseTerrainPlacement( terrainPoint ) )
    {
        return;
    }

    PlaceEditorObjectAtTerrainPoint( objectType, fixedObject, terrainPoint );
}


void SkullbonezRun::PlaceEditorObjectAtTerrainPoint( int objectType, bool fixedObject, const Vector3& terrainPoint )
{
    const int modelCount = m_cGameModelCollection.GetModelCount();
    const int type = std::clamp( objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const int requiredModelCount = tree ? tree->partCount : 1;
    if ( modelCount + requiredModelCount > ActiveGameModelCapacity() )
    {
        fprintf( stderr, "[editor] Cannot place object: model capacity reached.\n" );
        return;
    }

    EnterInteractiveSceneRun();
    const Vector3 placementScale = EditorClampPlacementScale( type, m_editor.placementScale );
    const int serial = m_editor.placedObjectSerial++;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( m_systems.terrain && m_systems.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        m_systems.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const bool alignToTerrain = EditorObjectAlignsToTerrainNormal( type, m_editor.autoTerrainAlign );
    const Quaternion placementOrientation = EditorOrientationFromTerrainNormal( type, terrainNormal, m_editor.autoTerrainAlign );
    Quaternion placementOrientationCopy = placementOrientation;
    const RotationMatrix placementRotation = placementOrientationCopy.GetOrientationMatrix();
    const bool placementFixed = tree && tree->forceFixed ? true : fixedObject;
    const char* modePrefix = placementFixed ? "static" : ( tree && tree->seedAsleep ? "sleeping" : "dynamic" );

    auto addModel = [&]( GameModel model, bool modelFixed, bool modelStartsAsleep = false )
    {
        model.SetFixed( modelFixed );
        const int index = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( model ) );
        if ( !modelFixed )
        {
            if ( modelStartsAsleep )
            {
                m_cGameModelCollection.SeedModelAsleep( index );
            }
            else
            {
                m_cGameModelCollection.WakeModel( index );
            }
        }
    };

    auto addSphere = [&]( const char* label, float radius, float restitution )
    {
        const float mass = CalculateSphereMass( radius );
        const Vector3 inertia = CalculateSphereInertia( radius, mass );
        const Vector3 center( terrainPoint.x, terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        GameModel model( &m_cWorldEnvironment,
                         center,
                         inertia,
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( restitution );
        model.AddBoundingSphere( radius );
        ApplyEditorSpawnMaterial( model, fixedObject, false );
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
        GameModel model( &m_cWorldEnvironment,
                         center,
                         CalculateBoxInertiaForHalfExtents( halfExtents, mass ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
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
        const Vector3 authoredOrigin = terrainPoint + hullRotation * Vector3( 0.0f, HullAuthoredBottomOffset( scaledHull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const Vector3 center = authoredOrigin + hullRotation * scaledHull.GetAuthoredCenterOfMass();
        GameModel model( &m_cWorldEnvironment,
                         center,
                         scaledHull.ComputeBoxApproxInertia( mass ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
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
                fprintf( stderr, "[editor] Cannot place tree: missing hull asset %s.\n", EditorHullAssetToken( part.hullAsset ) );
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
            const Vector3 authoredOrigin = terrainPoint + placementRotation * ( localOffset + Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f ) );
            const Vector3 center = authoredOrigin + placementRotation * hull.GetAuthoredCenterOfMass();
            const float mass = hull.GetDefaultMass();
            GameModel model( &m_cWorldEnvironment,
                             center,
                             hull.ComputeBoxApproxInertia( mass ),
                             mass );
            model.SetTerrain( m_systems.terrain.get() );
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
    default:
        break;
    }

    SceneState().modelCount = m_cGameModelCollection.GetModelCount();
}
