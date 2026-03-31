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
#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include <time.h>
#include <cstring>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
SkullbonezRun::SkullbonezRun(const char* pScenePath)
{
	// init scene mode
	this->isSceneMode			= (pScenePath != nullptr);
	this->isScenePhysics		= true;
	this->isSceneText			= true;
	this->isScreenshotSaved		= false;
	this->targetFrameCount		= -1;
	this->currentFrame			= 0;
	this->screenshotFrame		= -1;
	this->screenshotMs			= -1;
	this->screenshotPath[0]		= '\0';

	if (pScenePath)
		strcpy_s(this->scenePath, sizeof(this->scenePath), pScenePath);
	else
		this->scenePath[0] = '\0';

	// init members
	this->cCameras				= 0;
	this->cTextures				= 0;
	this->cSkyBox				= 0;
	this->selectedCamera		= 0;
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
	Text2d::DeleteFont();

	this->cTextures->Destroy();
	this->cCameras->Destroy();
	this->cSkyBox->Destroy();
}



/* -- INITIALISE ------------------------------------------------------------------*/
void SkullbonezRun::Initialise(void)
{
	// Init window
	this->cWindow = SkullbonezWindow::Instance();

	// Set loading text
	this->cWindow->SetTitleText("::SKULLBONEZ CORE:: -- LOADING!!!");

	// Init textures
	this->cTextures = TextureCollection::Instance();

	// Init OpenGL
	this->SetInitialOpenGlState();

	// Init terrain 
	// path to height map | map size pixels | step size | times to wrap texture
	this->cTerrain = std::make_unique<Terrain>(TERRAIN_RAW_PATH, 256, 8, 15);


	// Init SkyBox (xMin, xMax, yMin, yMax, zMin, zMax)
	this->cSkyBox = SkyBox::Instance(-250, 300, -300, 300, -250, 300);

	// Init world environment
	this->cWorldEnvironment = WorldEnvironment(FLUID_HEIGHT,
											   FLUID_DENSITY,
											   GAS_DENSITY,
											   GRAVITATIONAL_FORCE);

	// Branch on scene mode vs legacy mode
	if (this->isSceneMode)
	{
		TestScene scene = TestScene::LoadFromFile(this->scenePath);
		this->isScenePhysics   = scene.IsPhysicsEnabled();
		this->isSceneText      = scene.IsTextEnabled();
		this->targetFrameCount = scene.GetFrameCount();
		this->screenshotFrame  = scene.GetScreenshotFrame();
		this->screenshotMs     = scene.GetScreenshotMs();

		if (scene.GetScreenshotPath()[0] != '\0')
			strcpy_s(this->screenshotPath, sizeof(this->screenshotPath), scene.GetScreenshotPath());

		// Override RNG seed for deterministic scenes
		if (scene.GetSeed() > 0)
			srand(scene.GetSeed());

		this->SetUpCamerasFromScene(scene);

		// Use legacy random ball generation or explicit ball list
		if (scene.GetLegacyBallCount() > 0)
			this->SetUpGameModels(scene.GetLegacyBallCount());
		else
			this->SetUpGameModelsFromScene(scene);
	}
	else
	{
		this->SetUpCameras();
		this->SetUpGameModels();
	}

	// Init font (HDC, font)
	Text2d::BuildFont(this->cWindow->sDevice, "Verdana");

	// Restore initial window text
	if (this->isSceneMode)
		this->cWindow->SetTitleText("::SKULLBONEZ CORE:: [SCENE MODE]");
	else
		this->cWindow->SetTitleText("::SKULLBONEZ CORE::");

	// begin timing
	this->cUpdateTimer.StartTimer();
	this->cCameraTimer.StartTimer();
	this->cSimulationTimer.StartTimer();
}



