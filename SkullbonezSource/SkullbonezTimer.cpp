// --- Includes ---
#include "SkullbonezTimer.h"


// --- Usings ---
using namespace SkullbonezCore::Environment;


Timer::Timer()
{
    LARGE_INTEGER tmpPerformanceFreq;

    // get the frequency of the performance timer, if the function fails,
    // throw an exception
    if ( !QueryPerformanceFrequency( &tmpPerformanceFreq ) )
    {
        NoPerformanceCounterSupport();
    }

    // the platform SDK states that the above function can succeed, but set
    // the argument to 0.  If this happens, the system also does not support
    // the performance counter.  In this case, we also throw an exception
    if ( !tmpPerformanceFreq.QuadPart )
    {
        NoPerformanceCounterSupport();
    }

    // set the performace frequency member
    m_performanceFrequency = static_cast<double>( tmpPerformanceFreq.QuadPart );

    // we now do one final test to ensure the system supports the performance
    // counter.  If we succeed here, we know the CPU supports the performance
    // counter and the class will work as expected
    LARGE_INTEGER currTimeTemp;
    if ( !QueryPerformanceCounter( &currTimeTemp ) )
    {
        NoPerformanceCounterSupport();
    }

    // set our initial time (time this class was created)
    m_initialTime = GetCurrentTimeInSeconds();

    // initialise our other variables
    m_frameCountCurrentSecond = 0;
    m_currentFPSValue = 0;
    m_frameTimer = 0;
    m_startTime = 0;
    m_endTime = 0;
}


void Timer::NoPerformanceCounterSupport()
{
    throw std::runtime_error( "This system does not support high resolution counters (Timer::Timer)." );
}


void Timer::StartTimer()
{
    // set member m_startTime to current time
    m_startTime = GetCurrentTimeInSeconds();
}


void Timer::StopTimer()
{
    // set member m_endTime to current time
    m_endTime = GetCurrentTimeInSeconds();
}


double Timer::GetElapsedTime()
{
    // return the number of seconds passed between StartTimer and EndTimer call
    return m_endTime - m_startTime;
}


double Timer::GetTimeSinceLastStart()
{
    // return time passed since last StartTimer call
    return GetCurrentTimeInSeconds() - m_startTime;
}


double Timer::GetTotalTime()
{
    return GetCurrentTimeInSeconds() - m_initialTime;
}


double Timer::GetCurrentTimeInSeconds()
{
    // get the current time
    LARGE_INTEGER currTimeTmp;
    QueryPerformanceCounter( &currTimeTmp );

    // return the current time
    return static_cast<double>( currTimeTmp.QuadPart ) / m_performanceFrequency;
}


bool Timer::IncrementFrameCount()
{
    // set the frame timer to the current time if the frame counter has been reset
    if ( !m_frameCountCurrentSecond )
    {
        m_frameTimer = GetCurrentTimeInSeconds();
    }

    // increment the fps
    ++m_frameCountCurrentSecond;

    // return whether a second has passed
    return ( GetCurrentTimeInSeconds() - m_frameTimer > 1 );
}


void Timer::StoreFpsAndResetFrameCounter()
{
    // store fps count
    m_currentFPSValue = m_frameCountCurrentSecond;

    // reset frame counter
    m_frameCountCurrentSecond = 0;
}


int Timer::GetCurrentFPS()
{
    return m_currentFPSValue;
}
