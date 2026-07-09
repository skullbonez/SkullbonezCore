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
  Lane R result: Recoverable platform/environment startup failure reported
    through an owner/message result instead of an exception.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - m_performanceFrequency is captured by Initialise() before any time sample.
  - Timer callers must treat a failed Initialise() as a startup boundary failure.
  - FPS counters are a one-second bucket separate from interval timing.

Related:
  - SkullbonezSource/Core/Timer.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"
#include "SbResult.h"

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
    int m_frameCountCurrentSecond = 0;   // Frames accumulated for the current one-second FPS bucket.
    int m_currentFPSValue = 0;           // Last completed one-second frame count.
    double m_frameTimer = 0.0;           // Start time of the active FPS bucket.
    double m_initialTime = 0.0;          // Startup timestamp for total-runtime queries.
    double m_startTime = 0.0;            // Last StartTimer timestamp.
    double m_endTime = 0.0;              // Last StopTimer timestamp.
    double m_performanceFrequency = 0.0; // Counter ticks per second for this CPU.
    bool m_initialized = false;          // True after the platform counter check succeeds.

    double GetCurrentTimeInSeconds();    // Converts the high-resolution counter to seconds.

  public:
    Timer() = default;                   // Starts inert; call Initialise before sampling time.
    ~Timer() = default;
    Basics::SbResult Initialise();       // Captures counter frequency and starts the total-runtime clock.
    void StartTimer();                   // Starts a measured interval.
    void StopTimer();                    // Captures the active measured interval end timestamp.
    double GetElapsedTime();             // Seconds between the last StartTimer and StopTimer calls.
    double GetTimeSinceLastStart();      // Seconds since the active interval started.
    double GetTotalTime();               // Seconds since this Timer successfully initialized.
    bool IncrementFrameCount();          // Advances FPS bucket; true means a full second elapsed.
    void StoreFpsAndResetFrameCounter(); // Publishes the finished FPS bucket and starts a new one.
    int GetCurrentFPS();                 // Last published frames-per-second value.
};
} // namespace Environment
} // namespace SkullbonezCore
