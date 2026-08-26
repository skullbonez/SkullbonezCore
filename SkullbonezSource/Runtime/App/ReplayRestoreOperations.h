/*
File: SkullbonezSource/Runtime/App/ReplayRestoreOperations.h
Purpose:
  Applies retained replay solver samples back into live runtime owners.

Summary:
  App owns Replay restore as a controlled cross-owner rollback boundary. The operation resolves
  durable body identities, preflights surviving joint topology before mutation,
  trims live owners, commits body rows then solver state, and applies matching world,
  camera, and presentation state, then reports success so App can apply the
  detached launcher packet to its Tools sibling.

Glossary:
  Restore operands: Concrete live owners borrowed only for one synchronous
    apply or capture call.

Invariants:
  - Restore must reject samples whose body ids no longer match live store rows.
  - Restore borrows SceneWorld once and resolves physics, environment, cameras,
    entities, and stores locally; it never borrows the lifecycle controller.
  - Body state, solver caches, world settings, and scene flags are restored as
    one ordered operation; App applies tool visuals only after success.
  - Physics body rows commit before solver state because body restoration
    invalidates topology-derived caches; the solver snapshot is the final
    Physics commit that makes versioned hysteresis bytes authoritative.
  - Every recoverable rejection occurs before TrimForReplayRestore; rejection
    after trim is fatal because the commit boundary has already been crossed.
  - The operation stores no borrowed reference after returning.

Related:
  - SkullbonezSource/Runtime/App/ReplayValidation.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezTests/TestPhysicsHandles.cpp
  - SkullbonezTests/TestDeterminism.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Replay/ReplayRecorder.h"
#include "../Camera/CameraCollection.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Scene/SceneWorld.h"
#include "../Scene/SceneSessionState.h"
#include "../../Core/FatalError.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace SkullbonezCore
{
namespace Runtime
{
class ReplayRestoreOperations
{
  public:
    using ResolvedBodyTable = std::array<Physics::PhysicsBodyHandle, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

    static void ApplyCameraSample( Environment::CameraCollection& cameras, const ReplayCameraSample& sample )
    {
        // Invariant: eye, target, and up form one recorded pose. Publishing
        // only two components would retain roll from the unrelated live slot.
        cameras.CancelTween();
        cameras.SetPrimaryPose( sample.eye, sample.view, sample.up );
        cameras.SetCamera();
    }

    // Resolves every retained body by stable scene object identity. modelRow is only
    // a cache hint; callers may deliberately pass stale hints to prove that a
    // restore cannot be redirected to another live body.
    static bool ResolveBodiesForRestore( const Physics::PhysicsBodyStore& bodyStore, const ReplaySolverFrameSample& sample,
                                         ResolvedBodyTable& outBodies, char* outReason, std::size_t reasonSize )
    {
        const int liveModelCount = bodyStore.Count();

        if ( sample.bodies.size() > outBodies.size() || sample.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
        {
            WriteReason( outReason, reasonSize, "selected frame needs unavailable bodies" );
            return false;
        }

        const int restoreModelCount = static_cast<int>( sample.bodies.size() );

        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            const Physics::PhysicsBodyHandle liveHandle = bodyStore.HandleForSceneObjectId( body.id, body.modelRow.value );
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

    static bool ApplySolverSampleState( SceneWorld& world, SceneSessionState& scene, OverlayDebugState& debug,
                                        const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize )
    {
        if ( sample.worldSnapshot.physics.version < 1 ||
             sample.worldSnapshot.physics.version > Physics::PHYSICS_SOLVER_SNAPSHOT_VERSION )
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
        ResolvedBodyTable resolvedBodies {};
        Physics::PhysicsEngine& physics = world.Physics();

        if ( !ResolveBodiesForRestore( Physics::PhysicsEngine::ReadBodies( physics ), sample, resolvedBodies, outReason,
                                       reasonSize ) )
        {
            return false;
        }

        // Invariant: this is the last recoverable check before trimming any
        // live topology. Physics validates the exact joint rows that will
        // survive the requested body-count trim, not the larger live set.
        if ( !physics.CanRestoreReplaySolverSnapshot( sample.worldSnapshot.physics,
                                                      Physics::MakePhysicsBodyCountFromNonNegativeInt( restoreModelCount ) ) )
        {
            WriteReason( outReason, reasonSize, "snapshot solver topology mismatch" );
            return false;
        }

        if ( !world.TrimForReplayRestore( restoreModelCount ) )
        {
            WriteReason( outReason, reasonSize, "failed to trim live model list" );
            return false;
        }

        scene.ResetSceneObjectIdCursor( Physics::PhysicsEngine::ReadBodies( physics ) );

        for ( std::size_t bodyIndex = 0; bodyIndex < sample.bodies.size(); ++bodyIndex )
        {
            const ReplaySolverBodySample& body = sample.bodies[bodyIndex];
            Math::Orientation::Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2],
                                                       body.orientation[3] );
            const Physics::PhysicsBodyRestoreState restore { resolvedBodies[bodyIndex],
                                                             body.id,
                                                             body.fixed,
                                                             body.position,
                                                             orientation,
                                                             body.linearVelocity,
                                                             body.angularVelocity,
                                                             body.mass,
                                                             body.inverseMass,
                                                             body.rotationalInertia,
                                                             body.inverseRotationalInertia };

            if ( !physics.RestoreReplayBodyState( restore ) )
            {
                SB_FATAL( "Runtime/ReplayRestore",
                          "Replay body commit failed after stable-id preflight; live state may be partially restored" );
            }
        }

        // Invariant: body rows restore first because each body mutation
        // invalidates topology-derived stage state. The solver snapshot is the
        // final Physics commit and makes v4 hysteresis bytes authoritative.
        if ( !physics.RestoreReplaySolverSnapshot( sample.worldSnapshot.physics,
                                                   Physics::MakePhysicsBodyCountFromNonNegativeInt( restoreModelCount ) ) )
        {
            SB_FATAL( "Runtime/ReplayRestore",
                      "Replay solver commit rejected topology accepted by the pre-mutation preflight" );
        }

        physics.ClearPendingBodyImpulses();

        world.Tornado().SetReplayState( sample.worldSnapshot.tornadoCaptureSeconds,
                                        sample.worldSnapshot.tornadoEjectCooldownSeconds, sample.worldSnapshot.tornadoConfig,
                                        sample.worldSnapshot.tornadoSystemConfig,
                                        sample.worldSnapshot.tornadoSystemElapsedSeconds );

        world.Environment().SetGravity( sample.world.gravity );
        world.Environment().SetFluidSurfaceHeight( sample.world.fluidHeight );
        world.Environment().SetFluidDensity( sample.world.fluidDensity );
        debug.isWaterHidden = sample.world.waterHidden;
        debug.isTerrainHidden = sample.world.terrainHidden;

        // Compatibility: restore the saved scene/capture request; Runtime
        // resolves effective pacing again from live launch/session context.
        scene.isFixedStep = sample.world.fixedStep;
        scene.isScenePhysics = sample.world.scenePhysicsEnabled;
        scene.isSceneText = sample.world.sceneTextEnabled;
        scene.modelCount = restoreModelCount;

        if ( world.Tornado().VisualAutoEnableWithTornado() )
        {
            world.Tornado().SetVisualEnabled( sample.worldSnapshot.tornadoConfig.enabled ||
                                              sample.worldSnapshot.tornadoSystemConfig.enabled );
        }

        ApplyCameraSample( world.Cameras(), sample.camera );

        WriteReason( outReason, reasonSize, "applied" );
        return true;
    }

    // Captures the live stores through a one-frame verifier recorder so hash
    // calculation uses the exact same field order as normal replay capture.
    static bool CaptureCurrentSolverSample( SceneWorld& world, const SceneSessionState& scene,
                                            const OverlayDebugState& debug, const ReplayLauncherVisualSample& launcherVisual,
                                            const ReplaySolverFrameSample& reference, ReplaySolverFrameSample& outSample )
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

        ReplayWorldPresentationSample worldSample;
        worldSample.gravity = world.Environment().GetGravity();
        worldSample.fluidHeight = world.Environment().GetFluidSurfaceHeight();
        worldSample.fluidDensity = world.Environment().GetFluidDensity();
        worldSample.fixedStep = scene.isFixedStep;
        worldSample.scenePhysicsEnabled = scene.isScenePhysics;
        worldSample.sceneTextEnabled = scene.isSceneText;
        worldSample.waterHidden = debug.isWaterHidden;
        worldSample.terrainHidden = debug.isTerrainHidden;

        ReplayCameraSample cameraSample;
        cameraSample.eye = world.Cameras().GetCameraTranslation();
        cameraSample.view = world.Cameras().GetCameraView();
        cameraSample.up = world.Cameras().GetCameraUp();

        std::array<const char*, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> entityDisplayNames = {};
        const SceneEntityStore& entities = world.Entities();
        const std::size_t entityNameCount = (std::min)( static_cast<std::size_t>( entities.Count() ),
                                                        entityDisplayNames.size() );

        for ( std::size_t entityIndex = 0; entityIndex < entityNameCount; ++entityIndex )
        {
            const SceneEntityRecord* entity = entities.TryGet( static_cast<int>( entityIndex ) );
            entityDisplayNames[entityIndex] = entity ? entity->displayName : nullptr;
        }

        verifier.CaptureFrame( reference.branch, reference.eventCursor, reference.sceneFrame,
                               reference.physicsDt > 0.0f ? reference.physicsDt : PHYSICS_FIXED_DT, worldSample,
                               cameraSample, launcherVisual, world.Physics(), world.Tornado(),
                               std::span<const char* const>( entityDisplayNames.data(), entityNameCount ) );

        const ReplaySolverFrameSample* verified = verifier.LatestSample();

        if ( !verified )
        {
            return false;
        }

        outSample = *verified;
        return true;
    }

    static bool CaptureCurrentSolverHash( SceneWorld& world, const SceneSessionState& scene, const OverlayDebugState& debug,
                                          const ReplayLauncherVisualSample& launcherVisual,
                                          const ReplaySolverFrameSample& reference, uint64_t& outSolverHash,
                                          uint64_t& outPresentationHash, std::size_t& outBodyCount )
    {
        ReplaySolverFrameSample verified;

        if ( !CaptureCurrentSolverSample( world, scene, debug, launcherVisual, reference, verified ) )
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
