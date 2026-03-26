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
	std::vector<float> timeRemaining((int)this->gameModels.size(), fChangeInTime);

	// update the velocity of all models
	for(int x=0; x<(int)this->gameModels.size(); ++x)
		this->gameModels[x].ApplyForces(fChangeInTime);

	// detect and respond to collisions between game models
	for(int x=0; x<(int)this->gameModels.size()-1; ++x) 
	{
		for(int y=x+1; y<(int)this->gameModels.size(); ++y)
		{
			// skip pairs where either ball has exhausted its frame time
			if(timeRemaining[x] <= 0.0f || timeRemaining[y] <= 0.0f) continue;

			// use the minimum remaining time window for this pair
			float availableTime = (std::min)(timeRemaining[x], timeRemaining[y]);

			// check the collision time
			float colTime = this->gameModels[x].CollisionDetectGameModel(this->gameModels[y], availableTime);

			// if there is a response required, perform it
			if(this->gameModels[x].IsResponseRequired() && this->gameModels[y].IsResponseRequired())
			{
				// advance both models to the collision point
				this->gameModels[x].UpdatePosition(colTime);
				this->gameModels[y].UpdatePosition(colTime);

				// subtract consumed time
				timeRemaining[x] -= colTime;
				timeRemaining[y] -= colTime;

				// velocity-only response (clears isResponseRequired on both models)
				this->gameModels[x].CollisionResponseGameModel(this->gameModels[y]);
			}
			else
			{
				// sweep test found no collision — check for static overlap
				// (handles grounded/slow balls that the sweep test misses)
				this->gameModels[x].StaticOverlapResponseGameModel(this->gameModels[y]);
			}
		}
	}

	// detect and respond to collisions between game models and the terrain
	for(int x=0; x<(int)this->gameModels.size(); ++x)
	{
		// only check terrain if this model has remaining time
		if(timeRemaining[x] > 0.0f)
		{
			// check the collision time
			float colTime = this->gameModels[x].CollisionDetectTerrain(timeRemaining[x]);

			// if a response is required, perform it
			if(this->gameModels[x].IsResponseRequired())
			{
				// update the time step before the collision
				this->gameModels[x].UpdatePosition(colTime);

				// calculate response and update the remaining time step (terrain response advances position internally)
				this->gameModels[x].CollisionResponseTerrain(timeRemaining[x] - colTime);

				// terrain response already advanced position; zero remaining time
				timeRemaining[x] = 0.0f;
			}
		}
	}

	// apply the remaining time steps
	for(int x=0; x<(int)this->gameModels.size(); ++x)
	{
		// advance by whatever time remains
		if(timeRemaining[x] > 0.0f)
		{
			this->gameModels[x].UpdatePosition(timeRemaining[x]);
		}
	}

}
