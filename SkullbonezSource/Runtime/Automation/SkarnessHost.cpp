#include "SkarnessHost.h"

#if defined( SKULLBONEZ_SKARNESS )

#include "../../../ThirdPtySource/nlohmann/json.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace SkullbonezCore::Runtime
{
namespace
{
using Json = nlohmann::ordered_json;
constexpr std::size_t SKARNESS_COMMAND_CAPACITY = 128u;
constexpr std::size_t SKARNESS_REQUEST_HISTORY_CAPACITY = 256u;
constexpr std::size_t SKARNESS_RECEIVE_CAPACITY = 1024u * 1024u;

HANDLE NativePipe( void* pipe )
{
    return static_cast<HANDLE>( pipe );
}

class LocalSecurityDescriptor
{
  public:
    ~LocalSecurityDescriptor()
    {
        if ( m_descriptor )
        {
            LocalFree( m_descriptor );
        }
    }

    bool BuildForCurrentUser()
    {
        HANDLE token = nullptr;

        if ( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) )
        {
            return false;
        }

        DWORD required = 0;
        GetTokenInformation( token, TokenUser, nullptr, 0, &required );
        std::vector<unsigned char> storage( required );
        const bool readUser = required > 0 &&
                              GetTokenInformation( token, TokenUser, storage.data(), required, &required ) != FALSE;
        CloseHandle( token );

        if ( !readUser )
        {
            return false;
        }

        LPSTR sid = nullptr;
        const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>( storage.data() );

        if ( !ConvertSidToStringSidA( tokenUser->User.Sid, &sid ) )
        {
            return false;
        }

        // Invariant: the pipe DACL grants access only to the Windows account
        // that launched this process. The session token remains a second guard.
        const std::string sddl = "D:P(A;;GA;;;" + std::string( sid ) + ")";
        LocalFree( sid );
        return ConvertStringSecurityDescriptorToSecurityDescriptorA( sddl.c_str(), SDDL_REVISION_1, &m_descriptor,
                                                                     nullptr ) != FALSE;
    }

    PSECURITY_DESCRIPTOR Get() const noexcept
    {
        return m_descriptor;
    }

  private:
    PSECURITY_DESCRIPTOR m_descriptor = nullptr;
};

std::string BuildSessionToken()
{
    std::random_device random;
    std::array<unsigned int, 4> words = { random(), random(), random(), random() };
    std::ostringstream token;
    token << std::hex << std::setfill( '0' );

    for ( unsigned int word : words )
    {
        token << std::setw( 8 ) << word;
    }

    return token.str();
}

bool ReadBoolean( const Json& arguments, const char* name, bool& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_boolean() )
    {
        return false;
    }

    out = found->get<bool>();
    return true;
}

bool ReadNumber( const Json& arguments, const char* name, double& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_number() )
    {
        return false;
    }

    out = found->get<double>();
    return true;
}

bool ReadInteger( const Json& arguments, const char* name, int& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_number_integer() )
    {
        return false;
    }

    out = found->get<int>();
    return true;
}

bool ReadUnsignedInteger( const Json& arguments, const char* name, uint64_t& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_number_unsigned() )
    {
        return false;
    }

    out = found->get<uint64_t>();
    return out != 0;
}

bool ReadString( const Json& arguments, const char* name, std::string& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_string() )
    {
        return false;
    }

    out = found->get<std::string>();
    return !out.empty();
}
} // namespace

SkarnessHost::~SkarnessHost()
{
    Shutdown( "closed" );
}

bool SkarnessHost::Configure( const char* sessionDirectory, std::string& outReason )
{
    if ( !sessionDirectory || sessionDirectory[0] == '\0' )
    {
        outReason = "--skarness expects a session directory";
        return false;
    }

    std::error_code error;
    m_sessionDirectory = std::filesystem::absolute( sessionDirectory, error ).lexically_normal();

    if ( error || !std::filesystem::create_directories( m_sessionDirectory, error ) && error )
    {
        outReason = "could not create the Skarness session directory";
        return false;
    }

    m_manifestPath = m_sessionDirectory / "session.json";
    m_tracePath = m_sessionDirectory / "runtime.skarness.ndjson";
    m_physicsTracePath = m_sessionDirectory / "physics.physicsdiag.ndjson";
    m_physicsTracePathString = m_physicsTracePath.string();
    m_sessionToken = BuildSessionToken();
    m_runId = m_sessionToken.substr( 0, 24 );
    m_pipeName = "\\\\.\\pipe\\skarness-" + std::to_string( GetCurrentProcessId() ) + "-" + m_sessionToken.substr( 0, 12 );
    m_trace.open( m_tracePath, std::ios::binary | std::ios::out | std::ios::trunc );

    if ( !m_trace )
    {
        outReason = "could not create the Skarness state trace";
        return false;
    }

    if ( !CreatePipe( outReason ) )
    {
        return false;
    }

    m_enabled = true;
    m_paused = true;

    if ( !WriteManifest( "waiting" ) )
    {
        outReason = "could not publish the Skarness session manifest";
        Shutdown( "failed" );
        return false;
    }

    return true;
}

