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
#include "SkullbonezGameModelCollection.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
GameModelCollection::GameModelCollection(int iMaxCount)
{
	// init members
	this->curCount		 = 0;
	this->maxCount		 = iMaxCount;
	this->gameModelArray = new GameModel*[iMaxCount];
	
	// init array to null
	for(int x=0; x<iMaxCount; ++x) this->gameModelArray[x] = 0;
};



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
GameModelCollection::~GameModelCollection(void)
{
	// delete all game models
	for(int x=0; x<this->maxCount; ++x)	
		if(this->gameModelArray[x]) 
			delete this->gameModelArray[x];

	// perform array delete
	if(this->gameModelArray) delete [] this->gameModelArray;
};



/* -- ADD GAME MODEL --------------------------------------------------------------*/
void GameModelCollection::AddGameModel(GameModel* gameModel)
{
	// check for space
	if(this->curCount == this->maxCount)
		throw "Game model array is full!  (GameModelCollection::AddGameModel)";

	// add the model
	this->gameModelArray[this->curCount] = gameModel;

	// increment the counter
	++this->curCount;
}



/* -- RENDER MODELS ---------------------------------------------------------------*/
void GameModelCollection::RenderModels(void)
{
	// render the game models
	for(int x=0; x<this->curCount; ++x) 
		this->gameModelArray[x]->RenderCollisionBounds();
}



/* -- GET MODEL POSITION ----------------------------------------------------------*/
Vector3 GameModelCollection::GetModelPosition(int index)
{
	if(index >= this->curCount)
	{
		throw "No game model exists at the specified index.  "
			  "(GameModelCollection::GetModelPosition)";
	}

	return this->gameModelArray[index]->GetPosition();
}



/* -- RUN PHYSICS -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void GameModelCollection::RunPhysics(float fChangeInTime)
{
	// array to keep track of whether time steps have been applied or not
	bool *isTimeStepApplied = new bool[this->curCount];

	// clear the contents of the new array and update the velocity of all models
	for(int x=0; x<this->curCount; ++x)
	{
		this->gameModelArray[x]->ApplyForces(fChangeInTime);
		isTimeStepApplied[x] = false;
	}

	// detect and respond to collisions between game models
	for(int x=0; x<this->curCount-1; ++x) 
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			for(int y=x+1; y<this->curCount; ++y)
			{
				// if the time step is not applied at this position
				if(!isTimeStepApplied[y])
				{
					// check the collision time
					float colTime = this->gameModelArray[x]->CollisionDetectGameModel(*this->gameModelArray[y], fChangeInTime);

					// if there is a response required, perform it
					if(this->gameModelArray[x]->IsResponseRequired() && this->gameModelArray[y]->IsResponseRequired())
					{
						// update the time step before the collision
						this->gameModelArray[x]->UpdatePosition(colTime);
						this->gameModelArray[y]->UpdatePosition(colTime);

						// calculate response and update the remaining time step
						this->gameModelArray[x]->CollisionResponseGameModel(*this->gameModelArray[y], fChangeInTime - colTime);

						// note that the timestep has been applied
						isTimeStepApplied[x] = true;
						isTimeStepApplied[y] = true;
					}
				}
			}
		}
	}

	// detect and respond to collisions between game models and the terrain
	for(int x=0; x<this->curCount; ++x)
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			// check the collision time
			float colTime = this->gameModelArray[x]->CollisionDetectTerrain(fChangeInTime);

			// if a response is required, perform it
			if(this->gameModelArray[x]->IsResponseRequired())
			{
				// update the time step before the collision
				this->gameModelArray[x]->UpdatePosition(colTime);

				// calculate response and update the remaining time step
				this->gameModelArray[x]->CollisionResponseTerrain(fChangeInTime - colTime);

				// note that the timestep has been applied
				isTimeStepApplied[x] = true;
			}
		}
	}

	// apply the remaining time steps
	for(int x=0; x<this->curCount; ++x)
	{
		// if the time step is not applied at this position
		if(!isTimeStepApplied[x])
		{
			// apply the time step to this model
			this->gameModelArray[x]->UpdatePosition(fChangeInTime);
		}
	}

	// array delete the local array
	if(isTimeStepApplied) delete [] isTimeStepApplied;
}