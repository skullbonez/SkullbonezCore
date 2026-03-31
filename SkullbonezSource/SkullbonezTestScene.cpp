/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\'"VUUUV"'/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezTestScene.h"
#include <cstring>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
TestScene::TestScene(void)
{
	this->isPhysicsEnabled	= true;
	this->frameCount		= -1;
}



/* -- LOAD FROM FILE --------------------------------------------------------------*/
TestScene TestScene::LoadFromFile(const char* path)
{
	TestScene scene;

	FILE* file = nullptr;
	errno_t err = fopen_s(&file, path, "r");
	if (err != 0 || !file)
	{
		char msg[256];
		sprintf_s(msg, sizeof(msg), "Failed to open scene file: %s  (TestScene::LoadFromFile)", path);
		throw std::runtime_error(msg);
	}

	char line[512];
	int lineNumber = 0;

	while (fgets(line, sizeof(line), file))
	{
		++lineNumber;

		// strip newline
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
		if (len > 1 && line[len - 2] == '\r') line[len - 2] = '\0';

		// skip blank lines and comments
		if (line[0] == '\0' || line[0] == '#') continue;

		// parse physics directive
		if (strncmp(line, "physics ", 8) == 0)
		{
			if (strcmp(line + 8, "off") == 0)		scene.isPhysicsEnabled = false;
			else if (strcmp(line + 8, "on") == 0)	scene.isPhysicsEnabled = true;
			else
			{
				fclose(file);
				char msg[256];
				sprintf_s(msg, sizeof(msg), "Invalid physics value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 8);
				throw std::runtime_error(msg);
			}
			continue;
		}

		// parse frames directive
		if (strncmp(line, "frames ", 7) == 0)
		{
			if (strcmp(line + 7, "unlimited") == 0)
			{
				scene.frameCount = -1;
			}
			else
			{
				scene.frameCount = atoi(line + 7);
				if (scene.frameCount <= 0)
				{
					fclose(file);
					char msg[256];
					sprintf_s(msg, sizeof(msg), "Invalid frame count at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 7);
					throw std::runtime_error(msg);
				}
			}
			continue;
		}

		// parse camera line
		if (strncmp(line, "camera ", 7) == 0)
		{
			if ((int)scene.cameras.size() >= TOTAL_CAMERA_COUNT)
			{
				fclose(file);
				char msg[256];
				sprintf_s(msg, sizeof(msg), "Too many cameras at line %d (max %d)  (TestScene::LoadFromFile)", lineNumber, TOTAL_CAMERA_COUNT);
				throw std::runtime_error(msg);
			}

			SceneCamera cam;
			memset(&cam, 0, sizeof(cam));

			int parsed = sscanf_s(line + 7, "%63s %f %f %f %f %f %f %f %f %f",
				cam.name, (unsigned)sizeof(cam.name),
				&cam.position.x, &cam.position.y, &cam.position.z,
				&cam.view.x, &cam.view.y, &cam.view.z,
				&cam.up.x, &cam.up.y, &cam.up.z);

			if (parsed != 10)
			{
				fclose(file);
				char msg[256];
				sprintf_s(msg, sizeof(msg), "Invalid camera at line %d (expected 10 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed);
				throw std::runtime_error(msg);
			}

			scene.cameras.push_back(cam);
			continue;
		}

		// parse ball line
		if (strncmp(line, "ball ", 5) == 0)
		{
			SceneBall ball;
			memset(&ball, 0, sizeof(ball));

			// try full line (with force)
			int parsed = sscanf_s(line + 5, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f",
				ball.name, (unsigned)sizeof(ball.name),
				&ball.posX, &ball.posY, &ball.posZ,
				&ball.radius, &ball.mass, &ball.moment, &ball.restitution,
				&ball.forceX, &ball.forceY, &ball.forceZ,
				&ball.forcePosX, &ball.forcePosY, &ball.forcePosZ);

			if (parsed != 14 && parsed != 8)
			{
				fclose(file);
				char msg[256];
				sprintf_s(msg, sizeof(msg), "Invalid ball at line %d (expected 8 or 14 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed);
				throw std::runtime_error(msg);
			}

			scene.balls.push_back(ball);
			continue;
		}

		// unknown directive
		fclose(file);
		char msg[256];
		sprintf_s(msg, sizeof(msg), "Unknown directive at line %d: %.64s  (TestScene::LoadFromFile)", lineNumber, line);
		throw std::runtime_error(msg);
	}

	fclose(file);

	// validate
	if (scene.cameras.empty())
		throw std::runtime_error("Scene file must define at least one camera.  (TestScene::LoadFromFile)");

	return scene;
}



/* -- IS PHYSICS ENABLED ----------------------------------------------------------*/
bool TestScene::IsPhysicsEnabled(void) const
{
	return this->isPhysicsEnabled;
}



/* -- GET FRAME COUNT -------------------------------------------------------------*/
int TestScene::GetFrameCount(void) const
{
	return this->frameCount;
}



/* -- GET CAMERA COUNT ------------------------------------------------------------*/
int TestScene::GetCameraCount(void) const
{
	return (int)this->cameras.size();
}



/* -- GET BALL COUNT --------------------------------------------------------------*/
int TestScene::GetBallCount(void) const
{
	return (int)this->balls.size();
}



/* -- GET CAMERA ------------------------------------------------------------------*/
const SceneCamera& TestScene::GetCamera(int index) const
{
	if (index < 0 || index >= (int)this->cameras.size())
		throw std::runtime_error("Camera index out of range.  (TestScene::GetCamera)");

	return this->cameras[index];
}



/* -- GET BALL --------------------------------------------------------------------*/
const SceneBall& TestScene::GetBall(int index) const
{
	if (index < 0 || index >= (int)this->balls.size())
		throw std::runtime_error("Ball index out of range.  (TestScene::GetBall)");

	return this->balls[index];
}
