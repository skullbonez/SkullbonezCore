/*
File: SkullbonezSource/Runtime/Editor/EditorTools.cpp
Purpose:
  Owns editor placement scale math for primitive bodies, hulls, trees, and compound assets.

Mental model:
  Placement gestures start as mouse deltas and wheel clicks. This file maps
  that input into safe object scale values before RunInput commits the object.

Glossary:
  Placement gesture: Mouse drag and wheel input used to size an editor object
    before placement commits.
  Hull scale: Per-axis size multiplier for convex hull editor assets.
  Uniform scale: One shared size value applied to all axes.
  Scale lock: Placement rule that keeps authored multi-part asset proportions coherent.

Invariants:
  - Object-type classification must stay in sync with UI::EditorTab entries.
  - Scale helpers clamp before objects are committed to the scene.

Related:
  - SkullbonezSource/Runtime/Editor/EditorTools.h
  - Agentic/Plans/TODO/interaction-state-machine.md
*/
#include "EditorTools.h"

#include "../CameraCollection.h"
#include "../CaptureController.h"
#include "../InputController.h"
#include "../RuntimeFileWriter.h"
#include "../Scene/SceneRuntime.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Common.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Scene/SceneSnapshotWriter.h"
#include "../../UI/UICommands.h"
#include "../../UI/UITabEditor.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <utility>

using SkullbonezCore::GameObjects::SceneSaveRequest;
using SkullbonezCore::GameObjects::SceneSaveView;
using SkullbonezCore::GameObjects::SceneSnapshotWriter;
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

