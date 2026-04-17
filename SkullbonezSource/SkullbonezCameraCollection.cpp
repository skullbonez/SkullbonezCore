/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   |   .--. .--.   |
                                   | )/   | |   \( |
                                   |/ \__/   \__/ \|
                                   /      /^\      \
                                   \__    '='    __/
                                     |\         /|
                                     |\'"VUUUV"'/|
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezCameraCollection.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Transformation;

/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
CameraCollection* CameraCollection::pInstance = 0;

/* -- CONSTRUCTOR -----------------------------------------------------------------*/
CameraCollection::CameraCollection( void )
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

/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
CameraCollection* CameraCollection::Instance( void )
{
    if ( !CameraCollection::pInstance )
    {
        static CameraCollection instance;
        CameraCollection::pInstance = &instance;
    }
    return CameraCollection::pInstance;
}

/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void CameraCollection::Destroy( void )
{
    if ( CameraCollection::pInstance )
    {
        *CameraCollection::pInstance = CameraCollection();
        CameraCollection::pInstance = 0;
    }
}

/* -- SET LOCKED MODE -------------------------------------------------------------*/
void CameraCollection::SetLockedMode( const bool fIsLocked )
{
    m_cameraArray[m_selectedCamera].IsLockedMode = fIsLocked;
    m_cameraArray[m_selectedCamera].DoCalculateViewMagnitude = !fIsLocked;
}

/* -- ADD CAMERA ------------------------------------------------------------------*/
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
        m_cameraArray[m_arrayPosition].DoPreserveViewMagnitude = true;
    }

    ++m_arrayPosition;
}

/* -- SET TWEEN SPEED -------------------------------------------------------------*/
void CameraCollection::SetTweenSpeed( float fTweenSpeed )
{
    m_tweenSpeed = fTweenSpeed;
}

/* -- SET TWEEN PATH --------------------------------------------------------------*/
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

/* -- UPDATE TWEEN PATH -----------------------------------------------------------*/
void CameraCollection::UpdateTweenPath( void )
{
    // update the destination (use vector difference)
    m_tweenPath = m_cameraArray[m_selectedCamera] - m_tweenStart;
}

/* -- SELECT CAMERA ---------------------------------------------------------------*/
void CameraCollection::SelectCamera( uint32_t hash,
                                     const bool fTween )
{
    // local to store requested camera index
    int selectionRequest = this->FindIndex( hash );

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
        this->SetTweenPath( -1, selectionRequest );
    }
    else if ( fTween )
    {
        // if not currently tweening, reference from the current selected camera
        this->SetTweenPath( m_selectedCamera, selectionRequest );
    }

    // turn off view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].DoPreserveViewMagnitude = false;

    // set the requested primary as the primary
    m_selectedCamera = selectionRequest;

    // turn on view magnitude preservation for the current camera
    m_cameraArray[m_selectedCamera].DoPreserveViewMagnitude = true;

    // specify if tweening
    m_isTweening = fTween;

    // reset the tweening counter whether tweening or not
    m_tweenProgress = 0;

    // store the initial primary m_position
    this->ResetRelativity();
}

/* -- GET SELECTED CAMERA HASH ----------------------------------------------------*/
uint32_t CameraCollection::GetSelectedCameraName( void )
{
    return m_cameraHashes[m_selectedCamera];
}

/* -- ROTATE PRIMARY --------------------------------------------------------------*/
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

/* -- SET VIEW COORDINATES --------------------------------------------------------*/
void CameraCollection::SetViewCoordinates( const Vector3& vView )
{
    m_cameraArray[m_selectedCamera].View = vView;
}

/* -- MOVE PRIMARY ----------------------------------------------------------------*/
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

/* -- GET PRIMARY MOVEMENT BUFFER -------------------------------------------------*/
const Vector3& CameraCollection::GetPrimaryMovementBuffer( void )
{
    return m_cameraArray[m_selectedCamera].MovementBuffer;
}

/* -- IS PRIMARY LOCKED -----------------------------------------------------------*/
bool CameraCollection::IsPrimaryLocked( void )
{
    return m_cameraArray[m_selectedCamera].IsLockedMode;
}

/* -- GET CAMERA TRANSLATION ------------------------------------------------------*/
const Vector3& CameraCollection::GetCameraTranslation( void )
{
    return ( m_cameraArray[m_selectedCamera].Position );
}

/* -- GET CAMERA TRANSLATION ------------------------------------------------------*/
const Vector3& CameraCollection::GetCameraTranslation( uint32_t hash )
{
    return ( m_cameraArray[this->FindIndex( hash )].Position );
}

/* -- RELATIVE UPDATE -------------------------------------------------------------*/
void CameraCollection::CancelTween( void )
{
    m_isTweening = false;
}

void CameraCollection::RelativeUpdate( uint32_t hash,
                                       float yMin,
                                       float yMax )
{
    int cameraIndex = this->FindIndex( hash );

    // return if an attempt to relative update the current camera is being made
    if ( m_selectedCamera == cameraIndex )
    {
        return;
    }

    // use vector addition to apply the updates to the specified camera
    m_cameraArray[cameraIndex].AddCamera( this->GetUpdateCamera() );

    // cap the y m_position of the camera to specified value if required
    if ( m_cameraArray[cameraIndex].Position.y < yMin )
    {
        m_cameraArray[cameraIndex].Position.y = yMin;
    }
    else if ( m_cameraArray[cameraIndex].Position.y > yMax )
    {
        m_cameraArray[cameraIndex].Position.y = yMax;
    }
}

