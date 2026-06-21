/*
File: SkullbonezSource/Timer.cpp
Purpose:
  Measures elapsed time for frame pacing and simulation updates.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Timer.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Timer.h"


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

    m_performanceFrequency = static_cast<double>( tmpPerformanceFreq.QuadPart );

    // we now do one final test to ensure the system supports the performance
    // counter.  If we succeed here, we know the CPU supports the performance
    // counter and the class will work as expected
    LARGE_INTEGER currTimeTemp;
    if ( !QueryPerformanceCounter( &currTimeTemp ) )
    {
        NoPerformanceCounterSupport();
    }

    m_initialTime = GetCurrentTimeInSeconds();

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
    m_startTime = GetCurrentTimeInSeconds();
}


void Timer::StopTimer()
{
    m_endTime = GetCurrentTimeInSeconds();
}


double Timer::GetElapsedTime()
{
    return m_endTime - m_startTime;
}


double Timer::GetTimeSinceLastStart()
{
    return GetCurrentTimeInSeconds() - m_startTime;
}


double Timer::GetTotalTime()
{
    return GetCurrentTimeInSeconds() - m_initialTime;
}


double Timer::GetCurrentTimeInSeconds()
{
    LARGE_INTEGER currTimeTmp;
    QueryPerformanceCounter( &currTimeTmp );

    return static_cast<double>( currTimeTmp.QuadPart ) / m_performanceFrequency;
}


bool Timer::IncrementFrameCount()
{
    if ( !m_frameCountCurrentSecond )
    {
        m_frameTimer = GetCurrentTimeInSeconds();
    }

    ++m_frameCountCurrentSecond;

    return ( GetCurrentTimeInSeconds() - m_frameTimer > 1 );
}


void Timer::StoreFpsAndResetFrameCounter()
{
    m_currentFPSValue = m_frameCountCurrentSecond;

    m_frameCountCurrentSecond = 0;
}


int Timer::GetCurrentFPS()
{
    return m_currentFPSValue;
}