static_assert( UI::EditorTab::OBJECT_TYPE_COUNT == 37,
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
    case UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP:
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
    // Concept: Object families define the shape of the scale value. Trees and
    // buildings ignore user scale, balls use one radius, hulls use hull-local
    // factors, and boxes use world half extents.
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


void ResetEditorUnfocusedInputState( EditorGizmoContext context )
{
    // Lifetime: Losing focus cancels gesture-owned state only. Persistent
    // editor choices such as object type and static/dynamic placement survive
    // so toggling focus does not rewrite the authoring mode.
    context.editor.viewportLookActive = false;
    context.editor.placementScaleActive = false;
    context.editor.placementScaleWheelSteps = 0;
    CancelEditorGizmoDragState( context );
    context.editor.gizmoDragStartAxisT = 0.0f;
    context.editor.gizmoDragStartRotationAngle = 0.0f;
    context.editor.gizmoDragStartPosition = Math::Vector::ZERO_VECTOR;
    context.editor.gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
}


void ClearEditorManipulationState( EditorGizmoContext context )
{
    context.editor.placementPreviewVisible = false;
    context.editor.placementScaleActive = false;
    context.editor.placementScaleWheelSteps = 0;
    context.editor.placementScale = EditorDefaultPlacementScale( context.editor.objectType );
    context.editor.placementScaleStart = context.editor.placementScale;
    CancelEditorGizmoDragState( context );
    context.editor.placementAltitudeSteps = 0;
    context.editor.placementYawRadians = 0.0f;
}


EditorKeyboardShortcutResult HandleEditorKeyboardShortcut( RuntimeInputAction action, bool isDown, bool wasPressed )
{
    EditorKeyboardShortcutResult result;
    switch ( action )
    {
    case RuntimeInputAction::ToggleEditorTool:
        // Concept: Alt is both a level input for replay velocity editing and a
        // press edge for editor placement-mode toggling.
        result.altDown = isDown;
        result.togglePlacementMode = wasPressed;
        return result;
    default:
        return result;
    }
}


EditorPlacementModeChangeResult
SetEditorPlacementMode( EditorGizmoContext context, bool enabled, bool clearManipulation )
{
    context.editor.placementModeEnabled = context.editor.editorModeEnabled && enabled;
    context.editor.viewportLookActive = false;
    if ( clearManipulation )
    {
        ClearEditorManipulationState( context );
    }

    EditorPlacementModeChangeResult result;
    result.placementModeEnabled = context.editor.placementModeEnabled;
    result.worldOwner =
        result.placementModeEnabled ? WorldInteractionOwner::EditorPlacement : WorldInteractionOwner::EditorGizmo;
    return result;
}


EditorPlacementModeChangeResult ToggleEditorPlacementMode( EditorGizmoContext context )
{
    return SetEditorPlacementMode( context, !context.editor.placementModeEnabled, true );
}


void EnterEditorModeState( EditorGizmoContext context, RunCameraMode restoreCameraMode )
{
    context.editor.editorModeEnabled = true;
    context.editor.placementModeEnabled = true;
    context.editor.viewportLookActive = false;
    ClearEditorManipulationState( context );
    context.editor.restoreCameraModeAfterEditor = restoreCameraMode;
}


void ExitEditorModeState( EditorGizmoContext context )
{
    context.editor.editorModeEnabled = false;
    context.editor.viewportLookActive = false;
    context.editor.placementPreviewVisible = false;
    context.editor.placementModeEnabled = false;
    CancelEditorGizmoDragState( context );
    context.editor.placementScaleActive = false;
    context.editor.placementScaleWheelSteps = 0;
    context.editor.placementScale = EditorDefaultPlacementScale( context.editor.objectType );
    context.editor.placementScaleStart = context.editor.placementScale;
    context.editor.placementAltitudeSteps = 0;
    context.editor.placementYawRadians = 0.0f;
    context.editor.restoreCameraModeAfterEditor = RunCameraMode::Demo;
}


bool SetEditorPlaceStaticObject( RunEditorPlacementState& editor, bool placeStaticObject )
{
    if ( editor.placeStaticObject == placeStaticObject )
    {
        return false;
    }

    editor.placeStaticObject = placeStaticObject;
    return true;
}


void ToggleEditorPlaceStaticObject( RunEditorPlacementState& editor )
{
    editor.placeStaticObject = !editor.placeStaticObject;
}


void ToggleEditorTerrainAlign( RunEditorPlacementState& editor )
{
    editor.autoTerrainAlign = !editor.autoTerrainAlign;
    editor.placementPreviewVisible = false;
    editor.placementScaleActive = false;
    editor.placementScaleWheelSteps = 0;
}


EditorObjectTypeRequestResult
SelectEditorObjectType( EditorGizmoContext context, int requestedObjectType, bool enterPlacementMode )
{
    EditorObjectTypeRequestResult result;
    const int objectType = ClampEditorObjectType( requestedObjectType );
    if ( objectType != context.editor.objectType )
    {
        context.editor.objectType = objectType;
        ClearEditorManipulationState( context );
        result.objectTypeChanged = true;
    }
    else if ( enterPlacementMode )
    {
        ClearEditorManipulationState( context );
    }
    result.enterPlacementMode = enterPlacementMode && context.editor.editorModeEnabled;
    return result;
}


EditorPlacementPreModeUICommandResult ApplyEditorPlacementPreModeUICommands( EditorGizmoContext context,
                                                                             const UI::UIEditorCommands& commands )
{
    EditorPlacementPreModeUICommandResult result;
    result.toggleEditorMode = commands.toggleEditorMode;
    result.togglePlacementMode = commands.togglePlacementMode;
    if ( commands.requestPlaceStatic && SetEditorPlaceStaticObject( context.editor, commands.requestedPlaceStatic ) )
    {
        result.setPlaceStatic = true;
    }
    if ( commands.requestedObjectType >= 0 )
    {
        const EditorObjectTypeRequestResult objectTypeRequest =
            SelectEditorObjectType( context, commands.requestedObjectType, commands.enterPlacementMode );
        result.requestedObjectType = true;
        result.enterPlacementMode = objectTypeRequest.enterPlacementMode;
    }
    return result;
}


EditorPlacementPostModeUICommandResult ApplyEditorPlacementPostModeUICommands( RunEditorPlacementState& editor,
                                                                               const UI::UIEditorCommands& commands )
{
    EditorPlacementPostModeUICommandResult result;
    if ( commands.togglePlaceStatic )
    {
        ToggleEditorPlaceStaticObject( editor );
        result.toggledPlaceStatic = true;
    }
    if ( commands.toggleTerrainAlign )
    {
        ToggleEditorTerrainAlign( editor );
        result.toggledTerrainAlign = true;
    }
    return result;
}


void HandleEditorSaveHotkey( EditorSaveHotkeyContext context, RuntimeInputAction action, bool wasPressed )
{
    // Why: the binding table owns the key/action pair, while editor tools keep
    // numbered snapshot paths and screenshot commands behind the editor boundary.
    switch ( action )
    {
    case RuntimeInputAction::SaveSceneSnapshot:
    {
        if ( !wasPressed )
        {
            return;
        }

        static int sSnapshotSeq = 0;
        char path[256] = {};
        if ( RuntimeFileWriter::NextNumberedPath( path,
                                                  sizeof( path ),
                                                  "Scenes",
                                                  "snapshot_",
                                                  ".scene.json",
                                                  sSnapshotSeq,
                                                  100 ) )
        {
            // Lifetime: the save view borrows cold owner arrays only for this
            // synchronous file write; editor input retains none of the rows.
            const auto& groups = context.models.SceneObjectGroups();
            const auto& joints = context.models.GetPointJointConstraints();
            const SceneSaveView saveView{ context.entities,
                                          context.models.BodyStore(),
                                          context.models.Colliders(),
                                          groups.data(),
                                          static_cast<int>( groups.size() ),
                                          joints.data(),
                                          static_cast<int>( joints.size() ),
                                          context.world.GetGravity(),
                                          context.world.GetFluidSurfaceHeight(),
                                          context.world.GetFluidDensity(),
                                          context.world.GetMutualGravitySettings() };
            const SceneSaveRequest request{ path,
                                            context.cameras.GetCameraTranslation(),
                                            context.cameras.GetCameraView(),
                                            context.cameras.GetCameraUp(),
                                            context.scene.isScenePhysics,
                                            context.scene.isSceneText };
            const SbResult saveResult = SceneSnapshotWriter::Save( saveView, request );
            if ( !saveResult.ok )
            {
                fprintf( stderr, "[%s] %s\n", saveResult.error.owner, saveResult.error.message );
            }
        }
        return;
    }

    case RuntimeInputAction::SaveScreenshot:
    {
        if ( !wasPressed )
        {
            return;
        }

        static int sScreenshotSeq = 0;
        char path[256] = {};
        if ( RuntimeFileWriter::NextNumberedPath( path,
                                                  sizeof( path ),
                                                  "Screenshots",
                                                  "screenshot_",
                                                  ".bmp",
                                                  sScreenshotSeq,
                                                  100 ) )
        {
            const SbResult queueResult = context.capture.QueueScreenshot( path );
            if ( !queueResult.ok )
            {
                std::fprintf( stderr, "%s: %s\n", queueResult.error.owner, queueResult.error.message );
                std::fflush( stderr );
            }
        }
        return;
    }

    default:
        return;
    }
}


} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
