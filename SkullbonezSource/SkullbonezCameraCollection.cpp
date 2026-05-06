// --- Includes ---
#include "SkullbonezCameraCollection.h"


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Transformation;


CameraCollection::CameraCollection()
{
    m_arrayPosition = 0;
    m_selectedCamera = 0;
    m_isTweening = 0;
    m_tweenProgress = 0;
    m_tweenSpeed = 0;
    m_terrain = 0;

    for ( int count = 0; count < TOTAL_CAMERA_COUNT; ++count )
    {
        m_cameraHashes[count] = 0;
    }

    m_primaryStore.ZeroCamera();
}


CameraCollection* CameraCollection::Instance()
{
    if ( !CameraCollection::pInstance )
    {
        static CameraCollection instance;
        CameraCollection::pInstance = &instance;
    }
    return CameraCollection::pInstance;
}


void CameraCollection::Destroy()
{
    if ( CameraCollection::pInstance )
    {
        *CameraCollection::pInstance = CameraCollection();
        CameraCollection::pInstance = nullptr;
    }
}


void CameraCollection::Reset()
{
    m_arrayPosition = 0;
    m_selectedCamera = 0;
    m_isTweening = false;
    m_tweenProgress = 0.0f;
    m_tweenSpeed = 0.0f;

    for ( int i = 0; i < TOTAL_CAMERA_COUNT; ++i )
    {
        m_cameraHashes[i] = 0;
        m_cameraArray[i].ZeroCamera();
    }

    m_primaryStore.ZeroCamera();
    m_tweenPath.ZeroCamera();
    m_tweenCamera.ZeroCamera();
    m_tweenStart.ZeroCamera();
}


void CameraCollection::SetLockedMode( const bool fIsLocked )
{
    m_cameraArray[m_selectedCamera].m_isLockedMode = fIsLocked;
    m_cameraArray[m_selectedCamera].m_doCalculateViewMagnitude = !fIsLocked;
}


void CameraCollection::AddCamera( const Vector3& vPosition,
                                  const Vector3& vView,
                                  const Vector3& vUp,
                                  uint32_t hash )
{
    if ( m_arrayPosition == TOTAL_CAMERA_COUNT )
    {
        throw std::runtime_error( "Camera array full!  (CameraCollection::AddCamera)" );
    }

    m_cameraHashes[m_arrayPosition] = hash;

    m_cameraArray[m_arrayPosition].SetAll( vPosition, vView, vUp );

    if ( !m_arrayPosition )
    {
        m_primaryStore = m_cameraArray[m_arrayPosition];
        m_cameraArray[m_arrayPosition].m_doPreserveViewMagnitude = true;
    }

    ++m_arrayPosition;
}


void CameraCollection::SetTweenSpeed( float fTweenSpeed )
{
    m_tweenSpeed = fTweenSpeed;
}


void CameraCollection::SetTweenPath( int fromIndex, int toIndex )
{
    // if the fromIndex is specifying to use the existing tween camera
    if ( fromIndex == -1 )
    {
        // use vector difference to determine the tweening vector
        // use the tween camera instead of the toIndex
        m_tweenPath = m_cameraArray[toIndex] - m_tweenCamera;

        // set the camera where the tween is beginning from
        m_tweenStart = m_tweenCamera;
    }
    else
    {
        // use vector difference to determine the tweening vector
        // use fromIndex and toIndex to determine this
        m_tweenPath = m_cameraArray[toIndex] - m_cameraArray[fromIndex];

        // set the camera where the tween is beginning from
        m_tweenStart = m_cameraArray[fromIndex];
    }
}


void CameraCollection::UpdateTweenPath()
{
    // update the destination (use vector difference)
    m_tweenPath = m_cameraArray[m_selectedCamera] - m_tweenStart;
}


