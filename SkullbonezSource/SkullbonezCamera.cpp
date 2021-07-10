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
#include "SkullbonezCamera.h"
#include "SkullbonezRotationMatrix.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Camera::Camera(void) {}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
Camera::~Camera(void) {}



/* -- SET ALL ---------------------------------------------------------------------*/
void Camera::SetAll(const Vector3& vPosition,	// Set position
					const Vector3& vView,		// Set view
					const Vector3& vUpVector)	// Set up vector
{
	this->Position			= vPosition;
	this->View				= vView;
	this->UpVector			= vUpVector;
	
	// set initial view vector magnitude
	this->ViewMagnitude = Vector::Distance(this->Position, this->View);

	// init movement buffer
	this->MovementBuffer.Zero();
	
	// normalise this in case it was not supplied as a unit vector
	// (do not normalise the zero vector though)
	if(this->UpVector != Vector::ZERO_VECTOR) this->UpVector.Normalise();

	// init is locked mode to false by default
	this->IsLockedMode					= false;

	// init vars to do with view vector magnitude preservation
	this->IsFinishedTranslationRecursed = false;
	this->DoPreserveViewMagnitude		= false;

	// view magnitude must be initially calculated
	this->DoCalculateViewMagnitude		= true;

	// init the boundary to something massive until the user changes it
	this->Boundary.xMin = -99999.9f;
	this->Boundary.xMax =  99999.9f;
	this->Boundary.zMin = -99999.9f;
	this->Boundary.zMax =  99999.9f;
}



/* -- MOVE CAMERA -----------------------------------------------------------------*/
void Camera::MoveCamera(const TravelDirection enumDir,
						float	fQuantity)
{
	// declare local variable to store movement results
	Vector3 movementResults = Vector::ZERO_VECTOR;

	switch(enumDir)
	{
	case this->Forward:

		if(this->IsLockedMode)
		{	
			// in locked mode we only want to be able to translate the camera 
			// within a certain distance to the view point, so here we test to
			// ensure this rule is not violated
			if(Vector::Distance(this->Position, this->View) < MIN_CAMERA_VIEW_MAG)
				return;

			// if there has been a change in view magnitude, we need to recalculate
			this->DoCalculateViewMagnitude = true;
		}

		// movement result is along the view vector, positive direction
		movementResults = this->GetViewVectorNormalised() * fQuantity;
		break;

	case this->Left:
		// left movement does not exist in locked mode
		if(!this->IsLockedMode)
			// movement result is along the right vector, negative direction
			movementResults = this->GetRightVector() * -fQuantity;

		break;

	case this->Right:
		// right movement does not exist in locked mode
		if(!this->IsLockedMode)
			// movement result is along the right vector, positive direction
			movementResults = this->GetRightVector() * fQuantity;

		break;

	case this->Backward:

		if(this->IsLockedMode) 
		{
			// in locked mode we only want to be able to translate the camera
			// within a certain distance from the view point, so here we test to
			// ensure this rule is not violated
			if(Vector::Distance(this->Position, this->View) > MAX_CAMERA_VIEW_MAG) 
				return;
			
			// if there has been a change in view magnitude, we need to recalculate
			this->DoCalculateViewMagnitude = true;
		}

		// movement result is along the view vector, negative direction
		movementResults = this->GetViewVectorNormalised() * -fQuantity;
	}

	this->MovementBuffer += movementResults;
}



/* -- APPLY MOVEMENT BUFFER -------------------------------------------------------*/
void Camera::ApplyMovementBuffer(void)
{
	// store old position
	Vector3 oldPosition = this->Position;

	// apply the movement vector to the camera position
	this->PrepareTranslation();
	this->Position += this->MovementBuffer;
	this->FinishTranslation();

	// get actual translation (movementBuffer may have been restricted)
	Vector3 actualTranslation = this->Position - oldPosition;

	// update the view vector with the cameras translation, however,
	// if we are in locked mode we do not move the view vector at all
	if(!this->IsLockedMode) this->View += actualTranslation;


	// reset the movement buffer
	this->MovementBuffer.Zero();
}