bool SkarnessHost::CreatePipe( std::string& outReason )
{
    LocalSecurityDescriptor descriptor;

    if ( !descriptor.BuildForCurrentUser() )
    {
        outReason = "could not restrict the Skarness named pipe to the current user";
        return false;
    }

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof( security );
    security.lpSecurityDescriptor = descriptor.Get();
    const HANDLE pipe = CreateNamedPipeA( m_pipeName.c_str(), PIPE_ACCESS_DUPLEX,
                                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 64u * 1024u, 64u * 1024u, 0,
                                          &security );

    if ( pipe == INVALID_HANDLE_VALUE )
    {
        outReason = "could not create the Skarness named pipe";
        return false;
    }

    m_pipe = pipe;
    return true;
}

void SkarnessHost::Shutdown( const char* status )
{
    if ( !m_pipe && !m_enabled )
    {
        return;
    }

    if ( m_enabled )
    {
        (void)WriteManifest( status ? status : "closed" );
    }

    const HANDLE pipe = NativePipe( m_pipe );

    if ( pipe && pipe != INVALID_HANDLE_VALUE )
    {
        if ( m_connected )
        {
            FlushFileBuffers( pipe );
            DisconnectNamedPipe( pipe );
        }

        CloseHandle( pipe );
    }

    m_trace.close();
    m_pipe = nullptr;
    m_connected = false;
    m_enabled = false;
}

void SkarnessHost::AcceptClient()
{
    if ( m_connected || !m_pipe )
    {
        return;
    }

    const HANDLE pipe = NativePipe( m_pipe );
    const BOOL accepted = ConnectNamedPipe( pipe, nullptr );
    const DWORD error = accepted ? ERROR_SUCCESS : GetLastError();

    if ( accepted || error == ERROR_PIPE_CONNECTED )
    {
        m_connected = true;
        (void)WriteManifest( "connected" );
    }
}

void SkarnessHost::DisconnectClient()
{
    if ( m_pipe && m_connected )
    {
        DisconnectNamedPipe( NativePipe( m_pipe ) );
    }

    m_connected = false;
    m_receiveBuffer.clear();
    m_paused = true;
    m_stepFramesRemaining = 0;
    m_renderFramesRemaining = 0;
    m_stepRequestId.clear();
    m_renderStepRequestId.clear();
    m_untilRequestId.clear();
    m_untilCondition.clear();
    m_stopRequestId.clear();
    m_stepCompletesAfterFrame = false;
    m_untilFramesRemaining = 0;
    m_untilStepsPhysics = false;
    m_stopAfterFrame = false;
    m_stopRequested = false;
    (void)WriteManifest( "waiting" );
}

void SkarnessHost::PollCommands()
{
    if ( !m_enabled )
    {
        return;
    }

    AcceptClient();

    if ( !m_connected )
    {
        return;
    }

    DWORD available = 0;

    if ( !PeekNamedPipe( NativePipe( m_pipe ), nullptr, 0, nullptr, &available, nullptr ) )
    {
        DisconnectClient();
        return;
    }

    while ( available > 0 )
    {
        std::array<char, 4096> chunk = {};
        const DWORD requested = (std::min)( available, static_cast<DWORD>( chunk.size() ) );
        DWORD read = 0;

        if ( !ReadFile( NativePipe( m_pipe ), chunk.data(), requested, &read, nullptr ) || read == 0 )
        {
            DisconnectClient();
            return;
        }

        m_receiveBuffer.append( chunk.data(), read );

        if ( m_receiveBuffer.size() > SKARNESS_RECEIVE_CAPACITY )
        {
            DisconnectClient();
            return;
        }

        if ( !PeekNamedPipe( NativePipe( m_pipe ), nullptr, 0, nullptr, &available, nullptr ) )
        {
            DisconnectClient();
            return;
        }
    }

    std::size_t newline = 0;

    while ( ( newline = m_receiveBuffer.find( '\n' ) ) != std::string::npos )
    {
        std::string line = m_receiveBuffer.substr( 0, newline );
        m_receiveBuffer.erase( 0, newline + 1u );

        if ( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }

        if ( !line.empty() )
        {
            ConsumeRequestLine( line );
        }
    }
}

