/*
File: SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl
Purpose:
  Implements replay prediction job stepping and prediction-path visualization.

Mental model:
  The visualizer advances a prediction job in small slices, restores live physics state after
  each mutation window, and emits only bounded overlay traces for the current frame.

Glossary:
  Mutation window: Period where live physics stores temporarily contain prediction state.
  Visualizer budget: Millisecond cap shared by prediction stepping and overlay drawing.

Invariants:
  - Every successful prediction-state swap must restore live body and solver snapshots.
  - This file must only be included from RunReplayTools.cpp inside the prediction anonymous namespace.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

bool BeginReplayPredictionJob( ReplayRuntime& replayRuntime,
                               SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                               bool scenePhysics,
                               double fallbackSourceSimulationSeconds,
                               double simulationTotalSeconds,
                               ReplayFrameIndex sourceFrameIndex,
                               uint64_t sourceSolverHash,
                               const std::chrono::steady_clock::time_point& budgetStart,
                               double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );
    // Hazard: begin captures the initial prediction snapshot. If setup spends
    // the visualizer slice, leave the prediction dirty so the next frame retries
    // instead of piling tree/draw work onto the same render frame.
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    replayRuntime.CancelPredictionJob( false );
    replayRuntime.Prediction().targetId = replayRuntime.PathVisualizer().targetId;
    replayRuntime.Prediction().dirty = false;

    if ( !replayRuntime.Prediction().enabled || !scenePhysics )
    {
        return false;
    }

    replayRuntime.Prediction().sourceFrameIndex = sourceFrameIndex;
    replayRuntime.Prediction().sourceSolverHash = sourceSolverHash;
    if ( const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample() )
    {
        replayRuntime.Prediction().sourceSimulationSeconds = latest->simulationSeconds;
    }
    else
    {
        replayRuntime.Prediction().sourceSimulationSeconds = fallbackSourceSimulationSeconds;
    }
    replayRuntime.Prediction().lastBuildTime = simulationTotalSeconds;

    if ( replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        std::vector<GameModel>& models = modelCollection.MutablePhysicsModelsForCompatibility();
        int targetIndex = -1;
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                replayRuntime.Prediction().dirty = true;
                return false;
            }

            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() ==
                 replayRuntime.PathVisualizer().targetId.value )
            {
                targetIndex = i;
                break;
            }
        }
        if ( targetIndex < 0 )
        {
            return false;
        }
        replayRuntime.Prediction().targetModelIndex = targetIndex;
        replayRuntime.PathVisualizer().targetModelIndex = targetIndex;
    }

    replayRuntime.Prediction().horizonSeconds = std::clamp( replayRuntime.Prediction().horizonSeconds,
                                                            REPLAY_PREDICTION_MIN_SECONDS,
                                                            REPLAY_PREDICTION_MAX_SECONDS );
    const int predictionTicks =
        (std::max)( 1, static_cast<int>( std::ceil( replayRuntime.Prediction().horizonSeconds / PHYSICS_FIXED_DT ) ) );
    replayRuntime.Prediction().targetTickCount = predictionTicks;
    replayRuntime.Prediction().nextTick = 1;
    replayRuntime.Prediction().buildFrames.reserve( static_cast<std::size_t>( predictionTicks + 1 ) );

    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( !CaptureReplayPredictionBodyState( modelCollection, replayRuntime.Prediction().predictionBodies ) )
    {
        replayRuntime.CancelPredictionJob( true );
        return false;
    }
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    modelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot( replayRuntime.Prediction().predictionWorld,
                                                                    modelCollection.GetModelCount() );
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    CaptureReplayPredictionFrame( replayRuntime, modelCollection, 0 );
    replayRuntime.Prediction().building = true;

    return !replayRuntime.Prediction().buildFrames.empty();
}


bool StepReplayPredictionJob( ReplayRuntime& replayRuntime,
                              SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                              double simulationTotalSeconds,
                              const std::chrono::steady_clock::time_point& budgetStart,
                              double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );
    if ( !replayRuntime.Prediction().building )
    {
        return replayRuntime.Prediction().complete;
    }

    if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    // Hazard: everything after liveRestoreBodies/liveRestoreWorld succeeds may
    // swap live state for prediction state. All early exits before RestoreLive
    // must happen before the swap, or after the restore block below.
    if ( !CaptureReplayPredictionBodyState( modelCollection, replayRuntime.Prediction().liveRestoreBodies ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    modelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot( replayRuntime.Prediction().liveRestoreWorld,
                                                                    modelCollection.GetModelCount() );
    if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

#ifdef _DEBUG
    const bool previousDiagnosticsSuppressed = modelCollection.GetPhysicsEngine().SetDiagnosticsSuppressed( true );
#endif

    bool jobApplied = false;
    bool jobStateCaptured = false;
    bool progressed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyJobState" );
        jobApplied =
            ApplyReplayPredictionBodyState( modelCollection, replayRuntime.Prediction().predictionBodies ) &&
            modelCollection.GetPhysicsEngine().RestoreReplaySolverSnapshot( replayRuntime.Prediction().predictionWorld,
                                                                            modelCollection.GetModelCount() );
        modelCollection.InvalidatePhysicsStreams();
    }

    if ( jobApplied )
    {
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
            while ( replayRuntime.Prediction().nextTick <= replayRuntime.Prediction().targetTickCount )
            {
                if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
                {
                    break;
                }

                {
                    PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                    SimulationPhysicsStep{ &modelCollection.GetPhysicsEngine(), &modelCollection }.Run(
                        PHYSICS_FIXED_DT );
                }
                CaptureReplayPredictionFrame( replayRuntime,
                                              modelCollection,
                                              static_cast<ReplayFrameIndex>( replayRuntime.Prediction().nextTick ) );
                ++replayRuntime.Prediction().nextTick;
                progressed = true;

                if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
                {
                    break;
                }
            }
        }

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
            jobStateCaptured =
                CaptureReplayPredictionBodyState( modelCollection, replayRuntime.Prediction().predictionBodies );
            if ( jobStateCaptured )
            {
                modelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot(
                    replayRuntime.Prediction().predictionWorld,
                    modelCollection.GetModelCount() );
            }
        }
    }

#ifdef _DEBUG
    modelCollection.GetPhysicsEngine().SetDiagnosticsSuppressed( previousDiagnosticsSuppressed );
#endif

    bool liveRestored = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/RestoreLive" );
        liveRestored =
            ApplyReplayPredictionBodyState( modelCollection, replayRuntime.Prediction().liveRestoreBodies ) &&
            modelCollection.GetPhysicsEngine().RestoreReplaySolverSnapshot( replayRuntime.Prediction().liveRestoreWorld,
                                                                            modelCollection.GetModelCount() );
        modelCollection.InvalidatePhysicsStreams();
    }

    if ( !jobApplied || !jobStateCaptured || !liveRestored )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( replayRuntime.Prediction().nextTick > replayRuntime.Prediction().targetTickCount )
    {
        replayRuntime.Prediction().building = false;
        replayRuntime.Prediction().complete = true;
        replayRuntime.Prediction().frames.swap( replayRuntime.Prediction().buildFrames );
        replayRuntime.Prediction().buildFrames.clear();
        replayRuntime.ClearPredictionFutureNodeCache();
        replayRuntime.Prediction().lastBuildTime = simulationTotalSeconds;
    }

    return progressed || replayRuntime.Prediction().complete;
}


void RenderReplayPredictionVisualizer( ReplayRuntime& replayRuntime,
                                       SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       bool scenePhysics,
                                       double fallbackSourceSimulationSeconds,
                                       double simulationTotalSeconds,
                                       RunEditorTracer& tracer,
                                       const std::chrono::steady_clock::time_point& budgetStart,
                                       double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    if ( !replayRuntime.Prediction().enabled )
    {
        if ( replayRuntime.Prediction().building )
        {
            replayRuntime.CancelPredictionJob( true );
        }
        return;
    }

    const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample();
    const ReplayFrameIndex latestFrame = latest ? latest->frameIndex : 0;
    const uint64_t latestHash = latest ? latest->solverHash : 0;
    const double now = simulationTotalSeconds;
    const bool sourceChanged =
        replayRuntime.Prediction().targetId.value != replayRuntime.PathVisualizer().targetId.value ||
        replayRuntime.Prediction().sourceFrameIndex != latestFrame ||
        replayRuntime.Prediction().sourceSolverHash != latestHash;
    const bool refreshDue = ( now - replayRuntime.Prediction().lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool allowAutomaticRefresh = !replayRuntime.Scrubber().liveAdvanceHeld;
    if ( replayRuntime.Prediction().dirty ||
         ( allowAutomaticRefresh && !replayRuntime.Prediction().building && sourceChanged && refreshDue ) )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
        BeginReplayPredictionJob( replayRuntime,
                                  modelCollection,
                                  scenePhysics,
                                  fallbackSourceSimulationSeconds,
                                  simulationTotalSeconds,
                                  latestFrame,
                                  latestHash,
                                  budgetStart,
                                  budgetMilliseconds );
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
    }
    if ( replayRuntime.Prediction().building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds <= 0.0 )
        {
            return;
        }
        StepReplayPredictionJob( replayRuntime,
                                 modelCollection,
                                 simulationTotalSeconds,
                                 budgetStart,
                                 budgetMilliseconds );
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
    }

    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = replayRuntime.ActivePredictionFrames();
    const bool usingBuildFrames = &activePredictionFrames == &replayRuntime.Prediction().buildFrames;
    if ( activePredictionFrames.size() < 2 )
    {
        return;
    }

    const std::vector<GameModel>& models = modelCollection.Models();
    if ( !replayRuntime.PathVisualizer().hasTarget || replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
        if ( replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    models,
                                                    tracer,
                                                    budgetStart,
                                                    budgetMilliseconds );
        }
        return;
    }

    const ReplayFrameIndex lastFrame = activePredictionFrames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( activePredictionFrames.size() );
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : activePredictionFrames )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame,
                                                      replayRuntime.PathVisualizer().targetId,
                                                      replayRuntime.PathVisualizer().targetModelIndex );
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
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        UpdateReplayPredictionFutureNodeCache( replayRuntime.Prediction(),
                                               activePredictionFrames,
                                               usingBuildFrames,
                                               models,
                                               replayRuntime.PathVisualizer().targetId,
                                               budgetStart,
                                               budgetMilliseconds );
    }
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.presentFrame = 0;
        childDraw.lastFrame = lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( replayRuntime.Prediction().futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = replayRuntime.Prediction().futureNodes[i];
        }

        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : activePredictionFrames )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

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
            if ( !importantChildFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }

            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
                {
                    return;
                }

                ReplayPathChildDrawState& drawState = childDraw.nodes[i];
                const RunReplayPredictionBodySample* body =
                    FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelIndex );
                if ( !body )
                {
                    continue;
                }

                if ( frame.frameIndex <= drawState.node.firstFrame )
                {
                    if ( !drawState.markerDrawn )
                    {
                        const float radius =
                            ReplayFutureMarkerRadiusForModelIndex( childDraw.models, body->modelIndex );
                        tracer.AddReplayFutureTargetMarker( body->position, radius, drawState.node.depth );
                        drawState.markerDrawn = true;
                    }
                    if ( drawState.hasIncomingPrevious &&
                         VectorMagSquared( body->position - drawState.incomingPrevious ) >
                             REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
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

                if ( frame.frameIndex >= drawState.node.firstFrame && drawState.hasPrevious &&
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

        for ( const RunReplayPathTraceNode& node : replayRuntime.Prediction().futureNodes )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

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

    if ( replayRuntime.Prediction().ragdollVisualsEnabled &&
         !ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                models,
                                                tracer,
                                                budgetStart,
                                                budgetMilliseconds );
    }
}
