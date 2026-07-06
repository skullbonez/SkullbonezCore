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
  Replay velocity body view: Local value view resolved from PhysicsBodyStore and
    ColliderStore for one replay target; model index is a UI/presentation hint
    after replay identity resolves to a PhysicsBodyHandle.

Invariants:
  - Pointer capture must end whenever the drag exits or the edited target becomes invalid.
  - Edited velocities are clamped before waking or mutating the physics body.
  - Hit testing, drag-start values, and gizmo drawing must read store rows, not
    the post-step GameModel body mirror.
  - This file must only be included from RunReplayTools.cpp after cause-tree input handling.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

struct ReplayVelocityBodyView
{
    PhysicsBodyHandle body;
    int modelIndex = -1;
    Vector3 position = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion orientation = IDENTITY_QUATERNION;
    Vector3 linearVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const CollisionShape* shape = nullptr;
    float radius = 1.0f;
    bool fixed = false;
};


static bool TryResolveReplayVelocityBodyView( const ReplayRuntime& replayRuntime,
                                              const PhysicsBodyStore& bodyStore,
                                              const ColliderStore& colliderStore,
                                              ReplayVelocityBodyView& outView )
{
    outView = ReplayVelocityBodyView{};
    const PhysicsBodyHandle bodyHandle = replayRuntime.ResolveVelocityEditBodyHandle( bodyStore );
    const int modelIndex = bodyStore.ModelIndexForHandle( bodyHandle );
    if ( modelIndex < 0 || modelIndex >= bodyStore.Count() )
    {
        return false;
    }
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider =
        body ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) ) : nullptr;
    if ( !body || !collider )
    {
        return false;
    }

    // Invariant: replay velocity edit resolves identity to a body handle before
    // it reads pose, velocity, or shape rows. modelIndex remains only for UI
    // gesture metadata and collider pairing while replay/editor identity moves
    // away from transient GameModel order.
    outView.body = bodyHandle;
    outView.modelIndex = modelIndex;
    outView.position = body->position;
    outView.orientation = body->orientation;
    outView.linearVelocity = body->linearVelocity;
    outView.angularVelocity = body->angularVelocity;
    outView.shape = &collider->shape;
    outView.radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) );
    outView.fixed = body->isFixed;
    return outView.shape != nullptr;
}