bool SkarnessHost::RememberRequestId( const std::string& requestId )
{
    if ( std::find( m_recentRequestIds.begin(), m_recentRequestIds.end(), requestId ) != m_recentRequestIds.end() )
    {
        return false;
    }

    m_recentRequestIds.push_back( requestId );

    if ( m_recentRequestIds.size() > SKARNESS_REQUEST_HISTORY_CAPACITY )
    {
        m_recentRequestIds.pop_front();
    }

    return true;
}

void SkarnessHost::ConsumeRequestLine( const std::string& line )
{
    const Json request = Json::parse( line, nullptr, false );

    if ( request.is_discarded() || !request.is_object() )
    {
        SendLifecycle( "", "rejected", "invalid JSON request" );
        return;
    }

    const bool envelopeValid = request.contains( "schemaVersion" ) && request["schemaVersion"].is_number_unsigned() &&
                               request.contains( "sessionToken" ) && request["sessionToken"].is_string() &&
                               request.contains( "requestId" ) && request["requestId"].is_string() &&
                               request.contains( "command" ) && request["command"].is_string();

    if ( !envelopeValid )
    {
        SendLifecycle( "", "rejected", "request envelope types are invalid" );
        return;
    }

    const uint32_t schemaVersion = request["schemaVersion"].get<uint32_t>();
    const std::string sessionToken = request["sessionToken"].get<std::string>();
    const std::string requestId = request["requestId"].get<std::string>();
    const std::string commandName = request["command"].get<std::string>();
    const Json arguments = request.contains( "arguments" ) && request["arguments"].is_object() ? request["arguments"]
                                                                                               : Json::object();

    if ( schemaVersion != SKARNESS_SCHEMA_VERSION || sessionToken != m_sessionToken || requestId.empty() ||
         commandName.empty() )
    {
        SendLifecycle( requestId, "rejected", "schema, token, requestId, or command is invalid" );
        return;
    }

    if ( !RememberRequestId( requestId ) )
    {
        const auto completed = std::find_if( m_completedRequests.begin(), m_completedRequests.end(),
                                             [&requestId]( const CompletedRequest& result )
                                             { return result.requestId == requestId; } );

        if ( completed != m_completedRequests.end() )
        {
            (void)SendJsonLine( completed->response );
        }
        else
        {
            SendLifecycle( requestId, "duplicate", "the original request is still pending" );
        }
        return;
    }

    if ( commandName == "capabilities.get" )
    {
        SendCapabilities( requestId );
        return;
    }

    if ( commandName == "run.pause" || commandName == "run.resume" )
    {
        if ( !m_stepRequestId.empty() )
        {
            SendLifecycle( m_stepRequestId, "rejected", "cancelled by run pause/resume" );
        }
        if ( !m_renderStepRequestId.empty() )
        {
            SendLifecycle( m_renderStepRequestId, "rejected", "cancelled by run pause/resume" );
        }
        if ( !m_untilRequestId.empty() )
        {
            SendLifecycle( m_untilRequestId, "rejected", "cancelled by run pause/resume" );
        }

        m_paused = commandName == "run.pause";
        m_stepFramesRemaining = 0;
        m_renderFramesRemaining = 0;
        m_stepRequestId.clear();
        m_renderStepRequestId.clear();
        m_untilRequestId.clear();
        m_untilCondition.clear();
        m_stepCompletesAfterFrame = false;
        m_untilFramesRemaining = 0;
        m_untilStepsPhysics = false;
        SendLifecycle( requestId, "accepted" );
        CompleteCommand( requestId, true );
        return;
    }

    if ( commandName == "state.subscribe" )
    {
        SendLifecycle( requestId, "accepted" );
        SendLifecycle( requestId, "applied" );
        return;
    }

    if ( commandName == "session.stop" )
    {
        if ( m_stopAfterFrame )
        {
            SendLifecycle( requestId, "rejected", "session stop is already pending" );
            return;
        }

        m_stopRequestId = requestId;
        m_stopAfterFrame = true;
        SendLifecycle( requestId, "accepted" );
        return;
    }

    if ( commandName == "run.step" || commandName == "run.step_frames" )
    {
        int count = 1;

        if ( arguments.contains( "count" ) && ( !ReadInteger( arguments, "count", count ) || count < 1 || count > 100000 ) )
        {
            SendLifecycle( requestId, "rejected", "count must be an integer in 1..100000" );
            return;
        }

        const bool physicsTicks = commandName == "run.step";

        if ( physicsTicks ? !m_stepRequestId.empty() : !m_renderStepRequestId.empty() )
        {
            SendLifecycle( requestId, "rejected", "a step command of this kind is already active" );
            return;
        }

        m_paused = true;

        if ( physicsTicks )
        {
            m_stepFramesRemaining = static_cast<uint32_t>( count );
            m_stepRequestId = requestId;
        }
        else
        {
            m_renderFramesRemaining = static_cast<uint32_t>( count );
            m_renderStepRequestId = requestId;
        }

        SendLifecycle( requestId, "accepted" );
        return;
    }

    if ( commandName == "run.until" )
    {
        std::string condition;
        int maximum = 0;
        const bool hasMaxFrames = arguments.contains( "maxFrames" );
        const char* maximumName = hasMaxFrames ? "maxFrames" : "maxTicks";

        if ( !m_untilRequestId.empty() || !m_stepRequestId.empty() || !m_renderStepRequestId.empty() )
        {
            SendLifecycle( requestId, "rejected", "another bounded run command is active" );
            return;
        }

        if ( !ReadString( arguments, "condition", condition ) ||
             ( condition != "prediction.complete" && condition != "prediction.geometry" &&
               condition != "prediction.submitted" && condition != "prediction.rendered" &&
               condition != "prediction.causal_rendered" ) ||
             !ReadInteger( arguments, maximumName, maximum ) || maximum < 1 || maximum > 100000 )
        {
            SendLifecycle( requestId, "rejected", "condition or maxFrames/maxTicks is invalid" );
            return;
        }

        m_paused = true;
        m_untilRequestId = requestId;
        m_untilCondition = std::move( condition );
        m_untilFramesRemaining = static_cast<uint32_t>( maximum );
        m_untilStepsPhysics = !hasMaxFrames;
        SendLifecycle( requestId, "accepted" );
        return;
    }

    SkarnessCommand command;
    command.requestId = requestId;
    bool valid = true;

    if ( commandName == "capture.screenshot" )
    {
        command.type = SkarnessCommandType::CaptureScreenshot;
        valid = ReadString( arguments, "path", command.text );
    }
    else if ( commandName == "replay.set_recording_enabled" )
    {
        command.type = SkarnessCommandType::ReplaySetRecordingEnabled;
        valid = ReadBoolean( arguments, "enabled", command.enabled );
    }
    else if ( commandName == "replay.jump_to_start" )
    {
        command.type = SkarnessCommandType::ReplayJumpToStart;
    }
    else if ( commandName == "replay.jump_to_end" )
    {
        command.type = SkarnessCommandType::ReplayJumpToEnd;
    }
    else if ( commandName == "replay.set_playback_paused" )
    {
        command.type = SkarnessCommandType::ReplaySetPlaybackPaused;
        valid = ReadBoolean( arguments, "paused", command.enabled );
    }
    else if ( commandName == "replay.step_backward" )
    {
        command.type = SkarnessCommandType::ReplayStepBackward;
    }
    else if ( commandName == "replay.step_forward" )
    {
        command.type = SkarnessCommandType::ReplayStepForward;
    }
    else if ( commandName == "replay.set_reveal_speed" )
    {
        command.type = SkarnessCommandType::ReplaySetRevealSpeed;
        valid = ReadNumber( arguments, "rate", command.number ) && command.number > 0.0;
    }
    else if ( commandName == "replay.scrub" )
    {
        command.type = SkarnessCommandType::ReplayScrub;
        valid = ReadNumber( arguments, "normalized", command.number ) && command.number >= 0.0 && command.number <= 1.0;
    }
    else if ( commandName == "replay.set_prediction_enabled" )
    {
        command.type = SkarnessCommandType::ReplaySetPredictionEnabled;
        valid = ReadBoolean( arguments, "enabled", command.enabled );
    }
    else if ( commandName == "replay.set_prediction_detail" )
    {
        command.type = SkarnessCommandType::ReplaySetPredictionDetailMode;
        valid = ReadBoolean( arguments, "highDetail", command.enabled );
    }
    else if ( commandName == "replay.set_prediction_horizon" )
    {
        command.type = SkarnessCommandType::ReplaySetPredictionHorizon;
        valid = ReadNumber( arguments, "seconds", command.number ) && command.number >= 1.0 && command.number <= 120.0;
    }
    else if ( commandName == "replay.set_velocity_edit_enabled" )
    {
        command.type = SkarnessCommandType::ReplaySetVelocityEditEnabled;
        valid = ReadBoolean( arguments, "enabled", command.enabled );
    }
    else if ( commandName == "replay.set_ragdoll_visuals_enabled" )
    {
        command.type = SkarnessCommandType::ReplaySetRagdollVisualsEnabled;
        valid = ReadBoolean( arguments, "enabled", command.enabled );
    }
    else if ( commandName == "replay.set_past_path_visible" )
    {
        command.type = SkarnessCommandType::ReplaySetPastPathVisible;
        valid = ReadBoolean( arguments, "visible", command.enabled );
    }
    else if ( commandName == "replay.restore_branch" )
    {
        command.type = SkarnessCommandType::ReplayRestoreBranch;
    }
    else if ( commandName == "replay.save" )
    {
        command.type = SkarnessCommandType::ReplaySave;
        valid = !arguments.contains( "path" ) || ReadString( arguments, "path", command.text );
    }
    else if ( commandName == "replay.load" )
    {
        command.type = SkarnessCommandType::ReplayLoad;
        valid = ReadString( arguments, "path", command.text );
    }
    else if ( commandName == "replay.return_to_live" )
    {
        command.type = SkarnessCommandType::ReplayReturnToLive;
    }
    else if ( commandName == "replay.select_cause_row" )
    {
        command.type = SkarnessCommandType::ReplaySelectCauseRow;
        valid = ReadInteger( arguments, "row", command.integer ) && command.integer >= 0;
    }
    else if ( commandName == "prediction.select_target" || commandName == "replay.set_path_target" )
    {
        command.type = SkarnessCommandType::PredictionSelectTarget;
        const bool hasName = arguments.contains( "name" );
        const bool hasId = arguments.contains( "sceneObjectId" );
        valid = hasName != hasId && ( hasName ? ReadString( arguments, "name", command.text )
                                              : ReadUnsignedInteger( arguments, "sceneObjectId", command.unsignedInteger ) );
    }
    else
    {
        SendLifecycle( requestId, "rejected", "unknown command" );
        return;
    }

    if ( !valid )
    {
        SendLifecycle( requestId, "rejected", "missing or invalid command arguments" );
        return;
    }

    if ( m_commands.size() >= SKARNESS_COMMAND_CAPACITY )
    {
        SendLifecycle( requestId, "rejected", "command queue is full" );
        return;
    }

    m_commands.push_back( std::move( command ) );
    SendLifecycle( requestId, "accepted" );
}

