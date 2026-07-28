/*
File: SkullbonezSource/Runtime/Editor/EditorTools.cpp
Purpose:
  Owns editor placement math and the focused cold save/capture actions invoked
  by editor input.

Summary:
  Placement gestures map to safe object scale values before commit. Separate
  scene-snapshot and screenshot operations keep persistence authority from
  travelling in one multi-owner hotkey context.

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
  - Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md
*/
#include "EditorTools.h"

#include "../Camera/CameraCollection.h"
#include "../Capture/CaptureController.h"
#include "../Input/InputController.h"
#include "../Tools/RuntimeFileWriter.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneSaveOperations.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Common.h"
#include "../Scene/SceneController.h"
#include "../../UI/UICommands.h"
#include "../../UI/UITabEditor.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <utility>

using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore
{
namespace Runtime
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
        return Vector3( std::clamp( scale.x, 0.05f, 20.0f ), std::clamp( scale.y, 0.05f, 20.0f ),
                        std::clamp( scale.z, 0.05f, 20.0f ) );
    }

    return Vector3( std::clamp( scale.x, 0.25f, 200.0f ), std::clamp( scale.y, 0.25f, 200.0f ),
                    std::clamp( scale.z, 0.25f, 200.0f ) );
}

Vector3 EditorPlacementScaleFromGesture( int objectType, const Vector3& startScale, float dragPixelsX, float dragPixelsY,
                                         int wheelSteps )
{
    const int type = ClampEditorObjectType( objectType );

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


void ResetEditorUnfocusedInputState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction )
{

    // Lifetime: Losing focus cancels gesture-owned state only. Persistent
    // editor choices such as object type and static/dynamic placement survive
    // so toggling focus does not rewrite the authoring mode.
    editor.viewportLookActive = false;
    EndEditorPlacementScaleGesture( interaction );
    editor.placementScaleWheelSteps = 0;
    CancelEditorGizmoDragState( editor, interaction );
    editor.gizmoDragStartAxisT = 0.0f;
    editor.gizmoDragStartRotationAngle = 0.0f;
    editor.gizmoDragStartPosition = Math::Vector::ZERO_VECTOR;
    editor.gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
}


void ClearEditorManipulationState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction )
{
    editor.placementPreviewVisible = false;
    EndEditorPlacementScaleGesture( interaction );
    editor.placementScaleWheelSteps = 0;
    editor.placementScale = EditorDefaultPlacementScale( editor.objectType );
    editor.placementScaleStart = editor.placementScale;
    CancelEditorGizmoDragState( editor, interaction );
    editor.placementAltitudeSteps = 0;
    editor.placementYawRadians = 0.0f;
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


EditorPlacementModeChangeResult SetEditorPlacementMode( RunEditorPlacementState& editor,
                                                        RuntimeInteractionController& interaction, bool enabled,
                                                        bool clearManipulation )
{
    editor.placementModeEnabled = editor.editorModeEnabled && enabled;
    editor.viewportLookActive = false;

    if ( clearManipulation )
    {
        ClearEditorManipulationState( editor, interaction );
    }

    EditorPlacementModeChangeResult result;
    result.placementModeEnabled = editor.placementModeEnabled;
    result.worldOwner = result.placementModeEnabled ? WorldInteractionOwner::EditorPlacement
                                                    : WorldInteractionOwner::EditorGizmo;

    return result;
}


EditorPlacementModeChangeResult ToggleEditorPlacementMode( RunEditorPlacementState& editor,
                                                           RuntimeInteractionController& interaction )
{
    return SetEditorPlacementMode( editor, interaction, !editor.placementModeEnabled, true );
}


void EnterEditorModeState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction,
                           RunCameraMode restoreCameraMode )
{
    editor.editorModeEnabled = true;
    editor.placementModeEnabled = true;
    editor.viewportLookActive = false;
    ClearEditorManipulationState( editor, interaction );
    editor.restoreCameraModeAfterEditor = restoreCameraMode;
}


