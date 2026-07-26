/*
File: SkullbonezSource/Core/Timer.cpp
Purpose:
  Measures elapsed time for frame pacing and simulation updates.

Summary:
  Timer.cpp measures elapsed time for frame pacing and simulation updates. As
  an implementation unit, keep edits anchored on process-wide contracts,
  diagnostics, and validation-sensitive state and on the glossary/invariants
  below.

Glossary:
  Lane R result: Recoverable platform/environment startup failure reported
    through an owner/message result instead of an exception.

Invariants:
  - Timer startup returns a Lane R result when high-resolution counters are
    unavailable because frame pacing and profiling depend on sub-frame timing.
  - Elapsed intervals use the last StartTimer/StopTimer pair; total time remains
    relative to successful startup.

Related:
  - SkullbonezSource/Core/Timer.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Timer.h"
#include "FatalError.h"
#include "PlatformWin32.h"


using namespace SkullbonezCore::Environment;


namespace
{
SkullbonezCore::Core::SbResult NoPerformanceCounterSupport( const char* failedCall )
{
    return SkullbonezCore::Core::SbResult::Failure( "Core/Timer",
                                                    "This system does not support high resolution counters (%s failed).",
                                                    failedCall && failedCall[0] != '\0' ? failedCall : "counter query" );
}
} // namespace


SkullbonezCore::Core::SbResult Timer::Initialise()
{
    LARGE_INTEGER tmpPerformanceFreq;

    if ( !QueryPerformanceFrequency( &tmpPerformanceFreq ) )
    {
        return NoPerformanceCounterSupport( "QueryPerformanceFrequency" );
    }

    // The platform SDK allows a successful frequency query to report zero. The
    // runtime treats that as the same startup environment failure as an API
    // failure because all later time conversion would be undefined.

    if ( !tmpPerformanceFreq.QuadPart )
    {
        return NoPerformanceCounterSupport( "QueryPerformanceFrequency zero frequency" );
    }

    m_performanceFrequency = static_cast<double>( tmpPerformanceFreq.QuadPart );

    // Probe a current counter value before declaring the timer ready. After
    // startup succeeds, per-frame timer reads are an engine invariant.
    LARGE_INTEGER currTimeTemp;

    if ( !QueryPerformanceCounter( &currTimeTemp ) )
    {
        return NoPerformanceCounterSupport( "QueryPerformanceCounter" );
    }

    m_initialized = true;
    m_initialTime = GetCurrentTimeInSeconds();

    m_frameCountCurrentSecond = 0;
    m_currentFPSValue = 0;
    m_frameTimer = 0;
    m_startTime = 0;
    m_endTime = 0;
    return SkullbonezCore::Core::SbResult::Success();
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

    if ( !m_initialized || m_performanceFrequency <= 0.0 )
    {
        SB_FATAL( "Core/Timer", "Timer sampled before successful Initialise()." );
    }

    LARGE_INTEGER currTimeTmp;

    if ( !QueryPerformanceCounter( &currTimeTmp ) )
    {
        SB_FATAL( "Core/Timer", "QueryPerformanceCounter failed after timer startup succeeded." );
    }

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
