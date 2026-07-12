/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
Purpose:
  Provides the runtime tool state ownership boundary.

Summary:
  Run routes input and passes borrowed world services here. RuntimeTools mutates
  only tool-owned state, launcher-created projectiles, and explicit physics
  targets selected by the current tool action.

Glossary:
  Launcher ray: Camera-centered tool ray used for laser impulses, projectile
    aim, and raycast visualization.
  Replay visual sample: Snapshot of launcher lines and laser shots used by
    replay scrubbing.
  Contact release: Rule that lets selected fixed authored props become dynamic
    after a strong enough launcher impulse.
  Body store: Physics-owned dense body rows holding simulation position, mass,
    fixed state, and handles for command targets.
  Collider store: Physics-owned dense collider rows holding shape-derived bounds
    used by launcher broad hit tests.
  Physics body handle: Generational id for the body-store row that receives
    launcher impulses or wake commands.

Invariants:
  - Raycast and laser histories are fixed-capacity replay state; preserve cursor
    wrap behavior and restore clamping.
  - Terrain/model hit tests pick the closest valid hit without changing world
    state.
  - Projectile creation must respect the active model capacity before adding to
    SceneController.
  - Launcher physics mutation enters PhysicsEngine through body handles: ray
    hits resolve model indices at the tool boundary, while spawned projectiles
    use the handle returned by creation.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/LauncherTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
*/
#include "RuntimeTools.h"
#include "../../Assets/AssetKeys.h"

