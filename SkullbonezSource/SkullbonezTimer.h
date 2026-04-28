/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TIMER_H
#define SKULLBONEZ_TIMER_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Environment
{
/* -- Timer ------------------------------------------------------------------------------------------------------------------------------------------------------

    An easy to use timing mechanism aimed to be useful for games development.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Timer
{

  private:
    int m_frameCountCurrentSecond; // Stores the total amount of rendered frames for the current second
    int m_currentFPSValue;         // Stores the last calculated FPS value
    double m_frameTimer;           // Assists in working out FPS
    double m_initialTime;          // Stores the time of when the class was created
    double m_startTime;            // Stores the time of the call to StartTimer()
    double m_endTime;              // Stores the time of the call to StopTimer()
    double m_performanceFrequency; // Stores the performance frequency of the high resolution counter (this is specific to the CPU).

    double GetCurrentTimeInSeconds();   // Returns the current time
    void NoPerformanceCounterSupport(); // Throws an exception when called

  public:
    Timer(); // Default constructor
    ~Timer() = default;
    void StartTimer();                   // Begins timing
    void StopTimer();                    // Ends timing
    double GetElapsedTime();             // Returns the amount of time between StartTimer and StopTimer in seconds
    double GetTimeSinceLastStart();      // Returns the amount of time since the last StartTimer call
    double GetTotalTime();               // Returns the amount of time the current instance has been instantiated for
    bool IncrementFrameCount();          // Increments the frame count and returns true if 1 second has passed
    void StoreFpsAndResetFrameCounter(); // Returns frames per second and resets the frame counter to zero
    int GetCurrentFPS();                 // Returns the current FPS value
};
} // namespace Environment
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