void CameraCollection::SelectCamera( uint32_t hash,
                                     const bool fTween )
{
    // local to store requested camera index
    int selectionRequest = FindIndex( hash );

    // check to ensure we are not selecting the current camera
    if ( selectionRequest == m_selectedCamera )
    {
        return;
    }

    // it is not possible to tween if there is only one camera in the scene
    if ( fTween && m_arrayPosition == 1 )
    {
        throw std::runtime_error( "Cannot tween when only one camera exists.  (CameraCollection::SelectCamera)" );
    }

    // where should the tween camera be referenced FROM?
    if ( m_isTweening && fTween )
    {
        // if currently tweening, reference from the current tween camera m_position
        SetTweenPath( -1, selectionRequest );
    }
    else if ( fTween )
    {
        // if not currently tweening, reference from the current selected camera
        SetTweenPath( m_selectedCamera, selectionRequest );
    }

    // turn off view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].m_doPreserveViewMagnitude = false;

    // set the requested primary as the primary
    m_selectedCamera = selectionRequest;

    // turn on view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].m_doPreserveViewMagnitude = true;

    // specify if tweening
    m_isTweening = fTween;

    // reset the tweening counter whether tweening or not
    m_tweenProgress = 0;

    // store the initial primary m_position
    ResetRelativity();
}


uint32_t CameraCollection::GetSelectedCameraName()
{
    return m_cameraHashes[m_selectedCamera];
}


void CameraCollection::RotatePrimary( float xMove,
                                      float yMove )
{
    // make sure a camera exists to update
    if ( !m_arrayPosition )
    {
        throw std::runtime_error( "No camera defined.  (CameraCollection::RotatePrimary)" );
    }

    // rotate the primary camera
    m_cameraArray[m_selectedCamera].RotateCamera( xMove, yMove );
}


void CameraCollection::SetViewCoordinates( const Vector3& vView )
{
    m_cameraArray[m_selectedCamera].m_view = vView;
}


void CameraCollection::SetPrimaryPosition( const Vector3& vPos )
{
    m_cameraArray[m_selectedCamera].m_position = vPos;
}


void CameraCollection::MovePrimary( Camera::TravelDirection enumDir,
                                    float fQuantity )
{
    // make sure a camera exists to update
    if ( !m_arrayPosition )
    {
        throw std::runtime_error( "No camera defined.  (CameraCollection::MovePrimary)" );
    }

    // move the primary camera
    m_cameraArray[m_selectedCamera].MoveCamera( enumDir, fQuantity );
}


const Vector3& CameraCollection::GetPrimaryMovementBuffer()
{
    return m_cameraArray[m_selectedCamera].m_movementBuffer;
}


bool CameraCollection::IsPrimaryLocked()
{
    return m_cameraArray[m_selectedCamera].m_isLockedMode;
}


const Vector3& CameraCollection::GetCameraTranslation()
{
    return ( m_cameraArray[m_selectedCamera].m_position );
}


const Vector3& CameraCollection::GetCameraTranslation( uint32_t hash )
{
    return ( m_cameraArray[FindIndex( hash )].m_position );
}


void CameraCollection::CancelTween()
{
    m_isTweening = false;
}


void CameraCollection::RelativeUpdate( uint32_t hash,
                                       float yMin,
                                       float yMax )
{
    int cameraIndex = FindIndex( hash );

    // return if an attempt to relative update the current camera is being made
    if ( m_selectedCamera == cameraIndex )
    {
        return;
    }

    // use vector addition to apply the updates to the specified camera
    m_cameraArray[cameraIndex].ApplyDelta( GetCameraDelta() );

    // cap the y m_position of the camera to specified value if required
    if ( m_cameraArray[cameraIndex].m_position.y < yMin )
    {
        m_cameraArray[cameraIndex].m_position.y = yMin;
    }
    else if ( m_cameraArray[cameraIndex].m_position.y > yMax )
    {
        m_cameraArray[cameraIndex].m_position.y = yMax;
    }
}


Camera CameraCollection::GetCameraDelta()
{
    // get the camera representing the updates the non primaries have been missing out on
    // (vector difference will tell us this)
    return ( m_cameraArray[m_selectedCamera] - m_primaryStore );
}


void CameraCollection::ApplyPrimaryMovementBuffer()
{
    // apply the translation
    m_cameraArray[m_selectedCamera].ApplyMovementBuffer();
}


