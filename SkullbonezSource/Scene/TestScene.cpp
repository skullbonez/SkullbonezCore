/*
File: SkullbonezSource/Scene/TestScene.cpp
Purpose:
  Stores parsed test-scene JSON and applies it to runtime scene state.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Lane R result: Recoverable load outcome carrying owner/message diagnostics
    for authored scene/style data failures.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene JSON spellings are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/Scene/TestScene.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "TestScene.h"

#include <exception>


using namespace SkullbonezCore::Basics;

namespace
{
SbResult
TryLoadSceneFile( const char* path, SkullbonezCore::Assets::AssetContext assets, bool styleOnly, TestScene& outScene )
{
    try
    {
        outScene = styleOnly ? LoadStyleSceneFromFileImpl( path, assets ) : LoadTestSceneFromFileImpl( path, assets );
        return SbResult::Success();
    }
    catch ( const std::exception& e )
    {
        // Why: the parser still uses exceptions internally until the remaining
        // fable-05 P4.1 rows thread SbResult through every JSON helper. Runtime
        // callers get Lane R diagnostics now instead of a process-level escape.
        return SbResult::Failure( "Scene/TestSceneParser", "%s", e.what() );
    }
}
} // namespace


TestScene::TestScene()
{
}


TestScene TestScene::LoadFromFile( const char* path )
{
    return LoadTestSceneFromFileImpl( path, Assets::AssetContext{} );
}


SbResult TestScene::TryLoadFromFile( const char* path, TestScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext{}, false, outScene );
}


TestScene TestScene::LoadFromFile( const char* path, const Assets::AssetSystem& assets )
{
    return LoadTestSceneFromFileImpl( path, Assets::AssetContext{ &assets } );
}


SbResult TestScene::TryLoadFromFile( const char* path, const Assets::AssetSystem& assets, TestScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext{ &assets }, false, outScene );
}


TestScene TestScene::LoadStyleFromFile( const char* path )
{
    return LoadStyleSceneFromFileImpl( path, Assets::AssetContext{} );
}


SbResult TestScene::TryLoadStyleFromFile( const char* path, TestScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext{}, true, outScene );
}


TestScene TestScene::LoadStyleFromFile( const char* path, const Assets::AssetSystem& assets )
{
    return LoadStyleSceneFromFileImpl( path, Assets::AssetContext{ &assets } );
}


SbResult TestScene::TryLoadStyleFromFile( const char* path, const Assets::AssetSystem& assets, TestScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext{ &assets }, true, outScene );
}


bool TestScene::IsPhysicsEnabled() const
{
    return m_sceneOptions.isPhysicsEnabled;
}


bool TestScene::IsTextEnabled() const
{
    return m_sceneOptions.isTextEnabled;
}


bool TestScene::IsTextOnly() const
{
    return m_sceneOptions.isTextOnly;
}


bool TestScene::IsWaterHidden() const
{
    return m_sceneOptions.waterHidden;
}


bool TestScene::IsTerrainHidden() const
{
    return m_sceneOptions.terrainHidden;
}


bool TestScene::IsEditableScene() const
{
    return m_sceneOptions.editableScene;
}


bool TestScene::HasCinematicRenderingOverride() const
{
    return m_sceneOptions.hasCinematicRenderingOverride;
}


bool TestScene::IsCinematicRenderingEnabled() const
{
    return m_sceneOptions.cinematicRendering;
}


bool TestScene::HasCinematicExposure() const
{
    return m_sceneOptions.hasCinematicExposure;
}


float TestScene::GetCinematicExposure() const
{
    return m_sceneOptions.cinematicExposure;
}


bool TestScene::HasCinematicGamma() const
{
    return m_sceneOptions.hasCinematicGamma;
}


float TestScene::GetCinematicGamma() const
{
    return m_sceneOptions.cinematicGamma;
}


uint64_t TestScene::GetCinematicOverrideMask() const
{
    return m_sceneOptions.cinematicOverrideMask;
}


const CinematicRenderConfig& TestScene::GetCinematicRenderConfig() const
{
    return m_sceneOptions.cinematicRender;
}


int TestScene::GetFrameCount() const
{
    return m_sceneOptions.frameCount;
}


const char* TestScene::GetScreenshotPath() const
{
    return m_captureOptions.screenshotPath;
}


int TestScene::GetScreenshotFrame() const
{
    return m_captureOptions.screenshotFrame;
}


int TestScene::GetScreenshotMs() const
{
    return m_captureOptions.screenshotMs;
}


unsigned int TestScene::GetSeed() const
{
    return m_sceneOptions.seed;
}


int TestScene::GetSolverBallCount() const
{
    return m_sceneOptions.solverBallCount;
}


int TestScene::GetSolverBoxCount() const
{
    return m_sceneOptions.solverBoxCount;
}


bool TestScene::HasModelCapacityOverride() const
{
    return m_sceneOptions.modelCapacity > 0;
}


int TestScene::GetModelCapacity() const
{
    return m_sceneOptions.modelCapacity;
}


bool TestScene::HasWorkerThreadOverride() const
{
    return m_sceneOptions.workerThreads >= -1;
}


int TestScene::GetWorkerThreads() const
{
    return m_sceneOptions.workerThreads;
}


const char* TestScene::GetPerfLogPath() const
{
    return m_loggingOptions.perfLogPath;
}


bool TestScene::IsPerfLogFlushEnabled() const
{
    return m_loggingOptions.isPerfLogFlush;
}


int TestScene::GetPerfLogFlushInterval() const
{
    return m_loggingOptions.perfLogFlushInterval;
}

bool TestScene::HasVsyncOverride() const
{
    return m_runtimeOverrides.hasVsyncOverride;
}


bool TestScene::IsVsyncEnabled() const
{
    return m_runtimeOverrides.isVsyncEnabled;
}


bool TestScene::HasPipelineSyncOverride() const
{
    return m_runtimeOverrides.hasPipelineSyncOverride;
}


bool TestScene::IsPipelineSyncEnabled() const
{
    return m_runtimeOverrides.isPipelineSyncEnabled;
}


int TestScene::GetScreenshotInterval() const
{
    return m_captureOptions.screenshotInterval;
}


const char* TestScene::GetScreenshotDir() const
{
    return m_captureOptions.screenshotDir;
}


int TestScene::GetCameraCount() const
{
    return static_cast<int>( m_cameras.size() );
}


float TestScene::GetTimeScale() const
{
    return m_sceneOptions.timeScale;
}


bool TestScene::IsFixedStep() const
{
    return m_sceneOptions.isFixedStep;
}


bool TestScene::ShouldPauseSnapshotState() const
{
    return m_sceneOptions.pauseSnapshotState;
}


uint32_t TestScene::GetPhysicsDebugFlags() const
{
    return m_sceneOptions.physicsDebugFlags;
}


bool TestScene::IsPhysicsDebugTransparent() const
{
    return m_sceneOptions.physicsDebugTransparent;
}


float TestScene::GetPhysicsDebugAlpha() const
{
    return m_sceneOptions.physicsDebugAlpha;
}


float TestScene::GetPhysicsDebugContactLinger() const
{
    return m_sceneOptions.physicsDebugContactLinger;
}


float TestScene::GetTrackHeight() const
{
    return m_sceneOptions.trackHeight;
}


float TestScene::GetAutoCycleInterval() const
{
    return m_sceneOptions.autoCycleInterval;
}


bool TestScene::IsScreenshotAndExit() const
{
    return m_sceneOptions.screenshotAndExit;
}


bool TestScene::IsExitOnComplete() const
{
    return m_sceneOptions.exitOnComplete;
}


bool TestScene::IsCollisionVisualizerEnabled() const
{
    return m_sceneOptions.collisionVisualizer;
}


bool TestScene::IsBroadphaseOverlayEnabled() const
{
    return m_sceneOptions.broadphaseOverlay;
}


bool TestScene::IsWaterFreezeDebugEnabled() const
{
    return m_sceneOptions.waterFreezeDebug;
}


bool TestScene::IsWaterFlatDebugEnabled() const
{
    return m_sceneOptions.waterFlatDebug;
}


int TestScene::GetWaterReflectionMode() const
{
    return m_sceneOptions.waterReflectionMode;
}


bool TestScene::HasFlatSlope() const
{
    return m_terrainOverride.hasFlatSlope;
}


float TestScene::GetFlatBaseY() const
{
    return m_terrainOverride.flatBaseY;
}


float TestScene::GetFlatSlopeX() const
{
    return m_terrainOverride.flatSlopeX;
}


float TestScene::GetFlatSlopeZ() const
{
    return m_terrainOverride.flatSlopeZ;
}


int TestScene::GetBallCount() const
{
    return static_cast<int>( m_balls.size() );
}


const SceneCamera& TestScene::GetCamera( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_cameras.size() ) )
    {
        throw std::runtime_error( "Camera index out of range.  (TestScene::GetCamera)" );
    }

    return m_cameras[index];
}


const SceneBall& TestScene::GetBall( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_balls.size() ) )
    {
        throw std::runtime_error( "Ball index out of range.  (TestScene::GetBall)" );
    }

    return m_balls[index];
}


int TestScene::GetBallStateCount() const
{
    return static_cast<int>( m_ballStates.size() );
}


const SceneBallState& TestScene::GetBallState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_ballStates.size() ) )
    {
        throw std::runtime_error( "BallState index out of range.  (TestScene::GetBallState)" );
    }

    return m_ballStates[index];
}


int TestScene::GetBoxStateCount() const
{
    return static_cast<int>( m_boxStates.size() );
}


const SceneBoxState& TestScene::GetBoxState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_boxStates.size() ) )
    {
        throw std::runtime_error( "BoxState index out of range.  (TestScene::GetBoxState)" );
    }

    return m_boxStates[index];
}


int TestScene::GetBoxCount() const
{
    return static_cast<int>( m_boxes.size() );
}


const SceneBox& TestScene::GetBox( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_boxes.size() ) )
    {
        throw std::runtime_error( "Box index out of range.  (TestScene::GetBox)" );
    }

    return m_boxes[index];
}


int TestScene::GetConvexHullCount() const
{
    return static_cast<int>( m_convexHulls.size() );
}


const SceneConvexHull& TestScene::GetConvexHull( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_convexHulls.size() ) )
    {
        throw std::runtime_error( "ConvexHull index out of range.  (TestScene::GetConvexHull)" );
    }

    return m_convexHulls[index];
}


int TestScene::GetConvexHullStateCount() const
{
    return static_cast<int>( m_convexHullStates.size() );
}


const SceneConvexHullState& TestScene::GetConvexHullState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_convexHullStates.size() ) )
    {
        throw std::runtime_error( "ConvexHullState index out of range.  (TestScene::GetConvexHullState)" );
    }

    return m_convexHullStates[index];
}


int TestScene::GetRagdollCount() const
{
    return static_cast<int>( m_ragdolls.size() );
}


const SceneRagdoll& TestScene::GetRagdoll( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_ragdolls.size() ) )
    {
        throw std::runtime_error( "Ragdoll index out of range.  (TestScene::GetRagdoll)" );
    }

    return m_ragdolls[index];
}


int TestScene::GetPointJointConstraintCount() const
{
    return static_cast<int>( m_pointJointConstraints.size() );
}


const ScenePointJointConstraint& TestScene::GetPointJointConstraint( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_pointJointConstraints.size() ) )
    {
        throw std::runtime_error( "PointJointConstraint index out of range.  (TestScene::GetPointJointConstraint)" );
    }

    return m_pointJointConstraints[index];
}


int TestScene::GetRequiredContactCount() const
{
    return static_cast<int>( m_requiredContacts.size() );
}


const SceneRequiredContact& TestScene::GetRequiredContact( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_requiredContacts.size() ) )
    {
        throw std::runtime_error( "RequiredContact index out of range.  (TestScene::GetRequiredContact)" );
    }

    return m_requiredContacts[index];
}


int TestScene::GetRequiredBroadphaseXCellCount() const
{
    return static_cast<int>( m_requiredBroadphaseXCells.size() );
}


const SceneRequiredBroadphaseXCells& TestScene::GetRequiredBroadphaseXCell( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_requiredBroadphaseXCells.size() ) )
    {
        throw std::runtime_error(
            "RequiredBroadphaseXCell index out of range.  (TestScene::GetRequiredBroadphaseXCell)" );
    }

    return m_requiredBroadphaseXCells[index];
}


int TestScene::GetObjectMaterialOverrideCount() const
{
    return static_cast<int>( m_objectMaterials.size() );
}


const SceneObjectMaterialOverride& TestScene::GetObjectMaterialOverride( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_objectMaterials.size() ) )
    {
        throw std::runtime_error(
            "Object material override index out of range.  (TestScene::GetObjectMaterialOverride)" );
    }

    return m_objectMaterials[index];
}


bool TestScene::HasWorldOverride() const
{
    return m_worldOverride.hasWorldOverride;
}


float TestScene::GetWorldGravity() const
{
    return m_worldOverride.worldGravity;
}


float TestScene::GetWorldFluidHeight() const
{
    return m_worldOverride.worldFluidHeight;
}


float TestScene::GetWorldFluidDensity() const
{
    return m_worldOverride.worldFluidDensity;
}


bool TestScene::HasTornadoSystem() const
{
    return m_tornadoSystem.hasTornadoSystem;
}


const SkullbonezCore::Physics::TornadoSystemConfig& TestScene::GetTornadoSystemConfig() const
{
    return m_tornadoSystem.config;
}


const SceneUIOptions& TestScene::GetUIOptions() const
{
    return m_UIOptions;
}