/* -- SET UP GAME MODELS ----------------------------------------------------------*/
void SkullbonezRun::SetUpGameModels(int count)
{
	this->modelCount = count;

	for(int x=0; x<this->modelCount; ++x)
	{
		// randomly generate the model properties
		float xPos					 = 400.0f + (float)(rand() % 400);
		float yPos					 = 100 + (float)(rand() % 250);
		float zPos					 = 400.0f + (float)(rand() % 400);	
		float mass					 = 50.0f + (float)(rand() % 50);
		float moment				 = 5.0f + (float)(rand() % 15);
		float coefficientRestitution = 0.5f + ((float)(rand() % 5) / 10.0f);
		float radius				 = (1.0f + (float)(rand() % 10)) * 0.5f; // max of 5.5
		float xForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 1000) : 1.0f - (float)(rand() % 1000);
		float yForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 1000) : 1.0f - (float)(rand() % 1000);
		float zForce				 = (rand() % 10 > 4) ? 1.0f + (float)(rand() % 1000) : 1.0f - (float)(rand() % 1000);
		float xForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;
		float yForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;
		float zForcePos				 = (rand() % 10 > 4) ? 1.0f : -1.0f;

		// stack-allocate game model and configure it
		GameModel gameModel(&this->cWorldEnvironment, Vector3(xPos, yPos, zPos), Vector3(moment, moment, moment), mass);
		gameModel.SetCoefficientRestitution(coefficientRestitution);
		gameModel.SetTerrain(this->cTerrain.get());
		gameModel.AddBoundingSphere(radius);
		gameModel.SetImpulseForce(Vector3(xForce, yForce, zForce), Vector3(xForcePos, yForcePos, zForcePos));

		// move the game model into the collection
		this->cGameModelCollection.AddGameModel(std::move(gameModel));
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

			// Logic (skip physics in scene mode when disabled)
			if (!this->isSceneMode || this->isScenePhysics)
				this->UpdateLogic((float)secondsPerFrame);

			// Begin render timer
			this->cWorkTimer.StartTimer();

			// Render
			this->Render();							

			// Render overlay text
			if (!this->isSceneMode || this->isSceneText)
				this->DrawWindowText(secondsPerFrame);

			// Scene mode: check screenshot triggers (read back buffer before swap)
			if (this->isSceneMode && this->screenshotPath[0] != '\0' && !this->isScreenshotSaved)
			{
				bool shouldCapture = false;

				if (this->screenshotFrame > 0 && (this->currentFrame + 1) >= this->screenshotFrame)
					shouldCapture = true;

				if (this->screenshotMs > 0 && this->cSimulationTimer.GetTimeSinceLastStart() * 1000.0 >= this->screenshotMs)
					shouldCapture = true;

				if (shouldCapture)
				{
					this->SaveScreenshot(this->screenshotPath);
					this->isScreenshotSaved = true;
					PostQuitMessage(0);
				}
			}

			// Stop render timer
			this->cWorkTimer.StopTimer();

			// Store render time
			this->renderTime = (float)this->cWorkTimer.GetElapsedTime();

			// Swap back buffer
			SwapBuffers(this->cWindow->sDevice);

			// Stop frame timer
			this->cFrameTimer.StopTimer();

			// Scene mode: count frames, hold after target reached (skip if screenshot auto-exit pending)
			if (this->isSceneMode && this->targetFrameCount > 0 && !this->isScreenshotSaved)
			{
				++this->currentFrame;
				if (this->currentFrame >= this->targetFrameCount)
				{
					// hold — keep rendering but don't advance logic
					for (;;)
					{
						MSG holdMsg;
						if (PeekMessage(&holdMsg, NULL, 0, 0, PM_REMOVE))
						{
							if (holdMsg.message == WM_QUIT) return false;
							TranslateMessage(&holdMsg);
							DispatchMessage(&holdMsg);
						}
						else
						{
							Sleep(16);
						}
					}
				}
			}

			// End the simulation when required (legacy mode only)
			if (!this->isSceneMode && this->cSimulationTimer.GetTimeSinceLastStart() > 20.0)
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
	this->cGameModelCollection.RunPhysics(fSecondsPerFrame);

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
	this->cTextures->SelectTexture(TEXTURE_BOUNDING_SPHERE);
	this->cGameModelCollection.RenderModels();

	// render the deep fluid ----------------------
	glPushMatrix();
		this->cTextures->SelectTexture(TEXTURE_WATER);

		glPushMatrix();
			glTranslatef(0.0f, -1.0f, 6300.0f);
			this->cWorldEnvironment.RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(0.0f, -1.0f, -5000.0f);
			this->cWorldEnvironment.RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(6300.0f, -1.0f, 0.0f);
			this->cWorldEnvironment.RenderFluid();
		glPopMatrix();

		glPushMatrix();
			glTranslatef(-5000.0f, -1.0f, 0.0f);
			this->cWorldEnvironment.RenderFluid();
		glPopMatrix();
	glPopMatrix();

	// render terrain ------------------------------
	{
		// Read current matrices from GL state (set by gluLookAt/gluPerspective)
		float viewMat[16], projMat[16];
		glGetFloatv(GL_MODELVIEW_MATRIX, viewMat);
		glGetFloatv(GL_PROJECTION_MATRIX, projMat);
		Matrix4 view(viewMat);
		Matrix4 proj(projMat);

		this->cTextures->SelectTexture(TEXTURE_GROUND);
		this->cTerrain->Render(view, proj, lightPosition);
	}

	// render ground shadows on top of terrain
	this->cGameModelCollection.RenderShadows(this->cTerrain.get());
	
	// render the fluid ---------------------------
	glPushMatrix();
		this->cTextures->SelectTexture(TEXTURE_WATER);
		this->cWorldEnvironment.RenderFluid();
	glPopMatrix();
}



