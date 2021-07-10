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
#include "SkullbonezTimer.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Timer::Timer(void)
{
	LARGE_INTEGER tmpPerformanceFreq;

	// get the frequency of the performance timer, if the function fails,
	// throw an exception
	if(!QueryPerformanceFrequency(&tmpPerformanceFreq))
		this->NoPerformanceCounterSupport();

	// the platform SDK states that the above function can succeed, but set
	// the argument to 0.  If this happens, the system also does not support
	// the performance counter.  In this case, we also throw an exception
	if(!tmpPerformanceFreq.QuadPart)
		this->NoPerformanceCounterSupport();

	// set the performace frequency member
	this->PerformanceFrequency = (double)tmpPerformanceFreq.QuadPart;

	// we now do one final test to ensure the system supports the performance
	// counter.  If we succeed here, we know the CPU supports the performance
	// counter and the class will work as expected
	LARGE_INTEGER currTimeTemp;
	if(!QueryPerformanceCounter(&currTimeTemp))
		this->NoPerformanceCounterSupport();

	// set our initial time (time this class was created)
	this->InitialTime = this->GetCurrentTimeInSeconds();

	// initialise our other variables
	this->FrameCountCurrentSecond = 0;
	this->CurrentFPSValue		  = 0;
	this->FrameTimer			  = 0;
	this->StartTime				  = 0;
	this->EndTime				  = 0;
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
Timer::~Timer(void) {}



/* -- NO PERFORMACE COUNTER SUPPORT -----------------------------------------------*/
void Timer::NoPerformanceCounterSupport(void)
{
	throw "This system does not support high resolution counters (Timer::Timer).";
}



/* -- START TIMER -----------------------------------------------------------------*/
void Timer::StartTimer(void)
{
	// set member StartTime to current time
	this->StartTime = this->GetCurrentTimeInSeconds();
}



/* -- STOP TIMER ------------------------------------------------------------------*/
void Timer::StopTimer(void)
{
	// set member EndTime to current time
	this->EndTime = this->GetCurrentTimeInSeconds();
}



/* -- GET ELAPSED TIME ------------------------------------------------------------*/
double Timer::GetElapsedTime(void)
{
	// return the number of seconds passed between StartTimer and EndTimer call
	return this->EndTime - this->StartTime;
}



/* -- GET TIME SINCE LAST START ---------------------------------------------------*/
double Timer::GetTimeSinceLastStart(void)
{
	// return time passed since last StartTimer call
	return this->GetCurrentTimeInSeconds() - this->StartTime;
}



/* -- GET TOTAL TIME --------------------------------------------------------------*/
double Timer::GetTotalTime(void)
{
	return this->GetCurrentTimeInSeconds() - this->InitialTime;
}



/* -- GET CURRENT TIME IN SECONDS -------------------------------------------------*/
double Timer::GetCurrentTimeInSeconds(void)
{
	// get the current time
	LARGE_INTEGER currTimeTmp;
	QueryPerformanceCounter(&currTimeTmp);

	// return the current time
	return (double)currTimeTmp.QuadPart / this->PerformanceFrequency;
}



/* -- INCREMENT FRAME COUNT -------------------------------------------------------*/
bool Timer::IncrementFrameCount(void)
{
	// set the frame timer to the current time if the frame counter has been reset
	if(!this->FrameCountCurrentSecond)
		this->FrameTimer = this->GetCurrentTimeInSeconds();

	// increment the fps
	++this->FrameCountCurrentSecond;

	// return whether a second has passed
	return (this->GetCurrentTimeInSeconds() - this->FrameTimer > 1);
}



/* -- STORE FPS AND RESET FRAME COUNTER -------------------------------------------*/
void Timer::StoreFpsAndResetFrameCounter(void)
{
	// store fps count
	this->CurrentFPSValue = this->FrameCountCurrentSecond;

	// reset frame counter
	this->FrameCountCurrentSecond = 0;
}



/* -- GET CURRENT FPS -------------------------------------------------------------*/
int Timer::GetCurrentFPS(void)
{
	return this->CurrentFPSValue;
}