/* -- GET UPDATE CAMERA -----------------------------------------------------------*/
Camera CameraCollection::GetUpdateCamera( void )
{
    // get the camera representing the updates the non primaries have been missing out on
    // (vector difference will tell us this)
    return ( m_cameraArray[m_selectedCamera] - m_primaryStore );
}

/* -- APPLY PRIMARY MOVEMENT BUFFER -----------------------------------------------*/
void CameraCollection::ApplyPrimaryMovementBuffer( void )
{
    // apply the translation
    m_cameraArray[m_selectedCamera].ApplyMovementBuffer();
}

/* -- AMMEND PRIMARY Y ------------------------------------------------------------*/
void CameraCollection::AmmendPrimaryY( float yCoordinate )
{
    float difference = yCoordinate -
                       m_cameraArray[m_selectedCamera].Position.y;

    m_cameraArray[m_selectedCamera].Position.y = yCoordinate;

    if ( !m_cameraArray[m_selectedCamera].IsLockedMode )
    {
        m_cameraArray[m_selectedCamera].View.y += difference;
    }
}

/* -- RESET RELATIVITY ------------------------------------------------------------*/
void CameraCollection::ResetRelativity( void )
{
    // set the primary store to the current camera
    m_primaryStore = m_cameraArray[m_selectedCamera];
}

/* -- SET CAMERA ------------------------------------------------------------------*/
void CameraCollection::SetCamera( void )
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
        this->SetViewMatrix( m_cameraArray[m_selectedCamera] );
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
        this->UpdateTweenPath();

        // get the current tweening start m_position
        m_tweenCamera = m_tweenStart;

        // add the desired proportion of the tween vector to the starting camera
        m_tweenCamera += m_tweenPath * m_tweenProgress;

        // normalise the up vector now it has been played around with
        m_tweenCamera.UpVector.Normalise();

        // avoid going through the m_terrain during tweens
        if ( m_terrain )
        {
            // check the m_height of the m_terrain
            float terrainHeight =
                m_terrain->GetTerrainHeightAt( m_tweenCamera.Position.x,
                                               m_tweenCamera.Position.z );

            // update the tween camera if necessary
            if ( m_tweenCamera.Position.y < terrainHeight )
            {
                m_tweenCamera.Position.y = terrainHeight + Cfg().minCameraHeight;
            }
        }
        else
        {
            throw std::runtime_error( "No m_terrain mesh set!  (CameraCollection::SetCamera)" );
        }

        // compute view matrix from tween camera
        this->SetViewMatrix( m_tweenCamera );
    }
}

/* -- SET VIEW MATRIX -------------------------------------------------------------*/
void CameraCollection::SetViewMatrix( Camera& cCameraData )
{
    m_currentViewMatrix = Matrix4::LookAt(
        cCameraData.Position,
        cCameraData.View,
        cCameraData.UpVector );
}

/* -- DEBUG: GET VIEW MAG ---------------------------------------------------------*/
float CameraCollection::DEBUG_GetViewMag( void )
{
    return ( Vector::Distance( m_cameraArray[m_selectedCamera].Position,
                               m_cameraArray[m_selectedCamera].View ) );
}

/* -- DEBUG: GET VIEW MAG TARGET --------------------------------------------------*/
float CameraCollection::DEBUG_GetViewMagTarget( void )
{
    return m_cameraArray[m_selectedCamera].ViewMagnitude;
}

/* -- FIND INDEX ------------------------------------------------------------------*/
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

/* -- IS CAMERA SELECTED ----------------------------------------------------------*/
bool CameraCollection::IsCameraSelected( uint32_t hash )
{
    return ( this->FindIndex( hash ) == m_selectedCamera );
}

/* -- IS CAMERA TWEENING ----------------------------------------------------------*/
bool CameraCollection::IsCameraTweening( void )
{
    return m_isTweening;
}

/* -- GET CAMERA VIEW -------------------------------------------------------------*/
const Vector3& CameraCollection::GetCameraView( void )
{
    return m_cameraArray[m_selectedCamera].View;
}

/* -- SET CAMERA XZ BOUNDS --------------------------------------------------------*/
void CameraCollection::SetCameraXZBounds( const XZBounds bounds )
{
    for ( int count = 0; count < m_arrayPosition; ++count )
    {
        m_cameraArray[count].Boundary = bounds;
    }
}

/* -- SET CAMERA XZ BOUNDS --------------------------------------------------------*/
void CameraCollection::SetCameraXZBounds( uint32_t hash,
                                          const XZBounds bounds )
{
    int targetIndex = this->FindIndex( hash );
    m_cameraArray[targetIndex].Boundary = bounds;
}

/* -- SET TERRAIN -----------------------------------------------------------------*/
void CameraCollection::SetTerrain( Terrain* m_cTerrain )
{
    m_terrain = m_cTerrain;
}