#include "../../Core/Common.h"
#include "../../Core/Log.h"
#include "../Scene/SceneController.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../UI/UICommands.h"
#include "../../UI/UILayout.h"
#include "../CameraCollection.h"
#include "../Editor/EditorTools.h"
#include "../Editor/EditorOverlayTools.h"
#include "../InputRouter.h"
#include "../RuntimeInteractionCommands.h"
#include "../RuntimeInteractionController.h"
#include "../Replay/ReplayRecorder.h"
#include "../Replay/ReplayRuntime.h"
#include "../Scene/SceneRuntime.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace SkullbonezCore::Basics
{
bool RuntimeTools::PrepareSelectionCommand( const RuntimeInteractionCommand& command,
                                            const Basics::SceneController& collection,
                                            RuntimeInteractionSelectionPlan& outPlan )
{
    outPlan = RuntimeInteractionSelectionPlan{};
    if ( command.type != RuntimeInteractionCommandType::SetEditorSelection )
    {
        return false;
    }

    const Physics::PhysicsBodyStore& bodyStore = collection.BodyStore();
    const Physics::ColliderStore& colliderStore = collection.Colliders();
    Physics::PhysicsBodyHandle selectedBody;
    Physics::PhysicsColliderHandle selectedCollider;
    Physics::ModelRowHint selectedModelRow;
    if ( command.body.IsValid() )
    {
        selectedBody = command.body;
        selectedCollider = command.collider;
        const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( selectedBody );
        const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( selectedCollider );
        const int bodyRow = bodyStore.ModelIndexForHandle( selectedBody );
        if ( !body || !collider || colliderStore.ModelIndexForHandle( selectedCollider ) != bodyRow ||
             collider->body != selectedBody )
        {
            return false;
        }
        selectedModelRow.value = bodyRow;
    }
    else if ( command.collider.IsValid() )
    {
        return false;
    }

    if ( !m_editor.selectedBody.IsValid() )
    {
        m_editor.selectedModelRow.value = -1;
        outPlan.previousModelRow.value = -1;
    }
    else
    {
        outPlan.previousModelRow.value = bodyStore.ResolveModelRow( m_editor.selectedBody, m_editor.selectedModelRow );
    }
    outPlan.modelRow = selectedModelRow;
    outPlan.previousBody = m_editor.selectedBody;
    outPlan.body = selectedBody;
    outPlan.previousCollider = m_editor.selectedCollider;
    outPlan.collider = selectedCollider;
    outPlan.selectionScope = command.selectionScope;
    outPlan.claimSelectionOwner = command.claimSelectionOwner;
    return true;
}


bool RuntimeTools::CommitSelectionCommand( const RuntimeInteractionSelectionPlan& plan,
                                           RuntimeInteractionEvent& outEvent )
{
    outEvent = RuntimeInteractionEvent{};
    // Invariant: preparation and commit are synchronous around the optional
    // owner transition. Transition cleanup may deliberately clear the previous
    // selection; the prepared command still becomes the new authoritative one.
    m_editor.selectedModelRow = plan.modelRow;
    m_editor.selectedBody = plan.body;
    m_editor.selectedCollider = plan.collider;
    if ( plan.previousModelRow.value != plan.modelRow.value || plan.previousBody != plan.body ||
         plan.previousCollider != plan.collider )
    {
        outEvent.type = RuntimeInteractionEventType::SelectionChanged;
        outEvent.previousModelRow = plan.previousModelRow;
        outEvent.modelRow = plan.modelRow;
        outEvent.previousBody = plan.previousBody;
        outEvent.body = plan.body;
        outEvent.previousCollider = plan.previousCollider;
        outEvent.collider = plan.collider;
        outEvent.selectionScope = plan.selectionScope;
        Log().WriteEventf(
            "runtime_interaction_command_event type=selection_changed scope=%s previous_model=%d model=%d",
            outEvent.selectionScope == RuntimeInteractionSelectionScope::Inspect ? "inspect" : "editor",
            outEvent.previousModelRow.value,
            outEvent.modelRow.value );
    }
    return true;
}


bool RuntimeTools::ApplySelectionCommand( const RuntimeInteractionCommand& command,
                                          const Basics::SceneController& collection )
{
    // Why: owner-claiming commands need composition to apply transition cleanup
    // between prepare and commit. The convenience path is intentionally limited
    // to commands whose interaction owner is already established.
    if ( command.claimSelectionOwner )
    {
        return false;
    }
    RuntimeInteractionSelectionPlan plan;
    RuntimeInteractionEvent event;
    return PrepareSelectionCommand( command, collection, plan ) && CommitSelectionCommand( plan, event );
}


bool RuntimeTools::HasActiveEditorInteractionState( const RuntimeInteractionController& interaction ) const
{
    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    return m_editor.editorModeEnabled || m_editor.placementModeEnabled || m_editor.viewportLookActive ||
           m_editor.placementPreviewVisible || gesture == RuntimeInteractionGestureKind::EditorPlacementScaleDrag ||
           gesture == RuntimeInteractionGestureKind::GizmoDrag || m_editor.hotGizmoAxis >= 0 ||
           m_editor.hotRotationAxis >= 0;
}


bool RuntimeTools::InspectGizmoInteractionActive( RunCameraMode cameraMode, bool replayInspectionActive ) const
{
    return !m_editor.editorModeEnabled && cameraMode == RunCameraMode::Inspect && !replayInspectionActive;
}


void RuntimeTools::ClearEditorInteractionForTransition( bool clearSelection,
                                                        Basics::SceneController& collection,
                                                        Physics::PhysicsEngine& physics,
                                                        RuntimeInteractionController& interaction )
{
    RunInternal::ClearEditorManipulationState( { m_editor, collection, physics, interaction } );
    m_editor.viewportLookActive = false;
    m_editor.placementModeEnabled = false;
    m_editor.hotGizmoAxis = -1;
    m_editor.hotRotationAxis = -1;
    if ( clearSelection )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.claimSelectionOwner = false;
        ApplySelectionCommand( command, collection );
    }
}


void RuntimeTools::CancelMousePickup( InputRouter& inputRouter, RuntimeInteractionController& interaction )
{
    // Invariant: the controller gesture is the sole capture fact. Clear native
    // presentation only when this owner actually held the typed pickup gesture.
    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        inputRouter.ReleaseNativeCapture();
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
    }
    m_mousePickup = RunMousePickupState{};
}


namespace
{
constexpr float RAY_CAST_TEST_MAX_DISTANCE = 5000.0f;
constexpr float RAY_CAST_TEST_VISUAL_MISS_DISTANCE = 360.0f;
constexpr float LAUNCHER_PROJECTILE_RADIUS = 0.85f;
constexpr float LAUNCHER_PROJECTILE_MASS = 6.0f;
constexpr float LAUNCHER_PROJECTILE_RESTITUTION = 0.42f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_LEAD = 3.2f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET = 0.28f;

// Why: launcher tools borrow already-prepared physics stores from the Run owner.
// A count mismatch means the caller has not completed the cold topology repair
// edge, so tool code fails closed instead of rebuilding model-owned descriptors.
bool LauncherPhysicsStoresReady( const Physics::PhysicsEngine& physics, int modelCount )
{
    return SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).Count() == modelCount &&
           SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ).Count() == modelCount;
}


