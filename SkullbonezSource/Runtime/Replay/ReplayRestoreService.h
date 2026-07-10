/*
File: SkullbonezSource/Runtime/Replay/ReplayRestoreService.h
Purpose:
  Applies retained replay solver samples back into live runtime owners.

Mental model:
  Replay restore is a controlled rollback boundary. The service validates that
  saved replay body ids still match live physics rows, trims presentation/model
  state to the sampled body count, restores body and solver caches, then applies
  the matching world/camera/tool presentation state.

Glossary:
  Solver sample: Replay frame containing restorable physics body rows and hidden
    solver world cache state.
  Replay body id: Stable physics-owned identity used to reject stale model slots.
  Restore context: Frame-local borrow packet for the live owners that a restore
    must mutate together.

Invariants:
  - Restore must reject samples whose body ids no longer match live store rows.
  - Body state, solver caches, world settings, scene flags, and tool visuals are
    restored as one ordered operation.
  - The service must not store context borrows after returning.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Physics/PhysicsEngine.h
*/
#pragma once

#include "ReplayRecorder.h"
#include "../CameraCollection.h"
#include "../RunDebugState.h"
#include "../RunRuntimeSettings.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneRuntime.h"
#include "../Tools/RuntimeTools.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsEngineStoreQueries.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../World/WorldEnvironment.h"

#include <cstddef>
#include <cstring>
#include <array>

namespace SkullbonezCore
{
namespace Basics
{
struct ReplaySolverSampleRestoreContext
{
    // Lifetime: Run builds this from live owners for one restore call. Every
    // referenced subsystem outlives the call, and ReplayRestoreService copies
    // only sampled values into those owners.
    GameObjects::GameModelCollection& presentations;
    Physics::PhysicsEngine& physics;
    SceneController& sceneController;
    Environment::WorldEnvironment& world;
    RunSceneState& scene;
    RunRuntimeSettings& runtimeSettings;
    RunDebugState& debug;
    Environment::CameraCollection* cameras = nullptr;
    RuntimeTools& runtimeTools;
};

class ReplayRestoreService
{
  public:
    static bool ApplySolverSampleState( const ReplaySolverSampleRestoreContext& context,
                                        const ReplaySolverFrameSample& sample,
                                        char* outReason,
                                        std::size_t reasonSize )
    {
        if ( sample.worldSnapshot.version < 1 || sample.worldSnapshot.version > 2 )
        {
            WriteReason( outReason, reasonSize, "unsupported snapshot version" );
            return false;
        }

        if ( sample.worldSnapshot.modelCount != static_cast<int>( sample.bodies.size() ) )
        {
            WriteReason( outReason, reasonSize, "snapshot body count mismatch" );
            return false;
        }

        const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngineStoreQueries::BodyStore( context.physics );
        const int liveModelCount = bodyStore.Count();
        if ( sample.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
        {
            WriteReason( outReason, reasonSize, "selected frame needs unavailable bodies" );
            return false;
        }

        const int restoreModelCount = static_cast<int>( sample.bodies.size() );
        std::array<Physics::PhysicsBodyHandle, MAX_GAME_MODELS> resolvedBodies{};
        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            const Physics::PhysicsBodyHandle liveHandle =
                bodyStore.HandleForReplayBodyId( body.id.value, body.modelRow.value );
            const Physics::PhysicsBodyRecord* liveBody = bodyStore.RecordForHandle( liveHandle );
            const int liveRow = bodyStore.ModelIndexForHandle( liveHandle );
            // Invariant: the replay id resolves identity. The retained row is
            // only a cache and cannot redirect restore after topology changes.
            if ( !liveBody || liveBody->replayBodyId != body.id.value || liveRow < 0 || liveRow >= restoreModelCount )
            {
                WriteReason( outReason, reasonSize, "selected frame body ids no longer match" );
                return false;
            }
            resolvedBodies[bodyIndex] = liveHandle;
        }

        if ( !context.sceneController.TrimForReplayRestore( context.presentations,
                                                            context.physics,
                                                            restoreModelCount ) )
        {
            WriteReason( outReason, reasonSize, "failed to trim live model list" );
            return false;
        }
        context.scene.ResetSceneObjectIdCursor( Physics::PhysicsEngineStoreQueries::BodyStore( context.physics ) );

        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            Math::Orientation::Quaternion orientation( body.orientation[0],
                                                       body.orientation[1],
                                                       body.orientation[2],
                                                       body.orientation[3] );
            if ( !context.physics.RestoreReplayBodyState( resolvedBodies[bodyIndex],
                                                          body.id.value,
                                                          body.fixed,
                                                          body.position,
                                                          orientation,
                                                          body.linearVelocity,
                                                          body.angularVelocity,
                                                          body.mass,
                                                          body.inverseMass,
                                                          body.rotationalInertia,
                                                          body.inverseRotationalInertia ) )
            {
                WriteReason( outReason, reasonSize, "failed to restore replay body state" );
                return false;
            }
        }
        context.physics.ClearPendingBodyImpulses();

        if ( !context.physics.RestoreReplaySolverSnapshot(
                 sample.worldSnapshot,
                 Physics::MakePhysicsBodyCountFromNonNegativeInt( restoreModelCount ) ) )
        {
            WriteReason( outReason, reasonSize, "failed to restore solver world snapshot" );
            return false;
        }

        context.world.SetGravity( sample.world.gravity );
        context.world.SetFluidSurfaceHeight( sample.world.fluidHeight );
        context.world.SetFluidDensity( sample.world.fluidDensity );
        context.debug.isWaterHidden = sample.world.waterHidden;
        context.debug.isTerrainHidden = sample.world.terrainHidden;
        context.scene.isFixedStep = sample.world.fixedStep;
        context.scene.isScenePhysics = sample.world.scenePhysicsEnabled;
        context.scene.isSceneText = sample.world.sceneTextEnabled;
        context.scene.modelCount = restoreModelCount;
        context.runtimeSettings.isPhysicsSleepEnabled = sample.worldSnapshot.sleepEnabled;
        context.runtimeSettings.tornadoField = sample.worldSnapshot.tornadoConfig;
        context.runtimeSettings.tornadoSystem = sample.worldSnapshot.tornadoSystemConfig;
        if ( context.runtimeSettings.tornadoVisual.autoEnableWithTornado )
        {
            context.runtimeSettings.tornadoVisual.enabled =
                context.runtimeSettings.tornadoField.enabled || context.runtimeSettings.tornadoSystem.enabled;
        }

        if ( context.cameras )
        {
            context.cameras->CancelTween();
            context.cameras->SetPrimaryPosition( sample.camera.eye );
            context.cameras->SetViewCoordinates( sample.camera.view );
            context.cameras->SetCamera();
        }

        context.runtimeTools.RestoreReplayLauncherVisualSample( sample.launcherVisual );
        WriteReason( outReason, reasonSize, "applied" );
        return true;
    }

  private:
    static void WriteReason( char* outReason, std::size_t reasonSize, const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    }
};
} // namespace Basics
} // namespace SkullbonezCore
