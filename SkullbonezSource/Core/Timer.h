/*
File: SkullbonezSource/Core/Timer.h
Purpose:
  Measures elapsed time for frame pacing and simulation updates.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  High-resolution counter: Windows performance counter used for sub-frame time
  measurement.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Core/Timer.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"

namespace SkullbonezCore
{
namespace Environment
{
/* -- Timer
------------------------------------------------------------------------------------------------------------------------------------------------------

    An easy to use timing mechanism aimed to be useful for games development.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Timer
{

  private:
    int m_frameCountCurrentSecond;       // Frames accumulated for the current one-second FPS bucket.
    int m_currentFPSValue;               // Last completed one-second frame count.
    double m_frameTimer;                 // Start time of the active FPS bucket.
    double m_initialTime;                // Construction timestamp for total-runtime queries.
    double m_startTime;                  // Last StartTimer timestamp.
    double m_endTime;                    // Last StopTimer timestamp.
    double m_performanceFrequency;       // Counter ticks per second for this CPU.

    double GetCurrentTimeInSeconds();    // Converts the high-resolution counter to seconds.
    void NoPerformanceCounterSupport();  // Throws an exception when called

  public:
    Timer();                             // Captures counter frequency and starts the total-runtime clock.
    ~Timer() = default;
    void StartTimer();                   // Starts a measured interval.
    void StopTimer();                    // Captures the active measured interval end timestamp.
    double GetElapsedTime();             // Seconds between the last StartTimer and StopTimer calls.
    double GetTimeSinceLastStart();      // Seconds since the active interval started.
    double GetTotalTime();               // Seconds since this Timer was constructed.
    bool IncrementFrameCount();          // Advances FPS bucket; true means a full second elapsed.
    void StoreFpsAndResetFrameCounter(); // Publishes the finished FPS bucket and starts a new one.
    int GetCurrentFPS();                 // Last published frames-per-second value.
};
} // namespace Environment
} // namespace SkullbonezCore