/* -- ROTATE CAMERA ---------------------------------------------------------------*/
void Camera::RotateCamera(float xMove, float yMove)
{
	// caps the y rotation quantity to avoid view-up collisions
	float yMoveCapped = this->UpVectorViewVectorRotationCap(yMove);

	// the following code will move the view vector - this is not allowed
	// in locked mode
	if(!this->IsLockedMode)
	{
		/*
			NOTE: view member is set to original position plus our rotation result
			vector - this translates our new view vector into a point relative
			to the camera position

			ALSO: note that the normalised view vector is rotated - this is because
			in free camera mode, the view vector is always unit vector length from
			the cameras translation
		*/

		// the mouses xMove will always represent a pivot on the up vector 
		// (repsective to the camera translation)
		this->View = this->Position + 
			Transformation::RotatePointAboutArbitrary(xMove,
													  this->UpVector,
													  this->GetViewVectorRaw());

		// the mouses yMove will always represent a pivot on the right vector 
		// (repsective to the camera translation)
		this->View = this->Position + 
			Transformation::RotatePointAboutArbitrary(yMoveCapped,
													  this->GetRightVector(),
													  this->GetViewVectorRaw());
	}
	else
	{
		/*
			NOTE: position member is set to original view plus our rotation result
			vector - this translates our new position vector into a point relative
			to the camera view

			ALSO: the raw negated view vector is used here as a rotation subject 
			because in locked camera mode, the distance the camera is from the view 
			point is variable.  The view vector is negated to represent view point 
			to camera translation opposed to camera to view point.
		*/

		// local to add up translation proposal
		Vector3 proposedTranslation;

		// the mouses xMove will always represent a pivot on the up vector 
		// (repsective to the view point)
		proposedTranslation = this->View + 
				    Transformation::RotatePointAboutArbitrary(xMove,
															  this->UpVector,
															  -this->GetViewVectorRaw());

		// store proposed translation into the movement buffer
		this->MovementBuffer += proposedTranslation - this->Position;

		// the mouses yMove will always represent a pivot on the right vector 
		// (repsective to the view point)
		proposedTranslation = this->View + 
				    Transformation::RotatePointAboutArbitrary(yMoveCapped,
															  this->GetRightVector(),
															  -this->GetViewVectorRaw());

		// store proposed translation into the movement buffer
		this->MovementBuffer += proposedTranslation - this->Position;
	}
}



/* -- PREPARE TRANSLATION ---------------------------------------------------------*/
void Camera::PrepareTranslation(void)
{
	// store the current X and Z position before translation
	// (we want to revert the translation if bounds are exceeded)
	this->XZStore.x = this->Position.x;
	this->XZStore.z = this->Position.z;
}



/* -- FINISH TRANSLATION ----------------------------------------------------------*/
void Camera::FinishTranslation(void)
{
	bool isOnBoundX = false;
	bool isOnBoundZ = false;

	// reposition X on a bound violation
	if(this->Position.x < this->Boundary.xMin + MIN_CAMERA_HEIGHT)
		this->Position.x   = this->Boundary.xMin + MIN_CAMERA_HEIGHT;
	else if(this->Position.x > this->Boundary.xMax - MIN_CAMERA_HEIGHT)
		this->Position.x   = this->Boundary.xMax - MIN_CAMERA_HEIGHT;

	// set if X is on a boundary
	isOnBoundX = ((this->Position.x == 
				   this->Boundary.xMin + MIN_CAMERA_HEIGHT) ||
				  (this->Position.x == 
				   this->Boundary.xMax - MIN_CAMERA_HEIGHT));

	// reposition Z on a bound violation
	if(this->Position.z < this->Boundary.zMin + MIN_CAMERA_HEIGHT)
		this->Position.z   = this->Boundary.zMin + MIN_CAMERA_HEIGHT;
	else if(this->Position.z > this->Boundary.zMax - MIN_CAMERA_HEIGHT)
		this->Position.z   = this->Boundary.zMax - MIN_CAMERA_HEIGHT;

	// set if Z is on a boundary
	isOnBoundZ = ((this->Position.z == 
				   this->Boundary.zMin + MIN_CAMERA_HEIGHT) ||
				  (this->Position.z == 
				   this->Boundary.zMax - MIN_CAMERA_HEIGHT));

	// if we have recursed once already
	if(this->IsFinishedTranslationRecursed)
	{
		// reset the flag
		this->IsFinishedTranslationRecursed = false;

		// and exit the function
		return;
	}

	// if the flag has been set to recalculate the view magnitude
	if(this->DoCalculateViewMagnitude)
	{
		// recalculate it
		this->ViewMagnitude = Vector::Distance(this->Position, this->View);

		// reset the flag to false
		this->DoCalculateViewMagnitude = false;
	}
	else
	{
		// test to see if we need to recover lost view magnitude
		this->RecoverViewMagnitude(isOnBoundX, isOnBoundZ);
	}
}



