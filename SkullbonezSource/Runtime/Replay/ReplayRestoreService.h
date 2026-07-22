/*
File: SkullbonezSource/Runtime/Replay/ReplayRestoreService.h
Purpose:
  Applies retained replay solver samples back into live runtime owners.

Summary:
  Replay restore is a controlled rollback boundary. The service validates that
  saved scene object ids still match live physics rows, trims presentation/model
  state to the sampled body count, restores body and solver caches, then applies
  the matching world/camera/tool presentation state.

Glossary:
  Solver sample: Replay frame containing restorable physics body rows and hidden
    solver world cache state.
  Scene object id: Stable physics-owned identity used to reject stale model slots.
  Restore context: Frame-local borrow packet for the live owners that a restore
    must mutate together.

Invariants:
  - Restore must reject samples whose body ids no longer match live store rows.
  - Restore borrows SceneWorld once and resolves physics, environment, cameras,
    entities, and stores locally; it never borrows the lifecycle controller.
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
#include "ReplayRestoreTransactions.h"
#include "../CameraCollection.h"
#include "../RunDebugState.h"
#include "../Render/RuntimeRenderer.h"
#include "../Scene/SceneWorld.h"
#include "../Scene/SceneRuntime.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/FatalError.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../World/WorldEnvironment.h"

#include <cstddef>
#include <cstring>
#include <array>

namespace SkullbonezCore
{
namespace Runtime
{
class ReplayRestoreService
{
  public:
    using ResolvedBodyTable =
        std::array<Physics::PhysicsBodyHandle, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

    // Resolves every retained body by stable scene object identity. modelRow is only
    // a cache hint; callers may deliberately pass stale hints to prove that a
    // restore cannot be redirected to another live body.
    static bool ResolveBodiesForRestore( const Physics::PhysicsBodyStore& bodyStore,
                                         const ReplaySolverFrameSample& sample,
                                         ResolvedBodyTable& outBodies,
                                         char* outReason,
                                         std::size_t reasonSize )
    {
        const int liveModelCount = bodyStore.Count();
        if ( sample.bodies.size() > outBodies.size() ||
             sample.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
        {
            WriteReason( outReason, reasonSize, "selected frame needs unavailable bodies" );
            return false;
        }

        const int restoreModelCount = static_cast<int>( sample.bodies.size() );
        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            const Physics::PhysicsBodyHandle liveHandle =
                bodyStore.HandleForSceneObjectId( body.id, body.modelRow.value );
            const Physics::PhysicsBodyRecord* liveBody = bodyStore.RecordForHandle( liveHandle );
            const int liveRow = bodyStore.ModelIndexForHandle( liveHandle );
            // Invariant: the scene object id resolves identity. The retained row is
            // only a cache and cannot redirect restore after topology changes.
            if ( !liveBody || liveBody->sceneObjectId != body.id || liveRow < 0 || liveRow >= restoreModelCount )
            {
                WriteReason( outReason, reasonSize, "selected frame body ids no longer match" );
                return false;
            }
            for ( std::size_t previousIndex = 0; previousIndex < bodyIndex; ++previousIndex )
            {
                // Invariant: one sample row owns one live body. Duplicate ids
                // would otherwise apply two states to one handle while silently
                // leaving another body unrestored.
                if ( outBodies[previousIndex] == liveHandle )
                {
                    WriteReason( outReason, reasonSize, "selected frame contains duplicate body ids" );
                    return false;
                }
            }
            outBodies[bodyIndex] = liveHandle;
        }
        return true;
    }

    static bool ApplySolverSampleState( const ReplaySolverSampleRestoreContext& context,
                                        const ReplaySolverFrameSample& sample,
                                        char* outReason,
                                        std::size_t reasonSize )
    {
        if ( sample.worldSnapshot.physics.version < 1 || sample.worldSnapshot.physics.version > 2 )
        {
            WriteReason( outReason, reasonSize, "unsupported snapshot version" );
            return false;
        }

        if ( sample.worldSnapshot.physics.modelCount != static_cast<int>( sample.bodies.size() ) )
        {
            WriteReason( outReason, reasonSize, "snapshot body count mismatch" );
            return false;
        }

        const int restoreModelCount = static_cast<int>( sample.bodies.size() );
        ResolvedBodyTable resolvedBodies{};
        Physics::PhysicsEngine& physics = context.world.Physics();
        if ( !ResolveBodiesForRestore( Physics::PhysicsEngine::ReadBodies( physics ),
                                       sample,
                                       resolvedBodies,
                                       outReason,
                                       reasonSize ) )
        {
            return false;
        }

        if ( !context.world.TrimForReplayRestore( restoreModelCount ) )
        {
            WriteReason( outReason, reasonSize, "failed to trim live model list" );
            return false;
        }
        context.scene.ResetSceneObjectIdCursor( Physics::PhysicsEngine::ReadBodies( physics ) );

        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            Math::Orientation::Quaternion orientation( body.orientation[0],
                                                       body.orientation[1],
                                                       body.orientation[2],
                                                       body.orientation[3] );
            if ( !physics.RestoreReplayBodyState( resolvedBodies[bodyIndex],
                                                  body.id,
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
                SB_FATAL( "Runtime/ReplayRestore",
                          "Replay body commit failed after stable-id preflight; live state may be partially restored" );
            }
        }
        physics.ClearPendingBodyImpulses();

        if ( !physics.RestoreReplaySolverSnapshot(
                 sample.worldSnapshot.physics,
                 Physics::MakePhysicsBodyCountFromNonNegativeInt( restoreModelCount ) ) )
        {
            SB_FATAL( "Runtime/ReplayRestore",
                      "Replay solver commit rejected the version/count values accepted during preflight" );
        }
        context.world.Tornado().SetReplayState( sample.worldSnapshot.tornadoCaptureSeconds,
                                                sample.worldSnapshot.tornadoEjectCooldownSeconds,
                                                sample.worldSnapshot.tornadoConfig,
                                                sample.worldSnapshot.tornadoSystemConfig,
                                                sample.worldSnapshot.tornadoSystemElapsedSeconds );

        context.world.Environment().SetGravity( sample.world.gravity );
        context.world.Environment().SetFluidSurfaceHeight( sample.world.fluidHeight );
        context.world.Environment().SetFluidDensity( sample.world.fluidDensity );
        context.debug.isWaterHidden = sample.world.waterHidden;
        context.debug.isTerrainHidden = sample.world.terrainHidden;
        context.scene.isFixedStep = sample.world.fixedStep;
        context.scene.isScenePhysics = sample.world.scenePhysicsEnabled;
        context.scene.isSceneText = sample.world.sceneTextEnabled;
        context.scene.modelCount = restoreModelCount;
        if ( context.world.Tornado().VisualAutoEnableWithTornado() )
        {
            context.world.Tornado().SetVisualEnabled( sample.worldSnapshot.tornadoConfig.enabled ||
                                                      sample.worldSnapshot.tornadoSystemConfig.enabled );
        }

        context.world.Cameras().CancelTween();
        context.world.Cameras().SetPrimaryPosition( sample.camera.eye );
        context.world.Cameras().SetViewCoordinates( sample.camera.view );
        context.world.Cameras().SetCamera();

        context.runtimeTools.RestoreReplayLauncherVisualSample( sample.launcherVisual );
        WriteReason( outReason, reasonSize, "applied" );
        return true;
    }

    // Captures the live stores through a one-frame verifier recorder so hash
    // calculation uses the exact same field order as normal replay capture.
    static bool CaptureCurrentSolverSample( const ReplaySolverSampleRestoreContext& context,
                                            const ReplaySolverFrameSample& reference,
                                            ReplaySolverFrameSample& outSample )
    {
        ReplayRecorderConfig config;
        config.enabled = true;
        config.retentionSeconds = 1;
        config.checkpointIntervalFrames = 1;

        ReplaySolverRecorder verifier;
        if ( !verifier.Configure( config ) )
        {
            return false;
        }

        ReplayLauncherVisualSample launcherVisual;
        context.runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );

        ReplayCaptureInput input;
        input.branch = reference.branch;
        input.eventCursor = reference.eventCursor;
        input.sceneFrame = reference.sceneFrame;
        input.simulationSeconds = reference.simulationSeconds;
        input.physicsDt = reference.physicsDt > 0.0f ? reference.physicsDt : PHYSICS_FIXED_DT;
        input.fixedStep = context.scene.isFixedStep;
        input.scenePhysicsEnabled = context.scene.isScenePhysics;
        input.sceneTextEnabled = context.scene.isSceneText;
        input.waterHidden = context.debug.isWaterHidden;
        input.terrainHidden = context.debug.isTerrainHidden;
        input.cameras = &context.world.Cameras();
        input.world = &context.world.Environment();
        input.physics = &context.world.Physics();
        input.tornadoGameplay = &context.world.Tornado();
        input.entities = &context.world.Entities();
        input.bodyStore = &context.world.BodyStore();
        input.colliderStore = &context.world.Colliders();
        input.launcherVisual = &launcherVisual;
        verifier.CaptureFrame( input );

        const ReplaySolverFrameSample* verified = verifier.LatestSample();
        if ( !verified )
        {
            return false;
        }
        outSample = *verified;
        return true;
    }

    static bool CaptureCurrentSolverHash( const ReplaySolverSampleRestoreContext& context,
                                          const ReplaySolverFrameSample& reference,
                                          uint64_t& outSolverHash,
                                          uint64_t& outPresentationHash,
                                          std::size_t& outBodyCount )
    {
        ReplaySolverFrameSample verified;
        if ( !CaptureCurrentSolverSample( context, reference, verified ) )
        {
            return false;
        }
        outSolverHash = verified.solverHash;
        outPresentationHash = verified.presentationHash;
        outBodyCount = verified.bodies.size();
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
} // namespace Runtime
} // namespace SkullbonezCore
