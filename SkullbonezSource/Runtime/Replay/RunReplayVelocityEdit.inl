/*
File: SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl
Purpose:
  Implements replay velocity-edit picking, dragging, mutation, and overlay drawing.

Mental model:
  Velocity edit is a replay-owned interaction mode. It turns mouse rays into linear or angular
  velocity edits, then marks prediction dirty so the path preview reflects the changed live state.

Glossary:
  Velocity gizmo: Replay overlay that exposes linear axes and angular rings for one body.
  Live advance: Replay scrubber mode that lets edited live physics advance while tools stay active.

Invariants:
  - Pointer capture must end whenever the drag exits or the edited target becomes invalid.
  - Edited velocities are clamped before waking or mutating the physics body.
  - This file must only be included from RunReplayTools.cpp after cause-tree input handling.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

int HitReplayVelocityLinearAxis( const ReplayRuntime& replayRuntime,
                                 const std::vector<GameModel>& models,
                                 const Vector3& rayOrigin,
                                 const Vector3& rayDirection )
{
    const int modelIndex = replayRuntime.ResolveVelocityEditModelIndex( models );
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return -1;
    }

    const GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( model.GetVelocity(), axis );
        const Vector3 endpoint = origin + axisVector * ReplayVelocityLinearVisualAxisT( radius, component );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, endpoint );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int HitReplayVelocityAngularAxis( const ReplayRuntime& replayRuntime,
                                  const std::vector<GameModel>& models,
                                  const Vector3& rayOrigin,
                                  const Vector3& rayDirection )
{
    const int modelIndex = replayRuntime.ResolveVelocityEditModelIndex( models );
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return -1;
    }

    const GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float modelRadius = EditorModelRadius( model );
    int bestAxis = -1;
    float bestDiff = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = normal * rayDirection;
        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
        if ( rayT < 0.0f )
        {
            continue;
        }

        const float ringRadius =
            ReplayVelocityAngularVisualRadius( modelRadius,
                                               ReplayVelocityAxisComponent( model.GetAngularVelocity(), axis ) );
        const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );
        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( radial * normal ) );
        const float diff = fabsf( radialDistance - ringRadius );
        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool TryReplayVelocityAxisRayParameter( const ReplayRuntime& replayRuntime,
                                        const std::vector<GameModel>& models,
                                        int axis,
                                        const Vector3& rayOrigin,
                                        const Vector3& rayDirection,
                                        float& outAxisT )
{
    const int modelIndex = replayRuntime.ResolveVelocityEditModelIndex( models );
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return false;
    }

    const Vector3 axisOrigin = models[static_cast<std::size_t>( modelIndex )].GetPosition();
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


bool TryReplayVelocityAngularRayAngle( const ReplayRuntime& replayRuntime,
                                       const std::vector<GameModel>& models,
                                       int axis,
                                       const Vector3& rayOrigin,
                                       const Vector3& rayDirection,
                                       float& outAngle )
{
    const int modelIndex = replayRuntime.ResolveVelocityEditModelIndex( models );
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return false;
    }

    const Vector3 origin = models[static_cast<std::size_t>( modelIndex )].GetPosition();
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = normal * rayDirection;
    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( radial * normal );
    const float radialLenSq = radial * radial;
    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( radial * basisB, radial * basisA );
    return true;
}


static void ApplyReplayVelocityEditToModel( ReplayRuntime& replayRuntime,
                                            SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                            int modelIndex,
                                            const Vector3& linearVelocity,
                                            const Vector3& angularVelocity,
                                            double visibleUntil )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Apply" );
    // Invariant: replay velocity edit mutates live physics state deliberately,
    // then marks prediction dirty so retained and predicted overlays do not
    // present stale paths for the edited body.
    if ( modelIndex < 0 || modelIndex >= modelCollection.GetModelCount() )
    {
        return;
    }

    Vector3 clampedLinear = linearVelocity;
    Vector3 clampedAngular = angularVelocity;
    clampedLinear.x = std::clamp( clampedLinear.x, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.y = std::clamp( clampedLinear.y, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.z = std::clamp( clampedLinear.z, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedAngular.x =
        std::clamp( clampedAngular.x, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.y =
        std::clamp( clampedAngular.y, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.z =
        std::clamp( clampedAngular.z, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

    GameModel& model = modelCollection.GetModelAtIndex( modelIndex );
    if ( model.IsFixed() )
    {
        return;
    }

    model.SetLinearVelocity( clampedLinear );
    model.SetAngularVelocity( clampedAngular );
    modelCollection.CommitEditedModelPhysicsState( modelIndex, false );
    if ( VectorMagSquared( clampedLinear ) > TOLERANCE * TOLERANCE ||
         VectorMagSquared( clampedAngular ) > TOLERANCE * TOLERANCE )
    {
        modelCollection.WakeModel( modelIndex );
    }
    modelCollection.InvalidatePhysicsStreams();
    replayRuntime.MarkPredictionDirty();
    replayRuntime.Scrubber().visibleUntil = visibleUntil;
    replayRuntime.Scrubber().visible = true;
}


bool Run::TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayRuntime.VelocityEdit().leftWasDown;
    const bool leftReleased = !leftDown && m_replayRuntime.VelocityEdit().leftWasDown;
    m_replayRuntime.VelocityEdit().leftWasDown = leftDown;

    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled ||
         !SceneState().isScenePhysics || RuntimeWindowScreenWidth( m_systems, m_config ) <= 0 ||
         RuntimeWindowScreenHeight( m_systems, m_config ) <= 0 )
    {
        m_replayRuntime.VelocityEdit().hotLinearAxis = -1;
        m_replayRuntime.VelocityEdit().hotAngularAxis = -1;
        if ( m_replayRuntime.VelocityEdit().dragging )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
        }
        if ( m_replayRuntime.VelocityEdit().mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayRuntime.VelocityEdit().mouseCaptured = false;
        }
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( m_replayRuntime.VelocityEdit().dragging && ( leftReleased || !leftDown ) )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
            if ( m_replayRuntime.VelocityEdit().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.VelocityEdit().mouseCaptured = false;
            }
        }
        return m_replayRuntime.VelocityEdit().dragging;
    }

    const auto applyReplayVelocityEditDrag = [&]( const Vector3& dragRayOrigin, const Vector3& dragRayDirection )
    {
        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        const int modelIndex = m_replayRuntime.ResolveVelocityEditModelIndex( m_cGameModelCollection.Models() );
        if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() ||
             m_replayRuntime.VelocityEdit().activeAxis < 0 )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
            if ( m_replayRuntime.VelocityEdit().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.VelocityEdit().mouseCaptured = false;
            }
            return;
        }

        Vector3 linearVelocity = m_replayRuntime.VelocityEdit().dragStartLinearVelocity;
        Vector3 angularVelocity = m_replayRuntime.VelocityEdit().dragStartAngularVelocity;
        if ( m_replayRuntime.VelocityEdit().draggingAngular )
        {
            float currentAngle = 0.0f;
            if ( !TryReplayVelocityAngularRayAngle( m_replayRuntime,
                                                    m_cGameModelCollection.Models(),
                                                    m_replayRuntime.VelocityEdit().activeAxis,
                                                    dragRayOrigin,
                                                    dragRayDirection,
                                                    currentAngle ) )
            {
                return;
            }
            const float angleDelta =
                WrapEditorAngleDelta( currentAngle - m_replayRuntime.VelocityEdit().dragStartAngle );
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartAngularVelocity,
                                             m_replayRuntime.VelocityEdit().activeAxis ) +
                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
            ReplayVelocitySetAxisComponent(
                angularVelocity,
                m_replayRuntime.VelocityEdit().activeAxis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
        }
        else
        {
            float axisT = 0.0f;
            if ( !TryReplayVelocityAxisRayParameter( m_replayRuntime,
                                                     m_cGameModelCollection.Models(),
                                                     m_replayRuntime.VelocityEdit().activeAxis,
                                                     dragRayOrigin,
                                                     dragRayDirection,
                                                     axisT ) )
            {
                return;
            }
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartLinearVelocity,
                                             m_replayRuntime.VelocityEdit().activeAxis ) +
                ( axisT - m_replayRuntime.VelocityEdit().dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
            ReplayVelocitySetAxisComponent(
                linearVelocity,
                m_replayRuntime.VelocityEdit().activeAxis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
        }

        ApplyReplayVelocityEditToModel( m_replayRuntime,
                                        m_cGameModelCollection,
                                        modelIndex,
                                        linearVelocity,
                                        angularVelocity,
                                        m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS );
    };

    if ( m_replayRuntime.VelocityEdit().dragging )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            applyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
            if ( m_replayRuntime.VelocityEdit().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.VelocityEdit().mouseCaptured = false;
            }
        }
        return true;
    }

    m_replayRuntime.VelocityEdit().hotAngularAxis =
        uiBlocksMouse
            ? -1
            : HitReplayVelocityAngularAxis( m_replayRuntime, m_cGameModelCollection.Models(), rayOrigin, rayDirection );
    m_replayRuntime.VelocityEdit().hotLinearAxis =
        ( uiBlocksMouse || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            ? -1
            : HitReplayVelocityLinearAxis( m_replayRuntime, m_cGameModelCollection.Models(), rayOrigin, rayDirection );

    if ( !uiBlocksMouse && leftPressed )
    {
        const POINT mouse = Input::GetClientMouseCoordinates();
        const int modelIndex = m_replayRuntime.ResolveVelocityEditModelIndex( m_cGameModelCollection.Models() );
        if ( modelIndex >= 0 && modelIndex < m_cGameModelCollection.GetModelCount() )
        {
            const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
            if ( m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( m_replayRuntime,
                                                       m_cGameModelCollection.Models(),
                                                       m_replayRuntime.VelocityEdit().hotAngularAxis,
                                                       rayOrigin,
                                                       rayDirection,
                                                       startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                            InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        EnterReplayInspectionCamera();
                    }
                    else
                    {
                        ExitReplayInspectionCamera();
                    }
                    m_replayRuntime.Prediction().enabled = true;
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            modelIndex,
                                            m_replayRuntime.VelocityEdit().hotAngularAxis,
                                            true );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = true;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotAngularAxis;
                    m_replayRuntime.VelocityEdit().dragStartAngle = startAngle;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = model.GetVelocity();
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
            else if ( m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;
                if ( TryReplayVelocityAxisRayParameter( m_replayRuntime,
                                                        m_cGameModelCollection.Models(),
                                                        m_replayRuntime.VelocityEdit().hotLinearAxis,
                                                        rayOrigin,
                                                        rayDirection,
                                                        axisT ) )
                {
                    EnterInteractiveSceneRun();
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                            InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        EnterReplayInspectionCamera();
                    }
                    else
                    {
                        ExitReplayInspectionCamera();
                    }
                    m_replayRuntime.Prediction().enabled = true;
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            modelIndex,
                                            m_replayRuntime.VelocityEdit().hotLinearAxis,
                                            false );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = false;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotLinearAxis;
                    m_replayRuntime.VelocityEdit().dragStartAxisT = axisT;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = model.GetVelocity();
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
        }
    }

    return m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0;
}


void Run::RenderReplayVelocityEditOverlay( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled )
    {
        return;
    }

    const int modelIndex = m_replayRuntime.ResolveVelocityEditModelIndex( m_cGameModelCollection.Models() );
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo( model,
                                   m_replayRuntime.VelocityEdit().hotLinearAxis,
                                   m_replayRuntime.VelocityEdit().hotAngularAxis,
                                   m_replayRuntime.VelocityEdit().activeAxis,
                                   m_replayRuntime.VelocityEdit().draggingAngular );
}
