/*
File: SkullbonezSource/Runtime/Camera/CameraCollection.cpp
Purpose:
  Owns scene cameras and camera cycling state.

Summary:
  CameraCollection owns fixed scene camera slots, selection and tween state,
  and the frame's render-pose snapshot while borrowing optional terrain for
  movement clamps.

Invariants:
  - Camera slots are fixed-size and keyed by m_cameraHashes; scene code must
    register a camera before selecting it by hash.
  - m_renderCamera is a frame snapshot and may differ from the primary camera

    while a tween is active.
  - Zero or cancelled up vectors fall back to world +Y at the collection
    boundary before a render pose is published.

Related:
  - SkullbonezSource/Runtime/Camera/CameraCollection.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "CameraCollection.h"

#include "../../Core/FatalError.h"


using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


CameraCollection::CameraCollection()
{
    m_arrayPosition = 0;
    m_selectedCamera = 0;
    m_isTweening = 0;
    m_tweenProgress = 0;
    m_tweenSpeed = 0;
    m_terrain = 0;

    for ( int count = 0; count < SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT; ++count )
    {
        m_cameraHashes[count] = 0;
    }

    m_primaryStore.ZeroCamera();
    m_renderCamera.ZeroCamera();
}


void CameraCollection::ApplyMovementSettings( const CameraMovementSettings& settings )
{
    m_movementSettings = settings;
}


void CameraCollection::Reset()
{
    m_arrayPosition = 0;
    m_selectedCamera = 0;
    m_isTweening = false;
    m_tweenProgress = 0.0f;
    m_tweenSpeed = 0.0f;

    for ( int i = 0; i < SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT; ++i )
    {
        m_cameraHashes[i] = 0;
        m_cameraArray[i].ZeroCamera();
    }

    m_primaryStore.ZeroCamera();
    m_tweenPath.ZeroCamera();
    m_tweenCamera.ZeroCamera();
    m_tweenStart.ZeroCamera();
    m_renderCamera.ZeroCamera();
}


void CameraCollection::SetLockedMode( const bool isLocked )
{
    m_cameraArray[m_selectedCamera].m_isLockedMode = isLocked;
    m_cameraArray[m_selectedCamera].m_doCalculateViewMagnitude = !isLocked;
}


void CameraCollection::AddCamera( const Vector3& position, const Vector3& view, const Vector3& up, uint32_t hash )
{

    if ( m_arrayPosition == SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT )
    {
        SB_FATAL( "CameraCollection", "Camera slot capacity exhausted in AddCamera. count=%d capacity=%d hash=0x%08X",
                  m_arrayPosition, SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT, static_cast<unsigned int>( hash ) );
    }

    m_cameraHashes[m_arrayPosition] = hash;

    m_cameraArray[m_arrayPosition].SetAll( position, view, up );

    if ( !m_arrayPosition )
    {
        m_primaryStore = m_cameraArray[m_arrayPosition];
        m_cameraArray[m_arrayPosition].m_doPreserveViewMagnitude = true;
    }

    ++m_arrayPosition;
}


void CameraCollection::SetTweenSpeed( float tweenSpeed )
{
    m_tweenSpeed = tweenSpeed;
}


void CameraCollection::SetTweenPath( int fromIndex, int toIndex )
{

    // if the fromIndex is specifying to use the existing tween camera

    if ( fromIndex == -1 )
    {

        // use vector difference to determine the tweening vector
        // use the tween camera instead of the toIndex
        m_tweenPath = m_cameraArray[toIndex] - m_tweenCamera;

        m_tweenStart = m_tweenCamera;
    }
    else
    {

        // use vector difference to determine the tweening vector
        // use fromIndex and toIndex to determine this
        m_tweenPath = m_cameraArray[toIndex] - m_cameraArray[fromIndex];

        m_tweenStart = m_cameraArray[fromIndex];
    }
}


void CameraCollection::UpdateTweenPath()
{
    m_tweenPath = m_cameraArray[m_selectedCamera] - m_tweenStart;
}


Camera CameraCollection::GetTweenSourcePose() const
{

    if ( m_isTweening )
    {
        return m_tweenCamera;
    }

    // Why: mode changes can rewrite the selected slot before render advances the
    // new tween. The render pose is what the player actually saw last frame, so
    // it is the least surprising source for a smooth transition.

    if ( Vector::Distance( m_renderCamera.m_position, m_renderCamera.m_view ) > 0.000001f )
    {
        return m_renderCamera;
    }

    return m_cameraArray[m_selectedCamera];
}


void CameraCollection::SelectCamera( uint32_t hash, const bool tween )
{

    // local to store requested camera index
    int selectionRequest = FindIndex( hash );

    if ( selectionRequest == m_selectedCamera )
    {
        return;
    }

    // it is not possible to tween if there is only one camera in the scene

    if ( tween && m_arrayPosition == 1 )
    {
        SB_FATAL( "CameraCollection",
                  "SelectCamera cannot tween with one registered camera. hash=0x%08X selected=%d count=%d",
                  static_cast<unsigned int>( hash ), m_selectedCamera, m_arrayPosition );
    }

    // where should the tween camera be referenced FROM?

    if ( m_isTweening && tween )
    {

        // if currently tweening, reference from the current tween camera m_position
        SetTweenPath( -1, selectionRequest );
    }
    else if ( tween )
    {

        // if not currently tweening, reference from the current selected camera
        SetTweenPath( m_selectedCamera, selectionRequest );
    }

    // turn off view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].m_doPreserveViewMagnitude = false;

    m_selectedCamera = selectionRequest;

    // turn on view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].m_doPreserveViewMagnitude = true;

    // specify if tweening
    m_isTweening = tween;

    m_tweenProgress = 0;

    ResetRelativity();
}


uint32_t CameraCollection::GetSelectedCameraName()
{
    return m_cameraHashes[m_selectedCamera];
}


bool CameraCollection::HasCamera( uint32_t hash ) const
{

    for ( int count = 0; count < m_arrayPosition; ++count )
    {

        if ( m_cameraHashes[count] == hash )
        {
            return true;
        }
    }

    return false;
}


void CameraCollection::RotatePrimary( float xMove, float yMove )
{

    // make sure a camera exists to update

    if ( !m_arrayPosition )
    {
        SB_FATAL( "CameraCollection", "RotatePrimary requires at least one registered camera. count=%d selected=%d",
                  m_arrayPosition, m_selectedCamera );
    }

    // rotate the primary camera
    m_cameraArray[m_selectedCamera].RotateCamera( xMove, yMove, m_movementSettings );
}


void CameraCollection::SetViewCoordinates( const Vector3& view )
{
    m_cameraArray[m_selectedCamera].m_view = view;
}


void CameraCollection::SetPrimaryPosition( const Vector3& position )
{
    m_cameraArray[m_selectedCamera].m_position = position;
}


void CameraCollection::SetPrimaryPose( const Vector3& position, const Vector3& view, const Vector3& up )
{
    m_cameraArray[m_selectedCamera].SetAll( position, view, up );
}


void CameraCollection::TweenPrimaryToPose( const Vector3& position, const Vector3& view, const Vector3& up )
{

    if ( !m_arrayPosition )
    {
        SB_FATAL( "CameraCollection", "TweenPrimaryToPose requires at least one registered camera. count=%d selected=%d",
                  m_arrayPosition, m_selectedCamera );
    }

    const Camera tweenStart = GetTweenSourcePose();
    SetPrimaryPose( position, view, up );

    const Camera& destination = m_cameraArray[m_selectedCamera];

    if ( Vector::Distance( tweenStart.m_position, destination.m_position ) <= 0.000001f &&
         Vector::Distance( tweenStart.m_view, destination.m_view ) <= 0.000001f &&
         Vector::Distance( tweenStart.m_upVector, destination.m_upVector ) <= 0.000001f )
    {

        // Why: replay inspection can switch ownership to the free camera while
        // keeping the same visible pose. Treat that as a completed transition so
        // the retained tween state does not stay active for a no-op move.
        m_isTweening = false;
        m_tweenProgress = 0.0f;
        ResetRelativity();
        return;
    }

    m_tweenStart = tweenStart;
    UpdateTweenPath();
    m_isTweening = true;
    m_tweenProgress = 0.0f;
    ResetRelativity();
}


void CameraCollection::MovePrimary( Camera::TravelDirection direction, float amount )
{

    // make sure a camera exists to update

    if ( !m_arrayPosition )
    {
        SB_FATAL( "CameraCollection",
                  "MovePrimary requires at least one registered camera. direction=%d quantity=%f count=%d selected=%d",
                  static_cast<int>( direction ), amount, m_arrayPosition, m_selectedCamera );
    }

    // move the primary camera
    m_cameraArray[m_selectedCamera].MoveCamera( direction, amount, m_movementSettings );
}


const Vector3& CameraCollection::GetCameraTranslation() const
{
    return ( m_cameraArray[m_selectedCamera].m_position );
}


const Vector3& CameraCollection::GetRenderCameraTranslation() const
{
    return m_renderCamera.m_position;
}


const Vector3& CameraCollection::GetRenderCameraView() const
{
    return m_renderCamera.m_view;
}


const Vector3& CameraCollection::GetRenderCameraUp() const
{
    return m_renderCamera.m_upVector;
}


void CameraCollection::CancelTween()
{
    m_isTweening = false;
}


void CameraCollection::ApplyPrimaryMovementBuffer()
{
    m_cameraArray[m_selectedCamera].ApplyMovementBuffer( m_movementSettings );
}


void CameraCollection::AmmendPrimaryY( float yCoordinate )
{
    float difference = yCoordinate - m_cameraArray[m_selectedCamera].m_position.y;

    m_cameraArray[m_selectedCamera].m_position.y = yCoordinate;

    if ( !m_cameraArray[m_selectedCamera].m_isLockedMode )
    {
        m_cameraArray[m_selectedCamera].m_view.y += difference;
    }
}


void CameraCollection::ResetRelativity()
{
    m_primaryStore = m_cameraArray[m_selectedCamera];
}


void CameraCollection::SetCamera()
{

    // make sure a camera exists

    if ( !m_arrayPosition )
    {
        SB_FATAL( "CameraCollection", "SetCamera requires at least one registered camera. count=%d selected=%d tweening=%d",
                  m_arrayPosition, m_selectedCamera, m_isTweening ? 1 : 0 );
    }

    // if we are not in tween mode

    if ( !m_isTweening )
    {
        SetViewMatrix( m_cameraArray[m_selectedCamera] );
    }
    else
    {
        m_tweenProgress += ( ( 1 - m_tweenProgress ) * m_tweenSpeed );

        // turn off tweening if the current tween is complete

        if ( m_tweenProgress > 0.99999f )
        {
            m_isTweening = false;
        }

        // Keep the destination live; the target camera may move during a tween.
        UpdateTweenPath();

        m_tweenCamera = m_tweenStart;

        m_tweenCamera += m_tweenPath * m_tweenProgress;

        // Opposed endpoint up vectors can cancel at the tween midpoint.

        if ( !m_tweenCamera.m_upVector.TryNormalise() )
        {
            m_tweenCamera.m_upVector = Vector3( 0.0f, 1.0f, 0.0f );
        }

        // Avoid going through terrain during tweens when the scene owns a
        // terrain surface. Terrainless authored scenes deliberately bind null;
        // their cameras must remain unconstrained in space.

        if ( m_terrain )
        {
            float terrainHeight = m_terrain->GetTerrainHeightAt( m_tweenCamera.m_position.x, m_tweenCamera.m_position.z );

            if ( m_tweenCamera.m_position.y < terrainHeight )
            {
                m_tweenCamera.m_position.y = terrainHeight + m_movementSettings.minCameraHeight;
            }
        }

        SetViewMatrix( m_tweenCamera );
    }
}


void CameraCollection::SetViewMatrix( const Camera& camera )
{
    m_renderCamera = camera;
    m_currentViewMatrix = Matrix4::LookAt( camera.m_position, camera.m_view, camera.m_upVector );
}


int CameraCollection::FindIndex( uint32_t hash )
{

    for ( int count = 0; count < m_arrayPosition; ++count )
    {

        if ( m_cameraHashes[count] == hash )
        {
            return count;
        }
    }

    SB_FATAL( "CameraCollection", "Camera hash lookup failed. hash=0x%08X count=%d selected=%d",
              static_cast<unsigned int>( hash ), m_arrayPosition, m_selectedCamera );
}


bool CameraCollection::IsCameraSelected( uint32_t hash )
{
    return ( FindIndex( hash ) == m_selectedCamera );
}


const Vector3& CameraCollection::GetCameraView() const
{
    return m_cameraArray[m_selectedCamera].m_view;
}


const Vector3& CameraCollection::GetCameraUp() const
{
    return m_cameraArray[m_selectedCamera].m_upVector;
}


void CameraCollection::SetCameraXZBounds( const XZBounds bounds )
{

    for ( int count = 0; count < m_arrayPosition; ++count )
    {
        m_cameraArray[count].m_boundary = bounds;
    }
}


void CameraCollection::SetCameraXZBounds( uint32_t hash, const XZBounds bounds )
{
    int targetIndex = FindIndex( hash );
    m_cameraArray[targetIndex].m_boundary = bounds;
}


void CameraCollection::SetTerrain( Terrain* terrain )
{
    m_terrain = terrain;
}
