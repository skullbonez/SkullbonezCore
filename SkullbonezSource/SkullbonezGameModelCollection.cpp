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
#include "SkullbonezGameModelCollection.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
GameModelCollection::GameModelCollection(void)
{
	this->gameModels.reserve(200);
};



/* -- ADD GAME MODEL --------------------------------------------------------------*/
void GameModelCollection::AddGameModel(GameModel gameModel)
{
	this->gameModels.push_back(std::move(gameModel));
}



/* -- RENDER MODELS ---------------------------------------------------------------*/
void GameModelCollection::RenderModels(void)
{
	// render the game models
	for(int x=0; x<(int)this->gameModels.size(); ++x) 
		this->gameModels[x].RenderCollisionBounds();
}



/* -- GET MODEL POSITION ----------------------------------------------------------*/
Vector3 GameModelCollection::GetModelPosition(int index)
{
	if(index < 0 || index >= (int)this->gameModels.size())
	{
		throw std::runtime_error("No game model exists at the specified index.  (GameModelCollection::GetModelPosition)");
	}

	return this->gameModels[index].GetPosition();
}



/* -- RUN PHYSICS -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void GameModelCollection::RunPhysics(float fChangeInTime)
{
	std::vector<bool> isTimeStepApplied((int)this->gameModels.size(), false);

	// update the velocity of all models
	for(int x=0; x<(int)this->gameModels.size(); ++x)
		this->gameModels[x].ApplyForces(fChangeInTime);

	// detect and respond to collisions between game models
	for(int x=0; x<(int)this->gameModels.size()-1; ++x) 
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			for(int y=x+1; y<(int)this->gameModels.size(); ++y)
			{
				// if the time step is not applied at this position
				if(!isTimeStepApplied[y])
				{
					// check the collision time
					float colTime = this->gameModels[x].CollisionDetectGameModel(this->gameModels[y], fChangeInTime);

					// if there is a response required, perform it
					if(this->gameModels[x].IsResponseRequired() && this->gameModels[y].IsResponseRequired())
					{
						// update the time step before the collision
						this->gameModels[x].UpdatePosition(colTime);
						this->gameModels[y].UpdatePosition(colTime);

						// calculate response and update the remaining time step
						this->gameModels[x].CollisionResponseGameModel(this->gameModels[y], fChangeInTime - colTime);

						// note that the timestep has been applied
						isTimeStepApplied[x] = true;
						isTimeStepApplied[y] = true;
					}
				}
			}
		}
	}

	// detect and respond to collisions between game models and the terrain
	for(int x=0; x<(int)this->gameModels.size(); ++x)
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			// check the collision time
			float colTime = this->gameModels[x].CollisionDetectTerrain(fChangeInTime);

			// if a response is required, perform it
			if(this->gameModels[x].IsResponseRequired())
			{
				// update the time step before the collision
				this->gameModels[x].UpdatePosition(colTime);

				// calculate response and update the remaining time step
				this->gameModels[x].CollisionResponseTerrain(fChangeInTime - colTime);

				// note that the timestep has been applied
				isTimeStepApplied[x] = true;
			}
		}
	}

	// apply the remaining time steps
	for(int x=0; x<(int)this->gameModels.size(); ++x)
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			// apply the time step to this model
			this->gameModels[x].UpdatePosition(fChangeInTime);
		}
	}

}
