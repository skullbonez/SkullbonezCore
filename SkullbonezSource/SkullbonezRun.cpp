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
#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include <time.h>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
SkullbonezRun::SkullbonezRun(void)
{
	// init members
	this->cCameras				= 0;
	this->cTerrain				= 0;
	this->cTextures				= 0;
	this->cSkyBox				= 0;
	this->cWorldEnvironment 	= 0;
	this->selectedCamera		= 0;
	this->cGameModelCollection	= 0;
	this->timeSinceLastRender	= 0.0f;
	this->renderTime			= 0.0f;
	this->cameraTime			= 0.0f;
	this->r_renderTime			= 0.0f;
	this->physicsTime			= 0.0f;
	this->r_physicsTime			= 0.0f;
	this->r_fpsTime				= 0.0f;

	// seed the random number generator
	srand((unsigned)time(NULL));
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkullbonezRun::~SkullbonezRun(void)
{
	// kill statics
	Text2d::DeleteFont();

	// kill singletons
	this->cTextures->Destroy();
	this->cCameras->Destroy();
	this->cSkyBox->Destroy();

	// kill objects
	if(this->cTerrain)				delete this->cTerrain;
	if(this->cWorldEnvironment)		delete this->cWorldEnvironment;
	if(this->cGameModelCollection)  delete this->cGameModelCollection;
}



/* -- INITIALISE ------------------------------------------------------------------*/
void SkullbonezRun::Initialise(void)
{
	// Init window
	this->cWindow = SkullbonezWindow::Instance();

	// Set loading text
	this->cWindow->SetTitleText("::SKULLBONEZ CORE:: -- LOADING!!!");

	// Init textures
	this->cTextures = TextureCollection::Instance(TOTAL_TEXTURE_COUNT);

	// Init OpenGL
	this->SetInitialOpenGlState();

	// Init terrain 
	// path to height map | map size pixels | step size | times to wrap texture
	this->cTerrain = new Terrain(TERRAIN_RAW_PATH, 256, 8, 15);


	// Init SkyBox (xMin, xMax, yMin, yMax, zMin, zMax)
	this->cSkyBox = SkyBox::Instance(-250, 300, -300, 300, -250, 300);

	// Init world environment
	this->cWorldEnvironment = new WorldEnvironment(FLUID_HEIGHT,
												   FLUID_DENSITY,
												   GAS_DENSITY,
												   GRAVITATIONAL_FORCE);

	// Init camera
	this->SetUpCameras();

	// Init game models
	this->SetUpGameModels();

	// Init font (HDC, font)
	Text2d::BuildFont(this->cWindow->sDevice, "Verdana");

	// Restore initial window text
	this->cWindow->SetTitleText("::SKULLBONEZ CORE::");

	// begin timing
	this->cUpdateTimer.StartTimer();
	this->cCameraTimer.StartTimer();
	this->cSimulationTimer.StartTimer();
}



/* -- SET UP GAME MODELS ----------------------------------------------------------*/
void SkullbonezRun::SetUpGameModels(void)
{
	// randomly generate a number of models for the scene
	this->modelCount = 10 + (rand() % 150);

	// create the game model collection
	this->cGameModelCollection = new GameModelCollection(this->modelCount);

	for(int x=0; x<this->modelCount; ++x)
	{
		// randomly generate the model properties
		float xPos					 = 400.0f + (float)(rand() % 400);
		float yPos					 = 100 + (float)(rand() % 250);
		float zPos					 = 400.0f + (float)(rand() % 400);
		float mass					 = 100.0f + (float)(rand() % 1000);
		float moment				 = 5.0f + (float)(rand() % 15);
		float coefficientRestitution = 0.5f + ((float)(rand() % 5) / 10.0f);
		float radius				 = 1.0f + (float)(rand() % 10);
		float xForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 10000) : 1.0f - (float)(rand() % 10000);
		float yForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 10000) : 1.0f - (float)(rand() % 10000);
		float zForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 10000) : 1.0f - (float)(rand() % 10000);
		float xForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;
		float yForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;
		float zForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;

		// game model
		GameModel* gameModel = new GameModel(this->cWorldEnvironment, Vector3(xPos, yPos, zPos), Vector3(moment, moment, moment), mass);
		gameModel->SetCoefficientRestitution(coefficientRestitution);
		gameModel->SetTerrain(this->cTerrain);
		gameModel->AddBoundingSphere(radius);
		gameModel->SetImpulseForce(Vector3(xForce, yForce, zForce), Vector3(xForcePos, yForcePos, zForcePos));

		// add the game model
		this->cGameModelCollection->AddGameModel(gameModel);
	}
}



