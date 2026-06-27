/*
File: SkullbonezSource/Runtime/Replay/RunReplayQueryTools.inl
Purpose:
  Contains replay path-target picking and query helpers.

Mental model:
  Replay path queries translate a mouse pick into a stable ReplayBodyId target.
  The visualizer and prediction layers can then follow retained or future solver
  contacts without depending on transient model indices alone.

Glossary:
  ReplayBodyId: Stable body id retained across replay samples.
  Path target: Body selected for retained/future trajectory visualization.

Invariants:
  - Picking must prefer stable replay ids and only use model indices as hints.
  - Changing path targets invalidates prediction caches.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
bool Run::TryPickReplayPathTargetFromMouse( bool additive, bool clearOnMiss )
{
    // Concept: A path pick converts volatile mouse/model hits into stable
    // ReplayBodyId targets before prediction and retained-path caches observe it.
    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( clearOnMiss )
        {
            m_replayRuntime.ClearCameraFocusForRestore();
            ExitReplayInspectionCamera();
            m_replayRuntime.ClearPathVisualizerState();
        }
        return false;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    ReplayBodyId pickedId;
    int pickedIndex = -1;
    char pickedName[64] = {};
    if ( const ReplaySolverFrameSample* sample = m_replayRuntime.CurrentSolverScrubSample() )
    {
        float bestT = FLT_MAX;
        for ( const ReplaySolverBodySample& body : sample->bodies )
        {
            float radius = 1.0f;
            if ( body.modelIndex >= 0 && body.modelIndex < static_cast<int>( models.size() ) )
            {
                radius = EditorModelRadius( models[static_cast<std::size_t>( body.modelIndex )] ) + 1.0f;
            }
            float rayT = 0.0f;
            if ( IntersectRaySphere( rayOrigin, rayDirection, body.position, radius, rayT ) && rayT < bestT )
            {
                bestT = rayT;
                pickedId = body.id;
                pickedIndex = body.modelIndex;
                pickedName[0] = '\0';
                if ( body.name[0] != '\0' )
                {
                    strncpy_s( pickedName, sizeof( pickedName ), body.name, _TRUNCATE );
                }
            }
        }
    }
    else
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::ReplayPathTarget;
        request.models = &models;
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        RuntimePickResult result;
        if ( RuntimePickService::TryPickModel( request, result ) && result.modelIndex >= 0 &&
             result.modelIndex < static_cast<int>( models.size() ) )
        {
            pickedIndex = result.modelIndex;
            const GameModel& model = models[static_cast<std::size_t>( pickedIndex )];
            pickedId.value = model.GetReplayBodyId();
            const char* modelName = model.GetName();
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( pickedName, sizeof( pickedName ), modelName, _TRUNCATE );
            }
        }
    }

    if ( pickedIndex >= 0 && pickedIndex < static_cast<int>( models.size() ) )
    {
        const int collectionIndex = ReplayRagdollTorsoModelIndexForPart( models, pickedIndex );
        if ( collectionIndex >= 0 && collectionIndex < static_cast<int>( models.size() ) &&
             collectionIndex != pickedIndex )
        {
            const GameModel& rootModel = models[static_cast<std::size_t>( collectionIndex )];
            pickedIndex = collectionIndex;
            pickedId.value = rootModel.GetReplayBodyId();
            pickedName[0] = '\0';
            const char* rootName = rootModel.GetName();
            if ( rootName && rootName[0] != '\0' )
            {
                strncpy_s( pickedName, sizeof( pickedName ), rootName, _TRUNCATE );
            }
        }
    }

    if ( pickedId.value != 0 )
    {
        if ( !additive )
        {
            m_replayRuntime.PathVisualizer().targets.clear();
        }

        RunReplayPathTarget* target = FindReplayPathTarget( m_replayRuntime.PathVisualizer(), pickedId );
        if ( !target )
        {
            if ( m_replayRuntime.PathVisualizer().targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                m_replayRuntime.PathVisualizer().targets.erase( m_replayRuntime.PathVisualizer().targets.begin() );
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            m_replayRuntime.PathVisualizer().targets.push_back( nextTarget );
            target = &m_replayRuntime.PathVisualizer().targets.back();
        }

        target->modelIndex = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyPrimaryReplayPathTarget( m_replayRuntime.PathVisualizer(), pickedId, pickedIndex, target->name );
        m_replayRuntime.PathVisualizer().futureNodes.clear();
        m_replayRuntime.ClearPredictionCache();
        m_replayRuntime.MarkPredictionDirty();
        return true;
    }

    if ( clearOnMiss )
    {
        m_replayRuntime.ClearCameraFocusForRestore();
        ExitReplayInspectionCamera();
        m_replayRuntime.ClearPathVisualizerState();
    }
    return false;
}
