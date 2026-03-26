/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   �   .--. .--.   �
								   � )/   � �   \( �
								   �/ \__/   \__/ \�
								   /      /^\      \
								   \__    '='    __/
								   	 �\         /�
									 �\'"VUUUV"'/�
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezCameraCollection.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;



/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
CameraCollection* CameraCollection::pInstance = 0;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
CameraCollection::CameraCollection(void)
{
	this->arrayPosition		= 0;
	this->selectedCamera	= 0;
	this->isTweening		= 0;
	this->tweenProgress		= 0;
	this->tweenSpeed		= 0;
	this->terrain			= 0;

	for(int count=0; count<TOTAL_CAMERA_COUNT; ++count)
		this->cameraHashes[count] = 0;

	this->primaryStore.ZeroCamera();
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
CameraCollection* CameraCollection::Instance(void)
{
	if(!CameraCollection::pInstance)
	{
		static CameraCollection instance;
		CameraCollection::pInstance = &instance;
	}
	return CameraCollection::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void CameraCollection::Destroy(void)
{
	CameraCollection::pInstance = 0;
}



/* -- SET LOCKED MODE -------------------------------------------------------------*/
void CameraCollection::SetLockedMode(const bool fIsLocked)
{
	this->cameraArray[this->selectedCamera].IsLockedMode = fIsLocked;
	this->cameraArray[this->selectedCamera].DoCalculateViewMagnitude = !fIsLocked;
}



/* -- ADD CAMERA ------------------------------------------------------------------*/
void CameraCollection::AddCamera(const Vector3& vPosition,
							  const Vector3& vView,
							  const Vector3& vUp,
							  uint32_t       hash)
{
	if(this->arrayPosition == TOTAL_CAMERA_COUNT)
		throw std::runtime_error("Camera array full!  (CameraCollection::AddCamera)");

	this->cameraHashes[this->arrayPosition] = hash;

	this->cameraArray[this->arrayPosition].SetAll(vPosition, vView, vUp);

	if(!this->arrayPosition) 
	{	
		this->primaryStore = this->cameraArray[this->arrayPosition];
		this->cameraArray[this->arrayPosition].DoPreserveViewMagnitude = true;
	}

	++this->arrayPosition;
}



/* -- SET TWEEN SPEED -------------------------------------------------------------*/
void CameraCollection::SetTweenSpeed(float fTweenSpeed)
{
	this->tweenSpeed = fTweenSpeed;
}



/* -- SET TWEEN PATH --------------------------------------------------------------*/
void CameraCollection::SetTweenPath(int fromIndex, int toIndex)
{
	// if the fromIndex is specifying to use the existing tween camera
	if(fromIndex == -1)
	{
		// use vector difference to determine the tweening vector
		// use the tween camera instead of the toIndex
		this->tweenPath = this->cameraArray[toIndex] - this->tweenCamera;

		// set the camera where the tween is beginning from
		this->tweenStart = this->tweenCamera;
	}
	else
	{
		// use vector difference to determine the tweening vector
		// use fromIndex and toIndex to determine this
		this->tweenPath = this->cameraArray[toIndex] - this->cameraArray[fromIndex];

		// set the camera where the tween is beginning from
		this->tweenStart = this->cameraArray[fromIndex];
	}
}



/* -- UPDATE TWEEN PATH -----------------------------------------------------------*/
void CameraCollection::UpdateTweenPath(void)
{
	// update the destination (use vector difference)
	this->tweenPath = this->cameraArray[this->selectedCamera] - this->tweenStart;
}



/* -- SELECT CAMERA ---------------------------------------------------------------*/
void CameraCollection::SelectCamera(uint32_t hash,
									const bool fTween)
{
	// local to store requested camera index
	int selectionRequest = this->FindIndex(hash);

	// check to ensure we are not selecting the current camera
	if(selectionRequest == this->selectedCamera) return;

	// it is not possible to tween if there is only one camera in the scene
	if(fTween && this->arrayPosition == 1) 
		throw std::runtime_error("Cannot tween when only one camera exists.  (CameraCollection::SelectCamera)");

	// where should the tween camera be referenced FROM?
	if(this->isTweening && fTween)
	{
		// if currently tweening, reference from the current tween camera position
		this->SetTweenPath(-1, selectionRequest);
	}
	else if(fTween)
	{
		// if not currently tweening, reference from the current selected camera
		this->SetTweenPath(this->selectedCamera, selectionRequest);
	}

	// turn off view magnitude preservation for the current camera
	this->cameraArray[this->selectedCamera].DoPreserveViewMagnitude = false;

	// set the requested primary as the primary
	this->selectedCamera = selectionRequest;

	// turn on view magnitude preservation for the current camera
	this->cameraArray[this->selectedCamera].DoPreserveViewMagnitude = true;

	// specify if tweening
	this->isTweening = fTween;

	// reset the tweening counter whether tweening or not
	this->tweenProgress = 0;

	// store the initial primary position
	this->ResetRelativity();
}



/* -- GET SELECTED CAMERA HASH ----------------------------------------------------*/
uint32_t CameraCollection::GetSelectedCameraName(void)
{
	return this->cameraHashes[this->selectedCamera];
}



/* -- ROTATE PRIMARY --------------------------------------------------------------*/
void CameraCollection::RotatePrimary(float xMove,
								     float yMove)
{
	// make sure a camera exists to update
	if(!this->arrayPosition) throw std::runtime_error("No camera defined.  (CameraCollection::RotatePrimary)");

	// rotate the primary camera
	this->cameraArray[this->selectedCamera].RotateCamera(xMove, yMove);
}



/* -- SET VIEW COORDINATES --------------------------------------------------------*/
void CameraCollection::SetViewCoordinates(const Vector3& vView)
{
	this->cameraArray[this->selectedCamera].View = vView;
}



/* -- MOVE PRIMARY ----------------------------------------------------------------*/
void CameraCollection::MovePrimary(Camera::TravelDirection enumDir,
								   float fQuantity)
{
	// make sure a camera exists to update
	if(!this->arrayPosition) throw std::runtime_error("No camera defined.  (CameraCollection::MovePrimary)");

	// move the primary camera
	this->cameraArray[this->selectedCamera].MoveCamera(enumDir, fQuantity);
}



/* -- GET PRIMARY MOVEMENT BUFFER -------------------------------------------------*/
const Vector3& CameraCollection::GetPrimaryMovementBuffer(void)
{
	return this->cameraArray[this->selectedCamera].MovementBuffer;
}



/* -- IS PRIMARY LOCKED -----------------------------------------------------------*/
bool CameraCollection::IsPrimaryLocked(void)
{
	return this->cameraArray[this->selectedCamera].IsLockedMode;
}



/* -- TRANSLATE TO VIEW -----------------------------------------------------------*/
void CameraCollection::TranslateToView(void)
{
	glTranslatef(this->cameraArray[this->selectedCamera].View.x,
				 this->cameraArray[this->selectedCamera].View.y,
				 this->cameraArray[this->selectedCamera].View.z);
}



/* -- TRANSLATE TO VIEW -----------------------------------------------------------*/
void CameraCollection::TranslateToView(uint32_t hash)
{
	int targetIndex = this->FindIndex(hash);

	glTranslatef(this->cameraArray[targetIndex].View.x,
				 this->cameraArray[targetIndex].View.y,
				 this->cameraArray[targetIndex].View.z);
}



/* -- TRANSLATE TO POSITION -------------------------------------------------------*/
void CameraCollection::TranslateToPosition(void)
{
	// if we are in the middle of a tween, translate to the tween position
	if(this->isTweening)
	{
		glTranslatef(this->tweenCamera.Position.x,
					 this->tweenCamera.Position.y,
					 this->tweenCamera.Position.z);
	}
	// else translate to the primary camera position
	else
	{
		glTranslatef(this->cameraArray[this->selectedCamera].Position.x,
					 this->cameraArray[this->selectedCamera].Position.y,
					 this->cameraArray[this->selectedCamera].Position.z);
	}
}



/* -- TRANSLATE TO POSITION -------------------------------------------------------*/
void CameraCollection::TranslateToPosition(uint32_t hash)
{
	glTranslatef(this->cameraArray[this->FindIndex(hash)].Position.x,
				 this->cameraArray[this->FindIndex(hash)].Position.y,
				 this->cameraArray[this->FindIndex(hash)].Position.z);
}



/* -- TRANSLATE TO POSITION -------------------------------------------------------*/
void CameraCollection::TranslateToPosition(float yValue)
{
	// if we are in the middle of a tween, translate to the tween position
	if(this->isTweening)
	{
		glTranslatef(this->tweenCamera.Position.x,
					 yValue,
					 this->tweenCamera.Position.z);
	}
	// else translate to the primary camera position
	else
	{
		glTranslatef(this->cameraArray[this->selectedCamera].Position.x,
					 yValue,
					 this->cameraArray[this->selectedCamera].Position.z);
	}
}



/* -- GET CAMERA TRANSLATION ------------------------------------------------------*/
const Vector3& CameraCollection::GetCameraTranslation(void)
{
	return(this->cameraArray[this->selectedCamera].Position);
}



/* -- GET CAMERA TRANSLATION ------------------------------------------------------*/
const Vector3&	CameraCollection::GetCameraTranslation(uint32_t hash)
{
	return(this->cameraArray[this->FindIndex(hash)].Position);
}



/* -- RELATIVE UPDATE -------------------------------------------------------------*/
void CameraCollection::RelativeUpdate(uint32_t hash,
									  float yMin,
									  float yMax)
{
	int cameraIndex = this->FindIndex(hash);

	// return if an attempt to relative update the current camera is being made
	if(this->selectedCamera == cameraIndex) return;

	// use vector addition to apply the updates to the specified camera
	this->cameraArray[cameraIndex].AddCamera(this->GetUpdateCamera());

	// cap the y position of the camera to specified value if required
	if(this->cameraArray[cameraIndex].Position.y < yMin)
		this->cameraArray[cameraIndex].Position.y = yMin;
	else if(this->cameraArray[cameraIndex].Position.y > yMax)
		this->cameraArray[cameraIndex].Position.y = yMax;
}



/* -- GET UPDATE CAMERA -----------------------------------------------------------*/
Camera CameraCollection::GetUpdateCamera(void)
{
	// get the camera representing the updates the non primaries have been missing out on
	// (vector difference will tell us this)
	return (this->cameraArray[this->selectedCamera] - this->primaryStore);
}



/* -- APPLY PRIMARY MOVEMENT BUFFER -----------------------------------------------*/
void CameraCollection::ApplyPrimaryMovementBuffer(void)
{
	// apply the translation
	this->cameraArray[this->selectedCamera].ApplyMovementBuffer();
}



/* -- AMMEND PRIMARY Y ------------------------------------------------------------*/
void CameraCollection::AmmendPrimaryY(float yCoordinate)
{
	float difference = yCoordinate - 
					   this->cameraArray[this->selectedCamera].Position.y;

	this->cameraArray[this->selectedCamera].Position.y = yCoordinate;

	if(!this->cameraArray[this->selectedCamera].IsLockedMode)
		this->cameraArray[this->selectedCamera].View.y += difference;
}



/* -- RESET RELATIVITY ------------------------------------------------------------*/
void CameraCollection::ResetRelativity(void)
{
	// set the primary store to the current camera
	this->primaryStore = this->cameraArray[this->selectedCamera];
}



/* -- SET CAMERA ------------------------------------------------------------------*/
void CameraCollection::SetCamera(void)
{
	// make sure a camera exists
	if(!this->arrayPosition) throw std::runtime_error("No camera defined.  (CameraCollection::SetGluLookAt)");

	// if we are not in tween mode
	if(!this->isTweening)
	{
		// set open gl api with the selected camera settings
		this->SetGluLookAt(this->cameraArray[this->selectedCamera]);
	}
	else
	{
		// update the tweening counter
		this->tweenProgress += ((1 - this->tweenProgress) * this->tweenSpeed);

		// turn off tweening if the current tween is complete
		if(this->tweenProgress > 0.99999f) this->isTweening = false;

		// update the tween path
		// (basically account for movement of the camera we are tweening to)
		this->UpdateTweenPath();

		// get the current tweening start position
		this->tweenCamera = this->tweenStart;

		// add the desired proportion of the tween vector to the starting camera
		this->tweenCamera += this->tweenPath * tweenProgress;

		// normalise the up vector now it has been played around with
		this->tweenCamera.UpVector.Normalise();

		// avoid going through the terrain during tweens
		if(this->terrain)
		{
			// check the height of the terrain
			float terrainHeight = 
				this->terrain->GetTerrainHeightAt(this->tweenCamera.Position.x,
												  this->tweenCamera.Position.z);

			// update the tween camera if necessary
			if(this->tweenCamera.Position.y < terrainHeight)
				this->tweenCamera.Position.y = terrainHeight + MIN_CAMERA_HEIGHT;
		}
		else
		{
			throw std::runtime_error("No terrain mesh set!  (CameraCollection::SetCamera)");
		}

		// finally set the open gl api to the new camera settings
		this->SetGluLookAt(this->tweenCamera);
	}
}



/* -- SET GLU LOOK AT -------------------------------------------------------------*/
void CameraCollection::SetGluLookAt(Camera& cCameraData)
{
	// specify camera position, view, and up vector
	gluLookAt(cCameraData.Position.x,
			  cCameraData.Position.y,
			  cCameraData.Position.z,
			  cCameraData.View.x,
			  cCameraData.View.y,
			  cCameraData.View.z,
			  cCameraData.UpVector.x,
			  cCameraData.UpVector.y,
			  cCameraData.UpVector.z);
}



/* -- DEBUG: PLOT VIEW VECTORS ----------------------------------------------------*/
void CameraCollection::DEBUG_PlotViewVectors(void)
{
	for(int count=0; count<this->arrayPosition; ++count)
	{
		glBegin(GL_LINES);

			glVertex3f(this->cameraArray[count].Position.x,
					   this->cameraArray[count].Position.y,
					   this->cameraArray[count].Position.z);

			glVertex3f(this->cameraArray[count].View.x,
					   this->cameraArray[count].View.y,
					   this->cameraArray[count].View.z);

		glEnd();
	}
}



/* -- DEBUG: GET VIEW MAG ---------------------------------------------------------*/
float CameraCollection::DEBUG_GetViewMag(void)
{
	return (Vector::Distance(this->cameraArray[this->selectedCamera].Position,
						     this->cameraArray[this->selectedCamera].View));
}



/* -- DEBUG: GET VIEW MAG TARGET --------------------------------------------------*/
float CameraCollection::DEBUG_GetViewMagTarget(void)
{
	return this->cameraArray[this->selectedCamera].ViewMagnitude;
}



/* -- FIND INDEX ------------------------------------------------------------------*/
int CameraCollection::FindIndex(uint32_t hash)
{
	for(int count=0; count<this->arrayPosition; ++count)
		if(this->cameraHashes[count] == hash) return count;

	throw std::runtime_error("Camera does not exist.  (CameraCollection::FindIndex)");
}



/* -- IS CAMERA SELECTED ----------------------------------------------------------*/
bool CameraCollection::IsCameraSelected(uint32_t hash)
{
	return (this->FindIndex(hash) == this->selectedCamera);
}



/* -- IS CAMERA TWEENING ----------------------------------------------------------*/
bool CameraCollection::IsCameraTweening(void)
{
	return this->isTweening;
}



/* -- GET CAMERA VIEW -------------------------------------------------------------*/
const Vector3&	CameraCollection::GetCameraView(void)
{
	return this->cameraArray[this->selectedCamera].View;
}



/* -- SET CAMERA XZ BOUNDS --------------------------------------------------------*/
void CameraCollection::SetCameraXZBounds(const XZBounds bounds)
{
	for(int count=0; count<this->arrayPosition; ++count)
		this->cameraArray[count].Boundary = bounds;
}



/* -- SET CAMERA XZ BOUNDS --------------------------------------------------------*/
void CameraCollection::SetCameraXZBounds(uint32_t        hash,
										 const XZBounds  bounds)
{
	int targetIndex = this->FindIndex(hash);
	this->cameraArray[targetIndex].Boundary = bounds;
}



/* -- SET TERRAIN -----------------------------------------------------------------*/
void CameraCollection::SetTerrain(Terrain *cTerrain)
{
	this->terrain = cTerrain;
}