bool SkarnessHost::PopCommand( SkarnessCommand& outCommand )
{
    if ( m_commands.empty() )
    {
        return false;
    }

    outCommand = std::move( m_commands.front() );
    m_commands.pop_front();
    return true;
}

void SkarnessHost::CompleteCommand( const std::string& requestId, bool applied, const char* reason )
{
    PendingCompletion completion;
    completion.requestId = requestId;
    completion.applied = applied;

    if ( reason )
    {
        completion.reason = reason;
    }

    m_pendingCompletions.push_back( std::move( completion ) );
}

uint64_t SkarnessHost::BeginCapture( const std::string& requestId )
{
    const uint64_t token = m_nextCaptureToken++;
    m_pendingCaptures.push_back( PendingCapture { token, requestId } );
    return token;
}

void SkarnessHost::CompleteCapture( uint64_t token, bool applied, const char* reason )
{
    const auto found = std::find_if( m_pendingCaptures.begin(), m_pendingCaptures.end(),
                                     [token]( const PendingCapture& capture ) { return capture.token == token; } );

    if ( found == m_pendingCaptures.end() )
    {
        return;
    }

    CompleteCommand( found->requestId, applied, reason );
    m_pendingCaptures.erase( found );
}

SkarnessProceedPolicy SkarnessHost::TakeProceedPolicy()
{
    SkarnessProceedPolicy policy;

    if ( !m_enabled )
    {
        return policy;
    }

    const bool step = m_stepFramesRemaining > 0 || ( !m_untilRequestId.empty() && m_untilStepsPhysics );

    if ( m_stepFramesRemaining > 0 )
    {
        --m_stepFramesRemaining;

        if ( m_stepFramesRemaining == 0 )
        {
            m_stepCompletesAfterFrame = true;
        }
    }

    if ( m_paused )
    {
        policy.pauseLocked = true;
        policy.stepRequested = step;
    }

    return policy;
}

