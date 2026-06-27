/*
File: SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl
Purpose:
  Implements manipulator-mode mouse pickup capture, target tracking, and physics impulse application.

Mental model:
  Mouse pickup is a runtime tool owner distinct from editor placement. It borrows camera rays
  and physics models, captures the pointer while active, and releases cleanly at mode changes.

Glossary:
  Mouse pickup: Manipulator tool that drags a dynamic body toward a camera-facing target plane.
  Grab offset: World-space offset from body center to the point initially picked by the ray.

Invariants:
  - Pointer capture and interaction gesture state must end whenever pickup is canceled.
  - Body indices are revalidated against PhysicsModels before applying impulses.
  - This file must only be included from RunEditorTools.cpp after terrain-placement helpers.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

void Run::CancelMousePickup()
{
    if ( m_runtimeTools.MousePickup().mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
    }
    if ( m_interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        m_interaction.EndGesture( InteractionExitReason::EndGesture );
    }
    m_runtimeTools.MousePickup() = RunMousePickupState{};
}


bool Run::TickMousePickupInput( HWND hwnd, const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    if ( !IsManipulatorCameraMode() || m_runtimeTools.Editor().editorModeEnabled || m_replayRuntime.InspectionActive() )
    {
        CancelMousePickup();
        return false;
    }

    const auto UpdatePickupTarget = [&]() -> bool
    {
        // Concept: Manipulator drag follows a camera-facing plane through the
        // original grab point. That avoids depth jumps as the mouse moves while
        // still letting physics apply the final motion through impulses.
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
        {
            return false;
        }

        const float denom = rayDirection * m_runtimeTools.MousePickup().planeNormal;
        if ( fabsf( denom ) <= 1.0e-5f )
        {
            return false;
        }

        const float planeT =
            ( ( m_runtimeTools.MousePickup().planePoint - rayOrigin ) * m_runtimeTools.MousePickup().planeNormal ) /
            denom;
        if ( planeT < 0.0f )
        {
            return false;
        }

        m_runtimeTools.MousePickup().targetPoint = rayOrigin + rayDirection * planeT;
        return true;
    };

    if ( m_runtimeTools.MousePickup().active )
    {
        if ( mouseEdges.leftReleased || !mouseEdges.leftDown )
        {
            CancelMousePickup();
            return true;
        }
        UpdatePickupTarget();
        return true;
    }

    if ( !mouseEdges.leftPressed )
    {
        return false;
    }
    if ( suppressWorldActionThisFrame || m_UI.WantsNativeMouseCursor() )
    {
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        return true;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ManipulatorPickup;
    request.models = &m_cGameModelCollection.Models();
    request.rayOrigin = rayOrigin;
    request.rayDirection = rayDirection;

    RuntimePickResult result;
    if ( !RuntimePickService::TryPickModel( request, result ) )
    {
        return true;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int pickedIndex = result.modelIndex;
    if ( pickedIndex < 0 || pickedIndex >= static_cast<int>( models.size() ) )
    {
        return true;
    }

    const GameModel& picked = models[static_cast<size_t>( pickedIndex )];
    Vector3 cameraNormal = m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
    const float normalLenSq = VectorMagSquared( cameraNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return true;
    }
    cameraNormal *= 1.0f / sqrtf( normalLenSq );

    const Vector3 grabPoint = rayOrigin + rayDirection * result.rayT;
    m_runtimeTools.MousePickup().active = true;
    m_runtimeTools.MousePickup().mouseCaptured = true;
    m_runtimeTools.MousePickup().modelIndex = pickedIndex;
    m_runtimeTools.MousePickup().planePoint = grabPoint;
    m_runtimeTools.MousePickup().planeNormal = cameraNormal;
    m_runtimeTools.MousePickup().grabOffset = grabPoint - picked.GetPosition();
    m_runtimeTools.MousePickup().targetPoint = grabPoint;
    m_runtimeTools.MousePickup().preservedAngularVelocity = picked.GetAngularVelocity();
    m_runtimeTools.MousePickup().lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    UI::InputControl::BeginMouseCapture( hwnd );
    const POINT mouse = Input::GetClientMouseCoordinates();
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = mouse.x;
    gesture.startY = mouse.y;
    gesture.modelIndex = pickedIndex;
    m_interaction.BeginGesture( gesture,
                                RuntimePointerCaptureOwner::ToolGesture,
                                InteractionExitReason::EnterManipulator );
    EnterInteractiveSceneRun();
    UpdatePickupTarget();
    return true;
}


void Run::ApplyMousePickupPhysicsStep()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    // Hazard: Pickup stores a frame-local model index. Revalidate against the
    // physics model vector on every step before restoring angular velocity or
    // applying the spring-like impulse.
    std::vector<GameModel>& models = m_cGameModelCollection.MutablePhysicsModelsForCompatibility();
    if ( m_runtimeTools.MousePickup().modelIndex < 0 ||
         m_runtimeTools.MousePickup().modelIndex >= static_cast<int>( models.size() ) )
    {
        CancelMousePickup();
        return;
    }

    GameModel& model = models[static_cast<size_t>( m_runtimeTools.MousePickup().modelIndex )];
    if ( model.IsFixed() )
    {
        CancelMousePickup();
        return;
    }
    model.SetAngularVelocity( m_runtimeTools.MousePickup().preservedAngularVelocity );

    const Vector3 grabPoint = model.GetPosition() + m_runtimeTools.MousePickup().grabOffset;
    const Vector3 pull = m_runtimeTools.MousePickup().targetPoint - grabPoint;
    const float pullLenSq = VectorMagSquared( pull );
    if ( pullLenSq <= MOUSE_PICKUP_DEAD_ZONE * MOUSE_PICKUP_DEAD_ZONE )
    {
        m_runtimeTools.MousePickup().lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        return;
    }

    Vector3 impulse = pull * MOUSE_PICKUP_STIFFNESS - model.GetVelocity() * MOUSE_PICKUP_DAMPING;
    const float impulseLenSq = VectorMagSquared( impulse );
    if ( impulseLenSq > MOUSE_PICKUP_MAX_IMPULSE * MOUSE_PICKUP_MAX_IMPULSE )
    {
        impulse *= MOUSE_PICKUP_MAX_IMPULSE / sqrtf( impulseLenSq );
    }

    m_cGameModelCollection.ApplyBodyImpulse( m_runtimeTools.MousePickup().modelIndex,
                                             impulse,
                                             SkullbonezCore::Math::Vector::ZERO_VECTOR );
    m_cGameModelCollection.InvalidatePhysicsStreams();
    m_runtimeTools.MousePickup().lastImpulse = impulse;
}


void Run::RestoreMousePickupAngularVelocity()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    std::vector<GameModel>& models = m_cGameModelCollection.MutablePhysicsModelsForCompatibility();
    if ( m_runtimeTools.MousePickup().modelIndex < 0 ||
         m_runtimeTools.MousePickup().modelIndex >= static_cast<int>( models.size() ) )
    {
        CancelMousePickup();
        return;
    }

    GameModel& model = models[static_cast<size_t>( m_runtimeTools.MousePickup().modelIndex )];
    if ( model.IsFixed() )
    {
        CancelMousePickup();
        return;
    }

    model.SetAngularVelocity( m_runtimeTools.MousePickup().preservedAngularVelocity );
    m_cGameModelCollection.InvalidatePhysicsStreams();
}