// Why: launcher ray hits still identify targets by model index, but the physics
// mutation should run on the already-resolved body handle.
void ApplyLauncherPhysicsImpulse( Physics::PhysicsEngine& physics,
                                  Physics::PhysicsBodyHandle body,
                                  const Math::Vector::Vector3& impulse,
                                  const Math::Vector::Vector3& localApplicationPoint )
{
    if ( !body.IsValid() )
    {
        return;
    }

    physics.ApplyBodyImpulse( body, impulse, localApplicationPoint );
}


bool IntersectRaySphere( const Math::Vector::Vector3& rayOrigin,
                         const Math::Vector::Vector3& rayDirection,
                         const Math::Vector::Vector3& center,
                         float radius,
                         float& outT )
{
    const Math::Vector::Vector3 m = rayOrigin - center;
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
} // namespace

RunRayCastTestState& RuntimeTools::RayCastTest()
{
    return m_rayCastTest;
}

const RunRayCastTestState& RuntimeTools::RayCastTest() const
{
    return m_rayCastTest;
}

bool RuntimeTools::ApplyRayCastVisualizationUICommand( const UI::UIPhysicsCommands& commands )
{
    if ( !commands.toggleRayCastVisualization )
    {
        return false;
    }

    m_rayCastTest.visualizeRays = !m_rayCastTest.visualizeRays;
    return true;
}

RayCastLauncherTuningUICommandResult
RuntimeTools::ApplyRayCastLauncherTuningUICommands( const UI::UIPhysicsCommands& commands )
{
    RayCastLauncherTuningUICommandResult result;
    if ( commands.requestRayCastImpulseStrength )
    {
        const float previousImpulseStrength = m_rayCastTest.impulseStrength;
        m_rayCastTest.impulseStrength = std::clamp( commands.requestedRayCastImpulseStrength,
                                                    UI::Layout::UI_RAY_IMPULSE_MIN,
                                                    UI::Layout::UI_RAY_IMPULSE_MAX );
        result.setImpulseStrength = true;
        result.impulseConfigChangedFlags = previousImpulseStrength != m_rayCastTest.impulseStrength ? 1u : 0u;
        result.impulseConfigImpulseStrength = m_rayCastTest.impulseStrength;
        result.impulseConfigProjectileSpeed = m_rayCastTest.projectileSpeed;
    }
    if ( commands.requestLauncherProjectileSpeed )
    {
        const float previousProjectileSpeed = m_rayCastTest.projectileSpeed;
        m_rayCastTest.projectileSpeed = std::clamp( commands.requestedLauncherProjectileSpeed,
                                                    UI::Layout::UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                    UI::Layout::UI_LAUNCHER_PROJECTILE_SPEED_MAX );
        result.setProjectileSpeed = true;
        result.projectileConfigChangedFlags = previousProjectileSpeed != m_rayCastTest.projectileSpeed ? 2u : 0u;
        result.projectileConfigImpulseStrength = m_rayCastTest.impulseStrength;
        result.projectileConfigProjectileSpeed = m_rayCastTest.projectileSpeed;
    }
    return result;
}

void RuntimeTools::ClearRayCastTestLines()
{
    m_rayCastTest.lines = {};
    m_rayCastTest.nextLine = 0;
}

void RuntimeTools::AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit )
{
    if ( !m_rayCastTest.visualizeRays )
    {
        return;
    }

    // Invariant: The line history is intentionally bounded; replay restores the
    // same cursor so overwriting the oldest slot is observable tool state.
    RunRayCastTestLine& line =
        m_rayCastTest.lines[static_cast<std::size_t>( m_rayCastTest.nextLine ) % RunRayCastTestState::MAX_LINES];
    line.start = start;
    line.end = end;
    line.ageSeconds = 0.0f;
    line.active = true;
    line.hit = hit;
    m_rayCastTest.nextLine = ( m_rayCastTest.nextLine + 1 ) % static_cast<int>( RunRayCastTestState::MAX_LINES );
}

void RuntimeTools::TickRayCastTestLines( float dt )
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

bool RuntimeTools::HasLingeredRayCastLine( float maxAgeSeconds ) const
{
    if ( maxAgeSeconds <= 0.0f )
    {
        return false;
    }

    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active && line.ageSeconds < maxAgeSeconds )
        {
            return true;
        }
    }
    return false;
}