bool SkarnessHost::SendJsonLine( const std::string& line )
{
    if ( !m_connected )
    {
        return false;
    }

    const std::string framed = line + "\n";
    DWORD written = 0;

    if ( !WriteFile( NativePipe( m_pipe ), framed.data(), static_cast<DWORD>( framed.size() ), &written, nullptr ) ||
         written != framed.size() )
    {
        DisconnectClient();
        return false;
    }

    return true;
}

void SkarnessHost::SendLifecycle( const std::string& requestId, const char* status, const char* reason )
{
    Json response = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                      { "sequence", ++m_sequence },
                      { "kind", "command" },
                      { "requestId", requestId },
                      { "status", status ? status : "rejected" } };

    if ( reason && reason[0] != '\0' )
    {
        response["reason"] = reason;
    }

    const std::string line = response.dump();
    m_trace << line << '\n';
    m_trace.flush();

    if ( status && ( std::strcmp( status, "applied" ) == 0 || std::strcmp( status, "rejected" ) == 0 ) &&
         !requestId.empty() )
    {
        const auto existing = std::find_if( m_completedRequests.begin(), m_completedRequests.end(),
                                            [&requestId]( const CompletedRequest& result )
                                            { return result.requestId == requestId; } );

        if ( existing != m_completedRequests.end() )
        {
            existing->response = line;
        }
        else
        {
            m_completedRequests.push_back( CompletedRequest { requestId, line } );
        }

        if ( m_completedRequests.size() > SKARNESS_REQUEST_HISTORY_CAPACITY )
        {
            m_completedRequests.pop_front();
        }
    }

    (void)SendJsonLine( line );
}