/* -- RECOVER VIEW MAGNITUDE -------------------------------------------------------*/
void Camera::RecoverViewMagnitude(const bool isOnBoundX, const bool isOnBoundZ)
{
	// only recover view magnitude if the camera has been set to do so
	if(!this->DoPreserveViewMagnitude) return;

	// only recover view magnitude when in locked mode
	if(!this->IsLockedMode) return;

	// if we are not out of bounds on either the X or Z axis
	if(!isOnBoundX || !isOnBoundZ)
	{
		// calculate the current view vector magnitude
		float viewMagTmp = Vector::Distance(this->Position, this->View);

		// if the current view magnitude is under quota
		if(viewMagTmp < this->ViewMagnitude)
		{
			// store the cameras position
			Vector3 positionStore = this->Position;

			// extend the current view magnitude to its quota
			this->Position = this->View + 
							(-this->GetViewVectorNormalised() * this->ViewMagnitude);

			// restore the y component (we will add to this later)
			this->Position.y = positionStore.y;

			// keep track of which component gets modified
			bool isModifiedComponentX;

			// if neither X or Z are on their boundary
			if(!isOnBoundX && !isOnBoundZ)
			{
				// determine component distances from boundaries
				float dxMin = positionStore.x - this->Boundary.xMin + MIN_CAMERA_HEIGHT;
				float dxMax = this->Boundary.xMax - MIN_CAMERA_HEIGHT - positionStore.x;
				float dzMin = positionStore.z - this->Boundary.zMin + MIN_CAMERA_HEIGHT;
				float dzMax = this->Boundary.zMax - MIN_CAMERA_HEIGHT - positionStore.z;

				// determine closest boundary per component
				float dx	= (dxMin < dxMax) ? dxMin : dxMax;
				float dz	= (dzMin < dzMax) ? dzMin : dzMax;

				// restore the component who is closest to their closest boundary
				if(dx > dz)
				{
					// restore X component
					this->Position.x	 = positionStore.x;

					// X component is NOT being modified
					isModifiedComponentX = false;
				}
				else
				{
					// restore Z component
					this->Position.z = positionStore.z;

					// X component IS being modified
					isModifiedComponentX = true;
				}
			}
			else if(isOnBoundZ)
			{
				// restore X if already maxed
				this->Position.x	 = positionStore.x;

				// X component is NOT being modified
				isModifiedComponentX = false;
			}
			else
			{
				// restore Z if already maxed
				this->Position.z	 = positionStore.z;

				// X component IS being modified
				isModifiedComponentX = true;
			}

			// specify we have recursed this function
			this->IsFinishedTranslationRecursed = true;

			// indirectly recurse this function to cap the vector we have 
			// just applied to this->Position
			this->FinishTranslation();

			// check to see which component ended up getting modified
			if(isModifiedComponentX)
			{
				// if X was the modified component
				// work out how much it was modified in total
				float dx = positionStore.x - this->Position.x;

				// get the absolute value representing the difference of X
				dx		 = abs(dx);

				// add this to the current Y translation component
				this->Position.y += dx;
			}
			else
			{
				// if Z was the modified component
				// work out how much it was modified in total
				float dz = positionStore.z - this->Position.z;

				// get the absolute value representing the difference of Z
				dz		 = abs(dz);

				// add this to the current Y translation component
				this->Position.y += dz;
			}
		}
		// if the current view magnitude is over quota
		else if(viewMagTmp > this->ViewMagnitude)
		{
			// cap the magnitude to what it is set to
			this->View = this->Position + 
						 (this->GetViewVectorNormalised() * this->ViewMagnitude);
		}
	}
}



/* -- ADD CAMERA ------------------------------------------------------------------*/
void Camera::AddCamera(const Camera& updateCamera)
{
	this->PrepareTranslation();

	// this statement looks ugly but is simple:
	// 1. Cast the 'this' pointer to type 'Camera' pointer,
	// 2. Throw parentheses around it, and dereference it
	// 3. Use the += Camera operator overload to add the passed in Camera
	*((Camera*)this) += updateCamera;

	this->FinishTranslation();
}