bool RuntimeTools::HasSelectionOverlayWork( int modelCount, RunCameraMode cameraMode ) const
{
    const bool selectedModelValid =
        m_editor.selectedBody.IsValid() && m_editor.selectedCollider.IsValid() && modelCount >= 0;
    const bool placementPreview =
        m_editor.editorModeEnabled && m_editor.placementModeEnabled && m_editor.placementPreviewVisible;
    const bool editorSelection = m_editor.editorModeEnabled && !m_editor.placementModeEnabled && selectedModelValid;
    const bool inspectSelection =
        !m_editor.editorModeEnabled && cameraMode == RunCameraMode::Inspect && selectedModelValid;
    const bool attachSelection =
        !m_editor.editorModeEnabled && cameraMode == RunCameraMode::Attach && selectedModelValid;
    return placementPreview || editorSelection || inspectSelection || attachSelection;
}

bool RuntimeTools::HasMousePickupOverlayWork( const RuntimeInteractionGesture& gesture ) const
{
    return gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag && m_mousePickup.body.IsValid();
}

bool RuntimeTools::HasLauncherShots() const
{
    return m_laser.HasActiveShots();
}

const char* RuntimeTools::LauncherFireModeLabel() const
{
    return m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? "PROJECTILE" : "LASER";
}

void RuntimeTools::BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const
{
    // Concept: Replay captures visible launcher/tool feedback separately from
    // physics state so scrubbed frames can redraw rays and laser afterimages
    // without re-firing the tool.
    outSample.rayLines.clear();
    outSample.laserShots.clear();
    outSample.nextRayLine = m_rayCastTest.nextLine;
    outSample.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? ReplayLauncherFireMode::Projectile
                                                                                   : ReplayLauncherFireMode::Laser;
    outSample.visualizeRays = m_rayCastTest.visualizeRays;
    outSample.impulseStrength = m_rayCastTest.impulseStrength;
    outSample.projectileSpeed = m_rayCastTest.projectileSpeed;
    outSample.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        ReplayRayCastLineSample sample;
        sample.start = line.start;
        sample.end = line.end;
        sample.ageSeconds = line.ageSeconds;
        sample.active = line.active;
        sample.hit = line.hit;
        outSample.rayLines.push_back( sample );
    }
    m_laser.CaptureShots( outSample.laserShots, outSample.nextLaserShot );
}

void RuntimeTools::RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample )
{
    m_rayCastTest.lines = {};
    const std::size_t lineCount = (std::min)( sample.rayLines.size(), RunRayCastTestState::MAX_LINES );
    for ( std::size_t i = 0; i < lineCount; ++i )
    {
        RunRayCastTestLine& line = m_rayCastTest.lines[i];
        line.start = sample.rayLines[i].start;
        line.end = sample.rayLines[i].end;
        line.ageSeconds = sample.rayLines[i].ageSeconds;
        line.active = sample.rayLines[i].active;
        line.hit = sample.rayLines[i].hit;
    }
    // Invariant: Older or malformed replay samples may carry out-of-range
    // cursors; clamp by modulo instead of trusting serialized state.
    m_rayCastTest.nextLine = sample.nextRayLine % static_cast<int>( RunRayCastTestState::MAX_LINES );
    if ( m_rayCastTest.nextLine < 0 )
    {
        m_rayCastTest.nextLine += static_cast<int>( RunRayCastTestState::MAX_LINES );
    }
    m_rayCastTest.fireMode = sample.fireMode == ReplayLauncherFireMode::Projectile ? RunLauncherFireMode::Projectile
                                                                                   : RunLauncherFireMode::Laser;
    m_rayCastTest.visualizeRays = sample.visualizeRays;
    m_rayCastTest.impulseStrength = sample.impulseStrength;
    m_rayCastTest.projectileSpeed = sample.projectileSpeed;
    m_laser.RestoreShots( sample.laserShots, sample.nextLaserShot );
}