int HitReplayVelocityLinearAxis( const ReplayVelocityBodyView& body,
                                 const Vector3& rayOrigin,
                                 const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float radius = body.radius;
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( body.linearVelocity, axis );
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


int HitReplayVelocityAngularAxis( const ReplayVelocityBodyView& body,
                                  const Vector3& rayOrigin,
                                  const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float modelRadius = body.radius;
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
            ReplayVelocityAngularVisualRadius( modelRadius, ReplayVelocityAxisComponent( body.angularVelocity, axis ) );
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


bool TryReplayVelocityAxisRayParameter( const ReplayVelocityBodyView& body,
                                        int axis,
                                        const Vector3& rayOrigin,
                                        const Vector3& rayDirection,
                                        float& outAxisT )
{
    if ( axis < 0 || axis > 2 || body.modelIndex < 0 )
    {
        return false;
    }

    const Vector3 axisOrigin = body.position;
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


bool TryReplayVelocityAngularRayAngle( const ReplayVelocityBodyView& body,
                                       int axis,
                                       const Vector3& rayOrigin,
                                       const Vector3& rayDirection,
                                       float& outAngle )
{
    if ( axis < 0 || axis > 2 || body.modelIndex < 0 )
    {
        return false;
    }

    const Vector3 origin = body.position;
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


static void ApplyReplayVelocityEditToBody( ReplayRuntime& replayRuntime,
                                           SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                           PhysicsBodyHandle body,
                                           const Vector3& linearVelocity,
                                           const Vector3& angularVelocity,
                                           double visibleUntil )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Apply" );
    // Invariant: replay velocity edit mutates live physics state deliberately,
    // then marks prediction dirty so retained and predicted overlays do not
    // present stale paths for the edited body.
    if ( !body.IsValid() )
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

    if ( !modelCollection.GetPhysicsEngine().SetBodyVelocity( body, clampedLinear, clampedAngular, true ) )
    {
        return;
    }
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

    // Why: velocity edit resolves replay identity through store-owned handles.
    // Run owns the cold repair edge before the resolver reads body/collider rows.
    const bool velocityStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
    PhysicsEngine& velocityPhysics = m_cGameModelCollection.GetPhysicsEngine();
    const auto tryResolveVelocityBody = [&]( ReplayVelocityBodyView& outBody )
    {
        return velocityStoresReady && TryResolveReplayVelocityBodyView( m_replayRuntime,
                                                                        velocityPhysics.BodyStore(),
                                                                        velocityPhysics.Colliders(),
                                                                        outBody );
    };

    const auto applyReplayVelocityEditDrag = [&]( const Vector3& dragRayOrigin, const Vector3& dragRayDirection )
    {
        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        ReplayVelocityBodyView body;
        if ( !tryResolveVelocityBody( body ) || m_replayRuntime.VelocityEdit().activeAxis < 0 )
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
            if ( !TryReplayVelocityAngularRayAngle( body,
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
            if ( !TryReplayVelocityAxisRayParameter( body,
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

        ApplyReplayVelocityEditToBody( m_replayRuntime,
                                       m_cGameModelCollection,
                                       body.body,
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

    ReplayVelocityBodyView hotBody;
    const bool hasHotBody = !uiBlocksMouse && tryResolveVelocityBody( hotBody );
    m_replayRuntime.VelocityEdit().hotAngularAxis =
        hasHotBody ? HitReplayVelocityAngularAxis( hotBody, rayOrigin, rayDirection ) : -1;
    m_replayRuntime.VelocityEdit().hotLinearAxis =
        ( !hasHotBody || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            ? -1
            : HitReplayVelocityLinearAxis( hotBody, rayOrigin, rayDirection );

    if ( !uiBlocksMouse && leftPressed )
    {
        const POINT mouse = Input::GetClientMouseCoordinates();
        ReplayVelocityBodyView body;
        if ( tryResolveVelocityBody( body ) && !body.fixed )
        {
            if ( m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( body,
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
                                            body.modelIndex,
                                            m_replayRuntime.VelocityEdit().hotAngularAxis,
                                            true );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = true;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotAngularAxis;
                    m_replayRuntime.VelocityEdit().dragStartAngle = startAngle;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = body.linearVelocity;
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = body.angularVelocity;
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
                if ( TryReplayVelocityAxisRayParameter( body,
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
                                            body.modelIndex,
                                            m_replayRuntime.VelocityEdit().hotLinearAxis,
                                            false );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = false;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotLinearAxis;
                    m_replayRuntime.VelocityEdit().dragStartAxisT = axisT;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = body.linearVelocity;
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = body.angularVelocity;
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
        }

        // Concept: velocity edit owns replay body targeting. A click on the
        // body itself should select the replay path target for the velocity
        // gizmo, not fall through to normal editor/world selection and clear it.
        (void)TryPickReplayPathTargetFromMouse( false, false );
        if ( m_replayRuntime.PathVisualizer().hasTarget )
        {
            EnterInteractiveSceneRun();
            if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && m_replayRuntime.ShouldUseInspectionCamera() )
            {
                EnterReplayInspectionCamera();
            }
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                                InteractionExitReason::EnterReplay );
            m_replayRuntime.Prediction().enabled = true;
            m_replayRuntime.Scrubber().visibleUntil =
                m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
            m_replayRuntime.Scrubber().visible = true;
        }
        return true;
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

    ReplayVelocityBodyView body;
    if ( !m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology() )
    {
        return;
    }
    PhysicsEngine& velocityPhysics = m_cGameModelCollection.GetPhysicsEngine();
    if ( !TryResolveReplayVelocityBodyView( m_replayRuntime,
                                            velocityPhysics.BodyStore(),
                                            velocityPhysics.Colliders(),
                                            body ) ||
         body.fixed || !body.shape )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo( body.position,
                                   body.orientation,
                                   *body.shape,
                                   body.radius,
                                   body.linearVelocity,
                                   body.angularVelocity,
                                   m_replayRuntime.VelocityEdit().hotLinearAxis,
                                   m_replayRuntime.VelocityEdit().hotAngularAxis,
                                   m_replayRuntime.VelocityEdit().activeAxis,
                                   m_replayRuntime.VelocityEdit().draggingAngular );
}
