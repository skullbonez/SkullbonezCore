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
									 ?\"VUUUV"/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezTimer.h"
#ifndef _WIN32
#  include <time.h>    // clock_gettime, CLOCK_MONOTONIC
#endif



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;



/* ================================================================================
   PLATFORM-SPECIFIC TIME PRIMITIVE
   ================================================================================ */
#ifdef _WIN32

// Windows: QueryPerformanceCounter-based implementation
static double GetMonotonicSeconds()
{
	LARGE_INTEGER freq, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

#else

// Linux: clock_gettime(CLOCK_MONOTONIC) — nanosecond resolution, no overflow risk
static double GetMonotonicSeconds()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

#endif // _WIN32



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Timer::Timer(void)
{
#ifdef _WIN32
	// Validate that the performance counter works on this Windows system
	LARGE_INTEGER tmpPerformanceFreq;
	if(!QueryPerformanceFrequency(&tmpPerformanceFreq))
		throw std::runtime_error("This system does not support high resolution counters (Timer::Timer).");
	if(!tmpPerformanceFreq.QuadPart)
		throw std::runtime_error("This system does not support high resolution counters (Timer::Timer).");
	this->PerformanceFrequency = static_cast<double>(tmpPerformanceFreq.QuadPart);

	LARGE_INTEGER currTimeTemp;
	if(!QueryPerformanceCounter(&currTimeTemp))
		throw std::runtime_error("This system does not support high resolution counters (Timer::Timer).");
#else
	// On Linux, clock_gettime always works; no initialisation needed.
	this->PerformanceFrequency = 1.0;  // unused on Linux, kept to avoid uninit warning
#endif

	this->InitialTime              = this->GetCurrentTimeInSeconds();
	this->FrameCountCurrentSecond  = 0;
	this->CurrentFPSValue          = 0;
	this->FrameTimer               = 0;
	this->StartTime                = 0;
	this->EndTime                  = 0;
}



/* -- NO PERFORMACE COUNTER SUPPORT -----------------------------------------------*/
void Timer::NoPerformanceCounterSupport(void)
{
	throw std::runtime_error("This system does not support high resolution counters (Timer::Timer).");
}



/* -- START TIMER -----------------------------------------------------------------*/
void Timer::StartTimer(void)
{
	this->StartTime = this->GetCurrentTimeInSeconds();
}



/* -- STOP TIMER ------------------------------------------------------------------*/
void Timer::StopTimer(void)
{
	this->EndTime = this->GetCurrentTimeInSeconds();
}



/* -- GET ELAPSED TIME ------------------------------------------------------------*/
double Timer::GetElapsedTime(void)
{
	return this->EndTime - this->StartTime;
}



/* -- GET TIME SINCE LAST START ---------------------------------------------------*/
double Timer::GetTimeSinceLastStart(void)
{
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
#ifdef _WIN32
	LARGE_INTEGER currTimeTmp;
	QueryPerformanceCounter(&currTimeTmp);
	return static_cast<double>(currTimeTmp.QuadPart) / this->PerformanceFrequency;
#else
	return GetMonotonicSeconds();
#endif
}



/* -- INCREMENT FRAME COUNT -------------------------------------------------------*/
bool Timer::IncrementFrameCount(void)
{
	if(!this->FrameCountCurrentSecond)
		this->FrameTimer = this->GetCurrentTimeInSeconds();

	++this->FrameCountCurrentSecond;

	return (this->GetCurrentTimeInSeconds() - this->FrameTimer > 1);
}



/* -- STORE FPS AND RESET FRAME COUNTER -------------------------------------------*/
void Timer::StoreFpsAndResetFrameCounter(void)
{
	this->CurrentFPSValue = this->FrameCountCurrentSecond;
	this->FrameCountCurrentSecond = 0;
}



/* -- GET CURRENT FPS -------------------------------------------------------------*/
int Timer::GetCurrentFPS(void)
{
	return this->CurrentFPSValue;
}