bool RuntimeTools::TryRayCastTestHit( const Physics::PhysicsBodyStore& bodyStore,
                                      const Physics::ColliderStore& colliderStore,
                                      const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection,
                                      float maxDistance,
                                      int& outIndex,
                                      float& outT ) const
{
    outIndex = -1;
    outT = maxDistance;

    // Concept: launcher body picking uses collider bounding spheres, not exact
    // mesh intersections. The broad deterministic hit result is enough for
    // impulse placement and visual feedback, and it stays on store records.
    const int hitCount = (std::min)( bodyStore.Count(), colliderStore.Count() );
    const auto& bodies = bodyStore.Records();
    const auto& colliders = colliderStore.Records();
    for ( int i = 0; i < hitCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const Physics::PhysicsBodyRecord& body = bodies[index];
        const Physics::ColliderRecord& collider = colliders[index];
        const float radius = (std::max)( collider.boundingRadius, 1.0f );
        float rayT = 0.0f;
        if ( IntersectRaySphere( rayOrigin, rayDirection, body.position, radius, rayT ) && rayT <= maxDistance &&
             rayT < outT )
        {
            outIndex = i;
            outT = rayT;
        }
    }

    return outIndex >= 0;
}

bool RuntimeTools::TryLauncherTerrainHit( Geometry::Terrain* terrain,
                                          const Math::Vector::Vector3& rayOrigin,
                                          const Math::Vector::Vector3& rayDirection,
                                          float maxDistance,
                                          float& outT ) const
{
    outT = maxDistance;
    if ( !terrain )
    {
        return false;
    }

    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    // Why: Terrain has no dedicated runtime ray query here, so the launcher
    // walks the ray and bisects the first terrain crossing to keep laser and
    // projectile aim consistent.
    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = maxDistance * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Math::Vector::Vector3 sample = rayOrigin + rayDirection * t;
        if ( !terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = terrain->GetTerrainHeightAt( sample.x, sample.z );
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
                const Math::Vector::Vector3 mid = rayOrigin + rayDirection * midT;
                if ( !terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = terrain->GetTerrainHeightAt( mid.x, mid.z );
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

bool RuntimeTools::TryBuildLauncherCameraRay( Environment::CameraCollection* cameras,
                                              Math::Vector::Vector3& outOrigin,
                                              Math::Vector::Vector3& outDirection,
                                              Math::Vector::Vector3& outCameraUp ) const
{
    if ( !cameras )
    {
        return false;
    }

    outOrigin = cameras->GetCameraTranslation();
    outDirection = cameras->GetCameraView() - outOrigin;
    const float dirLenSq = Math::Vector::VectorMagSquared( outDirection );
    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    outDirection = outDirection * ( 1.0f / sqrtf( dirLenSq ) );
    outCameraUp = cameras->GetCameraUp();
    return true;
}

bool RuntimeTools::FireLauncherRay( Basics::SceneController& collection,
                                    Physics::PhysicsEngine& physics,
                                    RunSceneState& scene,
                                    Geometry::Terrain* terrain,
                                    int activeModelCapacity,
                                    const Math::Vector::Vector3& rayOrigin,
                                    const Math::Vector::Vector3& rayDirection,
                                    const Math::Vector::Vector3& cameraUp )
{
    const int modelCount = collection.SceneEntityCount();
    if ( !LauncherPhysicsStoresReady( physics, modelCount ) )
    {
        return false;
    }

    if ( m_rayCastTest.fireMode == RunLauncherFireMode::Projectile )
    {
        return FireLauncherProjectile( collection,
                                       physics,
                                       scene,
                                       terrain,
                                       activeModelCapacity,
                                       modelCount,
                                       rayOrigin,
                                       rayDirection,
                                       cameraUp );
    }

    FireLauncherLaser( physics, modelCount, terrain, rayOrigin, rayDirection, cameraUp );
    return false;
}


LauncherPointerResult RuntimeTools::RouteLauncherPointer( const LauncherPointerInput& input,
                                                          Environment::CameraCollection& cameras,
                                                          ReplayRuntime& replayRuntime,
                                                          Basics::SceneController& collection,
                                                          Physics::PhysicsEngine& physics,
                                                          RunSceneState& scene,
                                                          Geometry::Terrain* terrain )
{
    LauncherPointerResult result;
    if ( !input.launcherMode || !input.leftPressed || input.suppressWorldAction || input.uiWantsNativeCursor )
    {
        return result;
    }
    result.consumed = true;
    result.enteredInteractive = true;

    Math::Vector::Vector3 rayOrigin;
    Math::Vector::Vector3 rayDirection;
    Math::Vector::Vector3 cameraUp;
    if ( !TryBuildLauncherCameraRay( &cameras, rayOrigin, rayDirection, cameraUp ) )
    {
        return result;
    }

    const int modelCountBefore = collection.SceneEntityCount();
    replayRuntime.RecordLauncherFireEvent( rayOrigin,
                                           rayDirection,
                                           cameraUp,
                                           m_rayCastTest.fireMode == RunLauncherFireMode::Projectile,
                                           m_rayCastTest.impulseStrength,
                                           m_rayCastTest.projectileSpeed,
                                           modelCountBefore );
    // Why: the launcher is a cold input action, so it repairs any construction-
    // time collection/store drift before entering handle-based physics queries.
    if ( collection.RepairPhysicsBodyAndColliderTopology() && FireLauncherRay( collection,
                                                                               physics,
                                                                               scene,
                                                                               terrain,
                                                                               input.activeModelCapacity,
                                                                               rayOrigin,
                                                                               rayDirection,
                                                                               cameraUp ) )
    {
        scene.modelCount = collection.SceneEntityCount();
    }
    return result;
}

void RuntimeTools::FireLauncherLaser( Physics::PhysicsEngine& physics,
                                      int modelCount,
                                      Geometry::Terrain* terrain,
                                      const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection,
                                      const Math::Vector::Vector3& cameraUp )
{
    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ),
                                             SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ),
                                             rayOrigin,
                                             rayDirection,
                                             RAY_CAST_TEST_MAX_DISTANCE,
                                             modelHitIndex,
                                             modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit =
        TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

    const bool terrainIsClosest = terrainHit && ( !modelHit || terrainHitT < modelHitT );
    const bool hit = modelHit || terrainHit;
    const float hitT = terrainIsClosest ? terrainHitT : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Math::Vector::Vector3 visualEnd = rayOrigin + rayDirection * hitT;
    // Concept: The laser always records visual feedback; only a closest model
    // hit below turns into a physics impulse.
    m_laser.Fire( rayOrigin, rayDirection, cameraUp, hitT, hit );
    AddRayCastTestLine( rayOrigin, visualEnd, hit );

    if ( terrainIsClosest || !modelHit || modelHitIndex < 0 || modelHitIndex >= modelCount )
    {
        return;
    }

    const Physics::PhysicsBodyHandle body =
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).HandleForModelIndex( modelHitIndex );
    const Physics::PhysicsBodyRecord* bodyRecord =
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).RecordForHandle( body );
    if ( !bodyRecord )
    {
        return;
    }

    const Math::Vector::Vector3 hitPoint = rayOrigin + rayDirection * hitT;
    const Math::Vector::Vector3 localApplicationPoint = hitPoint - bodyRecord->position;
    const float mass = (std::max)( 0.001f, bodyRecord->mass );
    const float releaseSpeed = std::clamp( m_rayCastTest.impulseStrength / mass, 1.5f, 36.0f );
    if ( !physics.ReleaseFixedBodyAndAttachedTreeParts( body,
                                                        m_rayCastTest.impulseStrength,
                                                        rayDirection * releaseSpeed,
                                                        Math::Vector::ZERO_VECTOR ) )
    {
        return;
    }
    ApplyLauncherPhysicsImpulse( physics, body, rayDirection * m_rayCastTest.impulseStrength, localApplicationPoint );
}