void ExitEditorModeState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction )
{
    editor.history.Clear();
    editor.editorModeEnabled = false;
    editor.viewportLookActive = false;
    editor.placementPreviewVisible = false;
    editor.placementModeEnabled = false;
    EndEditorPlacementScaleGesture( interaction );
    CancelEditorGizmoDragState( editor, interaction );
    editor.placementScaleWheelSteps = 0;
    editor.placementScale = EditorDefaultPlacementScale( editor.objectType );
    editor.placementScaleStart = editor.placementScale;
    editor.placementAltitudeSteps = 0;
    editor.placementYawRadians = 0.0f;
    editor.restoreCameraModeAfterEditor = RunCameraMode::Demo;
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


void ToggleEditorTerrainAlign( RunEditorPlacementState& editor, RuntimeInteractionController& interaction )
{
    editor.autoTerrainAlign = !editor.autoTerrainAlign;
    editor.placementPreviewVisible = false;
    EndEditorPlacementScaleGesture( interaction );
    editor.placementScaleWheelSteps = 0;
}


EditorObjectTypeRequestResult SelectEditorObjectType( RunEditorPlacementState& editor,
                                                      RuntimeInteractionController& interaction, int requestedObjectType,
                                                      bool enterPlacementMode )
{
    EditorObjectTypeRequestResult result;
    const int objectType = ClampEditorObjectType( requestedObjectType );

    if ( objectType != editor.objectType )
    {
        editor.objectType = objectType;
        ClearEditorManipulationState( editor, interaction );
        result.objectTypeChanged = true;
    }
    else if ( enterPlacementMode )
    {
        ClearEditorManipulationState( editor, interaction );
    }

    result.enterPlacementMode = enterPlacementMode && editor.editorModeEnabled;
    return result;
}


EditorPlacementPreModeUICommandResult ApplyEditorPlacementPreModeUICommands( RunEditorPlacementState& editor,
                                                                             RuntimeInteractionController& interaction,
                                                                             const UI::UIEditorCommands& commands )
{
    EditorPlacementPreModeUICommandResult result;
    result.toggleEditorMode = commands.toggleEditorMode;
    result.togglePlacementMode = commands.togglePlacementMode;

    if ( commands.requestPlaceStatic && SetEditorPlaceStaticObject( editor, commands.requestedPlaceStatic ) )
    {
        result.setPlaceStatic = true;
    }

    if ( commands.requestedObjectType >= 0 )
    {
        const EditorObjectTypeRequestResult objectTypeRequest = SelectEditorObjectType( editor, interaction,
                                                                                        commands.requestedObjectType,
                                                                                        commands.enterPlacementMode );

        result.requestedObjectType = true;
        result.enterPlacementMode = objectTypeRequest.enterPlacementMode;
    }

    return result;
}


EditorPlacementPostModeUICommandResult ApplyEditorPlacementPostModeUICommands( RunEditorPlacementState& editor,
                                                                               RuntimeInteractionController& interaction,
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
        ToggleEditorTerrainAlign( editor, interaction );
        result.toggledTerrainAlign = true;
    }

    return result;
}


void HandleEditorSceneSaveHotkey( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, SceneWorld& world,
                                  const SceneSessionState& scene, const GameObjects::PresentationSaveState& presentation,
                                  bool wasPressed )
{

    if ( !wasPressed )
    {
        return;
    }

    static int sSnapshotSeq = 0;
    SkullbonezCore::Core::SbResult saveResult = SkullbonezCore::Core::SbResult::Success();

    if ( TrySaveNextEditorSceneSnapshot( diagnostics, sSnapshotSeq, world.GetSaveState(), scene.GetSaveState(), presentation,
                                         saveResult ) &&
         !saveResult.Ok() )
    {
        fprintf( stderr, "[%s] %s\n", saveResult.ErrorOwner(), saveResult.ErrorMessage() );
    }
}


void HandleEditorScreenshotHotkey( CaptureController& capture, bool wasPressed )
{

    if ( !wasPressed )
    {
        return;
    }

    static int sScreenshotSeq = 0;
    char path[256] = {};

    if ( RuntimeFileWriter::NextNumberedPath( path, sizeof( path ), "Screenshots", "screenshot_", ".bmp", sScreenshotSeq,
                                              100 ) )
    {
        const SkullbonezCore::Core::SbResult queueResult = capture.QueueScreenshot( path );

        if ( !queueResult.Ok() )
        {
            std::fprintf( stderr, "%s: %s\n", queueResult.ErrorOwner(), queueResult.ErrorMessage() );
            std::fflush( stderr );
        }
    }
}


} // namespace Runtime
} // namespace SkullbonezCore