/* -- SET UP CAMERAS ----------------------------------------------------------------------*/
void SkullbonezRun::SetUpCameras(void)
{
	this->cCameras = CameraCollection::Instance();

	this->cCameras->AddCamera(Vector3(321.0f, 110.0f, 557.0f), // Position
							  Vector3(581.0f, 40.0f,  633.0f), // View
						      Vector3(0.0f,   1.0f,   0.0f),   // Up
							  CAMERA_GAME_MODEL_1);

	this->cCameras->AddCamera(Vector3(730.0f, 100.0f, 380.0f), // Position	
							  Vector3(709.0f, 92.0f,  482.0f), // View	
						      Vector3(0.0f,   1.0f,   0.0f),   // Up
							  CAMERA_GAME_MODEL_2);

	this->cCameras->AddCamera(Vector3(900.0f, 110.0f,  900.0f),		// Position
							  Vector3(313.0f, 31.0f,   282.0f),		// View	
						      Vector3(0.0f,	  1.0f,	   0.0f),		// Up	
							  CAMERA_FREE);

	// set the camera boundaries
	this->cCameras->SetCameraXZBounds(this->cTerrain->GetXZBounds());

	// set the terrain
	this->cCameras->SetTerrain(this->cTerrain.get());

	// lock the cameras
	this->cCameras->SetLockedMode(true);
}



/* -- SET INITIAL OPEN GL STATE -----------------------------------------------------------*/
void SkullbonezRun::SetInitialOpenGlState(void)
{
	SkullbonezHelper::ResetGLResources();
	SkullbonezHelper::StateSetup();

	// load textures
	this->cTextures->CreateJpegTexture(TERRAIN_TEXTURE_PATH, TEXTURE_GROUND);
	this->cTextures->CreateJpegTexture(BOUNDING_SPHERE_PATH, TEXTURE_BOUNDING_SPHERE);
	this->cTextures->CreateJpegTexture(WATER_PATH,           TEXTURE_WATER);
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
	// In scene mode, use the first camera without cycling
	if (this->isSceneMode) return;

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
		this->cCameras->SelectCamera(CAMERA_GAME_MODEL_1, true);
		break;
	case 1:
		this->cCameras->SelectCamera(CAMERA_GAME_MODEL_2, true);
		break;
	case 2:
		this->cCameras->SelectCamera(CAMERA_FREE, true);
		break;
	}

	// set the view position of the selected camera based on the game model position
	if(this->cCameras->IsCameraSelected(CAMERA_GAME_MODEL_1)) this->cCameras->SetViewCoordinates(this->cGameModelCollection.GetModelPosition(0));
	if(this->cCameras->IsCameraSelected(CAMERA_GAME_MODEL_2)) this->cCameras->SetViewCoordinates(this->cGameModelCollection.GetModelPosition(1));

/*
	// reset relativity when a new request for synchronisation comes in
	if(this->sInputState.fAux1) this->cCameras->ResetRelativity();

	// sync cameras if in sync mode
	if(this->sInputState.fAux2) 
	{
		// perform the relative update
		this->RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
		this->RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
		this->RelativeUpdateCamera(CAMERA_FREE);

		// reset the relative variable as we have already performed the action on desired cameras
		this->cCameras->ResetRelativity();
	}
*/
}



/* -- RELATIVE UPDATE CAMERA --------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::RelativeUpdateCamera(uint32_t hash)
{
	if(!this->cCameras->IsCameraSelected(hash))
	{
		Vector3 translatedCameraPosition = this->cCameras->GetCameraTranslation(hash);
		float minY = this->cTerrain->GetTerrainHeightAt(translatedCameraPosition.x, translatedCameraPosition.z, true) + MIN_CAMERA_HEIGHT;
		this->cCameras->RelativeUpdate(hash, minY, MAX_CAMERA_HEIGHT);
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
	if(this->cCameras->IsCameraSelected(CAMERA_FREE))
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



/* -- SET UP CAMERAS FROM SCENE ---------------------------------------------------*/
void SkullbonezRun::SetUpCamerasFromScene(const TestScene& scene)
{
	this->cCameras = CameraCollection::Instance();

	for (int i = 0; i < scene.GetCameraCount(); ++i)
	{
		const SceneCamera& cam = scene.GetCamera(i);
		uint32_t hash = HashStr(cam.name);
		this->cCameras->AddCamera(cam.position, cam.view, cam.up, hash);
	}

	// set the camera boundaries
	this->cCameras->SetCameraXZBounds(this->cTerrain->GetXZBounds());

	// set the terrain
	this->cCameras->SetTerrain(this->cTerrain.get());

	// lock the cameras
	this->cCameras->SetLockedMode(true);
}



