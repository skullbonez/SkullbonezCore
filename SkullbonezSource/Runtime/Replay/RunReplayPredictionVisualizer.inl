/*
File: SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl
Purpose:
  Implements replay prediction job stepping and prediction-path visualization.

Mental model:
  The visualizer advances a private prediction engine in small slices and emits bounded overlay
  traces for the current frame. Prediction stepping, future-node topology, and visible path
  drawing each get short slices so one phase cannot make the others disappear.

Glossary:
  Build frames: In-progress prediction samples accumulated while a prediction job is still
    stepping. Future contact topology may be derived from them only through the committed
    future-node cache, never by exposing a half-built scratch vector to drawing.
  Future node tree: Bounded graph of bodies that the selected replay path is
    predicted to affect after the root body hits something. Nodes can come from
    solver contacts or first-movement fallback while contact data is still sparse.
  Causal markers: The two-box story of each affected body — a yellow outline fixed at its
    in-place prediction-start pose (perfect wall formation) and a grey outline drawn only at
    its final resting pose when the completed prediction actually ends with it at rest.
    Bodies still moving at the horizon end get a travel line and no grey box. Once shown,
    neither box ever leaves until the prediction is rebuilt.
  Prediction engine: Replay-owned physics facade copied from the live scene at prediction begin
    and stepped forward without writing live body, collider, or solver stores.
  Reveal cursor: Wall-clock playhead that caps which prediction frames may draw this render
    frame. It makes the causal tree unfold over real time — root line first, child lines when
    their causing frame is revealed. Monotonic per prediction: it plays once, holds at the
    horizon, and resets only when the prediction is rebuilt.
  Stable overlay pass: Short pre-step draw that keeps current world-space lines visible while
    heavy prediction jobs continue building fresher samples.
  Visualizer budget: Millisecond cap applied to each bounded prediction or overlay work slice.

Invariants:
  - Prediction samples must come from the private prediction engine, never from live store edits.
  - This file must only be included from RunReplayTools.cpp inside the prediction anonymous namespace.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

bool BeginReplayPredictionJob( ReplayRuntime& replayRuntime,
                               SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                               const EngineConfig& config,
                               const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
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
    // Hazard: begin captures the initial prediction snapshot. Budget may stop
    // us before setup starts, but once replay scratch and solver state are
    // reserved we must publish frame 0 so large predictions can draw progress
    // instead of thrashing a dirty begin job every render frame.
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    replayRuntime.CancelPredictionJob( true );
    replayRuntime.ClearPredictionFutureNodeCache();
    // Why: a new job means a new future. Restart the reveal cursor so the
    // causal tree unfolds from the root again instead of resuming mid-sweep
    // over samples that no longer exist.
    replayRuntime.Prediction().revealAnchor = std::chrono::steady_clock::now();
    replayRuntime.Prediction().revealAnchorValid = true;
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

    if ( !modelCollection.RepairPhysicsBodyAndColliderTopology() )
    {
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    PhysicsEngine& physicsEngine = modelCollection.GetPhysicsEngine();
    const int modelCount = physicsEngine.BodyStore().Count();
    const PhysicsBodyStore& liveBodyStore = physicsEngine.BodyStore();
    if ( replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        int targetIndex = -1;
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            replayRuntime.Prediction().dirty = true;
            return false;
        }
        if ( !TryResolveReplayBodyModelIndex( liveBodyStore,
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
    const std::size_t buildFrameCapacity = static_cast<std::size_t>( predictionTicks + 1 );
    if ( !ReserveReplayPredictionVector( replayRuntime.Prediction().buildFrames,
                                         buildFrameCapacity,
                                         0,
                                         "RunReplayPredictionState::buildFrames" ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    replayRuntime.Prediction().buildFrames.resize( buildFrameCapacity );
    replayRuntime.Prediction().buildFrameCount = 0;
    if ( !ReserveReplayPredictionFramePayloadVectors( replayRuntime.Prediction().buildFrames,
                                                      buildFrameCapacity,
                                                      static_cast<std::size_t>( modelCount ),
                                                      0,
                                                      "RunReplayPredictionFrame::bodies",
                                                      &RunReplayPredictionFrame::bodies ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    // Why: replay prediction is exploratory UI, so the initial contact payload
    // reserve is intentionally generous and later growth is rounded to large
    // chunks. The root trajectory still publishes even if optional contact-tree
    // payloads outgrow the reserve.
    const std::size_t initialDebugContactCapacity = ReplayPredictionInitialDebugContactCapacity( modelCount );
    (void)ReserveReplayPredictionFramePayloadVectors( replayRuntime.Prediction().buildFrames,
                                                      buildFrameCapacity,
                                                      initialDebugContactCapacity,
                                                      0,
                                                      "RunReplayPredictionFrame::debugContacts",
                                                      &RunReplayPredictionFrame::debugContacts );
    if ( !ReserveReplayPredictionVector( replayRuntime.Prediction().futureNodes,
                                         REPLAY_PATH_MAX_FUTURE_NODES,
                                         0,
                                         "RunReplayPredictionState::futureNodes" ) ||
         !ReserveReplayPredictionVector( replayRuntime.Prediction().futureNodeBuildScratch,
                                         REPLAY_PATH_MAX_FUTURE_NODES,
                                         0,
                                         "RunReplayPredictionState::futureNodeBuildScratch" ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( !CaptureReplayPredictionBodyState(
             modelCollection, liveBodyStore, workerPool, replayRuntime.Prediction().predictionBodies ) )
    {
        replayRuntime.CancelPredictionJob( true );
        return false;
    }

    physicsEngine.CaptureReplaySolverSnapshot( replayRuntime.Prediction().predictionWorld, modelCount );

    if ( !SeedReplayPredictionEngine( replayRuntime.Prediction(), physicsEngine, config, worldForces, modelCount ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( !replayRuntime.Prediction().predictionEngine ||
         !CaptureReplayPredictionFrame(
             replayRuntime, modelCollection, *replayRuntime.Prediction().predictionEngine, workerPool, 0 ) )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    replayRuntime.Prediction().building = true;

    return !replayRuntime.Prediction().buildFrames.empty();
}


bool StepReplayPredictionJob( ReplayRuntime& replayRuntime,
                              SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                              const EngineConfig& config,
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

    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    if ( !replayRuntime.Prediction().predictionEngineReady || !replayRuntime.Prediction().predictionEngine )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }
    PhysicsEngine& predictionEngine = *replayRuntime.Prediction().predictionEngine;

    bool progressed = false;
    bool predictionStepFailed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
        while ( replayRuntime.Prediction().nextTick <= replayRuntime.Prediction().targetTickCount )
        {
            // Why: a large prediction can spend most of the slice on frame
            // capture. Still take one tick per entered slice so the visible
            // build prefix advances instead of stalling forever.
            if ( progressed && ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                break;
            }

            {
                PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                if ( !StepPredictionEngineTick( predictionEngine,
                                                PHYSICS_FIXED_DT,
                                                config,
                                                replayRuntime.Prediction().predictionWorldForces,
                                                workerPool ) )
                {
                    predictionStepFailed = true;
                    replayRuntime.Prediction().dirty = true;
                    break;
                }
            }
            if ( !CaptureReplayPredictionFrame( replayRuntime,
                                                modelCollection,
                                                predictionEngine,
                                                workerPool,
                                                static_cast<ReplayFrameIndex>( replayRuntime.Prediction().nextTick ) ) )
            {
                predictionStepFailed = true;
                replayRuntime.Prediction().dirty = true;
                break;
            }
            ++replayRuntime.Prediction().nextTick;
            progressed = true;

            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                break;
            }
        }
    }

    if ( progressed )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
        predictionEngine.CaptureReplaySolverSnapshot( replayRuntime.Prediction().predictionWorld,
                                                      predictionEngine.BodyStore().Count() );
    }

    if ( predictionStepFailed )
    {
        replayRuntime.CancelPredictionJob( true );
        replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( replayRuntime.Prediction().nextTick > replayRuntime.Prediction().targetTickCount )
    {
        const float previousPresentT = replayRuntime.SolverPresentTrackPosition();
        const float previousSolverPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
        const bool hadCommittedPredictionFrames = replayRuntime.Prediction().frames.size() >= 2;
        const bool solverWasOldLiveEdge =
            !hadCommittedPredictionFrames && ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, 1.0f );
        const bool scrubberWasPinnedToPresent =
            !replayRuntime.Scrubber().historicalSamplePaused ||
            ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, previousPresentT ) || solverWasOldLiveEdge;

        replayRuntime.Prediction().building = false;
        replayRuntime.Prediction().complete = true;
        replayRuntime.Prediction().frames.swap( replayRuntime.Prediction().buildFrames );
        replayRuntime.Prediction().buildFrames.clear();
        replayRuntime.Prediction().buildFrameCount = 0;
        if ( replayRuntime.Prediction().baseline.valid )
        {
            UpdateReplayPredictionBaselineDivergence( replayRuntime.Prediction(),
                                                      replayRuntime.Prediction().frames,
                                                      replayRuntime.Prediction().frames.size() );
        }
        if ( scrubberWasPinnedToPresent )
        {
            // Why: prediction extends the normalized solver track by moving the
            // present marker left. A scrub value that meant "live/present"
            // before the swap must remain present, or render will preview the
            // far future and make the selected body appear to move.
            replayRuntime.SetTrackPosition( RunReplayTrack::Solver, replayRuntime.SolverPresentTrackPosition() );
            if ( replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
            {
                replayRuntime.Scrubber().historicalSamplePaused = false;
            }
        }
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
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const bool usingBuildFrames =
        prediction.building && prediction.buildFrameCount >= 2 &&
        ( prediction.frames.empty() || prediction.buildFrameCount >= prediction.frames.size() );
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames =
        usingBuildFrames ? prediction.buildFrames : prediction.frames;
    const std::size_t activePredictionFrameCount =
        usingBuildFrames ? (std::min)( prediction.buildFrameCount, activePredictionFrames.size() )
                         : activePredictionFrames.size();
    if ( activePredictionFrameCount < 2 )
    {
        return false;
    }

    // Concept: every pass below draws only frames at or before the reveal
    // cursor. That single clamp is what turns a finished prediction buffer into
    // an unfolding animation: the root line grows first, and each child starts
    // drawing when the cursor passes the frame where its cause happened.
    const ReplayFrameIndex lastFrame = activePredictionFrames[activePredictionFrameCount - 1].frameIndex;
    const ReplayFrameIndex revealFrame = ReplayPredictionRevealFrameIndex( replayRuntime.Prediction(), lastFrame );
    // Why: while the job is still building there is no authoritative ending,
    // so no grey resting box may be derived from the growing prefix.
    const bool bufferComplete = !usingBuildFrames;
    DrawReplayPredictionBaselineSnapshot( replayRuntime.Prediction().baseline, colliderStore, tracer );

    if ( !replayRuntime.PathVisualizer().hasTarget || replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
        if ( replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    activePredictionFrameCount,
                                                    revealFrame,
                                                    modelCollection,
                                                    tracer,
                                                    budgetStart,
                                                    budgetMilliseconds );
        }
        return true;
    }

    const std::size_t sampleStride = ReplayPathStrideForSampleCount( activePredictionFrameCount );
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        ReplayFrameIndex rootLastMotionFrame = 0;
        std::size_t ordinal = 0;
        for ( std::size_t frameIndex = 0; frameIndex < activePredictionFrameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = activePredictionFrames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                break;
            }

            // Why: the reveal-edge frame always draws so the line tip grows
            // smoothly instead of jumping ahead one stride at a time.
            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
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
            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                rootLastMotionFrame = frame.frameIndex;
            }
        }

        // Why: the root gets no yellow entry box — the white selection marker
        // already anchors where its story starts. Grey follows the same rule
        // as every child: only a completed prediction that actually ends at
        // rest may place a resting box, and only after the reveal cursor has
        // watched the root stop moving.
        if ( bufferComplete && revealFrame >= rootLastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            Vector3 rootRestPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Quaternion rootRestOrientation = IDENTITY_QUATERNION;
            if ( ReplayPredictionBodyRestingPose( activePredictionFrames,
                                                  activePredictionFrameCount,
                                                  replayRuntime.PathVisualizer().targetId,
                                                  replayRuntime.PathVisualizer().targetModelIndex,
                                                  rootRestPosition,
                                                  rootRestOrientation ) )
            {
                if ( ReplayColliderRecordForModelIndex( &colliderStore,
                                                        replayRuntime.PathVisualizer().targetModelIndex ) )
                {
                    RetainReplayPredictionRestMarker( replayRuntime.Prediction(),
                                                      replayRuntime.PathVisualizer().targetId,
                                                      replayRuntime.PathVisualizer().targetModelIndex,
                                                      rootRestPosition,
                                                      rootRestOrientation );
                }
            }
        }
    }
    const auto buildBudgetStart = std::chrono::steady_clock::now();
    bool drawFutureTree = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        if ( replayRuntime.Prediction().enabled )
        {
            // Why: downstream child paths must advance with the same populated
            // prediction prefix as the root line. The cache builder accepts growing
            // buildFrames and only publishes coherent node prefixes.
            UpdateReplayPredictionFutureNodeCache( replayRuntime.Prediction(),
                                                   activePredictionFrames,
                                                   activePredictionFrameCount,
                                                   usingBuildFrames,
                                                   modelCollection,
                                                   replayRuntime.PathVisualizer().targetId,
                                                   buildBudgetStart,
                                                   budgetMilliseconds );
            drawFutureTree =
                replayRuntime.Prediction().futureNodesCacheValid && !replayRuntime.Prediction().futureNodes.empty();
        }
        else
        {
            // Why: live play freezes prediction visualization. Keep drawing the
            // committed topology, but do not discover new child nodes while the
            // real simulation advances underneath the overlay.
            drawFutureTree = !replayRuntime.Prediction().futureNodes.empty();
        }
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

        // Why: budget exhaustion may stop line SCANNING, never the marker pass
        // below — bailing out before markers made causal boxes flicker off for
        // a frame whenever the child scan ran long.
        bool childScanBudgetExhausted = false;
        std::size_t ordinal = 0;
        for ( std::size_t frameIndex = 0; frameIndex < activePredictionFrameCount && !childScanBudgetExhausted;
              ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = activePredictionFrames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }
            if ( ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
            {
                break;
            }

            const std::size_t currentOrdinal = ordinal++;
            bool importantChildFrame =
                frame.frameIndex == 0 || frame.frameIndex == lastFrame || frame.frameIndex == revealFrame;
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
                    childScanBudgetExhausted = true;
                    break;
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

                if ( frame.frameIndex >= drawState.node.firstFrame && !drawState.active )
                {
                    // Why: contact propagation can wake a wall without giving
                    // every brick visible translation. Delay outlines and child
                    // trails until the sampled body has meaningful linear speed.
                    if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                    {
                        continue;
                    }
                    drawState.active = true;
                    // Concept: entry is the body's IN-PLACE pose from
                    // prediction frame 0 — the wall exactly as the live scene
                    // knows it, never a sampled pose from after the impulse
                    // arrived. Anchoring the trail start there makes lines
                    // grow straight out of the yellow formation box.
                    const RunReplayPredictionBodySample* initialSample =
                        FindReplayPredictionBodyByIdWithHint( activePredictionFrames[0],
                                                              drawState.node.id,
                                                              body->modelIndex );
                    drawState.hasEntryPose = true;
                    drawState.entryModelIndex = body->modelIndex;
                    drawState.entryPosition = initialSample ? initialSample->position : body->position;
                    drawState.entryOrientation = initialSample ? initialSample->orientation : body->orientation;
                    drawState.previous = drawState.entryPosition;
                    drawState.hasPrevious = true;
                    drawState.lastMotionFrame = frame.frameIndex;
                    continue;
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
                if ( frame.frameIndex >= drawState.node.firstFrame && drawState.active )
                {
                    if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                    {
                        drawState.lastMotionFrame = frame.frameIndex;
                    }
                    drawState.previous = body->position;
                    drawState.hasPrevious = true;
                }
            }
        }

        DrawReplayPredictionCausalMarkers( replayRuntime.Prediction(),
                                           childDraw,
                                           revealFrame,
                                           bufferComplete ? &activePredictionFrames : nullptr,
                                           bufferComplete ? activePredictionFrameCount : 0 );
    }

    if ( !ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawAffectedBodies" );
        DrawReplayPredictionAffectedBodyTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                replayRuntime.Prediction(),
                                                revealFrame,
                                                bufferComplete,
                                                replayRuntime.PathVisualizer().targetId,
                                                replayRuntime.PathVisualizer().targetModelIndex,
                                                replayRuntime.Prediction().futureNodes,
                                                modelCollection,
                                                colliderStore,
                                                tracer,
                                                childDrawBudgetStart,
                                                budgetMilliseconds );
    }

    if ( replayRuntime.Prediction().ragdollVisualsEnabled &&
         !ReplayPredictionBudgetExpired( childDrawBudgetStart, budgetMilliseconds ) )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                revealFrame,
                                                modelCollection,
                                                tracer,
                                                childDrawBudgetStart,
                                                budgetMilliseconds );
    }
    if ( bufferComplete )
    {
        RetainReplayPredictionEndStateMarkers( replayRuntime.Prediction(),
                                               revealFrame,
                                               activePredictionFrames,
                                               activePredictionFrameCount );
    }
    DrawReplayPredictionRetainedMarkers( replayRuntime.Prediction(),
                                         activePredictionFrames,
                                         activePredictionFrameCount,
                                         revealFrame,
                                         colliderStore,
                                         tracer );
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
            replayRuntime.CancelPredictionJob( false );
        }
        const ColliderStore& colliderStore = modelCollection.GetPhysicsEngine().Colliders();
        DrawReplayPredictionOverlay( replayRuntime,
                                     modelCollection,
                                     colliderStore,
                                     tracer,
                                     std::chrono::steady_clock::now(),
                                     budgetMilliseconds );
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
    const bool hasCommittedPrediction = replayRuntime.Prediction().frames.size() >= 2;
    // Invariant: a committed prediction is a frozen future for the current
    // branch. Space-stepping the paused live scene changes solver frame/hash,
    // but must not redraw the preview; explicit dirty events such as branch,
    // target, horizon, or predict toggles are the only rebuild triggers.
    const bool allowAutomaticRefresh = !replayRuntime.Scrubber().liveAdvanceHeld && !hasCommittedPrediction;
    if ( replayRuntime.Prediction().dirty ||
         ( allowAutomaticRefresh && !replayRuntime.Prediction().building && sourceChanged && refreshDue ) )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
        RunReplayPredictionState& prediction = replayRuntime.Prediction();
        if ( prediction.baseline.comparisonActive && !prediction.baseline.valid && prediction.frames.size() >= 2 )
        {
            if ( !CaptureReplayPredictionBaselineSnapshot( prediction,
                                                           prediction.frames,
                                                           prediction.frames.size(),
                                                           replayRuntime.PathVisualizer().targetId,
                                                           replayRuntime.PathVisualizer().targetModelIndex ) )
            {
                prediction.baseline.comparisonActive = false;
            }
        }
        BeginReplayPredictionJob( replayRuntime,
                                  modelCollection,
                                  config,
                                  worldForces,
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
                                     workerPool,
                                     simulationTotalSeconds,
                                     budgetStart,
                                     budgetMilliseconds );
        }
    }

    // Why: prediction stepping owns the private-engine budget, but visible replay
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
