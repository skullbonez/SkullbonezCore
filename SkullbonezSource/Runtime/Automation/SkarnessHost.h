#pragma once

#if defined( SKULLBONEZ_SKARNESS )

#include "SkarnessProtocol.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>

namespace SkullbonezCore::Runtime
{
class SkarnessHost
{
  public:
    SkarnessHost() = default;
    ~SkarnessHost();
    SkarnessHost( const SkarnessHost& ) = delete;
    SkarnessHost& operator=( const SkarnessHost& ) = delete;

    bool Configure( const char* sessionDirectory, std::string& outReason );
    void Shutdown( const char* status );
    void PollCommands();
    bool PopCommand( SkarnessCommand& outCommand );
    void CompleteCommand( const std::string& requestId, bool applied, const char* reason = nullptr );
    bool BeginSceneTransition( const std::string& requestId, uint64_t sourceGeneration, const char* expectedScenePath,
                               bool expectDemo );
    uint64_t BeginCapture( const std::string& requestId );
    void CompleteCapture( uint64_t token, bool applied, const char* reason = nullptr );
    SkarnessProceedPolicy TakeProceedPolicy();
    void PublishFrameState( const SkarnessFrameState& state );
    bool TakeStopRequested() noexcept;
    bool BeginPhysicsSceneGeneration( uint64_t generation ) noexcept;
    const char* PhysicsTracePath() const noexcept;
    const char* RunId() const noexcept;

    bool Enabled() const noexcept
    {
        return m_enabled;
    }
    bool Paused() const noexcept
    {
        return m_paused;
    }

  private:
    bool CreatePipe( std::string& outReason );
    void AcceptClient();
    void DisconnectClient();
    void ConsumeRequestLine( const std::string& line );
    bool RememberRequestId( const std::string& requestId );
    bool SendJsonLine( const std::string& line );
    void SendLifecycle( const std::string& requestId, const char* status, const char* reason = nullptr );
    void SendCapabilities( const std::string& requestId );
    bool WriteManifest( const char* status );
    bool UntilConditionMet( const SkarnessFrameState& state ) const noexcept;
    std::string UntilTimeoutReason( const SkarnessFrameState& state ) const;

    void* m_pipe = nullptr;
    struct PendingCompletion
    {
        std::string requestId;
        std::string reason;
        bool applied = false;
    };
    struct PendingCapture
    {
        uint64_t token = 0;
        std::string requestId;
    };
    struct CompletedRequest
    {
        std::string requestId;
        std::string response;
    };
    struct PendingSceneTransition
    {
        std::string requestId;
        std::string expectedScenePath;
        uint64_t sourceGeneration = 0;
        uint32_t framesRemaining = 0;
        bool expectDemo = false;
    };
    std::filesystem::path m_sessionDirectory;
    std::filesystem::path m_manifestPath;
    std::filesystem::path m_tracePath;
    std::filesystem::path m_physicsTracePath;
    std::ofstream m_trace;
    std::string m_pipeName;
    std::string m_sessionToken;
    std::string m_runId;
    std::string m_physicsTracePathString;
    std::string m_receiveBuffer;
    std::deque<SkarnessCommand> m_commands;
    std::deque<PendingCompletion> m_pendingCompletions;
    std::deque<PendingCapture> m_pendingCaptures;
    std::deque<std::string> m_recentRequestIds;
    std::deque<CompletedRequest> m_completedRequests;
    PendingSceneTransition m_pendingSceneTransition;
    uint64_t m_sequence = 0;
    uint64_t m_renderFrame = 0;
    uint64_t m_physicsSceneGeneration = ~uint64_t { 0 };
    uint64_t m_nextCaptureToken = 1;
    uint32_t m_stepFramesRemaining = 0;
    uint32_t m_renderFramesRemaining = 0;
    std::string m_stepRequestId;
    std::string m_renderStepRequestId;
    std::string m_untilRequestId;
    std::string m_untilCondition;
    std::string m_stopRequestId;
    bool m_stepCompletesAfterFrame = false;
    uint32_t m_untilFramesRemaining = 0;
    bool m_untilStepsPhysics = false;
    bool m_stopAfterFrame = false;
    bool m_stopRequested = false;
    bool m_enabled = false;
    bool m_connected = false;
    bool m_paused = true;
};
} // namespace SkullbonezCore::Runtime

#endif