/* -- UP VECTOR VIEW VECTOR ROTATION CAP ------------------------------------------*/
float Camera::UpVectorViewVectorRotationCap(float requestRadians)
{
	// get the negated view vector (translation minus view)
	Vector3 vNegatedView = -this->GetViewVectorNormalised();

	// get the amount of radians the view is to the up vector
	float currentUpAngle = acosf(vNegatedView * this->UpVector);

	// get the amount of radians the view is to the 'down vector'
	float currentDownAngle = acosf(vNegatedView * -this->UpVector);

	// pre-detect up-vector view-vector collision, return a capped rotation angle
	if(currentUpAngle - requestRadians < COLLISION_THRESHOLD) 
		return currentUpAngle - COLLISION_THRESHOLD;

	// pre-detect down-vector view-vector collision, return a capped rotation angle
	// NOTE:  request radians will be negative, and if required should be returned
	// as a negative value
	if(currentDownAngle + requestRadians < COLLISION_THRESHOLD) 
		return -(currentDownAngle - COLLISION_THRESHOLD);
	
	// no collisions have been detected, return the requested rotation amount
	return requestRadians;
}



/* -- GET RIGHT VECTOR ------------------------------------------------------------*/
Vector3 Camera::GetRightVector(void)
{
	// Get the right vector (cross of view and up vectors)
	Vector3 vRight = Vector::CrossProduct(this->GetViewVectorNormalised(), 
										  this->UpVector);

	// Normalise
	vRight.Normalise();

	// Return
	return vRight;
}



/* -- GET VIEW VECTOR RAW ---------------------------------------------------------*/
Vector3 Camera::GetViewVectorRaw(void)
{
	return this->View - this->Position;
}



/* -- GET VIEW VECTOR NORMALISED --------------------------------------------------*/
Vector3	Camera::GetViewVectorNormalised(void)
{
	/*
		Recall that vector subtraction works the following way:

				a = (-1, 2)
				b = ( 1, 2)

			    c = b-a (for -> direction)
				c = a-b (for <- direction)
			    -----------------------
				\		   |+y	      /
				 \		   |	  	 /
				  \		   |	    /
				   \	   |	   /
				    \	   |	  /
				   a \	   |	 / b
					  \    |    /
					   \   |   /
					    \  |  /
						 \ | /
						  \|/(0, 0)
		 -x	---------------|------------- +x
						   |-y

		The value of c is given by b-a = (1-(-1), 2-2)
		Therefore c = (2, 0)

		As the member this->Position corresponds to a point in 3d space
		where the camera is sitting, and the member this->View represents
		a point (NOT a vector) in 3d space where the camera is looking
		directly at, in order to find the vector representing the direction
		in which we are looking (so, the vector representing the path FROM
		the point this->Position TO the point this->View) we treat this->View
		and this->Position as vectors beginning at the origin.  We now have a
		3d case similar to the 2d image above - two vectors starting at the same
		position representing displacement to other points in space.  In order to
		find the vector that joints the two endpoints, we use vector subtraction
		(the triangle rule).  
		
		Remember, vector subtraction is NOT COMMUTITIVE, it is important that we
		have subtracted this->View - this->Position to find the current view, as
		this->Position - this->View would be looking in exactly the opposite
		direction to where we want to be looking (see image).
	*/

	// Get the view vector
	Vector3 vView = this->View - this->Position;

	// Normalise
	vView.Normalise();

	// Return
	return vView;
}



/* -- ZERO CAMERA -----------------------------------------------------------------*/
void Camera::ZeroCamera(void)
{
	this->Position.Zero();
	this->View.Zero();
	this->UpVector.Zero();
}



/* -- OPERATOR '=' ----------------------------------------------------------------*/
Camera& Camera::operator =(const Camera& target)
{
	this->Position		= target.Position;
	this->View			= target.View;
	this->UpVector		= target.UpVector;
	this->IsLockedMode	= target.IsLockedMode;

	return *this;
}



/* -- OPERATOR '+=' ---------------------------------------------------------------*/
Camera& Camera::operator +=(const Camera& target)
{
	this->Position += target.Position;
	this->View	   += target.View;
	this->UpVector += target.UpVector;

	return *this;
}



/* -- OPERATOR '-' ----------------------------------------------------------------*/
Camera Camera::operator -(const Camera& target)
{
	Camera result;
	result.SetAll(this->Position - target.Position,
				  this->View	 - target.View,
				  this->UpVector - target.UpVector);

	return result;
}



/* -- OPERATOR '*' ----------------------------------------------------------------*/
Camera Camera::operator *(float f)
{
	Camera result;
	result.SetAll(this->Position * f,
				  this->View	 * f,
				  this->UpVector * f);

	return result;
}