/* -- SET UP GAME MODELS FROM SCENE -----------------------------------------------*/
void SkullbonezRun::SetUpGameModelsFromScene(const TestScene& scene)
{
	this->modelCount = scene.GetBallCount();

	for (int i = 0; i < scene.GetBallCount(); ++i)
	{
		const SceneBall& ball = scene.GetBall(i);

		GameModel gameModel(&this->cWorldEnvironment,
			Vector3(ball.posX, ball.posY, ball.posZ),
			Vector3(ball.moment, ball.moment, ball.moment),
			ball.mass);

		gameModel.SetCoefficientRestitution(ball.restitution);
		gameModel.SetTerrain(this->cTerrain.get());
		gameModel.AddBoundingSphere(ball.radius);

		// apply force if any is specified
		if (ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f)
		{
			gameModel.SetImpulseForce(
				Vector3(ball.forceX, ball.forceY, ball.forceZ),
				Vector3(ball.forcePosX, ball.forcePosY, ball.forcePosZ));
		}

		this->cGameModelCollection.AddGameModel(std::move(gameModel));
	}
}



/* -- SAVE SCREENSHOT -------------------------------------------------------------*/
void SkullbonezRun::SaveScreenshot(const char* path)
{
	// Read viewport dimensions
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	int width  = viewport[2];
	int height = viewport[3];

	// Row stride padded to 4-byte boundary (BMP requirement)
	int rowStride = (width * 3 + 3) & ~3;
	int imageSize = rowStride * height;

	// Allocate pixel buffer
	std::vector<unsigned char> pixels(static_cast<size_t>(imageSize));

	// Read the back buffer (bottom-up, BGR — native BMP layout)
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadBuffer(GL_BACK);
	glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

	// BMP file header (14 bytes)
	unsigned char fileHeader[14] = {};
	int fileSize = 14 + 40 + imageSize;
	fileHeader[0]  = 'B';
	fileHeader[1]  = 'M';
	fileHeader[2]  = (unsigned char)(fileSize);
	fileHeader[3]  = (unsigned char)(fileSize >> 8);
	fileHeader[4]  = (unsigned char)(fileSize >> 16);
	fileHeader[5]  = (unsigned char)(fileSize >> 24);
	fileHeader[10] = 54; // pixel data offset

	// BMP info header (40 bytes)
	unsigned char infoHeader[40] = {};
	infoHeader[0]  = 40; // header size
	infoHeader[4]  = (unsigned char)(width);
	infoHeader[5]  = (unsigned char)(width >> 8);
	infoHeader[6]  = (unsigned char)(width >> 16);
	infoHeader[7]  = (unsigned char)(width >> 24);
	infoHeader[8]  = (unsigned char)(height);
	infoHeader[9]  = (unsigned char)(height >> 8);
	infoHeader[10] = (unsigned char)(height >> 16);
	infoHeader[11] = (unsigned char)(height >> 24);
	infoHeader[12] = 1;  // color planes
	infoHeader[14] = 24; // bits per pixel
	infoHeader[20] = (unsigned char)(imageSize);
	infoHeader[21] = (unsigned char)(imageSize >> 8);
	infoHeader[22] = (unsigned char)(imageSize >> 16);
	infoHeader[23] = (unsigned char)(imageSize >> 24);

	// Write to file
	FILE* file = nullptr;
	errno_t err = fopen_s(&file, path, "wb");
	if (err != 0 || !file)
	{
		char msg[512];
		sprintf_s(msg, sizeof(msg), "Failed to open screenshot file: %s  (SkullbonezRun::SaveScreenshot)", path);
		throw std::runtime_error(msg);
	}

	fwrite(fileHeader, 1, 14, file);
	fwrite(infoHeader, 1, 40, file);
	fwrite(pixels.data(), 1, static_cast<size_t>(imageSize), file);
	fclose(file);
}
