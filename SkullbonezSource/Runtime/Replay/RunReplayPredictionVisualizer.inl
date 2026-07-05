/*
File: SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl
Purpose:
  Implements replay prediction job stepping and prediction-path visualization.

Mental model:
  The visualizer advances a prediction job in small slices, restores live physics state after
  each mutation window, and emits bounded overlay traces for the current frame. Prediction
  stepping, future-node topology, and visible path drawing each get short slices so one phase
  cannot make the others disappear.

Glossary:
  Build frames: In-progress prediction samples accumulated while a prediction job is still
    stepping. Future contact topology may be derived from them only through the committed
    future-node cache, never by exposing a half-built scratch vector to drawing.
  Future node tree: Contact-derived graph of bodies that the selected replay path is predicted
    to affect after the root body hits something.
  Mutation window: Period where live physics stores temporarily contain prediction state.
  Stable overlay pass: Short pre-step draw that keeps current world-space lines visible while
    heavy prediction jobs continue building fresher samples.
  Visualizer budget: Millisecond cap applied to each bounded prediction or overlay work slice.

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
                               SkullbonezCore::Threading::WorkerPool& workerPool,
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
    replayRuntime.ClearPredictionFutureNodeCache();
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
        int targetIndex = -1;
        const int modelCount = modelCollection.GetModelCount();
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            replayRuntime.Prediction().dirty = true;
            return false;
        }
        const PhysicsBodyStore& bodyStore = modelCollection.GetPhysicsBodyStore();
        if ( !TryResolveReplayBodyModelIndex( bodyStore,
                                              replayRuntime.PathVisualizer().targetId,
                                              replayRuntime.PathVisualizer().targetModelIndex,
                                              modelCount,
                                              targetIndex ) )
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

    if ( !CaptureReplayPredictionBodyState( modelCollection, workerPool, replayRuntime.Prediction().predictionBodies ) )
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

    CaptureReplayPredictionFrame( replayRuntime, modelCollection, workerPool, 0 );
    replayRuntime.Prediction().building = true;

    return !replayRuntime.Prediction().buildFrames.empty();
}


bool StepReplayPredictionJob( ReplayRuntime& replayRuntime,
                              SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                              const EngineConfig& config,
                              const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                              SkullbonezCore::Threading::WorkerPool& workerPool,
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
    if ( !CaptureReplayPredictionBodyState( modelCollection,
                                            workerPool,
                                            replayRuntime.Prediction().liveRestoreBodies ) )
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
                    StepReplayPredictionPhysicsTick( modelCollection,
                                                     PHYSICS_FIXED_DT,
                                                     config,
                                                     worldForces,
                                                     workerPool );
                }
                CaptureReplayPredictionFrame( replayRuntime,
                                              modelCollection,
                                              workerPool,
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
            jobStateCaptured = CaptureReplayPredictionBodyState( modelCollection,
                                                                 workerPool,
                                                                 replayRuntime.Prediction().predictionBodies );
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
        // Why: future-node scratch was built from buildFrames. After this swap
        // those samples are the final frames, so keeping the cache preserves
        // progressively revealed child paths through the completion frame.
        replayRuntime.Prediction().lastBuildTime = simulationTotalSeconds;
    }

    return progressed || replayRuntime.Prediction().complete;
}


bool DrawReplayPredictionOverlay( ReplayRuntime& replayRuntime,
                                  const SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                  const ColliderStore& colliderStore,
                                  RunEditorTracer& tracer,
                                  const std::chrono::steady_clock::time_point& budgetStart,
                                  double budgetMilliseconds )
{
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = replayRuntime.ActivePredictionFrames();
    const bool usingBuildFrames = &activePredictionFrames == &replayRuntime.Prediction().buildFrames;
    if ( activePredictionFrames.size() < 2 )
    {
        return false;
    }

    if ( !replayRuntime.PathVisualizer().hasTarget || replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
        if ( replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    modelCollection,
                                                    tracer,
                                                    budgetStart,
                                                    budgetMilliseconds );
        }
        return true;
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
                break;
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
    const auto buildBudgetStart = std::chrono::steady_clock::now();
    bool drawFutureTree = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        // Why: the root path can spend its draw slice on long horizons. Future
        // topology still needs a bounded chance to advance every render frame so
        // contact children grow progressively instead of popping in after build.
        UpdateReplayPredictionFutureNodeCache( replayRuntime.Prediction(),
                                               activePredictionFrames,
                                               usingBuildFrames,
                                               modelCollection,
                                               replayRuntime.PathVisualizer().targetId,
                                               buildBudgetStart,
                                               budgetMilliseconds );
        drawFutureTree =
            replayRuntime.Prediction().futureNodesCacheValid && !replayRuntime.Prediction().futureNodes.empty();
    }
    const auto childDrawBudgetStart = std::chrono::steady_clock::now();

    if ( drawFutureTree )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.colliderStore = &colliderStore;
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
            if ( ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
            {
                return true;
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
                if ( ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
                {
                    return true;
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
                            ReplayFutureMarkerRadiusForModelIndex( childDraw.colliderStore, body->modelIndex );
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
            if ( ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
            {
                return true;
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
         !ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                modelCollection,
                                                tracer,
                                                childDrawBudgetStart,
                                                budgetMilliseconds );
    }
    return true;
}


void RenderReplayPredictionVisualizer( ReplayRuntime& replayRuntime,
                                       SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       const EngineConfig& config,
                                       const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
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
                                  workerPool,
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
    const ColliderStore& colliderStore = modelCollection.GetPhysicsEngine().Colliders();
    if ( replayRuntime.Prediction().building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds > 0.0 )
        {
            StepReplayPredictionJob( replayRuntime,
                                     modelCollection,
                                     config,
                                     worldForces,
                                     workerPool,
                                     simulationTotalSeconds,
                                     budgetStart,
                                     budgetMilliseconds );
        }
    }

    // Why: prediction stepping owns the mutation budget, but visible replay
    // lines need a draw chance even on frames where stepping consumes that
    // budget. Start a fresh draw-only timer so the overlay degrades by detail
    // instead of disappearing for a frame.
    DrawReplayPredictionOverlay( replayRuntime,
                                 modelCollection,
                                 colliderStore,
                                 tracer,
                                 std::chrono::steady_clock::now(),
                                 budgetMilliseconds );
}
