#include "SkarnessHost.h"
#include "SkarnessStateSerialization.h"

#if defined( SKULLBONEZ_SKARNESS )

#include "../../../ThirdPtySource/nlohmann/json.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
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

struct StateSubscription
{
    std::array<bool, SKARNESS_STATE_TOPICS.size()> topics = {};
    SkarnessStateDetail detail = SkarnessStateDetail::Normal;
};

bool SelectStateTopic( const std::string& name, StateSubscription& subscription )
{
    if ( name == "*" )
    {
        subscription.topics.fill( true );
        return true;
    }

    if ( name == "scene" || name == "scene.state" )
    {
        subscription.topics[1] = true;
        subscription.topics[2] = true;
        subscription.topics[16] = true;
        return true;
    }

    if ( name == "replay" || name == "replay.state" )
    {
        for ( std::size_t index = 6; index < subscription.topics.size(); ++index )
        {
            subscription.topics[index] = true;
        }
        return true;
    }

    for ( std::size_t index = 0; index < SKARNESS_STATE_TOPICS.size(); ++index )
    {
        if ( name == SKARNESS_STATE_TOPICS[index].name )
        {
            subscription.topics[index] = true;
            return true;
        }
    }
    return false;
}

bool ReadStateSubscription( const Json& arguments, StateSubscription& out )
{
    if ( !arguments.contains( "topics" ) || !arguments["topics"].is_array() )
    {
        return false;
    }

    for ( const Json& topic : arguments["topics"] )
    {
        if ( !topic.is_string() )
        {
            return false;
        }

        if ( !SelectStateTopic( topic.get<std::string>(), out ) )
        {
            return false;
        }
    }

    if ( arguments.contains( "detail" ) )
    {
        if ( !arguments["detail"].is_string() )
        {
            return false;
        }
        const std::string detail = arguments["detail"].get<std::string>();
        if ( detail == "summary" )
        {
            out.detail = SkarnessStateDetail::Summary;
        }
        else if ( detail == "normal" )
        {
            out.detail = SkarnessStateDetail::Normal;
        }
        else if ( detail == "full" )
        {
            out.detail = SkarnessStateDetail::Full;
        }
        else
        {
            return false;
        }
    }

    return true;
}

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
    return std::isfinite( out );
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

bool ReadUnsignedIntegerIncludingZero( const Json& arguments, const char* name, uint64_t& out )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_number_unsigned() )
    {
        return false;
    }

    out = found->get<uint64_t>();
    return true;
}