/* -- RUN -------------------------------------------------------------------------*/
bool SkullbonezRun::Run(void)
{
	/*
		Runs the application after initialisation - main message loop.

		Note:   To make it so the application only invalidates on events 
				(mouse movements etc) place WaitMessage() after input, 
				logic and render.
	*/

	// Variable to hold messages from message queue
	MSG	  msg;

	for(;;)		// Do until application is closed
	{
		if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))	// Check for msg
		{
			if(msg.message == WM_QUIT) break;			// Quit if requested
			TranslateMessage(&msg);						// Interpret msg
			DispatchMessage(&msg);						// Execute msg
		}
		else
		{
			// find out how many seconds passed during last frame
			double secondsPerFrame = this->cFrameTimer.GetElapsedTime();

			// if the last frame took an exceptionally long time, 
			// cap the time step to avoid making the simulation inaccurate
			// and to avoid numerical instability
			if(secondsPerFrame > 0.05) secondsPerFrame = 0.05;

			// Begin timer
			this->cFrameTimer.StartTimer();

			// Input
			// this->TakeInput();						

			// Logic
			this->UpdateLogic((float)secondsPerFrame);

			// Begin render timer
			this->cWorkTimer.StartTimer();

			// Render
			this->Render();							

			// Render overlay text
			this->DrawWindowText(secondsPerFrame);

			// Stop render timer
			this->cWorkTimer.StopTimer();

			// Store render time
			this->renderTime = (float)this->cWorkTimer.GetElapsedTime();

			// Swap back buffer
			SwapBuffers(this->cWindow->sDevice);

			// Stop frame timer
			this->cFrameTimer.StopTimer();

			// End the simulation when required
			if(this->cSimulationTimer.GetTimeSinceLastStart() > 20.0)
			{
				return true;
			}
		}
	}

	return false;
}



/* -- TAKE INPUT ------------------------------------------------------------------*/
void SkullbonezRun::TakeInput(void)
{
/*
	// Press C to toggle locked mode
	this->cCameras->SetLockedMode(Input::IsKeyToggled('C'));

	// Press M to free/enable the mouse in the application
	if(Input::IsKeyToggled('M'))
	{
		// remove cursor since the mouse is being used
		SetCursor(0);

		// get mouse coords and centre the mouse
		POINT currentCoords = Input::GetMouseCoordinates();
		Input::CentreMouseCoordinates();
		POINT centreCoords = Input::GetMouseCoordinates();

		// get the difference in the centre mouse pos and the moved mouse pos
		this->sInputState.xMove = currentCoords.x - centreCoords.x;
		this->sInputState.yMove = currentCoords.y - centreCoords.y;
	}

	// WASD camera movement
	this->sInputState.fUp		= Input::IsKeyDown('W');
	this->sInputState.fLeft		= Input::IsKeyDown('A');
	this->sInputState.fDown		= Input::IsKeyDown('S');
	this->sInputState.fRight	= Input::IsKeyDown('D');

	// Set if synchronised camera key just pressed (auxiliary 1)
	this->sInputState.fAux1		= Input::IsKeyDown('P');

	// Set synchronised cameras (auxiliary 2)
	this->sInputState.fAux2		= Input::IsKeyToggled('P');
*/
}



/* -- UPDATE LOGIC ----------------------------------------------------------------*/
void SkullbonezRun::UpdateLogic(float fSecondsPerFrame)
{
	// start the timer
	this->cWorkTimer.StartTimer();

	// update the game models
	this->cGameModelCollection->RunPhysics(fSecondsPerFrame);

	// stop the timer
	this->cWorkTimer.StopTimer();

	// store physics time
	this->physicsTime = (float)this->cWorkTimer.GetElapsedTime();

	// move the camera based on input 
	// (arguments are calculating time based movement quantities)
	this->MoveCamera(fSecondsPerFrame * KEY_MOVEMENT_CONST, 	
					 fSecondsPerFrame * MOUSE_MOVEMENT_CONST);

	// update camera tweening speed
	this->cCameras->SetTweenSpeed(TWEEN_RATE * fSecondsPerFrame);
}



/* -- RENDER ----------------------------------------------------------------------*/
void SkullbonezRun::Render(void)
{
	// Clear screen pixel and depth into buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// reset world matrix
	glLoadIdentity();

	// renders camera views etc
	this->SetViewingOrientation();

	// set the camera into its position
	this->cCameras->SetCamera();

	// now camera rotation has been done, draw OpenGL primitives
	this->DrawPrimitives();
}