void SkarnessHost::SendCapabilities( const std::string& requestId )
{
    static const std::array<const char*, 30> commands = { "capabilities.get",
                                                          "session.stop",
                                                          "capture.screenshot",
                                                          "run.pause",
                                                          "run.resume",
                                                          "run.step",
                                                          "run.step_frames",
                                                          "run.until",
                                                          "replay.set_recording_enabled",
                                                          "replay.jump_to_start",
                                                          "replay.jump_to_end",
                                                          "replay.set_playback_paused",
                                                          "replay.step_backward",
                                                          "replay.step_forward",
                                                          "replay.set_reveal_speed",
                                                          "replay.scrub",
                                                          "replay.set_prediction_enabled",
                                                          "replay.set_prediction_detail",
                                                          "replay.set_prediction_horizon",
                                                          "replay.set_velocity_edit_enabled",
                                                          "replay.set_ragdoll_visuals_enabled",
                                                          "replay.set_past_path_visible",
                                                          "replay.restore_branch",
                                                          "replay.save",
                                                          "replay.load",
                                                          "replay.return_to_live",
                                                          "replay.select_cause_row",
                                                          "prediction.select_target",
                                                          "replay.set_path_target",
                                                          "state.subscribe" };
    Json response = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                      { "sequence", ++m_sequence },
                      { "kind", "capabilities" },
                      { "requestId", requestId },
                      { "status", "applied" },
                      { "commands", commands },
                      { "topics", { "replay.state" } } };
    const std::string line = response.dump();
    m_trace << line << '\n';
    m_trace.flush();
    m_completedRequests.push_back( CompletedRequest { requestId, line } );

    if ( m_completedRequests.size() > SKARNESS_REQUEST_HISTORY_CAPACITY )
    {
        m_completedRequests.pop_front();
    }

    (void)SendJsonLine( line );
}