bool ReadVector3( const Json& arguments, const char* name, double& x, double& y, double& z )
{
    const auto found = arguments.find( name );

    if ( found == arguments.end() || !found->is_array() || found->size() != 3u )
    {
        return false;
    }

    for ( const Json& component : *found )
    {
        if ( !component.is_number() )
        {
            return false;
        }
    }

    x = ( *found )[0].get<double>();
    y = ( *found )[1].get<double>();
    z = ( *found )[2].get<double>();
    return std::isfinite( x ) && std::isfinite( y ) && std::isfinite( z );
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

enum class CommandParseStatus : uint8_t
{
    Unknown,
    Invalid,
    Valid
};

struct NamedCommand
{
    const char* name;
    SkarnessCommandType type;
};

CommandParseStatus ParseBasicCommand( const std::string& name, const Json& arguments, SkarnessCommand& command )
{
    static constexpr std::array noArgumentCommands = {
        NamedCommand { "scene.reset", SkarnessCommandType::SceneReset },
        NamedCommand { "scene.load_demo", SkarnessCommandType::SceneLoadDemo },
        NamedCommand { "scene.object.list", SkarnessCommandType::SceneObjectList },
        NamedCommand { "replay.jump_to_start", SkarnessCommandType::ReplayJumpToStart },
        NamedCommand { "replay.jump_to_end", SkarnessCommandType::ReplayJumpToEnd },
        NamedCommand { "replay.step_backward", SkarnessCommandType::ReplayStepBackward },
        NamedCommand { "replay.step_forward", SkarnessCommandType::ReplayStepForward },
        NamedCommand { "replay.velocity_commit", SkarnessCommandType::ReplayVelocityCommit },
        NamedCommand { "replay.velocity_cancel", SkarnessCommandType::ReplayVelocityCancel },
        NamedCommand { "prediction.reveal_reset", SkarnessCommandType::PredictionRevealReset },
        NamedCommand { "replay.restore_branch", SkarnessCommandType::ReplayRestoreBranch },
        NamedCommand { "replay.return_to_live", SkarnessCommandType::ReplayReturnToLive },
        NamedCommand { "replay.return_from_cause", SkarnessCommandType::ReplayReturnFromCause },
        NamedCommand { "replay.copy_cause_record", SkarnessCommandType::ReplayCopyCauseRecord },
        NamedCommand { "replay.trip_plan", SkarnessCommandType::ReplayTripPlan },
        NamedCommand { "replay.trip_commit", SkarnessCommandType::ReplayTripCommit },
        NamedCommand { "replay.trip_cancel", SkarnessCommandType::ReplayTripCancel },
        NamedCommand { "prediction.forecast_start", SkarnessCommandType::PredictionForecastStart },
        NamedCommand { "prediction.forecast_reset", SkarnessCommandType::PredictionForecastReset },
        NamedCommand { "prediction.forecast_stop", SkarnessCommandType::PredictionForecastStop },
    };
    struct BooleanCommand : NamedCommand
    {
        const char* argument;
    };
    static constexpr std::array booleanCommands = {
        BooleanCommand { { "replay.set_recording_enabled", SkarnessCommandType::ReplaySetRecordingEnabled }, "enabled" },
        BooleanCommand { { "replay.set_playback_paused", SkarnessCommandType::ReplaySetPlaybackPaused }, "paused" },
        BooleanCommand { { "replay.set_prediction_enabled", SkarnessCommandType::ReplaySetPredictionEnabled }, "enabled" },
        BooleanCommand { { "replay.set_prediction_detail", SkarnessCommandType::ReplaySetPredictionDetailMode },
                         "highDetail" },
        BooleanCommand { { "replay.set_velocity_edit_enabled", SkarnessCommandType::ReplaySetVelocityEditEnabled },
                         "enabled" },
        BooleanCommand { { "replay.set_ragdoll_visuals_enabled", SkarnessCommandType::ReplaySetRagdollVisualsEnabled },
                         "enabled" },
        BooleanCommand { { "replay.set_past_path_visible", SkarnessCommandType::ReplaySetPastPathVisible }, "visible" },
        BooleanCommand { { "replay.set_guide_arcs_enabled", SkarnessCommandType::ReplaySetGuideArcsEnabled }, "enabled" },
        BooleanCommand { { "replay.set_cause_inspector_open", SkarnessCommandType::ReplaySetCauseInspectorOpen }, "open" },
        BooleanCommand { { "replay.set_porkchop_visible", SkarnessCommandType::ReplaySetPorkchopVisible }, "visible" },
    };

    for ( const NamedCommand& entry : noArgumentCommands )
    {
        if ( name == entry.name )
        {
            command.type = entry.type;
            return CommandParseStatus::Valid;
        }
    }

    for ( const BooleanCommand& entry : booleanCommands )
    {
        if ( name == entry.name )
        {
            command.type = entry.type;
            return ReadBoolean( arguments, entry.argument, command.enabled ) ? CommandParseStatus::Valid
                                                                             : CommandParseStatus::Invalid;
        }
    }

    if ( name == "replay.close_cause_detail" )
    {
        command.type = SkarnessCommandType::ReplaySetCauseInspectorOpen;
        command.enabled = false;
        return CommandParseStatus::Valid;
    }

    return CommandParseStatus::Unknown;
}

CommandParseStatus ParseNumericCommand( const std::string& name, const Json& arguments, SkarnessCommand& command )
{
    struct IntegerCommand : NamedCommand
    {
        const char* argument;
        int minimum;
        int maximum;
    };
    static constexpr std::array integerCommands = {
        IntegerCommand { { "replay.set_retention_seconds", SkarnessCommandType::ReplaySetRetentionSeconds },
                         "seconds",
                         20,
                         600 },
        IntegerCommand { { "replay.set_memory_budget_mib", SkarnessCommandType::ReplaySetMemoryBudgetMiB }, "mib", 32, 512 },
        IntegerCommand { { "prediction.reveal_advance", SkarnessCommandType::PredictionRevealAdvance },
                         "frames",
                         1,
                         100000 },
        IntegerCommand { { "replay.select_cause_row", SkarnessCommandType::ReplaySelectCauseRow }, "row", 0, INT_MAX },
        IntegerCommand { { "replay.select_porkchop_cell", SkarnessCommandType::ReplaySelectPorkchopCell }, "cell", 0, 3071 },
    };
    struct NumberCommand : NamedCommand
    {
        const char* argument;
        double minimum;
        double maximum;
        bool minimumExclusive;
    };
    static constexpr std::array numberCommands = {
        NumberCommand { { "replay.set_reveal_speed", SkarnessCommandType::ReplaySetRevealSpeed },
                        "rate",
                        0.0,
                        DBL_MAX,
                        true },
        NumberCommand { { "replay.scrub", SkarnessCommandType::ReplayScrub }, "normalized", 0.0, 1.0, false },
        NumberCommand { { "replay.set_prediction_horizon", SkarnessCommandType::ReplaySetPredictionHorizon },
                        "seconds",
                        1.0,
                        120.0,
                        false },
        NumberCommand { { "replay.set_trip_time_of_flight", SkarnessCommandType::ReplaySetTripTimeOfFlight },
                        "seconds",
                        2.0,
                        120.0,
                        false },
    };

    for ( const IntegerCommand& entry : integerCommands )
    {
        if ( name == entry.name )
        {
            command.type = entry.type;
            const bool valid = ReadInteger( arguments, entry.argument, command.integer ) &&
                               command.integer >= entry.minimum && command.integer <= entry.maximum;
            return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
        }
    }

    for ( const NumberCommand& entry : numberCommands )
    {
        if ( name == entry.name )
        {
            command.type = entry.type;
            const bool valid = ReadNumber( arguments, entry.argument, command.number ) &&
                               ( entry.minimumExclusive ? command.number > entry.minimum
                                                        : command.number >= entry.minimum ) &&
                               command.number <= entry.maximum;
            return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
        }
    }

    if ( name == "replay.seek_frame" )
    {
        command.type = SkarnessCommandType::ReplaySeekFrame;
        return ReadUnsignedIntegerIncludingZero( arguments, "frame", command.unsignedInteger ) ? CommandParseStatus::Valid
                                                                                               : CommandParseStatus::Invalid;
    }

    return CommandParseStatus::Unknown;
}

bool ReadSceneIdentity( const Json& arguments, SkarnessCommand& command )
{
    const bool hasName = arguments.contains( "name" );
    const bool hasId = arguments.contains( "sceneObjectId" );
    return hasName != hasId && ( hasName ? ReadString( arguments, "name", command.text )
                                         : ReadUnsignedInteger( arguments, "sceneObjectId", command.unsignedInteger ) );
}

CommandParseStatus ParseValueCommand( const std::string& name, const Json& arguments, SkarnessCommand& command )
{
    if ( name == "capture.screenshot" || name == "replay.save" || name == "replay.load" )
    {
        command.type = name == "capture.screenshot" ? SkarnessCommandType::CaptureScreenshot
                       : name == "replay.save"      ? SkarnessCommandType::ReplaySave
                                                    : SkarnessCommandType::ReplayLoad;
        // Invariant: the App-side replay commands carry a fixed path buffer.
        // Reject overflow at the protocol boundary instead of silently changing
        // the caller's requested file through truncation.
        const bool valid = ReadString( arguments, "path", command.text ) && command.text.size() < 260u;
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "scene.load" )
    {
        command.type = SkarnessCommandType::SceneLoad;
        const bool hasName = arguments.contains( "name" );
        const bool hasPath = arguments.contains( "path" );
        return hasName != hasPath && ReadString( arguments, hasName ? "name" : "path", command.text )
                   ? CommandParseStatus::Valid
                   : CommandParseStatus::Invalid;
    }

    if ( name == "scene.object.resolve" || name == "replay.set_intercept_target" || name == "prediction.select_target" ||
         name == "replay.set_path_target" )
    {
        command.type = name == "scene.object.resolve"          ? SkarnessCommandType::SceneObjectResolve
                       : name == "replay.set_intercept_target" ? SkarnessCommandType::ReplaySetInterceptTarget
                                                               : SkarnessCommandType::PredictionSelectTarget;
        return ReadSceneIdentity( arguments, command ) ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "scene.object.select" )
    {
        command.type = SkarnessCommandType::SceneObjectSelect;
        const bool valid = ReadString( arguments, "scope", command.secondText ) &&
                           ( command.secondText == "inspect" || command.secondText == "editor" ) &&
                           ReadSceneIdentity( arguments, command );
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "scene.object.clear_selection" )
    {
        command.type = SkarnessCommandType::SceneObjectClearSelection;
        const bool valid = ReadString( arguments, "scope", command.secondText ) &&
                           ( command.secondText == "inspect" || command.secondText == "editor" );
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    return CommandParseStatus::Unknown;
}

CommandParseStatus ParsePlanningCommand( const std::string& name, const Json& arguments, SkarnessCommand& command )
{
    if ( name == "replay.set_path_color_mode" || name == "replay.set_cause_filter" ||
         name == "replay.set_cause_inspector_tab" )
    {
        const char* argument = name == "replay.set_path_color_mode" ? "mode"
                               : name == "replay.set_cause_filter"  ? "filter"
                                                                    : "tab";
        command.type = name == "replay.set_path_color_mode" ? SkarnessCommandType::ReplaySetPathColorMode
                       : name == "replay.set_cause_filter"  ? SkarnessCommandType::ReplaySetCauseFilter
                                                            : SkarnessCommandType::ReplaySetCauseInspectorTab;
        if ( !ReadString( arguments, argument, command.text ) )
        {
            return CommandParseStatus::Invalid;
        }
        const bool valid = name == "replay.set_path_color_mode"
                               ? command.text == "lane" || command.text == "velocity" || command.text == "time" ||
                                     command.text == "object" || command.text == "causal"
                           : name == "replay.set_cause_filter"
                               ? command.text == "all" || command.text == "prediction" || command.text == "contacts"
                               : command.text == "summary" || command.text == "raw" || command.text == "iterations";
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "replay.set_cause_filter_text" )
    {
        command.type = SkarnessCommandType::ReplaySetCauseFilterText;
        const auto found = arguments.find( "text" );
        const bool valid = found != arguments.end() && found->is_string();
        command.text = valid ? found->get<std::string>() : "";
        return valid && command.text.size() < 48u ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "replay.velocity_preview" )
    {
        command.type = SkarnessCommandType::ReplayVelocityPreview;
        const bool valid = ReadVector3( arguments, "linear", command.number, command.secondNumber, command.thirdNumber ) &&
                           ReadVector3( arguments, "angular", command.fourthNumber, command.fifthNumber,
                                        command.sixthNumber );
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "replay.select_cause" )
    {
        command.type = SkarnessCommandType::ReplaySelectCause;
        const bool valid = ReadInteger( arguments, "row", command.integer ) && command.integer >= 0 &&
                           ReadUnsignedInteger( arguments, "sceneObjectId", command.unsignedInteger ) &&
                           ReadUnsignedIntegerIncludingZero( arguments, "frame", command.secondUnsignedInteger ) &&
                           ReadUnsignedIntegerIncludingZero( arguments, "generation", command.thirdUnsignedInteger ) &&
                           ReadUnsignedIntegerIncludingZero( arguments, "bankEpoch", command.fourthUnsignedInteger ) &&
                           ReadUnsignedIntegerIncludingZero( arguments, "topologyVersion", command.fifthUnsignedInteger ) &&
                           ReadUnsignedIntegerIncludingZero( arguments, "publicationVersion", command.sixthUnsignedInteger );
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    if ( name == "camera.orbit_inspection" )
    {
        command.type = SkarnessCommandType::CameraOrbitInspection;
        const bool valid = ReadNumber( arguments, "yawRadians", command.number ) &&
                           ReadNumber( arguments, "pitchRadians", command.secondNumber ) &&
                           ( !arguments.contains( "wheelDelta" ) ||
                             ReadInteger( arguments, "wheelDelta", command.integer ) ) &&
                           command.integer >= -1200 && command.integer <= 1200 &&
                           std::fabs( command.number ) <= 6.283185307 && std::fabs( command.secondNumber ) <= 6.283185307;
        return valid ? CommandParseStatus::Valid : CommandParseStatus::Invalid;
    }

    return CommandParseStatus::Unknown;
}

CommandParseStatus ParseCommand( const std::string& name, const Json& arguments, SkarnessCommand& command )
{
    for ( const auto parser : { ParseBasicCommand, ParseNumericCommand, ParseValueCommand, ParsePlanningCommand } )
    {
        const CommandParseStatus status = parser( name, arguments, command );

        if ( status != CommandParseStatus::Unknown )
        {
            return status;
        }
    }

    return CommandParseStatus::Unknown;
}
} // namespace

SkarnessHost::~SkarnessHost()
{
    Shutdown( "closed" );
}

bool SkarnessHost::Configure( const char* sessionDirectory, bool manualInput, std::string& outReason )
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
    m_manualInput = manualInput;
    m_paused = !manualInput;

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
    m_stateSubscriptions.fill( false );
    m_receiveBuffer.clear();
    // Invariant: transport loss never changes accepted work. Terminal results
    // are retained by request id and replayed after a reconnect.
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

SkarnessHost::RememberRequestResult SkarnessHost::RememberRequestId( const std::string& requestId )
{
    if ( std::find( m_recentRequestIds.begin(), m_recentRequestIds.end(), requestId ) != m_recentRequestIds.end() )
    {
        return RememberRequestResult::Duplicate;
    }

    if ( m_recentRequestIds.size() >= SKARNESS_REQUEST_HISTORY_CAPACITY )
    {
        // Invariant: an in-flight id cannot be forgotten and applied twice.
        // Reclaim the oldest completed slot, or reject new work while every
        // retained id still has an unresolved command.
        const auto evict = std::find_if( m_recentRequestIds.begin(), m_recentRequestIds.end(),
                                         [this]( const std::string& retainedId )
                                         {
                                             return std::find_if( m_completedRequests.begin(), m_completedRequests.end(),
                                                                  [&retainedId]( const CompletedRequest& result )
                                                                  { return result.requestId == retainedId; } ) !=
                                                    m_completedRequests.end();
                                         } );

        if ( evict == m_recentRequestIds.end() )
        {
            return RememberRequestResult::Full;
        }

        const auto completed = std::find_if( m_completedRequests.begin(), m_completedRequests.end(),
                                             [&evict]( const CompletedRequest& result )
                                             { return result.requestId == *evict; } );
        m_completedRequests.erase( completed );
        m_recentRequestIds.erase( evict );
    }

    m_recentRequestIds.push_back( requestId );
    return RememberRequestResult::Inserted;
}

bool SkarnessHost::AdmitRequestId( const std::string& requestId )
{
    const RememberRequestResult remember = RememberRequestId( requestId );

    if ( remember == RememberRequestResult::Inserted )
    {
        return true;
    }

    if ( remember == RememberRequestResult::Duplicate )
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
        return false;
    }

    SendLifecycle( requestId, "rejected", "request history is full", false );
    return false;
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
        SendLifecycle( requestId, "rejected", "schema, token, requestId, or command is invalid", false );
        return;
    }

    if ( !AdmitRequestId( requestId ) )
    {
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
        m_untilLimit = 0;
        m_untilFramesRemaining = 0;
        m_untilStepsPhysics = false;
        SendLifecycle( requestId, "accepted" );
        CompleteCommand( requestId, true );
        return;
    }

    if ( commandName == "state.subscribe" )
    {
        if ( !m_subscriptionRequestId.empty() )
        {
            SendLifecycle( requestId, "rejected", "a subscription snapshot is already pending" );
            return;
        }
        StateSubscription subscription;

        if ( !ReadStateSubscription( arguments, subscription ) )
        {
            SendLifecycle( requestId, "rejected", "topics must be an array of strings" );
            return;
        }

        // Invariant: subscription completion is deferred until the next
        // after-render snapshot batch is durable. A client can therefore query
        // every selected topic immediately after receiving "applied".
        m_stateSubscriptions = subscription.topics;
        m_stateDetail = subscription.detail;
        m_stateInitialized.fill( false );
        m_statePayloadHashes.fill( 0u );
        m_stateOwnerVersions.fill( 0u );
        m_stateAppendCursors.fill( 0u );
        m_stateEvictCursors.fill( 0u );
        m_subscriptionSnapshotPending = true;
        m_subscriptionRequestId = requestId;
        SendLifecycle( requestId, "accepted" );
        return;
    }

    if ( commandName == "input.set_arrows" )
    {
        bool left = false;
        bool right = false;

        if ( m_manualInput || !ReadBoolean( arguments, "left", left ) || !ReadBoolean( arguments, "right", right ) )
        {
            SendLifecycle( requestId, "rejected", "automated input and boolean left/right values are required" );
            return;
        }

        m_arrowKeysDown = static_cast<uint8_t>( ( left ? 1 : 0 ) | ( right ? 2 : 0 ) );
        SendLifecycle( requestId, "accepted" );
        CompleteCommand( requestId, true );
        return;
    }

    if ( commandName == "input.pointer_drag" )
    {
        if ( m_manualInput )
        {
            SendLifecycle( requestId, "rejected", "manual input owns the pointer" );
            return;
        }

        std::string buttonName;
        PendingPointerDrag drag;
        bool validButton = true;
        drag.requestId = requestId;
        const bool hasArguments = ReadString( arguments, "button", buttonName ) &&
                                  ReadInteger( arguments, "x", drag.clientX ) &&
                                  ReadInteger( arguments, "y", drag.clientY ) &&
                                  ReadInteger( arguments, "deltaX", drag.deltaX ) &&
                                  ReadInteger( arguments, "deltaY", drag.deltaY );
        const bool bounded = drag.clientX >= 0 && drag.clientX <= 65535 && drag.clientY >= 0 && drag.clientY <= 65535 &&
                             std::abs( drag.deltaX ) <= 150 && std::abs( drag.deltaY ) <= 150;

        if ( buttonName == "left" )
        {
            drag.button = SkarnessPointerButton::Left;
        }
        else if ( buttonName == "right" )
        {
            drag.button = SkarnessPointerButton::Right;
        }
        else if ( buttonName == "middle" )
        {
            drag.button = SkarnessPointerButton::Middle;
        }
        else
        {
            validButton = false;
        }

        if ( !hasArguments || !bounded || !validButton )
        {
            SendLifecycle( requestId, "rejected", "pointer drag arguments are invalid" );
            return;
        }

        if ( !m_pendingPointerDrag.requestId.empty() )
        {
            SendLifecycle( requestId, "rejected", "another pointer drag is pending" );
            return;
        }

        m_pendingPointerDrag = std::move( drag );
        SendLifecycle( requestId, "accepted" );
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
        const bool hasMaxTicks = arguments.contains( "maxTicks" );
        const char* maximumName = hasMaxFrames ? "maxFrames" : "maxTicks";

        if ( !m_untilRequestId.empty() || !m_stepRequestId.empty() || !m_renderStepRequestId.empty() )
        {
            SendLifecycle( requestId, "rejected", "another bounded run command is active" );
            return;
        }

        if ( hasMaxFrames == hasMaxTicks || !ReadString( arguments, "condition", condition ) ||
             ( condition != "prediction.complete" && condition != "prediction.geometry" &&
               condition != "prediction.submitted" && condition != "prediction.rendered" &&
               condition != "prediction.causal_rendered" && condition != "camera.inspection_settled" &&
               condition != "camera.main_restored" ) ||
             !ReadInteger( arguments, maximumName, maximum ) || maximum < 1 || maximum > 100000 )
        {
            SendLifecycle( requestId, "rejected", "condition or maxFrames/maxTicks is invalid" );
            return;
        }

        m_paused = true;
        m_untilRequestId = requestId;
        m_untilCondition = std::move( condition );
        m_untilLimit = static_cast<uint32_t>( maximum );
        m_untilFramesRemaining = static_cast<uint32_t>( maximum );
        m_untilStepsPhysics = !hasMaxFrames;
        SendLifecycle( requestId, "accepted" );
        return;
    }

    SkarnessCommand command;
    command.requestId = requestId;

    const CommandParseStatus parseStatus = ParseCommand( commandName, arguments, command );

    if ( parseStatus == CommandParseStatus::Unknown )
    {
        SendLifecycle( requestId, "rejected", "unknown command" );
        return;
    }

    if ( parseStatus == CommandParseStatus::Invalid )
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

void SkarnessHost::CompleteCommand( const std::string& requestId, bool applied, const SkarnessCommandResult& result,
                                    const char* reason )
{
    PendingCompletion completion;
    completion.requestId = requestId;
    completion.result = result;
    completion.applied = applied;
    completion.hasResult = true;

    if ( reason )
    {
        completion.reason = reason;
    }

    m_pendingCompletions.push_back( std::move( completion ) );
}

bool SkarnessHost::BeginSceneTransition( const std::string& requestId, uint64_t sourceGeneration,
                                         const char* expectedScenePath, bool expectDemo )
{
    if ( !m_pendingSceneTransition.requestId.empty() )
    {
        return false;
    }

    m_pendingSceneTransition.requestId = requestId;
    m_pendingSceneTransition.expectedScenePath = expectedScenePath ? expectedScenePath : "";
    m_pendingSceneTransition.sourceGeneration = sourceGeneration;
    m_pendingSceneTransition.framesRemaining = 1200u;
    m_pendingSceneTransition.expectDemo = expectDemo;
    return true;
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

bool SkarnessHost::TakePointerInputFrame( SkarnessPointerInputFrame& outFrame )
{
    if ( !m_connected || m_pendingPointerDrag.requestId.empty() )
    {
        return false;
    }

    outFrame = {};
    outFrame.clientX = m_pendingPointerDrag.clientX;
    outFrame.clientY = m_pendingPointerDrag.clientY;
    outFrame.button = m_pendingPointerDrag.button;

    if ( m_pendingPointerDrag.phase == 0 )
    {
        // First publish the held edge with no movement so InputController seeds
        // its pointer baseline exactly as it does for a physical press.
        outFrame.buttonDown = true;
        ++m_pendingPointerDrag.phase;
    }
    else if ( m_pendingPointerDrag.phase == 1 )
    {
        outFrame.buttonDown = true;
        outFrame.rawMouseX = m_pendingPointerDrag.deltaX;
        outFrame.rawMouseY = m_pendingPointerDrag.deltaY;
        ++m_pendingPointerDrag.phase;
    }
    else
    {
        const std::string requestId = std::move( m_pendingPointerDrag.requestId );
        m_pendingPointerDrag = PendingPointerDrag {};
        CompleteCommand( requestId, true );
    }

    return true;
}

uint8_t SkarnessHost::ArrowKeysDown() const noexcept
{
    return m_connected ? m_arrowKeysDown : 0;
}

SkarnessProceedPolicy SkarnessHost::TakeProceedPolicy()
{
    SkarnessProceedPolicy policy;

    if ( !m_enabled )
    {
        return policy;
    }

    // Invariant: an accepted command retains its remaining work across pipe
    // loss, but no physics tick is consumed until its controller reconnects.
    if ( !m_connected )
    {
        policy.pauseLocked = true;
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

void SkarnessHost::StoreCompletedResponse( const std::string& requestId, const std::string& response )
{
    const bool retained = std::find( m_recentRequestIds.begin(), m_recentRequestIds.end(), requestId ) !=
                          m_recentRequestIds.end();

    if ( requestId.empty() || !retained )
    {
        return;
    }

    const auto existing = std::find_if( m_completedRequests.begin(), m_completedRequests.end(),
                                        [&requestId]( const CompletedRequest& result )
                                        { return result.requestId == requestId; } );

    if ( existing != m_completedRequests.end() )
    {
        existing->response = response;
    }
    else
    {
        m_completedRequests.push_back( CompletedRequest { requestId, response } );
    }
}

void SkarnessHost::SendLifecycle( const std::string& requestId, const char* status, const char* reason, bool retainResult,
                                  const SkarnessCommandResult* result )
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

    if ( result )
    {
        Json values = Json::object();

        if ( !result->objects.empty() )
        {
            Json objects = Json::array();

            for ( const SkarnessSceneObjectResult& object : result->objects )
            {
                objects.push_back( { { "sceneObjectId", object.sceneObjectId },
                                     { "modelRow", object.modelRow },
                                     { "name", object.name } } );
            }

            values["objects"] = std::move( objects );
        }

        if ( result->hasTextValue )
        {
            values[result->valueName] = result->textValue;
        }
        else if ( result->hasNumberValue )
        {
            values[result->valueName] = result->numberValue;
        }
        else if ( result->hasUnsignedValue )
        {
            values[result->valueName] = result->unsignedValue;
        }
        else if ( result->hasIntegerValue )
        {
            values[result->valueName] = result->integerValue;
        }
        else if ( result->hasBoolValue )
        {
            values[result->valueName] = result->boolValue;
        }

        response["result"] = std::move( values );
    }

    const std::string line = response.dump();
    m_trace << line << '\n';
    m_trace.flush();

    if ( retainResult && status && ( std::strcmp( status, "applied" ) == 0 || std::strcmp( status, "rejected" ) == 0 ) )
    {
        StoreCompletedResponse( requestId, line );
    }

    (void)SendJsonLine( line );
}

void SkarnessHost::SendCapabilities( const std::string& requestId )
{
    Json commandNames = Json::array();
    Json catalog = Json::array();
    Json topics = Json::array();

    for ( const SkarnessCapability& capability : SKARNESS_CAPABILITIES )
    {
        const bool available = capability.availability == SkarnessCapabilityAvailability::Always || !m_manualInput;

        if ( available )
        {
            commandNames.push_back( capability.name );
        }

        catalog.push_back( { { "name", capability.name },
                             { "owner", capability.owner },
                             { "arguments", capability.arguments },
                             { "available", available } } );
    }

    for ( const SkarnessStateTopic& topic : SKARNESS_STATE_TOPICS )
    {
        topics.push_back( { { "name", topic.name }, { "owner", topic.owner } } );
    }

    Json response = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                      { "sequence", ++m_sequence },
                      { "kind", "capabilities" },
                      { "requestId", requestId },
                      { "status", "applied" },
                      { "commands", std::move( commandNames ) },
                      { "catalog", std::move( catalog ) },
                      { "topics", std::move( topics ) },
                      { "stateDetail", { "summary", "normal", "full" } } };
    const std::string line = response.dump();
    m_trace << line << '\n';
    m_trace.flush();
    StoreCompletedResponse( requestId, line );

    (void)SendJsonLine( line );
}

void SkarnessHost::PublishFrameState( const SkarnessFrameState& state, const ReplayAutomationView& replay )
{
    if ( !m_enabled )
    {
        return;
    }

    const uint64_t renderFrame = ++m_renderFrame;
    SkarnessSerializedStateTopics topics;
    BuildSkarnessStateTopics( state, replay, m_stateDetail, topics );
    std::vector<std::string> traceLines;
    std::vector<std::string> notificationLines;
    const bool sceneReset = m_lastPublishedSceneGeneration != ~uint64_t { 0 } &&
                            m_lastPublishedSceneGeneration != state.sceneGeneration;

    const auto emit = [&]( std::size_t index, const char* kind, const std::string& payload, uint64_t payloadHash )
    {
        Json event = { { "schemaVersion", SKARNESS_SCHEMA_VERSION },
                       { "sequence", ++m_sequence },
                       { "runId", m_runId },
                       { "runtimeTurn", renderFrame },
                       { "sceneGeneration", state.sceneGeneration },
                       { "simulationTick", state.sceneFrame },
                       { "sceneFrame", state.sceneFrame },
                       { "simulationSeconds", state.simulationSeconds },
                       { "paused", state.paused },
                       { "renderFrame", renderFrame },
                       { "replayFrame", state.presentedReplayFrame },
                       { "topic", SKARNESS_STATE_TOPICS[index].name },
                       { "kind", kind },
                       { "ownerVersion", topics[index].ownerVersion },
                       { "payload", Json::parse( payload ) } };
        traceLines.push_back( event.dump() );
        if ( m_stateSubscriptions[index] )
        {
            if ( index < 16u )
            {
                event["payload"] = { { "durable", true }, { "bytes", payload.size() }, { "hash", payloadHash } };
            }
            notificationLines.push_back( event.dump() );
        }
    };

    for ( std::size_t index = 0; index < topics.size(); ++index )
    {
        const uint64_t payloadHash = std::hash<std::string> {}( topics[index].payload );
        const bool snapshot = !m_stateInitialized[index] || sceneReset ||
                              ( m_subscriptionSnapshotPending && m_stateSubscriptions[index] );

        if ( sceneReset )
        {
            emit( index, "reset", "{}", 0u );
        }

        if ( snapshot )
        {
            emit( index, "snapshot", topics[index].payload, payloadHash );
        }
        else if ( index >= 16u )
        {
            emit( index, "state", topics[index].payload, payloadHash );
        }
        else
        {
            if ( topics[index].evictCursor > m_stateEvictCursors[index] )
            {
                emit( index, "evict", topics[index].payload, payloadHash );
            }
            if ( topics[index].appendCursor > m_stateAppendCursors[index] )
            {
                emit( index, "append", topics[index].payload, payloadHash );
            }
            if ( payloadHash != m_statePayloadHashes[index] && topics[index].appendCursor <= m_stateAppendCursors[index] &&
                 topics[index].evictCursor <= m_stateEvictCursors[index] )
            {
                emit( index, "change", topics[index].payload, payloadHash );
            }
        }

        m_stateInitialized[index] = true;
        m_statePayloadHashes[index] = payloadHash;
        m_stateOwnerVersions[index] = topics[index].ownerVersion;
        m_stateAppendCursors[index] = topics[index].appendCursor;
        m_stateEvictCursors[index] = topics[index].evictCursor;
    }

    for ( const std::string& line : traceLines )
    {
        m_trace << line << '\n';
    }
    m_trace.flush();
    for ( const std::string& line : notificationLines )
    {
        (void)SendJsonLine( line );
    }
    m_lastPublishedSceneGeneration = state.sceneGeneration;
    m_subscriptionSnapshotPending = false;

    if ( !m_subscriptionRequestId.empty() )
    {
        SendLifecycle( m_subscriptionRequestId, "applied" );
        m_subscriptionRequestId.clear();
    }

    if ( !m_pendingSceneTransition.requestId.empty() )
    {
        const bool newGeneration = state.sceneGeneration > m_pendingSceneTransition.sourceGeneration;
        const bool expectedScene = m_pendingSceneTransition.expectDemo
                                       ? state.scenePath[0] == '\0' && !state.sceneMode
                                       : state.sceneMode && m_pendingSceneTransition.expectedScenePath == state.scenePath;

        if ( newGeneration && state.sceneReady )
        {
            SendLifecycle( m_pendingSceneTransition.requestId, expectedScene ? "applied" : "rejected",
                           expectedScene ? nullptr : "a different scene became active" );
            m_pendingSceneTransition = PendingSceneTransition {};
        }
        else if ( m_connected && m_pendingSceneTransition.framesRemaining > 0 &&
                  --m_pendingSceneTransition.framesRemaining == 0 )
        {
            SendLifecycle( m_pendingSceneTransition.requestId, "rejected",
                           "scene transition did not reach an activated generation" );
            m_pendingSceneTransition = PendingSceneTransition {};
        }
    }

    if ( m_stepCompletesAfterFrame )
    {
        SendLifecycle( m_stepRequestId, "applied" );
        m_stepRequestId.clear();
        m_stepCompletesAfterFrame = false;
    }

    if ( m_connected && m_renderFramesRemaining > 0 && --m_renderFramesRemaining == 0 )
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
            m_untilLimit = 0;
            m_untilFramesRemaining = 0;
            m_untilStepsPhysics = false;
        }
        else if ( m_connected && m_untilFramesRemaining > 0 && --m_untilFramesRemaining == 0 )
        {
            const std::string reason = UntilTimeoutReason( state );
            SendLifecycle( m_untilRequestId, "rejected", reason.c_str() );
            m_untilRequestId.clear();
            m_untilCondition.clear();
            m_untilLimit = 0;
            m_untilStepsPhysics = false;
        }
    }

    while ( !m_pendingCompletions.empty() )
    {
        const PendingCompletion& completion = m_pendingCompletions.front();
        SendLifecycle( completion.requestId, completion.applied ? "applied" : "rejected",
                       completion.reason.empty() ? nullptr : completion.reason.c_str(), true,
                       completion.hasResult ? &completion.result : nullptr );
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
    const bool currentSubmission = state.trajectorySubmitted && state.submittedPredictionTargetId == state.pathTargetId &&
                                   state.submittedPredictionSourceFrame == state.predictionSourceFrame &&
                                   state.submittedPredictionTopologyVersion == state.publishedPredictionTopologyVersion &&
                                   state.submittedGeometryHash != 0u && state.submittedGeometryBytes != 0u;

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
        return selectedTargetPublished && currentSubmission;
    }
    if ( m_untilCondition == "prediction.rendered" )
    {
        return state.predictionEnabled && selectedTargetPublished && state.predictionComplete &&
               state.publishedPredictionFrames >= 2 && state.trajectoryRecordCount > 0 && state.visualPacketHasGeometry &&
               currentSubmission && state.submittedFutureTreeReady;
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
               // Why: causal validity is proven by the child/marker/wireframe
               // agreement above. Submission is geometry-format neutral because
               // stable predictions may use retained line or ribbon lanes.
               currentSubmission && state.submittedFutureTreeReady;
    }
    if ( m_untilCondition == "camera.inspection_settled" )
    {
        return state.inspectionCameraActive && state.causeInspectionMode == 2 && !state.cameraTweenActive;
    }
    if ( m_untilCondition == "camera.main_restored" )
    {
        return !state.inspectionCameraActive && state.causeInspectionMode == 0 && !state.cameraTweenActive;
    }

    return false;
}

std::string SkarnessHost::UntilTimeoutReason( const SkarnessFrameState& state ) const
{
    std::ostringstream reason;
    reason << "condition '" << m_untilCondition << "' timed out: limitKind=" << ( m_untilStepsPhysics ? "ticks" : "frames" )
           << " limit=" << m_untilLimit << " observations=" << m_untilLimit << " enabled=" << state.predictionEnabled
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
           << " pathSaturated=" << state.retainedPathGeometrySaturated << " submitted=" << state.trajectorySubmitted
           << " segments=" << state.submittedSegmentCount << " vertices=" << state.submittedVertexCount
           << " futureTreeReady=" << state.submittedFutureTreeReady;
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
                      { "manualInput", m_manualInput },
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