/* -- DRAW PRIMITIVES ---------------------------------------------------------------------*/
void SkullbonezRun::DrawPrimitives(void)
{
	// set light position -------------------------
	float lightPosition[] = { 200.0f, 400.0f, 1200.0f, 1.0f };  // x, y, z, w

	// set position properties for light 0
	glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

	// render skybox ------------------------------
	glPushMatrix();
		this->cCameras->TranslateToPosition(SKYBOX_RENDER_HEIGHT);
		glScalef(SKY_BOX_SCALE, SKY_BOX_SCALE, SKY_BOX_SCALE);
		this->cSkyBox->Render();
	glPopMatrix();

	// render game models -----------------------------
	this->cTextures->SelectTexture("BoundingSphere");
	this->cGameModelCollection->RenderModels();

	// render the deep fluid ----------------------
	glPushMatrix();
		this->cTextures->SelectTexture("Water");

		glPushMatrix();
			glTranslatef(0.0f, -1.0f, 6300.0f);
			this->cWorldEnvironment->RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(0.0f, -1.0f, -5000.0f);
			this->cWorldEnvironment->RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(6300.0f, -1.0f, 0.0f);
			this->cWorldEnvironment->RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(-5000.0f, -1.0f, 0.0f);
			this->cWorldEnvironment->RenderFluid();
		glPopMatrix();
	glPopMatrix();

	// render terrain ------------------------------
	glPushMatrix();
		this->cTextures->SelectTexture("Ground");
		this->cTerrain->Render();
	glPopMatrix();
	
	// render the fluid ---------------------------
	glPushMatrix();
		this->cTextures->SelectTexture("Water");
		this->cWorldEnvironment->RenderFluid();
	glPopMatrix();
}



/* -- SET UP CAMERAS ----------------------------------------------------------------------*/
void SkullbonezRun::SetUpCameras(void)
{
	this->cCameras = CameraCollection::Instance(TOTAL_CAMERA_COUNT);

	this->cCameras->AddCamera(Vector3(321.0f, 110.0f, 557.0f), // Position
							  Vector3(581.0f, 40.0f,  633.0f), // View
						      Vector3(0.0f,   1.0f,   0.0f),   // Up
							  "GameModel1");				   // Camera name

	this->cCameras->AddCamera(Vector3(730.0f, 100.0f, 380.0f), // Position	
							  Vector3(709.0f, 92.0f,  482.0f), // View	
						      Vector3(0.0f,   1.0f,   0.0f),   // Up
							  "GameModel2");				   // Camera name

	this->cCameras->AddCamera(Vector3(900.0f, 110.0f,  900.0f),		// Position
							  Vector3(313.0f, 31.0f,   282.0f),		// View	
						      Vector3(0.0f,	  1.0f,	   0.0f),		// Up	
							  "Free");								// Camera name

	// set the camera boundaries
	this->cCameras->SetCameraXZBounds(this->cTerrain->GetXZBounds());

	// set the terrain
	this->cCameras->SetTerrain(this->cTerrain);

	// lock the cameras
	this->cCameras->SetLockedMode(true);
}



/* -- SET INITIAL OPEN GL STATE -----------------------------------------------------------*/
void SkullbonezRun::SetInitialOpenGlState(void)
{
	// set up initial open gl state
	SkullbonezHelper::StateSetup();

	// load textures
	this->cTextures->CreateJpegTexture(TERRAIN_TEXTURE_PATH, "Ground");
	this->cTextures->CreateJpegTexture(BOUNDING_SPHERE_PATH, "BoundingSphere");
	this->cTextures->CreateJpegTexture(WATER_PATH, "Water");
}



/* -- DRAW WINDOW TEXT --------------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::DrawWindowText(const double dSecondsPerFrame)
{
	// update timers
	this->cUpdateTimer.StopTimer();
	this->timeSinceLastRender += (float)this->cUpdateTimer.GetElapsedTime();
	this->cUpdateTimer.StartTimer();

	// if half a second has passed
	if(this->timeSinceLastRender > 0.5f)
	{
		if(dSecondsPerFrame) 
		{
			// update the display information
			this->r_fpsTime = 1.0f / (float)dSecondsPerFrame;
			this->r_physicsTime = this->physicsTime;
			this->r_renderTime = this->renderTime;
		}

		// reset time since last render
		this->timeSinceLastRender = 0.0f;
	}

	// white
	glColor3ub(255, 255, 255);

	// TOP
	Text2d::Render2dText(-0.53f, 0.39f, 0.02f, "SKULLBONEZ CORE | Simon Eschbach 2005");
	Text2d::Render2dText(0.39f,  0.39f, 0.02f, "Model Count: %i", this->modelCount);

	// BOTTOM
	Text2d::Render2dText(-0.53f,  -0.40f, 0.0175f, "FPS: %.1f",						this->r_fpsTime);
	Text2d::Render2dText(-0.455f, -0.40f, 0.0175f, " | Physics Time: %.5f seconds", this->r_physicsTime);
	Text2d::Render2dText(-0.2f,   -0.40f, 0.0175f, " | Render Time: %.5f seconds",  this->r_renderTime);
	Text2d::Render2dText( 0.05f,  -0.40f, 0.0175f, " | Contact:  s.eschbach@gmail.com   | www.simoneschbach.com");
}



/* -- SET VIEWING ORIENTATION -------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::SetViewingOrientation(void)
{
	// set viewing orientation
/*
	if(Input::IsKeyDown('1')) this->selectedCamera = 0;
	if(Input::IsKeyDown('2')) this->selectedCamera = 1;
	if(Input::IsKeyDown('3')) this->selectedCamera = 2;
*/

	// maintain the camera timer
	this->cCameraTimer.StopTimer();
	this->cameraTime += (float)this->cCameraTimer.GetElapsedTime();
	this->cCameraTimer.StartTimer();

	// change the viewing camera automatically
	if(this->cameraTime > 5.0f)
	{
		++this->selectedCamera;
		if(this->selectedCamera == 3) this->selectedCamera = 0;
		this->cameraTime = 0.0f;
	}

	// select camera based on input
	switch(this->selectedCamera)
	{
	case 0:
		this->cCameras->SelectCamera("GameModel1", true);
		break;
	case 1:
		this->cCameras->SelectCamera("GameModel2", true);
		break;
	case 2:
		this->cCameras->SelectCamera("Free", true);
		break;
	}

	// set the view position of the selected camera based on the game model position
	if(this->cCameras->IsCameraSelected("GameModel1")) this->cCameras->SetViewCoordinates(this->cGameModelCollection->GetModelPosition(0));
	if(this->cCameras->IsCameraSelected("GameModel2")) this->cCameras->SetViewCoordinates(this->cGameModelCollection->GetModelPosition(1));