void SkarnessHost::PublishFrameState( const SkarnessFrameState& state )
{
    if ( !m_enabled )
    {
        return;
    }

    Json payload = { { "replayCaptureEnabled", state.replayCaptureEnabled },
                     { "replayScrubPaused", state.replayScrubPaused },
                     { "replayPlaybackPaused", state.replayPlaybackPaused },
                     { "predictionEnabled", state.predictionEnabled },
                     { "predictionBuilding", state.predictionBuilding },
                     { "predictionComplete", state.predictionComplete },
                     { "predictionHighDetail", state.predictionHighDetail },
                     { "velocityEditEnabled", state.velocityEditEnabled },
                     { "ragdollVisualsEnabled", state.ragdollVisualsEnabled },
                     { "pastPathVisible", state.pastPathVisible },
                     { "hasPathTarget", state.hasPathTarget },
                     { "pathTargetId", state.pathTargetId },
                     { "pathTargetModelRow", state.pathTargetModelRow },
                     { "predictionHorizonSeconds", state.predictionHorizonSeconds },
                     { "predictionRevealProgress", state.predictionRevealProgress },
                     { "predictionGeneration", state.predictionGeneration },
                     { "publishedPredictionTargetId", state.publishedPredictionTargetId },
                     { "publishedPredictionFrames", state.publishedPredictionFrames },
                     { "trajectoryRecordCount", state.trajectoryRecordCount },
                     { "selectedFutureRootPointCount", state.selectedFutureRootPointCount },
                     { "contactChildIncomingCount", state.contactChildIncomingCount },
                     { "contactChildOutgoingCount", state.contactChildOutgoingCount },
                     { "childOutgoingPreEntryPointCount", state.childOutgoingPreEntryPointCount },
                     { "retainedEntryMarkerCount", state.retainedEntryMarkerCount },
                     { "retainedEndMarkerCount", state.retainedEndMarkerCount },
                     { "drawnCollisionWireframeCount", state.drawnCollisionWireframeCount },
                     { "drawnEndingWireframeCount", state.drawnEndingWireframeCount },
                     { "collisionWireframePathMismatchCount", state.collisionWireframePathMismatchCount },
                     { "endingWireframePathMismatchCount", state.endingWireframePathMismatchCount },
                     { "futureNodeCount", state.futureNodeCount },
                     { "retainedLineFloatCount", state.retainedLineFloatCount },
                     { "retainedRibbonVertexFloatCount", state.retainedRibbonVertexFloatCount },
                     { "retainedPathGeometrySaturated", state.retainedPathGeometrySaturated },
                     { "visualPacketHasGeometry", state.visualPacketHasGeometry },
                     { "trajectorySubmitted", state.trajectorySubmitted },
                     { "submittedSegmentCount", state.submittedSegmentCount },
                     { "submittedVertexCount", state.submittedVertexCount },
                     { "submittedFutureTreeReady", state.submittedFutureTreeReady } };
    Json event = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                   { "sequence", ++m_sequence },
                   { "kind", "state" },
                   { "topic", "replay.state" },
                   { "renderFrame", ++m_renderFrame },
                   { "sceneGeneration", state.sceneGeneration },
                   { "sceneFrame", state.sceneFrame },
                   { "simulationSeconds", state.simulationSeconds },
                   { "paused", state.paused },
                   { "payload", std::move( payload ) } };
    const std::string line = event.dump();
    m_trace << line << '\n';
    m_trace.flush();
    (void)SendJsonLine( line );

    if ( m_stepCompletesAfterFrame )
    {
        SendLifecycle( m_stepRequestId, "applied" );
        m_stepRequestId.clear();
        m_stepCompletesAfterFrame = false;
    }

    if ( m_renderFramesRemaining > 0 && --m_renderFramesRemaining == 0 )
    {
        SendLifecycle( m_renderStepRequestId, "applied" );
        m_renderStepRequestId.clear();
    }

    if ( !m_untilRequestId.empty() )
    {
        if ( UntilConditionMet( state ) )
        {
            SendLifecycle( m_untilRequestId, "applied" );
            m_untilRequestId.clear();
            m_untilCondition.clear();
            m_untilFramesRemaining = 0;
            m_untilStepsPhysics = false;
        }
        else if ( m_untilFramesRemaining > 0 && --m_untilFramesRemaining == 0 )
        {
            const std::string reason = UntilTimeoutReason( state );
            SendLifecycle( m_untilRequestId, "rejected", reason.c_str() );
            m_untilRequestId.clear();
            m_untilCondition.clear();
            m_untilStepsPhysics = false;
        }
    }

    while ( !m_pendingCompletions.empty() )
    {
        const PendingCompletion& completion = m_pendingCompletions.front();
        SendLifecycle( completion.requestId, completion.applied ? "applied" : "rejected",
                       completion.reason.empty() ? nullptr : completion.reason.c_str() );
        m_pendingCompletions.pop_front();
    }

    if ( m_stopAfterFrame )
    {
        SendLifecycle( m_stopRequestId, "applied" );
        m_stopRequestId.clear();
        m_stopAfterFrame = false;
        m_stopRequested = true;
    }
}

bool SkarnessHost::TakeStopRequested() noexcept
{
    const bool requested = m_stopRequested;
    m_stopRequested = false;
    return requested;
}

bool SkarnessHost::BeginPhysicsSceneGeneration( uint64_t generation ) noexcept
{
    if ( !m_enabled || m_physicsSceneGeneration == generation )
    {
        return false;
    }

    m_physicsSceneGeneration = generation;
    return true;
}

const char* SkarnessHost::PhysicsTracePath() const noexcept
{
    return m_physicsTracePathString.c_str();
}

const char* SkarnessHost::RunId() const noexcept
{
    return m_runId.c_str();
}