bool RuntimeTools::FireLauncherProjectile( Basics::SceneController& collection,
                                           Physics::PhysicsEngine& physics,
                                           RunSceneState& scene,
                                           Geometry::Terrain* terrain,
                                           int activeModelCapacity,
                                           int modelCount,
                                           const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection,
                                           const Math::Vector::Vector3& cameraUp )
{
    if ( !terrain || modelCount >= activeModelCapacity )
    {
        return false;
    }

    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ),
                                             SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ),
                                             rayOrigin,
                                             rayDirection,
                                             RAY_CAST_TEST_MAX_DISTANCE,
                                             modelHitIndex,
                                             modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit =
        TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

    const float hitT = terrainHit && ( !modelHit || terrainHitT < modelHitT )
                           ? terrainHitT
                           : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Math::Vector::Vector3 aimPoint = rayOrigin + rayDirection * hitT;
    // Concept: Projectiles spawn slightly in front of and below the camera,
    // then aim at the same closest model/terrain point that the laser would
    // visualize.
    Math::Vector::Vector3 up = cameraUp;
    const float upLenSq = Math::Vector::VectorMagSquared( up );
    up = upLenSq > TOLERANCE * TOLERANCE ? up * ( 1.0f / sqrtf( upLenSq ) ) : Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    const Math::Vector::Vector3 spawn =
        rayOrigin + rayDirection * LAUNCHER_PROJECTILE_SPAWN_LEAD - up * LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET;
    Math::Vector::Vector3 velocityDir = aimPoint - spawn;
    const float velocityDirLenSq = Math::Vector::VectorMagSquared( velocityDir );
    if ( velocityDirLenSq <= TOLERANCE * TOLERANCE )
    {
        velocityDir = rayDirection;
    }
    else
    {
        velocityDir = velocityDir * ( 1.0f / sqrtf( velocityDirLenSq ) );
    }

    const float moment = 0.4f * LAUNCHER_PROJECTILE_MASS * LAUNCHER_PROJECTILE_RADIUS * LAUNCHER_PROJECTILE_RADIUS;
    SceneEntityCreateDesc projectile;
    projectile.SetRenderTint( 0.72f, 0.88f, 1.0f, 1.0f );
    projectile.SetName( "launcher_projectile" );

    const Math::CollisionDetection::BoundingSphere projectileShape( LAUNCHER_PROJECTILE_RADIUS,
                                                                    Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
    const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
    projectile.sceneObjectId = sceneObjectId;
    const auto appendResult = collection.TryCreateSceneEntity(
        std::move( projectile ),
        Physics::MakePhysicsBodyCreateDesc( sceneObjectId,
                                            projectileShape,
                                            spawn,
                                            Math::Orientation::IDENTITY_QUATERNION,
                                            velocityDir * m_rayCastTest.projectileSpeed,
                                            Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                                            Math::Vector::Vector3( moment, moment, moment ),
                                            LAUNCHER_PROJECTILE_MASS,
                                            LAUNCHER_PROJECTILE_RESTITUTION,
                                            Physics::PhysicsBodyMotionKind::Dynamic,
                                            terrain,
                                            "launcher_projectile" ),
        Physics::MakeColliderCreateDesc( projectileShape, LAUNCHER_PROJECTILE_RESTITUTION, HashStr( "default" ) ) );
    if ( !appendResult.status.ok )
    {
        fprintf( stderr,
                 "[runtime-tools] launcher projectile creation failed: %s\n",
                 appendResult.status.error.message );
        return false;
    }
    const Physics::PhysicsBodyHandle projectileBody = appendResult.body;
    if ( projectileBody.IsValid() )
    {
        physics.WakeBody( projectileBody );
    }
    return true;
}