/*
	// reset relativity when a new request for synchronisation comes in
	if(this->sInputState.fAux1) this->cCameras->ResetRelativity();

	// sync cameras if in sync mode
	if(this->sInputState.fAux2) 
	{
		// perform the relative update
		this->RelativeUpdateCamera("GameModel1");
		this->RelativeUpdateCamera("GameModel2");
		this->RelativeUpdateCamera("Free");

		// reset the relative variable as we have already performed the action on desired cameras
		this->cCameras->ResetRelativity();
	}
*/
}



/* -- RELATIVE UPDATE CAMERA --------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::RelativeUpdateCamera(const char* cameraName)
{
	if(!this->cCameras->IsCameraSelected(cameraName))
	{
		// get the translated camera position
		Vector3 translatedCameraPosition = this->cCameras->GetCameraTranslation(cameraName);

		// perform calculations to suggest a new y coordinate (add MIN_CAMERA_HEIGHT so clipping will not occur)
		float minY = this->cTerrain->GetTerrainHeightAt(translatedCameraPosition.x, translatedCameraPosition.z, true) + MIN_CAMERA_HEIGHT;

		// perform relative update, use overload to ammend y coordinate if necessary
		this->cCameras->RelativeUpdate(cameraName, minY, MAX_CAMERA_HEIGHT);
	}
}



/* -- MOVE CAMERA -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::MoveCamera(float keyMovementQty, float mouseMovementQty)
{
	keyMovementQty; mouseMovementQty;
/*
	// orient camera based on mouse input if there is mouse movement
	if(!(!this->sInputState.xMove && !this->sInputState.yMove))
	{
		// orient the camera
		this->cCameras->RotatePrimary(this->sInputState.xMove * mouseMovementQty, this->sInputState.yMove * mouseMovementQty);
	}

	// ingnore these keys unless free camera is selected
	if(this->cCameras->IsCameraSelected("Free"))
	{
		// move camera based on key input
		if(this->sInputState.fUp)    this->cCameras->MovePrimary(Camera::TravelDirection::Forward,  keyMovementQty);
		if(this->sInputState.fLeft)  this->cCameras->MovePrimary(Camera::TravelDirection::Left,		keyMovementQty);
		if(this->sInputState.fDown)  this->cCameras->MovePrimary(Camera::TravelDirection::Backward, keyMovementQty);
		if(this->sInputState.fRight) this->cCameras->MovePrimary(Camera::TravelDirection::Right,	keyMovementQty);
	}

	// apply the allowed proportion of the camera translation
	this->cCameras->ApplyPrimaryMovementBuffer();
*/
	// get the translated camera position
	Vector3 translatedCameraPosition = this->cCameras->GetCameraTranslation();

	// perform calculations to suggest a new y coordinate (add MIN_CAMERA_HEIGHT to avoid clipping)
	float minY = this->cTerrain->GetTerrainHeightAt(translatedCameraPosition.x, translatedCameraPosition.z, true) + MIN_CAMERA_HEIGHT;

	// amend y coodinate if necessary
	if(minY > translatedCameraPosition.y)					this->cCameras->AmmendPrimaryY(minY);
	else if(translatedCameraPosition.y > MAX_CAMERA_HEIGHT) this->cCameras->AmmendPrimaryY(MAX_CAMERA_HEIGHT);
}