bool SkarnessHost::UntilConditionMet( const SkarnessFrameState& state ) const noexcept
{
    const bool selectedTargetPublished = state.hasPathTarget && state.pathTargetId != 0u &&
                                         state.pathTargetId == state.publishedPredictionTargetId;

    if ( m_untilCondition == "prediction.complete" )
    {
        return state.predictionEnabled && selectedTargetPublished && state.predictionComplete && !state.predictionBuilding;
    }
    if ( m_untilCondition == "prediction.geometry" )
    {
        return selectedTargetPublished && state.trajectoryRecordCount > 0 && state.visualPacketHasGeometry;
    }
    if ( m_untilCondition == "prediction.submitted" )
    {
        return selectedTargetPublished && state.trajectorySubmitted && state.submittedSegmentCount > 0 &&
               state.submittedVertexCount > 0;
    }
    if ( m_untilCondition == "prediction.rendered" )
    {
        return state.predictionEnabled && selectedTargetPublished && state.predictionComplete &&
               state.publishedPredictionFrames >= 2 && state.trajectoryRecordCount > 0 && state.visualPacketHasGeometry &&
               state.trajectorySubmitted && state.submittedSegmentCount > 0 && state.submittedVertexCount > 0 &&
               state.submittedFutureTreeReady;
    }
    if ( m_untilCondition == "prediction.causal_rendered" )
    {
        return state.predictionEnabled && selectedTargetPublished && state.predictionComplete &&
               state.selectedFutureRootPointCount >= 2 && state.contactChildIncomingCount > 0 &&
               state.contactChildOutgoingCount > 0 && state.childOutgoingPreEntryPointCount == 0 &&
               state.retainedEntryMarkerCount > 0 && state.retainedEndMarkerCount > 0 &&
               state.drawnCollisionWireframeCount == state.retainedEntryMarkerCount &&
               state.drawnEndingWireframeCount == state.retainedEndMarkerCount &&
               state.collisionWireframePathMismatchCount == 0 && state.endingWireframePathMismatchCount == 0 &&
               state.trajectorySubmitted &&
               state.submittedSegmentCount >= 32 && state.submittedVertexCount >= state.submittedSegmentCount * 6 &&
               state.submittedFutureTreeReady;
    }

    return false;
}

std::string SkarnessHost::UntilTimeoutReason( const SkarnessFrameState& state ) const
{
    std::ostringstream reason;
    reason << "condition '" << m_untilCondition << "' timed out: enabled=" << state.predictionEnabled
           << " target=" << state.hasPathTarget << " targetId=" << state.pathTargetId
           << " publishedTargetId=" << state.publishedPredictionTargetId << " building=" << state.predictionBuilding
           << " complete=" << state.predictionComplete << " frames=" << state.publishedPredictionFrames
           << " trajectories=" << state.trajectoryRecordCount << " geometry=" << state.visualPacketHasGeometry
           << " rootPoints=" << state.selectedFutureRootPointCount << " childIncoming=" << state.contactChildIncomingCount
           << " childOutgoing=" << state.contactChildOutgoingCount
           << " childOutgoingPreEntryPoints=" << state.childOutgoingPreEntryPointCount
           << " entryMarkers=" << state.retainedEntryMarkerCount << " endMarkers=" << state.retainedEndMarkerCount
           << " collisionWireframes=" << state.drawnCollisionWireframeCount
           << " endingWireframes=" << state.drawnEndingWireframeCount
           << " collisionPathMismatches=" << state.collisionWireframePathMismatchCount
           << " endingPathMismatches=" << state.endingWireframePathMismatchCount
           << " pathSaturated=" << state.retainedPathGeometrySaturated
           << " submitted=" << state.trajectorySubmitted << " segments=" << state.submittedSegmentCount
           << " vertices=" << state.submittedVertexCount << " futureTreeReady=" << state.submittedFutureTreeReady;
    return reason.str();
}

bool SkarnessHost::WriteManifest( const char* status )
{
    Json manifest = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                      { "processId", GetCurrentProcessId() },
                      { "pipe", m_pipeName },
                      { "sessionToken", m_sessionToken },
                      { "stateTrace", m_tracePath.string() },
                      { "physicsTrace", m_physicsTracePath.string() },
                      { "status", status ? status : "unknown" } };
    const std::filesystem::path partial = m_manifestPath.string() + ".partial";
    {
        std::ofstream output( partial, std::ios::binary | std::ios::out | std::ios::trunc );

        if ( !output )
        {
            return false;
        }

        output << manifest.dump( 2 ) << '\n';
    }

    return MoveFileExA( partial.string().c_str(), m_manifestPath.string().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != FALSE;
}
} // namespace SkullbonezCore::Runtime

#endif
