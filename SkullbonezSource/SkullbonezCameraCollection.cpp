/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ¦   .--. .--.   ¦
								   ¦ )/   ¦ ¦   \( ¦
								   ¦/ \__/   \__/ \¦
								   /      /^\      \
								   \__    '='    __/
								   	 ¦\         /¦
									 ¦\'"VUUUV"'/¦
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
CameraCollection::CameraCollection(int iCameraCount)
{
	// initialise all members requiring it
	this->maxCameraCount	= iCameraCount;
	this->cameraArray		= new Camera[iCameraCount];
	this->cameraNames		= new char*[iCameraCount];
	this->arrayPosition		= 0;
	this->selectedCamera	= 0;
	this->isTweening		= 0;
	this->tweenProgress		= 0;
	this->tweenSpeed		= 0;

	// initialise primary store member
	this->primaryStore.ZeroCamera();
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
CameraCollection::~CameraCollection(void)
{
	// delete the camera array
	if(this->cameraArray) delete [] cameraArray;

	// delete the strings
	for(int count=0; count<this->maxCameraCount; ++count)
		if(this->cameraNames[count]) delete [] cameraNames[count];

	// delete the string array
	if(this->cameraNames) delete [] this->cameraNames;
}


/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
CameraCollection* CameraCollection::Instance(int iCameraCount)
{
	// create an instance if one does not already exist
	if(!CameraCollection::pInstance) 
		CameraCollection::pInstance = new CameraCollection(iCameraCount);

	// retrurn the instance pointer
	return CameraCollection::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void CameraCollection::Destroy(void)
{
	// call the destructor
	if(CameraCollection::pInstance) delete CameraCollection::pInstance;

	// reset instance pointer to null
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
							  const char*	 cCameraName)
{
	// check for space in the camera array
	if(this->arrayPosition == this->maxCameraCount)
		throw "Camera array full!  (CameraCollection::AddCamera)";

	// allocate space for camera name
	this->cameraNames[this->arrayPosition] = new char[strlen(cCameraName) + 1];

	// deep copy the camera name
	strcpy(this->cameraNames[this->arrayPosition], cCameraName);

	// set the member data for the new camera
	this->cameraArray[this->arrayPosition].SetAll(vPosition, vView, vUp);

	// if this is the first camera added
	if(!this->arrayPosition) 
	{	
		//set the primary store to the index of the first camera
		this->primaryStore = this->cameraArray[this->arrayPosition];

		// enable view vector preservation
		this->cameraArray[this->arrayPosition].DoPreserveViewMagnitude = true;
	}

	// increment array position counter
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
void CameraCollection::SelectCamera(const char* sCameraName,
									const bool	 fTween)
{
	// local to store requested camera index
	int selectionRequest = this->FindIndex(sCameraName);

	// check to ensure we are not selecting the current camera
	if(selectionRequest == this->selectedCamera) return;

	// it is not possible to tween if there is only one camera in the scene
	if(fTween && this->arrayPosition == 1) 
		throw "Cannot tween when only one camera exists.  "
			  "(CameraCollection::SelectCamera)";

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



/* -- GET SELECTED CAMERA NAME ----------------------------------------------------*/
char* CameraCollection::GetSelectedCameraName(void)
{
	return this->cameraNames[this->selectedCamera];
}



/* -- ROTATE PRIMARY --------------------------------------------------------------*/
void CameraCollection::RotatePrimary(float xMove,
								     float yMove)
{
	// make sure a camera exists to update
	if(!this->arrayPosition) throw "No camera defined.  (CameraCollection::RotatePrimary)";

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
	if(!this->arrayPosition) throw "No camera defined.  (CameraCollection::MovePrimary)";

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
void CameraCollection::TranslateToView(const char* sCameraName)
{
	int targetIndex = this->FindIndex(sCameraName);

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
void CameraCollection::TranslateToPosition(const char* sCameraName)
{
	glTranslatef(this->cameraArray[this->FindIndex(sCameraName)].Position.x,
				 this->cameraArray[this->FindIndex(sCameraName)].Position.y,
				 this->cameraArray[this->FindIndex(sCameraName)].Position.z);
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
const Vector3&	CameraCollection::GetCameraTranslation(const char* sCameraName)
{
	return(this->cameraArray[this->FindIndex(sCameraName)].Position);
}



/* -- RELATIVE UPDATE -------------------------------------------------------------*/
void CameraCollection::RelativeUpdate(const char* sCameraName,
									  float yMin,
									  float yMax)
{
	// find the camera index
	int cameraIndex = this->FindIndex(sCameraName);

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
	if(!this->arrayPosition) throw "No camera defined.  (CameraCollection::SetGluLookAt)";

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
			throw "No terrain mesh set!  (CameraCollection::SetCamera)";
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
int CameraCollection::FindIndex(const char* sCameraName)
{
	// iterate through camera array
	for(int count=0; count<this->arrayPosition; ++count)
		// if the camera names are identical, return the index
		if(!strcmp(this->cameraNames[count], sCameraName)) return count;

	// throw an exception if the camera name does not exist
	throw "Camera does not exist.  (CameraCollection::FindIndex)";
}



/* -- IS CAMERA SELECTED ----------------------------------------------------------*/
bool CameraCollection::IsCameraSelected(const char* sCameraName)
{
	return (this->FindIndex(sCameraName) == this->selectedCamera);
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
void CameraCollection::SetCameraXZBounds(const char*	 sCameraName,
										 const XZBounds  bounds)
{
	int targetIndex = this->FindIndex(sCameraName);
	this->cameraArray[targetIndex].Boundary = bounds;
}



/* -- SET TERRAIN -----------------------------------------------------------------*/
void CameraCollection::SetTerrain(Terrain *cTerrain)
{
	this->terrain = cTerrain;
}