void CameraCollection::AmmendPrimaryY( float yCoordinate )
{
    float difference = yCoordinate -
                       m_cameraArray[m_selectedCamera].m_position.y;

    m_cameraArray[m_selectedCamera].m_position.y = yCoordinate;

    if ( !m_cameraArray[m_selectedCamera].m_isLockedMode )
    {
        m_cameraArray[m_selectedCamera].m_view.y += difference;
    }
}


void CameraCollection::ResetRelativity()
{
    // set the primary store to the current camera
    m_primaryStore = m_cameraArray[m_selectedCamera];
}


void CameraCollection::SetCamera()
{
    // make sure a camera exists
    if ( !m_arrayPosition )
    {
        throw std::runtime_error( "No camera defined.  (CameraCollection::SetGluLookAt)" );
    }

    // if we are not in tween mode
    if ( !m_isTweening )
    {
        // compute view matrix from selected camera
        SetViewMatrix( m_cameraArray[m_selectedCamera] );
    }
    else
    {
        // update the tweening counter
        m_tweenProgress += ( ( 1 - m_tweenProgress ) * m_tweenSpeed );

        // turn off tweening if the current tween is complete
        if ( m_tweenProgress > 0.99999f )
        {
            m_isTweening = false;
        }

        // update the tween path
        // (basically account for movement of the camera we are tweening to)
        UpdateTweenPath();

        // get the current tweening start m_position
        m_tweenCamera = m_tweenStart;

        // add the desired proportion of the tween vector to the starting camera
        m_tweenCamera += m_tweenPath * m_tweenProgress;

        // normalise the up vector now it has been played around with
        m_tweenCamera.m_upVector.Normalise();

        // avoid going through the m_terrain during tweens
        if ( m_terrain )
        {
            // check the m_height of the m_terrain
            float terrainHeight =
                m_terrain->GetTerrainHeightAt( m_tweenCamera.m_position.x,
                                               m_tweenCamera.m_position.z );

            // update the tween camera if necessary
            if ( m_tweenCamera.m_position.y < terrainHeight )
            {
                m_tweenCamera.m_position.y = terrainHeight + Cfg().minCameraHeight;
            }
        }
        else
        {
            throw std::runtime_error( "No m_terrain mesh set!  (CameraCollection::SetCamera)" );
        }

        // compute view matrix from tween camera
        SetViewMatrix( m_tweenCamera );
    }
}


void CameraCollection::SetViewMatrix( Camera& cCameraData )
{
    m_currentViewMatrix = Matrix4::LookAt(
        cCameraData.m_position,
        cCameraData.m_view,
        cCameraData.m_upVector );
}


float CameraCollection::DEBUG_GetViewMag()
{
    return ( Vector::Distance( m_cameraArray[m_selectedCamera].m_position,
                               m_cameraArray[m_selectedCamera].m_view ) );
}


float CameraCollection::DEBUG_GetViewMagTarget()
{
    return m_cameraArray[m_selectedCamera].m_viewMagnitude;
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

    throw std::runtime_error( "Camera does not exist.  (CameraCollection::FindIndex)" );
}


bool CameraCollection::IsCameraSelected( uint32_t hash )
{
    return ( FindIndex( hash ) == m_selectedCamera );
}


bool CameraCollection::IsCameraTweening()
{
    return m_isTweening;
}


const Vector3& CameraCollection::GetCameraView()
{
    return m_cameraArray[m_selectedCamera].m_view;
}


void CameraCollection::SetCameraXZBounds( const XZBounds bounds )
{
    for ( int count = 0; count < m_arrayPosition; ++count )
    {
        m_cameraArray[count].m_boundary = bounds;
    }
}


void CameraCollection::SetCameraXZBounds( uint32_t hash,
                                          const XZBounds bounds )
{
    int targetIndex = FindIndex( hash );
    m_cameraArray[targetIndex].m_boundary = bounds;
}


void CameraCollection::SetTerrain( Terrain* m_cTerrain )
{
    m_terrain = m_cTerrain;
}