LauncherLaser& RuntimeTools::Laser()
{
    return m_laser;
}

const LauncherLaser& RuntimeTools::Laser() const
{
    return m_laser;
}

RunMousePickupState& RuntimeTools::MousePickup()
{
    return m_mousePickup;
}

const RunMousePickupState& RuntimeTools::MousePickup() const
{
    return m_mousePickup;
}

RunEditorPlacementState& RuntimeTools::Editor()
{
    return m_editor;
}

const RunEditorPlacementState& RuntimeTools::Editor() const
{
    return m_editor;
}

RunEditorTracer& RuntimeTools::EditorTracer()
{
    return m_editorTracer;
}

const RunEditorTracer& RuntimeTools::EditorTracer() const
{
    return m_editorTracer;
}


void RuntimeTools::PrepareOverlayTrace( Basics::SceneController& models,
                                        const Assets::AssetSystem& assets,
                                        const ToolOverlayBuildInput& input )
{
    // Invariant: one owner clears and rebuilds the shared tracer exactly once
    // before replay appends its records for the same frame.
    m_editorTracer.Clear();
    RunInternal::BuildEditorToolOverlayTrace( { m_editor,
                                                m_rayCastTest,
                                                m_mousePickup,
                                                models,
                                                models.BodyStore(),
                                                models.Colliders(),
                                                assets,
                                                m_editorTracer },
                                              { input.rayLingerSeconds,
                                                input.inspectGizmoActive,
                                                input.scaleMode,
                                                input.gesture,
                                                input.attachedCameraTargetIndex,
                                                input.attachedCameraActiveFollow } );
}
} // namespace SkullbonezCore::Basics
