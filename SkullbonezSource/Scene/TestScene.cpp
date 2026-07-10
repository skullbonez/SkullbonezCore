/*
File: SkullbonezSource/Scene/TestScene.cpp
Purpose:
  Stores parsed test-scene JSON and applies it to runtime scene state.

Mental model:
  TestScene.cpp stores parsed test-scene JSON and applies it to runtime scene
  state. As an implementation unit, keep edits anchored on scene-file parsing
  or snapshot contracts and on the glossary/invariants below.

Glossary:
  Scene collection: Vector-backed parsed scene array for cameras, bodies,
    constraints, or validation expectations that authored setup later replays
    into runtime owners.
  Lane F fatal: Should-never-happen caller/scene-state invariant reported with
    owner diagnostics before process termination.
  Lane R result: Recoverable load outcome carrying owner/message diagnostics
    for authored scene/style data failures.

Invariants:
  - Command-line and scene JSON spellings are user-facing compatibility
    surface.
  - Scene collection getters require indices produced from the matching count
    API; out-of-range access means authored setup and parsed scene state have
    diverged.

Related:
  - SkullbonezSource/Scene/TestScene.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "TestScene.h"

#include "../Core/FatalError.h"

using namespace SkullbonezCore::Basics;

namespace
{
[[noreturn]] void
FatalSceneIndexOutOfRange( const char* collectionName, const char* functionName, int index, int count )
{
    // Concept: parsed scene getters are Lane F once parsing has succeeded.
    //
    // Scene JSON syntax/data failures are Lane R at TryLoadFromFile, but an
    // internal caller asking for an index outside the paired count is a scene
    // setup invariant failure. Fatal diagnostics keep the owner and collection
    // visible without unwinding through runtime setup.
    SB_FATAL( "TestScene",
              "%s index out of range in %s. index=%d count=%d",
              collectionName,
              functionName,
              index,
              count );
}


SbResult
TryLoadSceneFile( const char* path, SkullbonezCore::Assets::AssetContext assets, bool styleOnly, TestScene& outScene )
{
    return styleOnly ? TryLoadStyleSceneFromFileImpl( path, assets, outScene )
                     : TryLoadTestSceneFromFileImpl( path, assets, outScene );
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
        FatalSceneIndexOutOfRange( "Camera", "TestScene::GetCamera", index, static_cast<int>( m_cameras.size() ) );
    }

    return m_cameras[index];
}


const SceneBall& TestScene::GetBall( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_balls.size() ) )
    {
        FatalSceneIndexOutOfRange( "Ball", "TestScene::GetBall", index, static_cast<int>( m_balls.size() ) );
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
        FatalSceneIndexOutOfRange( "BallState",
                                   "TestScene::GetBallState",
                                   index,
                                   static_cast<int>( m_ballStates.size() ) );
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
        FatalSceneIndexOutOfRange( "BoxState",
                                   "TestScene::GetBoxState",
                                   index,
                                   static_cast<int>( m_boxStates.size() ) );
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
        FatalSceneIndexOutOfRange( "Box", "TestScene::GetBox", index, static_cast<int>( m_boxes.size() ) );
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
        FatalSceneIndexOutOfRange( "ConvexHull",
                                   "TestScene::GetConvexHull",
                                   index,
                                   static_cast<int>( m_convexHulls.size() ) );
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
        FatalSceneIndexOutOfRange( "ConvexHullState",
                                   "TestScene::GetConvexHullState",
                                   index,
                                   static_cast<int>( m_convexHullStates.size() ) );
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
        FatalSceneIndexOutOfRange( "Ragdoll", "TestScene::GetRagdoll", index, static_cast<int>( m_ragdolls.size() ) );
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
        FatalSceneIndexOutOfRange( "PointJointConstraint",
                                   "TestScene::GetPointJointConstraint",
                                   index,
                                   static_cast<int>( m_pointJointConstraints.size() ) );
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
        FatalSceneIndexOutOfRange( "RequiredContact",
                                   "TestScene::GetRequiredContact",
                                   index,
                                   static_cast<int>( m_requiredContacts.size() ) );
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
        FatalSceneIndexOutOfRange( "RequiredBroadphaseXCell",
                                   "TestScene::GetRequiredBroadphaseXCell",
                                   index,
                                   static_cast<int>( m_requiredBroadphaseXCells.size() ) );
    }

    return m_requiredBroadphaseXCells[index];
}


int TestScene::GetAssetLibraryCount() const
{
    return static_cast<int>( m_assetLibraries.size() );
}


const SceneAssetLibraryRef& TestScene::GetAssetLibrary( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetLibraries.size() ) )
    {
        FatalSceneIndexOutOfRange( "AssetLibrary",
                                   "TestScene::GetAssetLibrary",
                                   index,
                                   static_cast<int>( m_assetLibraries.size() ) );
    }

    return m_assetLibraries[index];
}


int TestScene::GetAssetInstanceCount() const
{
    return static_cast<int>( m_assetInstances.size() );
}


const SceneAssetInstanceRecord& TestScene::GetAssetInstance( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetInstances.size() ) )
    {
        FatalSceneIndexOutOfRange( "AssetInstance",
                                   "TestScene::GetAssetInstance",
                                   index,
                                   static_cast<int>( m_assetInstances.size() ) );
    }

    return m_assetInstances[index];
}


int TestScene::GetAssetPartCount() const
{
    return static_cast<int>( m_assetParts.size() );
}


const SceneAssetPartRef& TestScene::GetAssetPart( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetParts.size() ) )
    {
        FatalSceneIndexOutOfRange( "AssetPart",
                                   "TestScene::GetAssetPart",
                                   index,
                                   static_cast<int>( m_assetParts.size() ) );
    }

    return m_assetParts[index];
}


int TestScene::GetObjectMaterialOverrideCount() const
{
    return static_cast<int>( m_objectMaterials.size() );
}


const SceneObjectMaterialOverride& TestScene::GetObjectMaterialOverride( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_objectMaterials.size() ) )
    {
        FatalSceneIndexOutOfRange( "ObjectMaterialOverride",
                                   "TestScene::GetObjectMaterialOverride",
                                   index,
                                   static_cast<int>( m_objectMaterials.size() ) );
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


const SkullbonezCore::Physics::MutualGravitySettings& TestScene::GetWorldMutualGravitySettings() const
{
    return m_worldOverride.mutualGravity;
}


bool TestScene::HasMutualGravityEnabled() const
{
    return m_worldOverride.mutualGravity.enabled;
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
