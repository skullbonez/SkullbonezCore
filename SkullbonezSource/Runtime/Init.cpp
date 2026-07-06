/*
File: SkullbonezSource/Runtime/Init.cpp
Purpose:
  Bootstraps the Windows process, parses command-line options, and starts the run loop.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX11/OpenGL: Retired runtime renderer choices. The parser names them only to
  explain why old command lines are rejected.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  and platform APIs through reference-counted objects.
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.
  Validation gate: Repository script that proves a class of changes before
    commit or PR.
  Standalone physics smoke: Early-exit validation mode that exercises public
    physics API construction without runtime/window/renderer ownership.
  Runtime handle smoke: Early-exit validation mode that uses runtime
    GameModelCollection construction but proves returned physics handles stay
    aligned with body, collider, constraint, and render mirrors.

Invariants:
  - DX12 is the only runtime renderer; retired renderer flags are parsed only
    to produce clear failures for old command lines.
  - Startup options are resolved before Run owns subsystems so validation
    launches are deterministic from their CLI.
  - Early-exit smoke modes must return before worker, window, renderer, or Run
    startup if their evidence claims subsystem isolation.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Core/Common.h"
#include "Audio/ContactAudioService.h"
#include "Run.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "../Rendering/Text.h"
#include "Window.h"
#include "Input.h"
#include "../Core/Timer.h"
#include "../Rendering/IRenderBackend.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../GameObjects/GameModel.h"
#include "../GameObjects/GameModelCollection.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../World/WorldEnvironment.h"
#include "../Core/PlatformProfiler.h"
#include "../Core/WorkerPool.h"
#include <cerrno>
#include <float.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <io.h>
#include <objbase.h>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#ifdef _DEBUG
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#endif


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Threading;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


namespace
{
using Json = nlohmann::ordered_json;

char g_commandLineError[512] = {};

#ifdef _DEBUG
const char* ExceptionCodeName( DWORD code )
{
    switch ( code )
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    default:
        return "EXCEPTION_UNKNOWN";
    }
}


void WriteDebugCrashStack( EXCEPTION_POINTERS* exceptionInfo )
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions( symOptions );

    const BOOL symbolsReady = SymInitialize( process, nullptr, TRUE );
    if ( !symbolsReady )
    {
        Log().Writef( EngineLog::EventLogPath(), "    stack_symbols=unavailable error=%lu\n", GetLastError() );
    }

    CONTEXT context = {};
    if ( exceptionInfo && exceptionInfo->ContextRecord )
    {
        context = *exceptionInfo->ContextRecord;
    }
    else
    {
        RtlCaptureContext( &context );
    }

    STACKFRAME64 frame = {};
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#if defined( _M_X64 )
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#endif

    Log().Writef( EngineLog::EventLogPath(), "    stack_trace:\n" );
    for ( int frameIndex = 0; frameIndex < 64; ++frameIndex )
    {
        BOOL walked = StackWalk64( machineType,
                                   process,
                                   thread,
                                   &frame,
                                   &context,
                                   nullptr,
                                   SymFunctionTableAccess64,
                                   SymGetModuleBase64,
                                   nullptr );
        if ( !walked || frame.AddrPC.Offset == 0 )
        {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;
        char symbolStorage[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME] = {};
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>( symbolStorage );
        symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 symbolDisplacement = 0;
        const BOOL hasSymbol = symbolsReady && SymFromAddr( process, address, &symbolDisplacement, symbol );

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof( lineInfo );
        DWORD lineDisplacement = 0;
        const BOOL hasLine = symbolsReady && SymGetLineFromAddr64( process, address, &lineDisplacement, &lineInfo );

        if ( hasSymbol && hasLine )
        {
            Log().Writef( EngineLog::EventLogPath(),
                          "      #%02d 0x%016llX %s+0x%llX (%s:%lu)\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ),
                          symbol->Name,
                          static_cast<unsigned long long>( symbolDisplacement ),
                          lineInfo.FileName,
                          lineInfo.LineNumber );
        }
        else if ( hasSymbol )
        {
            Log().Writef( EngineLog::EventLogPath(),
                          "      #%02d 0x%016llX %s+0x%llX\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ),
                          symbol->Name,
                          static_cast<unsigned long long>( symbolDisplacement ) );
        }
        else
        {
            Log().Writef( EngineLog::EventLogPath(),
                          "      #%02d 0x%016llX <unknown>\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ) );
        }
    }

    if ( symbolsReady )
    {
        SymCleanup( process );
    }
}


LONG WINAPI DebugUnhandledExceptionFilter( EXCEPTION_POINTERS* exceptionInfo )
{
    DWORD exceptionCode = 0;
    void* exceptionAddress = nullptr;
    if ( exceptionInfo && exceptionInfo->ExceptionRecord )
    {
        exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
        exceptionAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
    }

    Log().WriteEventf( "crash exception=0x%08lX name=%s address=%p",
                       exceptionCode,
                       ExceptionCodeName( exceptionCode ),
                       exceptionAddress );
    WriteDebugCrashStack( exceptionInfo );
    Log().FlushAll();

    return EXCEPTION_EXECUTE_HANDLER;
}


void InstallDebugCrashLogger()
{
    SetUnhandledExceptionFilter( DebugUnhandledExceptionFilter );
    // Hazard: unhandled C++ failures often become std::terminate -> abort(),
    // which bypasses the SEH filter above and otherwise leaves only a CRT
    // dialog. Log the current exception, if any, before preserving termination.
    std::set_terminate(
        []()
        {
            char message[512] = "unknown";
            std::exception_ptr current = std::current_exception();
            if ( current )
            {
                try
                {
                    std::rethrow_exception( current );
                }
                catch ( const std::exception& e )
                {
                    strcpy_s( message, sizeof( message ), e.what() );
                }
                catch ( ... )
                {
                    strcpy_s( message, sizeof( message ), "non-std exception" );
                }
            }
            Log().WriteEventf( "terminate_abort message=\"%s\"", message );
            fprintf( stderr, "FATAL: terminate_abort %s\n", message );
            fflush( stderr );
            Log().FlushAll();
            std::abort();
        } );
}
#endif

bool FailCommandLineParse( const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    vsprintf_s( g_commandLineError, sizeof( g_commandLineError ), fmt, args );
    va_end( args );

    fprintf( stderr, "ERROR: %s\n", g_commandLineError );
    return false;
}

const char* GetCommandLineError()
{
    return g_commandLineError[0] != '\0' ? g_commandLineError : "Command line parsing failed.";
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

// GUI apps have no console by default — attach to the parent terminal so
// fprintf(stderr/stdout) is visible when launched from cmd/PowerShell.
bool IsStandardHandleRedirected( DWORD standardHandle )
{
    HANDLE handle = GetStdHandle( standardHandle );
    if ( handle == nullptr || handle == INVALID_HANDLE_VALUE )
    {
        return false;
    }

    const DWORD fileType = GetFileType( handle );
    return fileType == FILE_TYPE_PIPE || fileType == FILE_TYPE_DISK;
}

void AttachParentConsole()
{
    const bool stdoutRedirected = IsStandardHandleRedirected( STD_OUTPUT_HANDLE );
    const bool stderrRedirected = IsStandardHandleRedirected( STD_ERROR_HANDLE );

    if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
    {
        FILE* dummy = nullptr;
        if ( !stdoutRedirected )
        {
            freopen_s( &dummy, "CONOUT$", "w", stdout );
        }
        if ( !stderrRedirected )
        {
            freopen_s( &dummy, "CONOUT$", "w", stderr );
        }
    }
}

struct CommandLineView
{
    std::vector<std::string> tokens;
};

bool IsTokenWhitespace( char c )
{
    return c == ' ' || c == '\t';
}

CommandLineView TokenizeCommandLine( const char* cmdLine )
{
    CommandLineView result;
    if ( !cmdLine )
    {
        return result;
    }

    const char* cursor = cmdLine;
    while ( *cursor != '\0' )
    {
        while ( IsTokenWhitespace( *cursor ) )
        {
            ++cursor;
        }
        if ( *cursor == '\0' )
        {
            break;
        }

        std::string token;
        bool inQuote = false;
        while ( *cursor != '\0' )
        {
            const char c = *cursor;
            if ( c == '"' )
            {
                inQuote = !inQuote;
                ++cursor;
                continue;
            }
            if ( !inQuote && IsTokenWhitespace( c ) )
            {
                break;
            }
            token.push_back( c );
            ++cursor;
        }

        if ( !token.empty() )
        {
            result.tokens.push_back( token );
        }
    }
    return result;
}

bool IsOptionToken( const std::string& token )
{
    return token.size() >= 2 && token[0] == '-' && token[1] == '-';
}

bool IsOptionValueMissing( const char* value )
{
    return !value || *value == '\0';
}

bool OptionTokenMatches( const std::string& token, const char* optionName )
{
    return optionName && token == optionName;
}

bool OptionTokenHasAssignedValue( const std::string& token, const char* optionName, const char*& outValue )
{
    if ( !optionName )
    {
        return false;
    }

    const size_t optionLen = strlen( optionName );
    if ( token.size() <= optionLen || token.compare( 0, optionLen, optionName ) != 0 || token[optionLen] != '=' )
    {
        return false;
    }

    outValue = token.c_str() + optionLen + 1;
    return true;
}

const char* FindOptionValue( const CommandLineView& commandLine, const char* optionName )
{
    for ( size_t i = 0; i < commandLine.tokens.size(); ++i )
    {
        const std::string& token = commandLine.tokens[i];
        const char* assignedValue = nullptr;
        if ( OptionTokenHasAssignedValue( token, optionName, assignedValue ) )
        {
            return assignedValue;
        }
        if ( OptionTokenMatches( token, optionName ) )
        {
            if ( i + 1 < commandLine.tokens.size() && !IsOptionToken( commandLine.tokens[i + 1] ) )
            {
                return commandLine.tokens[i + 1].c_str();
            }
            return "";
        }
    }
    return nullptr;
}

const char* FindOptionValue( const CommandLineView& commandLine, const char* dashedName, const char* underscoredName )
{
    const char* value = FindOptionValue( commandLine, dashedName );
    return value ? value : FindOptionValue( commandLine, underscoredName );
}

bool HasOption( const CommandLineView& commandLine, const char* optionName )
{
    for ( const std::string& token : commandLine.tokens )
    {
        const char* assignedValue = nullptr;
        if ( OptionTokenMatches( token, optionName ) ||
             OptionTokenHasAssignedValue( token, optionName, assignedValue ) )
        {
            return true;
        }
    }
    return false;
}

char* TrimLineInPlace( char* text )
{
    while ( IsTokenWhitespace( *text ) )
    {
        ++text;
    }

    size_t len = strlen( text );
    while ( len > 0 && ( IsTokenWhitespace( text[len - 1] ) || text[len - 1] == '\r' || text[len - 1] == '\n' ) )
    {
        text[--len] = '\0';
    }
    return text;
}

bool ParseFloatToken( const char* value, float& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = strtod( value, &end );
    if ( end == value || *end != '\0' || errno == ERANGE )
    {
        return false;
    }

    out = static_cast<float>( parsed );
    return true;
}

bool ParseIntToken( const char* value, int& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = strtol( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX )
    {
        return false;
    }

    out = static_cast<int>( parsed );
    return true;
}

bool ParseUnsignedIntToken( const char* value, unsigned int& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = strtoul( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed > UINT_MAX )
    {
        return false;
    }

    out = static_cast<unsigned int>( parsed );
    return true;
}

bool CopyOptionPath( const char* value, const char* optionName, char* outPath, size_t outPathSize )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "%s requires an output path.", optionName );
    }
    if ( strlen( value ) >= outPathSize )
    {
        return FailCommandLineParse( "%s path is too long.", optionName );
    }

    strcpy_s( outPath, outPathSize, value );
    return true;
}

// ---------------------------------------------------------------------------
// --gen-atlas early exit
// SDF font atlas file generation path: exits before GPU context setup.
// True means the flag was present; outExitCode is 0 on success, 1 on failure.
// ---------------------------------------------------------------------------

bool HandleGenAtlas( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--gen-atlas" ) )
    {
        return false;
    }

    char outPath[MAX_PATH];
    const char* atlasArg = FindOptionValue( commandLine, "--gen-atlas" );
    if ( atlasArg && *atlasArg != '\0' )
    {
        if ( strlen( atlasArg ) >= MAX_PATH )
        {
            fprintf( stderr, "[gen-atlas] Output path is too long.\n" );
            outExitCode = 1;
            return true;
        }
        strcpy_s( outPath, atlasArg );
    }
    else
    {
        strcpy_s( outPath, "SkullbonezData/font_atlas.sdf" );
    }

    fprintf( stdout, "[gen-atlas] Generating SDF font atlas: %s\n", outPath );
    if ( SkullbonezCore::Text::Text2d::GenerateSdfAtlasToFile( "Verdana", outPath ) )
    {
        fprintf( stdout, "[gen-atlas] Done.\n" );
        outExitCode = 0;
    }
    else
    {
        fprintf( stderr, "[gen-atlas] FAILED.\n" );
        outExitCode = 1;
    }
    return true;
}

struct PhysicsRuntimeHandleSmokeResult
{
    bool passed = false;
    bool handlesMatchStores = false;
    bool renderMirrorMatches = false;
    bool jointUsesHandles = false;
    bool colliderRefreshMatches = false;
    bool reorderPreservesHandleState = false;
    int bodyCount = 0;
    int colliderCount = 0;
    int renderInstanceCount = 0;
    std::size_t pointJointCount = 0;
    PhysicsBodyHandle bodyA;
    std::string errorMessage;
};


PhysicsRuntimeHandleSmokeResult RunPhysicsRuntimeHandleSmokeSample()
{
    // Why: this smoke proves runtime-created bodies keep their returned physics
    // handles aligned with body/collider stores and render snapshots without opening the
    // window or renderer. WinMain runs the normal command-line/config bootstrap
    // before this helper so collection capacity uses the same config snapshot
    // as a regular runtime launch.
    auto world = std::make_unique<SkullbonezCore::Environment::WorldEnvironment>();
    auto collection = std::make_unique<SkullbonezCore::GameObjects::GameModelCollection>();
    PhysicsBodyHandle createdBodies[2];

    for ( int i = 0; i < 2; ++i )
    {
        SkullbonezCore::GameObjects::GameModel model;
        char name[32] = {};
        sprintf_s( name, sizeof( name ), "runtime_smoke_%d", i );
        model.SetName( name );
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = static_cast<uint32_t>( i + 1 );
        const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape(
            0.75f,
            SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
        createdBodies[i] = collection->AddGameModel(
            std::move( model ),
            MakePhysicsBodyCreateDesc(
                sceneObjectId,
                shape,
                SkullbonezCore::Math::Vector::Vector3( static_cast<float>( i ) * 2.0f, 4.0f, 0.0f ),
                SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f ),
                2.0f + static_cast<float>( i ),
                0.0f,
                PhysicsBodyMotionKind::Dynamic,
                nullptr,
                name ),
            MakeColliderCreateDesc( shape, 0.0f, HashStr( "default" ) ),
            sceneObjectId );
    }

    const PhysicsBodyHandle bodyA = createdBodies[0];
    const PhysicsBodyHandle bodyB = createdBodies[1];

    PhysicsPointJointCreateDesc jointDesc;
    jointDesc.bodyA = bodyA;
    jointDesc.bodyB = bodyB;
    jointDesc.localAnchorA = SkullbonezCore::Math::Vector::Vector3( 0.25f, 0.0f, 0.0f );
    jointDesc.localAnchorB = SkullbonezCore::Math::Vector::Vector3( -0.25f, 0.0f, 0.0f );
    const PhysicsConstraintHandle jointHandle = collection->GetPhysicsEngine().CreatePointJoint( jointDesc );

    const PhysicsBodyStore& bodyStore = collection->GetPhysicsBodyStore();
    const ColliderStore& colliderStore = collection->GetColliderStore();
    const RenderInstanceStore& renderStore = collection->GetRenderInstanceStore();
    const std::vector<PointJointConstraint>& pointJoints = collection->GetPointJointConstraints();
    const size_t initialColliderCount = colliderStore.Count();
    const ColliderRecord initialCollider = colliderStore.Records()[0];

    const SkullbonezCore::Math::Vector::Vector3 editedHalfExtents( 0.25f, 1.25f, 0.5f );
    constexpr float EDITED_RESTITUTION = 0.42f;
    collection->CommitEditedModelColliderState(
        0,
        MakeColliderCreateDesc( SkullbonezCore::Math::CollisionDetection::BoundingBox(
                                    editedHalfExtents,
                                    SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) ),
                                EDITED_RESTITUTION,
                                HashStr( "default" ) ) );
    const ColliderStore& refreshedColliderStore = collection->GetColliderStore();
    const ColliderRecord& refreshedCollider = refreshedColliderStore.Records()[0];
    const float expectedBoxRadius = sqrtf( 0.25f * 0.25f + 1.25f * 1.25f + 0.5f * 0.5f );
    // Invariant: same-count authoring edits must be visible through the explicit
    // collider edit commit. Store reads only auto-repair topology changes, so
    // tools and scene edits must commit before asking for collider records.
    const bool colliderRefreshMatches =
        initialCollider.shapeKind == ColliderShapeKind::Sphere &&
        refreshedCollider.shapeKind == ColliderShapeKind::Box &&
        fabsf( refreshedCollider.boundingRadius - expectedBoxRadius ) < 0.0001f &&
        fabsf( refreshedCollider.restitution - 0.42f ) < 0.0001f &&
        fabsf( refreshedCollider.projectedSurfaceArea - initialCollider.projectedSurfaceArea ) > 0.0001f &&
        fabsf( refreshedCollider.dragCoefficient - initialCollider.dragCoefficient ) > 0.0001f &&
        refreshedCollider.handle == initialCollider.handle && refreshedCollider.body == initialCollider.body &&
        refreshedColliderStore.Count() == initialColliderCount;

    const PhysicsBodyRecord* bodyARecord = bodyStore.RecordForModelIndex( 0 );
    const PhysicsBodyRecord* bodyBRecord = bodyStore.RecordForModelIndex( 1 );
    const RenderInstanceHandle renderHandleA = renderStore.HandleForModelIndex( 0 );
    const bool handlesMatchStores = bodyA.IsValid() && bodyB.IsValid() && bodyARecord && bodyBRecord &&
                                    bodyARecord->handle == bodyA && bodyBRecord->handle == bodyB &&
                                    colliderStore.HandleForModelIndex( 0 ).IsValid() &&
                                    colliderStore.HandleForModelIndex( 1 ).IsValid();
    const bool renderMirrorMatches = bodyARecord && renderStore.Count() == 2 && renderHandleA.IsValid() &&
                                     renderStore.ModelIndexForHandle( renderHandleA ) == 0 &&
                                     !renderStore.Records().empty() &&
                                     renderStore.Records()[0].replayBodyId == bodyARecord->replayBodyId;
    const bool jointUsesHandles = jointHandle.IsValid() && pointJoints.size() == 1 && pointJoints[0].bodyA == bodyA &&
                                  pointJoints[0].bodyB == bodyB && pointJoints[0].BodyAIndex( bodyStore ) == 0 &&
                                  pointJoints[0].BodyBIndex( bodyStore ) == 1;

    constexpr uint32_t REORDER_BODY_A_REPLAY_ID = 100u;
    constexpr uint32_t REORDER_BODY_B_REPLAY_ID = 101u;
    PhysicsBodyStore reorderBodyStore;
    std::vector<PhysicsBodyCreateDesc> reorderBodyDescs;
    for ( int i = 0; i < 2; ++i )
    {
        PhysicsBodyCreateDesc desc;
        desc.sceneObjectId =
            MakePhysicsSceneObjectIdFromReplayBodyId( REORDER_BODY_A_REPLAY_ID + static_cast<uint32_t>( i ) );
        desc.shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere(
            0.5f,
            SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
        desc.position = SkullbonezCore::Math::Vector::Vector3( static_cast<float>( i ) * 3.0f, 5.0f, 0.0f );
        desc.rotationalInertia = SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f );
        desc.mass = 3.0f + static_cast<float>( i );
        desc.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
        desc.volume = SkullbonezCore::Math::CollisionDetection::GetShapeVolume( desc.shape );
        desc.projectedSurfaceArea =
            SkullbonezCore::Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );
        desc.dragCoefficient = SkullbonezCore::Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );
        reorderBodyDescs.push_back( desc );
    }
    reorderBodyStore.LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t>{} );
    const PhysicsBodyHandle reorderedOriginalBody = reorderBodyStore.HandleForModelIndex( 0 );
    const uint32_t reorderBodyAReplayId = REORDER_BODY_A_REPLAY_ID;
    const uint32_t reorderBodyBReplayId = REORDER_BODY_B_REPLAY_ID;
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulse( 0.0f, 2.0f, 0.0f );
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulsePoint( 0.25f, 0.0f, 0.0f );
    const bool seededReorderState =
        reorderBodyStore.SetPendingBodyImpulse( reorderedOriginalBody, pendingImpulse, pendingImpulsePoint ) &&
        reorderBodyStore.SeedBodyAsleep( reorderedOriginalBody );
    reorderBodyDescs[0].sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( reorderBodyBReplayId );
    reorderBodyDescs[1].sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( reorderBodyAReplayId );
    reorderBodyStore.LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t>{} );
    const int reorderedBodyAIndex = reorderBodyStore.ModelIndexForHandle( reorderedOriginalBody );
    const PhysicsBodyRecord* reorderedBodyARecord =
        reorderedBodyAIndex >= 0 ? reorderBodyStore.RecordForModelIndex( reorderedBodyAIndex ) : nullptr;
    // Invariant: allocator-owned handles must carry physics-owned one-shot
    // state through a same-scene reorder. Otherwise the handle identity is only
    // nominally independent from model order.
    const bool reorderPreservesHandleState =
        seededReorderState && reorderedBodyAIndex == 1 && reorderedBodyARecord &&
        reorderedBodyARecord->handle == reorderedOriginalBody && reorderedBodyARecord->hasPendingImpulse &&
        reorderedBodyARecord->isSleeping &&
        fabsf( reorderedBodyARecord->pendingImpulse.y - pendingImpulse.y ) < 0.0001f &&
        fabsf( reorderedBodyARecord->pendingImpulseApplicationPoint.x - pendingImpulsePoint.x ) < 0.0001f;

    PhysicsRuntimeHandleSmokeResult result;
    result.handlesMatchStores = handlesMatchStores;
    result.renderMirrorMatches = renderMirrorMatches;
    result.jointUsesHandles = jointUsesHandles;
    result.colliderRefreshMatches = colliderRefreshMatches;
    result.reorderPreservesHandleState = reorderPreservesHandleState;
    result.bodyCount = bodyStore.Count();
    result.colliderCount = colliderStore.Count();
    result.renderInstanceCount = renderStore.Count();
    result.pointJointCount = pointJoints.size();
    result.bodyA = bodyA;
    result.passed = handlesMatchStores && renderMirrorMatches && jointUsesHandles && colliderRefreshMatches &&
                    reorderPreservesHandleState;
    return result;
}


bool HandlePhysicsStandaloneSmoke( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--physics-standalone-smoke" ) &&
         !HasOption( commandLine, "--physics_standalone_smoke" ) )
    {
        return false;
    }

    // Why: this option runs before WorkerPool, Window, renderer, Run, or scene
    // setup so it proves the public physics API and runtime handle alignment can
    // be constructed without renderer/window services.
    const PhysicsStandaloneSmokeResult result = RunPhysicsStandaloneSmoke();
    PhysicsRuntimeHandleSmokeResult runtimeMirror;
    try
    {
        runtimeMirror = RunPhysicsRuntimeHandleSmokeSample();
    }
    catch ( const std::exception& e )
    {
        runtimeMirror.errorMessage = e.what();
    }
    auto writeReport = [&]( FILE* stream )
    {
        if ( !stream )
        {
            return;
        }
        fprintf( stream,
                 "[physics-standalone-smoke] bodies=%u steps=%u final_position=(%.6f,%.6f,%.6f) "
                 "final_velocity=(%.6f,%.6f,%.6f) secondary_position=(%.6f,%.6f,%.6f) "
                 "secondary_velocity=(%.6f,%.6f,%.6f) secondary_step=%s lifecycle_checks=%s "
                 "contacts=%u contact_hash=0x%016llX runtime_mirror_checks=%s hash=0x%016llX\n",
                 result.bodyCount,
                 result.stepCount,
                 result.finalPosition.x,
                 result.finalPosition.y,
                 result.finalPosition.z,
                 result.finalLinearVelocity.x,
                 result.finalLinearVelocity.y,
                 result.finalLinearVelocity.z,
                 result.secondaryFinalPosition.x,
                 result.secondaryFinalPosition.y,
                 result.secondaryFinalPosition.z,
                 result.secondaryFinalLinearVelocity.x,
                 result.secondaryFinalLinearVelocity.y,
                 result.secondaryFinalLinearVelocity.z,
                 result.secondaryBodyAdvanced ? "pass" : "fail",
                 result.lifecycleChecksPassed ? "pass" : "fail",
                 result.contactCount,
                 static_cast<unsigned long long>( result.contactHash ),
                 runtimeMirror.passed ? "pass" : "fail",
                 static_cast<unsigned long long>( result.deterministicHash ) );
        fprintf( stream,
                 "[physics-runtime-handle-smoke] bodies=%d colliders=%d render_instances=%d point_joints=%zu "
                 "handle_a=(%u,%u) store_handles=%s render_mirror=%s joint_handles=%s collider_refresh=%s "
                 "reorder_state=%s\n",
                 runtimeMirror.bodyCount,
                 runtimeMirror.colliderCount,
                 runtimeMirror.renderInstanceCount,
                 runtimeMirror.pointJointCount,
                 runtimeMirror.bodyA.index,
                 runtimeMirror.bodyA.generation,
                 runtimeMirror.handlesMatchStores ? "pass" : "fail",
                 runtimeMirror.renderMirrorMatches ? "pass" : "fail",
                 runtimeMirror.jointUsesHandles ? "pass" : "fail",
                 runtimeMirror.colliderRefreshMatches ? "pass" : "fail",
                 runtimeMirror.reorderPreservesHandleState ? "pass" : "fail" );
        if ( !runtimeMirror.errorMessage.empty() )
        {
            fprintf( stream, "[physics-runtime-handle-smoke] error=\"%s\"\n", runtimeMirror.errorMessage.c_str() );
        }
        fflush( stream );
    };

    writeReport( stdout );

    const char* reportPath =
        FindOptionValue( commandLine, "--physics-standalone-smoke-log", "--physics_standalone_smoke_log" );
    if ( reportPath && !IsOptionValueMissing( reportPath ) )
    {
        FILE* reportFile = nullptr;
        if ( fopen_s( &reportFile, reportPath, "w" ) == 0 && reportFile )
        {
            writeReport( reportFile );
            fclose( reportFile );
        }
    }

    if ( !result.passed || !runtimeMirror.passed )
    {
        fprintf( stderr,
                 "FAIL: physics smoke final state, lifecycle, or runtime mirror checks did not match the expected "
                 "sample.\n" );
        outExitCode = 1;
        return true;
    }

    fprintf( stdout, "PASS: standalone physics and runtime handle mirror smoke matched expected state.\n" );
    outExitCode = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Command-line parsing
// ---------------------------------------------------------------------------

struct RendererOption
{
    const char* name;
    const char* alias;
};

struct ParsedArgs
{
    // Parsed command-line state. Defaults here are part of startup behavior:
    // validation scripts, desktop shortcuts, and scene automation all rely on
    // omitted flags producing these exact policies.
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;
    float timeScaleOverride = 0.0f; // 0 = not set
    bool fixedStep = false;
    unsigned int seedOverride = 0; // 0 = not set
    bool noWater = false;
    bool noSleep = false;
    bool noContactAudio = false;
    bool contactAudioSmoke = false;
    bool hasTornadoOverride = false;
    bool tornadoEnabled = false;
    bool tornadoVectors = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool interactiveRun = false;
    int frameCountOverride = -1;
    bool sceneLoadOnly = false;
    bool demoHeroStyle = false;
    bool uiStress = false;
    unsigned int uiStressSeed = 0x7F4A7C15u;
    int uiStressActions = 5;
    bool graphicsStress = false;
    unsigned int graphicsStressSeed = 0xC11E2026u;
    int graphicsStressActions = 12;
    int graphicsStressSceneIntervalFrames = 45;
    int graphicsStressMemoryIntervalFrames = 1800;
    RuntimeAllocation::RuntimeAllocationGuardMode allocationGuardMode =
        RuntimeAllocation::RuntimeAllocationGuardMode::Off;
    bool replayRecording = true;
    bool replayExplicit = false;
    int replaySeconds = REPLAY_PAST_BUFFER_SECONDS;
    bool replayScrubProbe = false;
    float replayScrubProbeNormalized = 0.25f;
    bool replayRestoreProbe = false;
    float replayRestoreProbeNormalized = 0.25f;
    bool replaySaveProbe = false;
    char replaySaveProbePath[260] = {};
    bool replayLoad = false;
    bool replayLoadProbe = false;
    char replayLoadPath[260] = {};
    bool replayRestoreFileProbe = false;
    char replayRestoreFileProbePath[260] = {};
    bool replayRestoreTargetFileProbe = false;
    char replayRestoreTargetFileProbePath[260] = {};
    bool replayRestoreBranchFileProbe = false;
    char replayRestoreBranchFileProbePath[260] = {};
    bool replayRestoreFailureFileProbe = false;
    char replayRestoreFailureFileProbePath[260] = {};
    char replayHashLogPath[260] = {};
    char liveStyleControlDir[260] = {};
    char sceneSnapshotOutPath[260] = {};
    char memoryDumpPath[260] = {};
    char interactionScriptPath[260] = {};
    char interactionReportPath[260] = {};
    bool suppressExitDialog = false;
    bool showProfiler = false;
    bool hideTopText = false;
    bool showBroadphaseVisualizer = false;
    bool workerSelfTest = false;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool hasPhysicsDebugFlagsOverride = false;
    uint32_t physicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
    bool hasPhysicsDebugTransparentOverride = false;
    bool physicsDebugTransparentOverride = false;
    bool hasPhysicsDebugAlphaOverride = false;
    float physicsDebugAlphaOverride = 0.28f;
    bool hasPhysicsDebugContactLingerOverride = false;
    float physicsDebugContactLingerOverride = 0.45f;
#ifdef _DEBUG
    char physicsRegressionLogOverride[256] = {};
    char physicsCollisionTimeLogOverride[256] = {};
    char physicsDiagnosticsPath[256] = {};
#endif
    bool physicsDiagnosticsRequested = false;
    bool fixedStepForcedByPhysicsDiagnostics = false;
    bool dumpConfig = false;
    bool dumpAssets = false;
#if defined( SKULLBONEZ_PROFILE_ENABLED ) && defined( SKULLBONEZ_PLATFORM_PROFILER_PIX )
    bool platformProfilerMarkers = true;
#else
    bool platformProfilerMarkers = false;
#endif
    bool platformProfilerMarkersExplicit = false;
};

struct CliFlagDirective
{
    // Table-driven flag parsing keeps aliases beside the canonical spelling.
    // That matters because command-line options are user-facing compatibility
    // surface, not private implementation detail.
    const char* name;
    const char* alias;
    void ( *apply )( ParsedArgs& args );
    const char* message;
};

struct CliValueDirective
{
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args );
};

struct ConfigCliValueDirective
{
    // Why: startup config options mutate the loaded EngineConfig before any
    // subsystem borrows it, so these handlers must not reopen the global config
    // singleton.
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args, EngineConfig& config );
};

struct PhysicsDebugComponentDirective
{
    const char* dashedName;
    const char* underscoredName;
    uint32_t flag;
};

struct PhysicsDebugFloatDirective
{
    const char* dashedName;
    const char* underscoredName;
    bool ParsedArgs::* hasOverride;
    float ParsedArgs::* value;
    float minValue;
    float maxValue;
    const char* errorMessage;
    bool enableTransparentBodies;
};

struct GeneratedObjectOverrideDirective
{
    const char* optionName;
    GeneratedObjectTypeOverride objectType;
    const char* message;
};

bool HasFlagDirective( const CommandLineView& commandLine, const CliFlagDirective& directive )
{
    return HasOption( commandLine, directive.name ) || ( directive.alias && HasOption( commandLine, directive.alias ) );
}

const char* FindValueDirective( const CommandLineView& commandLine, const CliValueDirective& directive )
{
    const char* value = FindOptionValue( commandLine, directive.name );
    if ( value || !directive.alias )
    {
        return value;
    }
    return FindOptionValue( commandLine, directive.alias );
}

const char* FindValueDirective( const CommandLineView& commandLine, const ConfigCliValueDirective& directive )
{
    const char* value = FindOptionValue( commandLine, directive.name );
    if ( value || !directive.alias )
    {
        return value;
    }
    return FindOptionValue( commandLine, directive.alias );
}

template <size_t N>
bool ApplyCliValueDirectives( const CommandLineView& commandLine,
                              ParsedArgs& out,
                              const CliValueDirective ( &directives )[N] )
{
    for ( const CliValueDirective& directive : directives )
    {
        const char* value = FindValueDirective( commandLine, directive );
        if ( value && !directive.apply( value, out ) )
        {
            return false;
        }
    }
    return true;
}

template <size_t N>
bool ApplyConfigCliValueDirectives( const CommandLineView& commandLine,
                                    ParsedArgs& out,
                                    EngineConfig& config,
                                    const ConfigCliValueDirective ( &directives )[N] )
{
    for ( const ConfigCliValueDirective& directive : directives )
    {
        const char* value = FindValueDirective( commandLine, directive );
        if ( value && !directive.apply( value, out, config ) )
        {
            return false;
        }
    }
    return true;
}

void ApplyCliFlagDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliFlagDirective kFlags[] = {
        { "--fixed-step",
          nullptr,
          []( ParsedArgs& args ) { args.fixedStep = true; },
          "[fixed-step] Forced via command line." },
        { "--no-water",
          nullptr,
          []( ParsedArgs& args ) { args.noWater = true; },
          "[water] Fluid surface starts below terrain." },
        { "--no-sleep",
          nullptr,
          []( ParsedArgs& args ) { args.noSleep = true; },
          "[physics] Sleep disabled via command line." },
        { "--no-contact-audio",
          "--mute-contact-audio",
          []( ParsedArgs& args ) { args.noContactAudio = true; },
          "[audio] Contact impact audio disabled." },
        { "--contact-audio-smoke",
          "--audio-smoke",
          []( ParsedArgs& args )
          {
              args.contactAudioSmoke = true;
              args.suppressExitDialog = true;
          },
          "[audio] Contact audio standalone smoke requested." },
        { "--scene-load-only",
          "--load-scenes-only",
          []( ParsedArgs& args )
          {
              args.sceneLoadOnly = true;
              args.suppressExitDialog = true;
          },
          "[scene-load-only] Load queued scenes without running frames." },
        { "--demohero",
          "--demo-hero",
          []( ParsedArgs& args )
          {
              args.demoHeroStyle = true;
              args.suppressExitDialog = true;
          },
          "[scene] Generated demo scene will use the low-poly hero rendering mode." },
        { "--profiler",
          "--show-profiler",
          []( ParsedArgs& args ) { args.showProfiler = true; },
          "[overlay] Profiler HUD enabled at startup." },
        { "--platform-profiler-markers",
          "--platform-profiler",
          []( ParsedArgs& args )
          {
              args.platformProfilerMarkers = true;
              args.platformProfilerMarkersExplicit = true;
          },
          "[platform-profiler] Platform profiler marker emission requested." },
        { "--pix-markers",
          "--pix",
          []( ParsedArgs& args )
          {
              args.platformProfilerMarkers = true;
              args.platformProfilerMarkersExplicit = true;
          },
          "[platform-profiler] PIX marker compatibility alias requested." },
        { "--hide-top-text",
          "--no-top-text",
          []( ParsedArgs& args ) { args.hideTopText = true; },
          "[overlay] Top HUD text hidden." },
        { "--broadphase-visualizer",
          "--broadphase-overlay",
          []( ParsedArgs& args ) { args.showBroadphaseVisualizer = true; },
          "[overlay] Broadphase visualizer enabled at startup." },
        { "--dump-config", nullptr, []( ParsedArgs& args ) { args.dumpConfig = true; }, nullptr },
        { "--dump-assets", nullptr, []( ParsedArgs& args ) { args.dumpAssets = true; }, nullptr },
        { "--replay-scrub-test",
          "--replay_scrub_test",
          []( ParsedArgs& args )
          {
              args.replayScrubProbe = true;
              args.replayScrubProbeNormalized = 0.25f;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = 1;
              args.fixedStep = true;
              args.suppressExitDialog = true;
          },
          "[replay] Scrub SkullScope probe enabled." },
        { "--replay-restore-test",
          "--replay_restore_test",
          []( ParsedArgs& args )
          {
              args.replayRestoreProbe = true;
              args.replayRestoreProbeNormalized = 0.25f;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = 1;
              args.fixedStep = true;
              args.suppressExitDialog = true;
          },
          "[replay] Restore hash SkullScope probe enabled." },
        { "--worker-self-test",
          "--workers-self-test",
          []( ParsedArgs& args )
          {
              args.workerSelfTest = true;
              args.suppressExitDialog = true;
          },
          "[workers] Self-test requested." },
    };

    for ( const CliFlagDirective& flag : kFlags )
    {
        if ( HasFlagDirective( commandLine, flag ) )
        {
            flag.apply( out );
            if ( flag.message )
            {
                fprintf( stdout, "%s\n", flag.message );
            }
        }
    }
}

bool ParseOnOffValue( const char* value, bool& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }
    if ( _stricmp( value, "on" ) == 0 || _stricmp( value, "true" ) == 0 || _stricmp( value, "yes" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _stricmp( value, "off" ) == 0 || _stricmp( value, "false" ) == 0 || _stricmp( value, "no" ) == 0 )
    {
        out = false;
        return true;
    }

    int numeric = 0;
    if ( ParseIntToken( value, numeric ) )
    {
        out = numeric != 0;
        return true;
    }
    return false;
}

bool ParseEnvironmentBool( const char* value, bool& out )
{
    if ( !value || *value == '\0' )
    {
        return false;
    }
    if ( _stricmp( value, "1" ) == 0 || _stricmp( value, "on" ) == 0 || _stricmp( value, "true" ) == 0 ||
         _stricmp( value, "yes" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _stricmp( value, "0" ) == 0 || _stricmp( value, "off" ) == 0 || _stricmp( value, "false" ) == 0 ||
         _stricmp( value, "no" ) == 0 )
    {
        out = false;
        return true;
    }
    return false;
}

bool ParseOptionalOnOffValue( const char* value, bool& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        out = true;
        return true;
    }
    return ParseOnOffValue( value, out );
}


bool ParseAllocationGuardModeValue( const char* value, RuntimeAllocation::RuntimeAllocationGuardMode& out )
{
    if ( IsOptionValueMissing( value ) || _stricmp( value, "measure" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Measure;
        return true;
    }
    if ( _stricmp( value, "off" ) == 0 || _stricmp( value, "none" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Off;
        return true;
    }
    if ( _stricmp( value, "gameplay" ) == 0 || _stricmp( value, "warn" ) == 0 || _stricmp( value, "warnings" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Gameplay;
        return true;
    }
    return false;
}


bool SceneArgHasPathSyntax( const std::string& sceneArg )
{
    return sceneArg.find( '/' ) != std::string::npos || sceneArg.find( '\\' ) != std::string::npos ||
           sceneArg.find( ':' ) != std::string::npos;
}


bool SceneArgHasExtension( const std::string& sceneArg )
{
    const size_t slash = sceneArg.find_last_of( "/\\" );
    const size_t dot = sceneArg.find_last_of( '.' );
    return dot != std::string::npos && ( slash == std::string::npos || dot > slash );
}


bool FileExistsForLaunch( const std::string& path )
{
    return _access( path.c_str(), 0 ) == 0;
}


std::string HeroSceneLaunchPath()
{
    return std::string( DATA_ROOT ) + "scenes/concept_12_low_poly_art_style.scene.json";
}


std::string ResolveSceneLaunchPath( const char* rawSceneArg )
{
    std::string sceneArg( rawSceneArg );
    if ( sceneArg.empty() || SceneArgHasPathSyntax( sceneArg ) )
    {
        return sceneArg;
    }

    if ( _stricmp( sceneArg.c_str(), "hero" ) == 0 || _stricmp( sceneArg.c_str(), "low_poly_hero" ) == 0 ||
         _stricmp( sceneArg.c_str(), "low-poly-hero" ) == 0 )
    {
        return HeroSceneLaunchPath();
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";
    if ( !SceneArgHasExtension( sceneArg ) )
    {
        const std::string sceneCandidate = sceneDir + sceneArg + ".scene.json";
        if ( FileExistsForLaunch( sceneCandidate ) )
        {
            return sceneCandidate;
        }
    }

    const std::string directCandidate = sceneDir + sceneArg;
    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return sceneArg;
}


std::string ResolveSuiteLaunchPath( const char* rawSuiteArg )
{
    std::string suiteArg( rawSuiteArg );
    if ( suiteArg.empty() || SceneArgHasPathSyntax( suiteArg ) )
    {
        return suiteArg;
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";
    if ( !SceneArgHasExtension( suiteArg ) )
    {
        const std::string suiteCandidate = sceneDir + suiteArg + ".suite.json";
        if ( FileExistsForLaunch( suiteCandidate ) )
        {
            return suiteCandidate;
        }
    }

    const std::string directCandidate = sceneDir + suiteArg;
    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return suiteArg;
}


bool ParsePhysicsDebugMode( const char* value, uint32_t& outFlags )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }
    if ( _stricmp( value, "none" ) == 0 || _stricmp( value, "off" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_NONE;
        return true;
    }
    if ( _stricmp( value, "axes" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_AXES;
        return true;
    }
    if ( _stricmp( value, "contacts" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_CONTACTS;
        return true;
    }
    if ( _stricmp( value, "sleep" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_SLEEP;
        return true;
    }
    if ( _stricmp( value, "pipeline" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_PIPELINE;
        return true;
    }
    if ( _stricmp( value, "terrain" ) == 0 || _stricmp( value, "terrain_contact" ) == 0 ||
         _stricmp( value, "terrain-contact" ) == 0 || _stricmp( value, "terrain_probe" ) == 0 ||
         _stricmp( value, "terrain-probe" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_TERRAIN_CONTACT;
        return true;
    }
    if ( _stricmp( value, "all" ) == 0 || _stricmp( value, "on" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_ALL;
        return true;
    }
    return false;
}

bool ApplyPhysicsDebugComponentOverride( const CommandLineView& commandLine,
                                         const char* dashedName,
                                         const char* underscoredName,
                                         uint32_t flag,
                                         ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, dashedName, underscoredName );
    if ( !value )
    {
        return true;
    }

    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "%s expects optional on|off.", dashedName );
    }

    if ( !out.hasPhysicsDebugFlagsOverride )
    {
        out.physicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
    }
    out.hasPhysicsDebugFlagsOverride = true;
    if ( enabled )
    {
        out.physicsDebugFlagsOverride |= flag;
    }
    else
    {
        out.physicsDebugFlagsOverride &= ~flag;
    }
    return true;
}

bool ApplyPhysicsDebugFloatOverride( const CommandLineView& commandLine,
                                     const PhysicsDebugFloatDirective& directive,
                                     ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, directive.dashedName, directive.underscoredName );
    if ( !value )
    {
        return true;
    }

    float parsed = 0.0f;
    if ( !ParseFloatToken( value, parsed ) || parsed < directive.minValue || parsed > directive.maxValue )
    {
        return FailCommandLineParse( directive.errorMessage );
    }

    out.*( directive.hasOverride ) = true;
    out.*( directive.value ) = parsed;
    if ( directive.enableTransparentBodies && !out.hasPhysicsDebugTransparentOverride )
    {
        out.hasPhysicsDebugTransparentOverride = true;
        out.physicsDebugTransparentOverride = true;
    }
    return true;
}

bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( commandLine, "--physics-debug", "--physics_debug" );
    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse(
                "--physics-debug expects none|axes|contacts|sleep|pipeline|terrain|all|on|off." );
        }
        out.hasPhysicsDebugFlagsOverride = true;
    }

    static const PhysicsDebugComponentDirective kComponentOverrides[] = {
        { "--physics-debug-axes", "--physics_debug_axes", PHYSICS_DEBUG_AXES },
        { "--physics-debug-contacts", "--physics_debug_contacts", PHYSICS_DEBUG_CONTACTS },
        { "--physics-debug-sleep", "--physics_debug_sleep", PHYSICS_DEBUG_SLEEP },
        { "--physics-debug-pipeline", "--physics_debug_pipeline", PHYSICS_DEBUG_PIPELINE },
        { "--physics-debug-terrain-contact", "--physics_debug_terrain_contact", PHYSICS_DEBUG_TERRAIN_CONTACT },
    };
    for ( const PhysicsDebugComponentDirective& component : kComponentOverrides )
    {
        if ( !ApplyPhysicsDebugComponentOverride( commandLine,
                                                  component.dashedName,
                                                  component.underscoredName,
                                                  component.flag,
                                                  out ) )
        {
            return false;
        }
    }

    const char* transparentValue =
        FindOptionValue( commandLine, "--physics-debug-transparent", "--physics_debug_transparent" );
    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }
        out.hasPhysicsDebugTransparentOverride = true;
    }

    static const PhysicsDebugFloatDirective kFloatOverrides[] = {
        { "--physics-debug-alpha",
          "--physics_debug_alpha",
          &ParsedArgs::hasPhysicsDebugAlphaOverride,
          &ParsedArgs::physicsDebugAlphaOverride,
          0.05f,
          1.0f,
          "--physics-debug-alpha expects 0.05..1.0.",
          true },
        { "--physics-debug-contact-linger",
          "--physics_debug_contact_linger",
          &ParsedArgs::hasPhysicsDebugContactLingerOverride,
          &ParsedArgs::physicsDebugContactLingerOverride,
          0.0f,
          5.0f,
          "--physics-debug-contact-linger expects 0.0..5.0 seconds.",
          false },
    };
    for ( const PhysicsDebugFloatDirective& directive : kFloatOverrides )
    {
        if ( !ApplyPhysicsDebugFloatOverride( commandLine, directive, out ) )
        {
            return false;
        }
    }

    if ( out.hasPhysicsDebugFlagsOverride )
    {
        fprintf( stdout, "[physics-debug] Flags override: 0x%02x\n", out.physicsDebugFlagsOverride );
    }
    if ( out.hasPhysicsDebugTransparentOverride )
    {
        fprintf( stdout,
                 "[physics-debug] Transparent bodies: %s\n",
                 out.physicsDebugTransparentOverride ? "on" : "off" );
    }
    if ( out.hasPhysicsDebugAlphaOverride )
    {
        fprintf( stdout, "[physics-debug] Body alpha: %.3f\n", out.physicsDebugAlphaOverride );
    }
    if ( out.hasPhysicsDebugContactLingerOverride )
    {
        fprintf( stdout, "[physics-debug] Contact linger: %.3fs\n", out.physicsDebugContactLingerOverride );
    }

    return true;
}

// Build the ordered list of scene paths from --suite or --scene.
// Falls back to a single empty string (generated demo mode) when neither flag is given.
bool ParseSceneArgs( const CommandLineView& commandLine, std::vector<std::string>& sceneList, bool& isSuiteOrSceneMode )
{
    const char* suiteArg = FindOptionValue( commandLine, "--suite" );
    const char* sceneArg = FindOptionValue( commandLine, "--scene" );
    const bool heroArg = HasOption( commandLine, "--hero" );
    const bool demoHeroArg = HasOption( commandLine, "--demohero" ) || HasOption( commandLine, "--demo-hero" );

    if ( ( suiteArg && sceneArg ) || ( heroArg && ( suiteArg || sceneArg ) ) ||
         ( demoHeroArg && ( suiteArg || sceneArg || heroArg ) ) )
    {
        return FailCommandLineParse( "--demohero, --hero, --suite, and --scene are mutually exclusive." );
    }

    if ( heroArg )
    {
        sceneList.push_back( HeroSceneLaunchPath() );
        isSuiteOrSceneMode = true;
        fprintf( stdout, "[scene] Hero scene selected.\n" );
    }
    else if ( suiteArg )
    {
        if ( IsOptionValueMissing( suiteArg ) )
        {
            return FailCommandLineParse( "--suite requires a path." );
        }

        // Resolve a suite JSON path from either a file token or a repository-relative path.
        const std::string suitePath = ResolveSuiteLaunchPath( suiteArg );

        std::ifstream suiteFile( suitePath );
        if ( !suiteFile )
        {
            return FailCommandLineParse( "--suite could not open '%s'.", suitePath.c_str() );
        }

        Json suite;
        try
        {
            suiteFile >> suite;
        }
        catch ( const std::exception& e )
        {
            return FailCommandLineParse( "--suite invalid JSON in '%s': %s", suitePath.c_str(), e.what() );
        }

        if ( !suite.is_object() )
        {
            return FailCommandLineParse( "--suite '%s' root must be an object.", suitePath.c_str() );
        }
        const auto formatIt = suite.find( "format" );
        if ( formatIt == suite.end() || !formatIt->is_string() ||
             formatIt->get<std::string>() != "skullbonez.suite.json" )
        {
            return FailCommandLineParse( "--suite '%s' must declare format skullbonez.suite.json.", suitePath.c_str() );
        }
        const auto scenesIt = suite.find( "scenes" );
        if ( scenesIt == suite.end() || !scenesIt->is_array() )
        {
            return FailCommandLineParse( "--suite '%s' must contain a scenes array.", suitePath.c_str() );
        }
        for ( const Json& scene : *scenesIt )
        {
            if ( !scene.is_string() )
            {
                return FailCommandLineParse( "--suite '%s' scenes entries must be strings.", suitePath.c_str() );
            }
            sceneList.push_back( ResolveSceneLaunchPath( scene.get<std::string>().c_str() ) );
        }
        isSuiteOrSceneMode = true;
    }
    else if ( sceneArg )
    {
        if ( IsOptionValueMissing( sceneArg ) )
        {
            return FailCommandLineParse( "--scene requires a path." );
        }

        if ( *sceneArg != '\0' )
        {
            // Support both quoted ("path with spaces") and unquoted tokens.
            // Quoted paths stop at the closing '"'; unquoted paths stop at whitespace.
            // This handles launchers (CDB, VS debugger) that wrap paths in quotes.
            sceneList.push_back( ResolveSceneLaunchPath( sceneArg ) );
            isSuiteOrSceneMode = true;
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // generated demo mode
    }
    return true;
}

bool ParseRendererArg( const CommandLineView& commandLine )
{
    static const RendererOption kRenderers[] = {
        { "dx12", "d3d12" },
    };

    const char* rendererArg = FindOptionValue( commandLine, "--renderer" );
    if ( !rendererArg )
    {
        return true;
    }

    if ( IsOptionValueMissing( rendererArg ) )
    {
        return FailCommandLineParse( "--renderer expects dx12. GL and DX11 are retired runtime choices." );
    }

    for ( const RendererOption& renderer : kRenderers )
    {
        if ( _stricmp( rendererArg, renderer.name ) == 0 ||
             ( renderer.alias && _stricmp( rendererArg, renderer.alias ) == 0 ) )
        {
            return true;
        }
    }

    return FailCommandLineParse( "--renderer expects dx12. GL and DX11 are retired runtime choices." );
}

// --vsync on|off patches the already-loaded startup config.
bool ApplyVsyncOverride( const CommandLineView& commandLine, EngineConfig& config )
{
    const char* vsyncArg = FindOptionValue( commandLine, "--vsync" );
    if ( !vsyncArg )
    {
        return true;
    }

    bool enabled = false;
    if ( !ParseOnOffValue( vsyncArg, enabled ) )
    {
        return FailCommandLineParse( "--vsync expects on|off." );
    }

    config.runtimeRender.vsyncEnabled = enabled;
    fprintf( stdout, "[vsync] %s via command line.\n", enabled ? "Enabled" : "Disabled" );
    return true;
}


bool ApplyCinematicShadowsOverride( const char* value, ParsedArgs& args, EngineConfig& config )
{
    static_cast<void>( config );
    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "--shadows expects optional on|off." );
    }

    args.hasCinematicShadowsOverride = true;
    args.cinematicShadows = enabled;
    fprintf( stdout, "[shadows] Shadow maps %s via command line.\n", enabled ? "enabled" : "disabled" );
    return true;
}


bool ApplyStartupCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out, EngineConfig& config )
{
    static const ConfigCliValueDirective kValues[] = {
        { "--switch-interval",
          nullptr,
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( value );
              static_cast<void>( args );
              static_cast<void>( config );
              return FailCommandLineParse( "--switch-interval is retired because DX12 is the only runtime renderer." );
          } },
        { "--time-scale",
          nullptr,
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              float timeScale = 0.0f;
              if ( !ParseFloatToken( value, timeScale ) || timeScale <= 0.0f )
              {
                  return FailCommandLineParse( "--time-scale expects a positive float." );
              }
              args.timeScaleOverride = timeScale;
              fprintf( stdout, "[time-scale] Override: %.4f\n", timeScale );
              return true;
          } },
        { "--tornado",
          nullptr,
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--tornado expects optional on|off." );
              }
              args.hasTornadoOverride = true;
              args.tornadoEnabled = enabled;
              fprintf( stdout, "[tornado] Force field %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--tornado-vectors",
          "--tornado-vector-field",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--tornado-vectors expects optional on|off." );
              }
              args.tornadoVectors = enabled;
              fprintf( stdout,
                       "[tornado] Velocity-field vectors %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--cinematic",
          "--cinematic-rendering",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--cinematic expects optional on|off." );
              }
              args.hasCinematicRenderingOverride = true;
              args.cinematicRendering = enabled;
              fprintf( stdout, "[cinematic] Rendering %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--shadows", "--shadow-maps", ApplyCinematicShadowsOverride },
        { "--cinematic-shadows", "--cinematic_shadows", ApplyCinematicShadowsOverride },
        { "--workers",
          "--worker-threads",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              int workerThreads = 0;
              const int maxWorkerThreads = WorkerPool::MaxThreadCount();
              if ( !ParseIntToken( value, workerThreads ) || workerThreads < -1 || workerThreads > maxWorkerThreads )
              {
                  char message[128] = {};
                  snprintf( message, sizeof( message ), "--workers expects -1, 0, or 1..%d.", maxWorkerThreads );
                  return FailCommandLineParse( message );
              }
              config.workerThreads = workerThreads;
              fprintf( stdout,
                       "[workers] Override: %d (resolved %d, max %d)\n",
                       config.workerThreads,
                       WorkerPool::ResolveThreadCount( config.workerThreads ),
                       maxWorkerThreads );
              return true;
          } },
        { "--model-capacity",
          nullptr,
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              int capacity = 0;
              if ( !ParseIntToken( value, capacity ) || capacity < 1 || capacity > MAX_GAME_MODELS )
              {
                  return FailCommandLineParse( "--model-capacity expects 1..%d.", MAX_GAME_MODELS );
              }
              config.gameModelCapacity = capacity;
              fprintf( stdout,
                       "[models] Active model capacity: %d (compiled max %d)\n",
                       config.gameModelCapacity,
                       MAX_GAME_MODELS );
              return true;
          } },
        { "--physics-parallel",
          "--parallel-physics",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--physics-parallel expects optional on|off." );
              }
              config.physicsParallel = enabled;
              config.physicsParallelApplyForces = enabled;
              config.physicsParallelTornadoField = enabled;
              config.physicsParallelNarrowphase = enabled;
              config.physicsParallelTerrainDetect = enabled;
              config.physicsParallelIntegrate = enabled;
              fprintf( stdout,
                       "[workers] Physics parallel jobs %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--shadow-parallel-prep",
          "--parallel-shadow-prep",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--shadow-parallel-prep expects optional on|off." );
              }
              config.shadowParallelPrep = enabled;
              fprintf( stdout,
                       "[workers] Shadow parallel prep %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--interactive",
          "--hold",
          []( const char* value, ParsedArgs& args, EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--interactive expects optional on|off." );
              }
              args.interactiveRun = enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              if ( enabled )
              {
                  fprintf( stdout, "[scene] Interactive hold enabled; scene automation will not quit the app.\n" );
              }
              return true;
          } },
    };

    return ApplyConfigCliValueDirectives( commandLine, out, config, kValues );
}


bool ApplyLiveStyleControlDir( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--live-style-control expects a directory path." );
    }
    if ( strlen( value ) >= sizeof( args.liveStyleControlDir ) )
    {
        return FailCommandLineParse( "--live-style-control path is too long." );
    }

    strcpy_s( args.liveStyleControlDir, sizeof( args.liveStyleControlDir ), value );
    args.interactiveRun = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[style-harness] Live style control directory: %s\n", args.liveStyleControlDir );
    return true;
}


bool ApplySceneSnapshotOutPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--scene-snapshot-out expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.sceneSnapshotOutPath ) )
    {
        return FailCommandLineParse( "--scene-snapshot-out path is too long." );
    }

    strcpy_s( args.sceneSnapshotOutPath, sizeof( args.sceneSnapshotOutPath ), value );
    args.sceneLoadOnly = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[scene-load-only] Snapshot output: %s\n", args.sceneSnapshotOutPath );
    return true;
}


bool ApplyMemoryDumpPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value, "--memory-dump", args.memoryDumpPath, sizeof( args.memoryDumpPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[memory] Dump output: %s\n", args.memoryDumpPath );
    return true;
}

bool ApplyInteractionScriptPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value,
                          "--interaction-script",
                          args.interactionScriptPath,
                          sizeof( args.interactionScriptPath ) ) )
    {
        return false;
    }

    args.interactiveRun = true;
    args.suppressExitDialog = true;
    args.replayRecording = true;
    fprintf( stdout, "[interaction] Script input: %s\n", args.interactionScriptPath );
    return true;
}

bool ApplyInteractionReportPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value,
                          "--interaction-report",
                          args.interactionReportPath,
                          sizeof( args.interactionReportPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[interaction] Report output: %s\n", args.interactionReportPath );
    return true;
}


bool ApplyReplayHashLogPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-hashes expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayHashLogPath ) )
    {
        return FailCommandLineParse( "--replay-hashes path is too long." );
    }

    strcpy_s( args.replayHashLogPath, sizeof( args.replayHashLogPath ), value );
    args.replayRecording = true;
    args.replayExplicit = true;
    fprintf( stdout, "[replay] Hash log: %s\n", args.replayHashLogPath );
    return true;
}

bool ApplyReplaySaveProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-save-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replaySaveProbePath ) )
    {
        return FailCommandLineParse( "--replay-save-probe path is too long." );
    }

    strcpy_s( args.replaySaveProbePath, sizeof( args.replaySaveProbePath ), value );
    args.replaySaveProbe = true;
    args.replayRecording = true;
    args.replayExplicit = true;
    args.replaySeconds = (std::max)( 1, args.replaySeconds );
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Save probe output: %s\n", args.replaySaveProbePath );
    return true;
}

bool ApplyReplayLoadPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-load expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayLoadPath ) )
    {
        return FailCommandLineParse( "--replay-load path is too long." );
    }

    strcpy_s( args.replayLoadPath, sizeof( args.replayLoadPath ), value );
    args.replayLoad = true;
    args.interactiveRun = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Load artifact: %s\n", args.replayLoadPath );
    return true;
}

bool ApplyReplayLoadProbePath( const char* value, ParsedArgs& args )
{
    if ( !ApplyReplayLoadPath( value, args ) )
    {
        return false;
    }

    args.replayLoadProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Load probe input: %s\n", args.replayLoadPath );
    return true;
}

bool ApplyReplayRestoreFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreFileProbePath, sizeof( args.replayRestoreFileProbePath ), value );
    args.replayRestoreFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore file probe input: %s\n", args.replayRestoreFileProbePath );
    return true;
}

bool ApplyReplayRestoreTargetFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-target-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreTargetFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-target-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreTargetFileProbePath, sizeof( args.replayRestoreTargetFileProbePath ), value );
    args.replayRestoreTargetFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore target probe input: %s\n", args.replayRestoreTargetFileProbePath );
    return true;
}

bool ApplyReplayRestoreBranchFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-branch-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreBranchFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-branch-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreBranchFileProbePath, sizeof( args.replayRestoreBranchFileProbePath ), value );
    args.replayRestoreBranchFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore branch probe input: %s\n", args.replayRestoreBranchFileProbePath );
    return true;
}

bool ApplyReplayRestoreFailureFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-failure-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreFailureFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-failure-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreFailureFileProbePath, sizeof( args.replayRestoreFailureFileProbePath ), value );
    args.replayRestoreFailureFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore failure probe input: %s\n", args.replayRestoreFailureFileProbePath );
    return true;
}


bool ApplyRunCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliValueDirective kValues[] = {
        { "--seed",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--seed expects a positive 32-bit integer." );
              }
              args.seedOverride = seed;
              fprintf( stdout, "[seed] Override: %u\n", args.seedOverride );
              return true;
          } },
        { "--frames",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames <= 0 )
              {
                  return FailCommandLineParse( "--frames expects a positive integer." );
              }
              args.frameCountOverride = frames;
              args.suppressExitDialog = true;
              fprintf( stdout, "[frames] Exit after %d frames.\n", args.frameCountOverride );
              return true;
          } },
        { "--allocation-guard",
          "--allocation_guard",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              RuntimeAllocation::RuntimeAllocationGuardMode mode = RuntimeAllocation::RuntimeAllocationGuardMode::Off;
              if ( !ParseAllocationGuardModeValue( value, mode ) )
              {
                  return FailCommandLineParse( "--allocation-guard expects off|measure|gameplay." );
              }
              args.allocationGuardMode = mode;
              fprintf( stdout,
                       "[allocation-guard] Requested mode: %s\n",
                       RuntimeAllocation::RuntimeAllocationGuardModeName( mode ) );
              return true;
          } },
        { "--live-style-control", "--style-harness", ApplyLiveStyleControlDir },
        { "--live_style_control", "--style_harness", ApplyLiveStyleControlDir },
        { "--scene-snapshot-out", "--scene_snapshot_out", ApplySceneSnapshotOutPath },
        { "--memory-dump", "--memory_dump", ApplyMemoryDumpPath },
        { "--interaction-script", "--interaction_script", ApplyInteractionScriptPath },
        { "--interaction-report", "--interaction_report", ApplyInteractionReportPath },
        { "--replay",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--replay expects optional on|off." );
              }
              args.replayRecording = enabled;
              args.replayExplicit = true;
              fprintf( stdout, "[replay] Capture %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--replay-seconds",
          "--replay_seconds",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int seconds = 0;
              if ( !ParseIntToken( value, seconds ) || seconds < 1 || seconds > 600 )
              {
                  return FailCommandLineParse( "--replay-seconds expects 1..600." );
              }
              args.replaySeconds = seconds;
              args.replayExplicit = true;
              fprintf( stdout, "[replay] Retention window: %d seconds.\n", args.replaySeconds );
              return true;
          } },
        { "--replay-scrub-probe",
          "--replay_scrub_probe",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              float normalized = 0.0f;
              if ( !ParseFloatToken( value, normalized ) || normalized < 0.0f || normalized >= 0.995f )
              {
                  return FailCommandLineParse(
                      "--replay-scrub-probe expects a normalized position in the range 0..0.995." );
              }
              args.replayScrubProbe = true;
              args.replayScrubProbeNormalized = normalized;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = (std::max)( 1, args.replaySeconds );
              args.fixedStep = true;
              args.suppressExitDialog = true;
              fprintf( stdout, "[replay] Scrub probe normalized position: %.3f\n", args.replayScrubProbeNormalized );
              return true;
          } },
        { "--replay-restore-probe",
          "--replay_restore_probe",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              float normalized = 0.0f;
              if ( !ParseFloatToken( value, normalized ) || normalized < 0.0f || normalized >= 0.995f )
              {
                  return FailCommandLineParse(
                      "--replay-restore-probe expects a normalized position in the range 0..0.995." );
              }
              args.replayRestoreProbe = true;
              args.replayRestoreProbeNormalized = normalized;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = (std::max)( 1, args.replaySeconds );
              args.fixedStep = true;
              args.suppressExitDialog = true;
              fprintf( stdout,
                       "[replay] Restore probe normalized position: %.3f\n",
                       args.replayRestoreProbeNormalized );
              return true;
          } },
        { "--replay-save-probe", "--replay_save_probe", ApplyReplaySaveProbePath },
        { "--replay-save-test", "--replay_save_test", ApplyReplaySaveProbePath },
        { "--replay-load", "--replay_load", ApplyReplayLoadPath },
        { "--replay-play", "--replay_play", ApplyReplayLoadPath },
        { "--replay-load-probe", "--replay_load_probe", ApplyReplayLoadProbePath },
        { "--replay-restore-file-probe", "--replay_restore_file_probe", ApplyReplayRestoreFileProbePath },
        { "--replay-restore-target-file-probe",
          "--replay_restore_target_file_probe",
          ApplyReplayRestoreTargetFileProbePath },
        { "--replay-restore-branch-file-probe",
          "--replay_restore_branch_file_probe",
          ApplyReplayRestoreBranchFileProbePath },
        { "--replay-restore-failure-file-probe",
          "--replay_restore_failure_file_probe",
          ApplyReplayRestoreFailureFileProbePath },
        { "--replay-hashes", "--replay_hashes", ApplyReplayHashLogPath },
        { "--ui-stress",
          "--ui_stress",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--ui-stress expects optional on|off." );
              }
              args.uiStress = enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              return true;
          } },
        { "--ui-stress-seed",
          "--ui_stress_seed",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--ui-stress-seed expects a positive 32-bit integer." );
              }
              args.uiStress = true;
              args.uiStressSeed = seed;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--ui-stress-actions",
          "--ui_stress_actions",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int actions = 0;
              if ( !ParseIntToken( value, actions ) || actions <= 0 || actions > 32 )
              {
                  return FailCommandLineParse( "--ui-stress-actions expects 1..32." );
              }
              args.uiStress = true;
              args.uiStressActions = actions;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress",
          "--graphics_stress",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--graphics-stress expects optional on|off." );
              }
              args.graphicsStress = enabled;
              args.interactiveRun = args.interactiveRun || enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              return true;
          } },
        { "--graphics-stress-seed",
          "--graphics_stress_seed",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--graphics-stress-seed expects a positive 32-bit integer." );
              }
              args.graphicsStress = true;
              args.graphicsStressSeed = seed;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-actions",
          "--graphics_stress_actions",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int actions = 0;
              if ( !ParseIntToken( value, actions ) || actions <= 0 || actions > 64 )
              {
                  return FailCommandLineParse( "--graphics-stress-actions expects 1..64." );
              }
              args.graphicsStress = true;
              args.graphicsStressActions = actions;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-scene-interval",
          "--graphics_stress_scene_interval",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames <= 0 || frames > 600 )
              {
                  return FailCommandLineParse( "--graphics-stress-scene-interval expects 1..600 frames." );
              }
              args.graphicsStress = true;
              args.graphicsStressSceneIntervalFrames = frames;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-memory-interval",
          "--graphics_stress_memory_interval",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames < 0 || frames > 36000 )
              {
                  return FailCommandLineParse( "--graphics-stress-memory-interval expects 0..36000 frames." );
              }
              args.graphicsStress = true;
              args.graphicsStressMemoryIntervalFrames = frames;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
    };

    return ApplyCliValueDirectives( commandLine, out, kValues );
}

bool ApplyGeneratedObjectOverride( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const GeneratedObjectOverrideDirective kOverrides[] = {
        { "--all-balls", GeneratedObjectTypeOverride::AllBalls, "[objects] Generated objects forced to balls." },
        { "--all-boxes", GeneratedObjectTypeOverride::AllBoxes, "[objects] Generated objects forced to boxes." },
    };

    const GeneratedObjectOverrideDirective* selected = nullptr;
    for ( const GeneratedObjectOverrideDirective& directive : kOverrides )
    {
        if ( !HasOption( commandLine, directive.optionName ) )
        {
            continue;
        }
        if ( selected )
        {
            return FailCommandLineParse( "--all-balls and --all-boxes are mutually exclusive." );
        }
        selected = &directive;
    }

    if ( selected )
    {
        out.objectTypeOverride = selected->objectType;
        fprintf( stdout, "%s\n", selected->message );
    }
    return true;
}


bool HandleContactAudioSmoke( const ParsedArgs& args, const EngineConfig& cfg, int& outExitCode )
{
    if ( !args.contactAudioSmoke )
    {
        return false;
    }

    // Concept: this smoke path proves decode, voice submission, and counters
    // without creating a window, renderer, worker pool, or physics world.
    SkullbonezCore::Runtime::Audio::ContactAudioService audio;
    audio.SetMasterGain( cfg.contactAudio.masterGain );
    audio.SetMaxDistanceScale( cfg.contactAudio.maxDistanceScale );
    audio.SetRollingLevelDb( cfg.contactAudio.rollingLevelDb );
    audio.SetRollingMaxDistance( cfg.contactAudio.rollingMaxDistance );
    audio.SetRollingMinSlipSpeed( cfg.contactAudio.rollingMinSlipSpeed );
    audio.SetRollingVoicesPerWindow( static_cast<uint32_t>( cfg.contactAudio.rollingVoicesPerWindow ) );
    const bool initialized = audio.Initialize();
    const bool loaded = audio.LoadContactAudioMap( "SkullbonezData/audio/contact_audio.materials.json" );
    const bool submitted = initialized && loaded && audio.PlaySmokeImpact( HashStr( "earth" ), 6.0f );
    Sleep( 350 );
    const SkullbonezCore::Runtime::Audio::ContactAudioStats& stats = audio.Stats();
    CreateDirectoryA( "TestOutput", nullptr );
    FILE* report = nullptr;
    if ( fopen_s( &report, "TestOutput/contact_audio_smoke.json", "w" ) == 0 && report )
    {
        fprintf( report,
                 "{\n"
                 "  \"initialized\": %s,\n"
                 "  \"loaded\": %s,\n"
                 "  \"submitted\": %s,\n"
                 "  \"eventsSeen\": %u,\n"
                 "  \"rejectedByThreshold\": %u,\n"
                 "  \"rejectedByCooldown\": %u,\n"
                 "  \"submittedVoices\": %u,\n"
                 "  \"droppedVoices\": %u\n"
                 "}\n",
                 initialized ? "true" : "false",
                 loaded ? "true" : "false",
                 submitted ? "true" : "false",
                 stats.eventsSeen,
                 stats.rejectedByThreshold,
                 stats.rejectedByCooldown,
                 stats.submittedVoices,
                 stats.droppedVoices );
        fclose( report );
    }
    fprintf( stdout,
             "[audio-smoke] initialized=%d loaded=%d submitted=%d events=%u threshold=%u cooldown=%u voices=%u "
             "dropped=%u report=TestOutput/contact_audio_smoke.json\n",
             initialized ? 1 : 0,
             loaded ? 1 : 0,
             submitted ? 1 : 0,
             stats.eventsSeen,
             stats.rejectedByThreshold,
             stats.rejectedByCooldown,
             stats.submittedVoices,
             stats.droppedVoices );
    fflush( stdout );
    outExitCode = submitted ? 0 : 1;
    return true;
}


// Guards --physics-regression-log against use in non-Debug builds.
// False means startup should abort.
bool ValidatePhysicsRegressionLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-regression-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-regression-log is only supported in Debug builds. Recompile with the Debug "
                                 "configuration to use physics regression logging." );
#else
    return true;
#endif
}


// Guards --physics-collision-time-log against use in non-Debug builds.
// False means startup should abort.
bool ValidatePhysicsCollisionTimeLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-collision-time-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-collision-time-log is only supported in Debug builds. Recompile with the "
                                 "Debug configuration to use collision-time logging." );
#else
    return true;
#endif
}


// Guards --physics-diag / --physics-diagnostics against use in non-Debug builds.
// Diagnostics traces are model-facing debug artifacts and are not a Profile/Release dependency.
bool ValidatePhysicsDiagnostics( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-diag" ) && !HasOption( commandLine, "--physics-diagnostics" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-diag is only supported in Debug builds. Recompile with the Debug "
                                 "configuration to use queryable physics diagnostics." );
#else
    return true;
#endif
}


// Guards the replay scrub SkullScope probe against use in non-Debug builds.
bool ValidateReplayScrubProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-scrub-test" ) && !HasOption( commandLine, "--replay_scrub_test" ) &&
         !HasOption( commandLine, "--replay-scrub-probe" ) && !HasOption( commandLine, "--replay_scrub_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse(
        "--replay-scrub-probe is only supported in Debug builds with SkullScope diagnostics." );
#else
    return true;
#endif
}

// Guards the replay restore hash SkullScope probe against use in non-Debug builds.
bool ValidateReplayRestoreProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-test" ) && !HasOption( commandLine, "--replay_restore_test" ) &&
         !HasOption( commandLine, "--replay-restore-probe" ) && !HasOption( commandLine, "--replay_restore_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse(
        "--replay-restore-probe is only supported in Debug builds with SkullScope diagnostics." );
#else
    return true;
#endif
}

// Guards the replay v2 save probe against use in non-Debug builds.
bool ValidateReplaySaveProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-save-probe" ) && !HasOption( commandLine, "--replay_save_probe" ) &&
         !HasOption( commandLine, "--replay-save-test" ) && !HasOption( commandLine, "--replay_save_test" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-save-probe is only supported in Debug builds." );
#else
    return true;
#endif
}

// Guards the replay v2 load probe against use in non-Debug builds.
bool ValidateReplayLoadProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-load-probe" ) && !HasOption( commandLine, "--replay_load_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-load-probe is only supported in Debug builds." );
#else
    return true;
#endif
}

// Guards the saved replay checkpoint restore probe against use in non-Debug builds.
bool ValidateReplayRestoreFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}

// Guards the saved replay checkpoint-plus-event target restore probe against use in non-Debug builds.
bool ValidateReplayRestoreTargetFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-target-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_target_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-target-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}

// Guards the saved replay checkpoint-plus-event branch-from-file probe against use in non-Debug builds.
bool ValidateReplayRestoreBranchFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-branch-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_branch_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-branch-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}

// Guards the saved replay expected-failure probe against use without SkullScope diagnostics.
bool ValidateReplayRestoreFailureFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-failure-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_failure_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-failure-file-probe is only supported in Debug builds." );
#else
    if ( !HasOption( commandLine, "--physics-diag" ) && !HasOption( commandLine, "--physics-diagnostics" ) )
    {
        return FailCommandLineParse(
            "--replay-restore-failure-file-probe requires --physics-diag so SkullScope can query the failure row." );
    }
    return true;
#endif
}

#ifdef _DEBUG
bool ParsePhysicsRegressionLogOverride( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* physLogArg = FindOptionValue( commandLine, "--physics-regression-log" );
    if ( !physLogArg )
    {
        return true;
    }

    if ( !CopyOptionPath( physLogArg, "--physics-regression-log", outPath, sizeof( outPath ) ) )
    {
        return false;
    }

    fprintf( stdout, "[physics-regression-log] Output: %s\n", outPath );
    return true;
}


bool ParsePhysicsCollisionTimeLogOverride( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* collisionLogArg = FindOptionValue( commandLine, "--physics-collision-time-log" );
    if ( !collisionLogArg )
    {
        return true;
    }

    if ( !CopyOptionPath( collisionLogArg, "--physics-collision-time-log", outPath, sizeof( outPath ) ) )
    {
        return false;
    }

    fprintf( stdout, "[physics-collision-time-log] Output: %s\n", outPath );
    return true;
}


bool ParsePhysicsDiagnosticsPath( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* diagArg = FindOptionValue( commandLine, "--physics-diag" );
    if ( !diagArg )
    {
        diagArg = FindOptionValue( commandLine, "--physics-diagnostics" );
    }
    if ( !diagArg )
    {
        return true;
    }

    return CopyOptionPath( diagArg, "--physics-diag", outPath, sizeof( outPath ) );
}
#endif

// ParsedArgs owns all command-line option state after this pass.
// Also loads engine.cfg and applies any overrides to the passed startup config.
// False means startup should abort, such as --physics-regression-log in Release.
bool ParseCommandLine( const CommandLineView& commandLine, EngineConfig& config, ParsedArgs& out )
{
    if ( !ParseSceneArgs( commandLine, out.sceneList, out.isSuiteOrSceneMode ) )
    {
        return false;
    }
    if ( !ParseRendererArg( commandLine ) )
    {
        return false;
    }

    config.Load( ( std::string( DATA_ROOT ) + "engine.cfg" ).c_str() );
    if ( !ApplyVsyncOverride( commandLine, config ) )
    {
        return false;
    }

    if ( !ValidatePhysicsRegressionLog( commandLine ) )
    {
        return false;
    }
    if ( !ValidatePhysicsCollisionTimeLog( commandLine ) )
    {
        return false;
    }
    if ( !ValidatePhysicsDiagnostics( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayScrubProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplaySaveProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayLoadProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreTargetFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreBranchFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreFailureFileProbe( commandLine ) )
    {
        return false;
    }

#ifdef _DEBUG
    if ( !ParsePhysicsRegressionLogOverride( commandLine, out.physicsRegressionLogOverride ) )
    {
        return false;
    }
    if ( !ParsePhysicsCollisionTimeLogOverride( commandLine, out.physicsCollisionTimeLogOverride ) )
    {
        return false;
    }
    if ( !ParsePhysicsDiagnosticsPath( commandLine, out.physicsDiagnosticsPath ) )
    {
        return false;
    }
    out.physicsDiagnosticsRequested = out.physicsDiagnosticsPath[0] != '\0';
#endif

    if ( !ApplyStartupCliValueDirectives( commandLine, out, config ) )
    {
        return false;
    }

    ApplyCliFlagDirectives( commandLine, out );

    bool envPlatformProfilerMarkers = false;
    char* envPlatformProfilerValue = nullptr;
    size_t envPlatformProfilerValueLen = 0;
    if ( _dupenv_s( &envPlatformProfilerValue, &envPlatformProfilerValueLen, "SKULLBONEZ_PLATFORM_PROFILER_MARKERS" ) ==
             0 &&
         ParseEnvironmentBool( envPlatformProfilerValue, envPlatformProfilerMarkers ) )
    {
        out.platformProfilerMarkers = envPlatformProfilerMarkers;
        out.platformProfilerMarkersExplicit = true;
        if ( envPlatformProfilerMarkers )
        {
            fprintf( stdout,
                     "[platform-profiler] Marker emission requested via SKULLBONEZ_PLATFORM_PROFILER_MARKERS.\n" );
        }
    }
    free( envPlatformProfilerValue );

    bool envPixMarkers = false;
    char* envPixValue = nullptr;
    size_t envPixValueLen = 0;
    if ( _dupenv_s( &envPixValue, &envPixValueLen, "SKULLBONEZ_PIX_MARKERS" ) == 0 &&
         ParseEnvironmentBool( envPixValue, envPixMarkers ) )
    {
        out.platformProfilerMarkers = envPixMarkers;
        out.platformProfilerMarkersExplicit = true;
        if ( envPixMarkers )
        {
            fprintf(
                stdout,
                "[platform-profiler] Marker emission requested via SKULLBONEZ_PIX_MARKERS compatibility alias.\n" );
        }
    }
    free( envPixValue );

#ifdef _DEBUG
    if ( out.physicsDiagnosticsRequested )
    {
        if ( !out.fixedStep )
        {
            out.fixedStep = true;
            out.fixedStepForcedByPhysicsDiagnostics = true;
            fprintf( stdout, "[physics-diag] Enabled: forcing fixed_step for deterministic queryable trace.\n" );
        }
        else
        {
            fprintf( stdout, "[physics-diag] Enabled: fixed_step already active.\n" );
        }
        fprintf( stdout, "[physics-diag] Output: %s\n", out.physicsDiagnosticsPath );
    }
#endif

    if ( !ApplyRunCliValueDirectives( commandLine, out ) )
    {
        return false;
    }

#ifdef _DEBUG
    if ( out.replayScrubProbe )
    {
        if ( !out.replayRecording )
        {
            return FailCommandLineParse( "--replay-scrub-probe requires replay capture; remove --replay off." );
        }
        if ( !out.physicsDiagnosticsRequested )
        {
            return FailCommandLineParse(
                "--replay-scrub-probe requires --physics-diag so SkullScope can query the result." );
        }
    }
    if ( out.replaySaveProbe && !out.replayRecording )
    {
        return FailCommandLineParse( "--replay-save-probe requires replay capture; remove --replay off." );
    }
#endif

    if ( out.uiStress )
    {
        fprintf( stdout, "[ui-stress] Enabled seed=%u actions=%d.\n", out.uiStressSeed, out.uiStressActions );
    }
    if ( out.graphicsStress )
    {
        fprintf( stdout,
                 "[graphics-stress] Enabled seed=%u actions=%d scene_interval_frames=%d memory_interval_frames=%d.\n",
                 out.graphicsStressSeed,
                 out.graphicsStressActions,
                 out.graphicsStressSceneIntervalFrames,
                 out.graphicsStressMemoryIntervalFrames );
    }

    if ( !ApplyGeneratedObjectOverride( commandLine, out ) )
    {
        return false;
    }

    if ( !ParsePhysicsDebugOverrides( commandLine, out ) )
    {
        return false;
    }

    if ( out.dumpConfig )
    {
        config.Dump( stdout );
    }

    PlatformProfiler::SetEnabled( out.platformProfilerMarkers );
    PlatformProfiler::SetDetailedRangesEnabled( out.platformProfilerMarkers && out.platformProfilerMarkersExplicit );
    if ( out.platformProfilerMarkers )
    {
        fprintf( stdout,
                 PlatformProfiler::IsAvailable()
                     ? "[platform-profiler] Platform profiler marker emission enabled.\n"
                     : "[platform-profiler] Platform profiler marker emission unavailable in this build; continuing "
                       "with in-engine profiler markers only.\n" );
    }

    return true;
}

// ---------------------------------------------------------------------------
// Render backend
// ---------------------------------------------------------------------------

RuntimeRenderBackendView InitRenderBackend( Window* window )
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
    auto backend = std::make_unique<RenderBackendDX12>();
    // Lifetime: SetGfxBackend takes ownership. Runtime render code keeps a
    // borrowed raytracing facet in RuntimeRenderBackendView instead of reopening
    // DXR through the global renderer accessor.
    RenderBackendDX12* renderBackend = backend.get();
    RuntimeRenderBackendView renderBackendView;
    renderBackendView.renderBackend = renderBackend;
    renderBackendView.rayTracingBackend = renderBackend;
    backend->Init( window->m_sWindow, window->m_sDevice, window->m_sWindowDimensions.x, window->m_sWindowDimensions.y );
    SetGfxBackend( std::move( backend ) );
    return renderBackendView;
}

// ---------------------------------------------------------------------------
// Main run
// Run is scoped here so its destructor releases render-owned resources
// before the DX12 backend and the Win32 window are torn down.
// ---------------------------------------------------------------------------

int RunApp( Window* window,
            ParsedArgs& args,
            EngineConfig& cfg,
            WorkerPool& workerPool,
            RuntimeRenderBackendView renderBackendView )
{
    {
        std::unique_ptr<Run> cRun =
            std::make_unique<Run>( *window, std::move( args.sceneList ), cfg, workerPool, renderBackendView );
        cRun->SetAllocationGuardMode( args.allocationGuardMode );
        if ( args.timeScaleOverride > 0.0f )
        {
            cRun->SetTimeScaleOverride( args.timeScaleOverride );
        }
        if ( args.fixedStep )
        {
            cRun->SetFixedStepOverride();
        }
        if ( args.seedOverride > 0 )
        {
            cRun->SetSeedOverride( args.seedOverride );
        }
        if ( args.noWater )
        {
            cRun->SetNoWaterOverride();
        }
        if ( args.noSleep )
        {
            cRun->SetNoSleepOverride();
        }
        if ( args.noContactAudio )
        {
            cRun->SetNoContactAudioOverride();
        }
        if ( args.hasTornadoOverride )
        {
            cRun->SetTornadoOverride( args.tornadoEnabled );
        }
        if ( args.tornadoVectors )
        {
            cRun->SetTornadoVectorFieldOverride( true );
        }
        if ( args.hasCinematicRenderingOverride )
        {
            cRun->SetCinematicRenderingOverride( args.cinematicRendering );
        }
        if ( args.hasCinematicShadowsOverride )
        {
            cRun->SetCinematicShadowsOverride( args.cinematicShadows );
        }
        if ( args.demoHeroStyle )
        {
            cRun->SetDemoHeroStyleOverride();
        }
        if ( args.interactiveRun )
        {
            cRun->SetInteractiveRunOverride();
        }
        if ( args.liveStyleControlDir[0] != '\0' )
        {
            cRun->SetLiveStyleControlDirectory( args.liveStyleControlDir );
        }
        if ( args.frameCountOverride > 0 )
        {
            cRun->SetFrameCountOverride( args.frameCountOverride );
        }
        if ( args.uiStress )
        {
            cRun->SetUIStressOverride( args.uiStressSeed, args.uiStressActions );
        }
        if ( args.graphicsStress )
        {
            cRun->SetGraphicsStressOverride( args.graphicsStressSeed,
                                             args.graphicsStressActions,
                                             args.graphicsStressSceneIntervalFrames,
                                             args.graphicsStressMemoryIntervalFrames );
        }
        if ( args.memoryDumpPath[0] != '\0' )
        {
            cRun->SetMainMemoryDumpPath( args.memoryDumpPath );
        }
        const bool replayDefaultAllowed =
            !args.isSuiteOrSceneMode || args.interactiveRun || args.liveStyleControlDir[0] != '\0';
        const bool replayEnabled =
            args.replayExplicit ? args.replayRecording : ( args.replayRecording && replayDefaultAllowed );
        if ( replayEnabled || args.replayHashLogPath[0] != '\0' )
        {
            cRun->SetReplayRecording( true,
                                      args.replaySeconds,
                                      args.replayHashLogPath[0] != '\0' ? args.replayHashLogPath : nullptr );
        }
#ifdef _DEBUG
        if ( args.replayScrubProbe )
        {
            cRun->SetReplayScrubProbe( args.replayScrubProbeNormalized );
        }
        if ( args.replayRestoreProbe )
        {
            cRun->SetReplayRestoreProbe( args.replayRestoreProbeNormalized );
        }
        if ( args.replaySaveProbe )
        {
            cRun->SetReplaySaveProbe( args.replaySaveProbePath );
        }
#endif
        if ( args.showProfiler )
        {
            cRun->SetInitialOverlayMode( OverlayMode::Timers );
        }
        if ( args.hideTopText )
        {
            cRun->SetTopTextHidden( true );
        }
        if ( args.showBroadphaseVisualizer )
        {
            cRun->SetBroadphaseVisualizerEnabled( true );
        }
        if ( args.objectTypeOverride != GeneratedObjectTypeOverride::Mixed )
        {
            cRun->SetGeneratedObjectTypeOverride( args.objectTypeOverride );
        }
        if ( args.hasPhysicsDebugFlagsOverride )
        {
            cRun->SetPhysicsDebugFlagsOverride( args.physicsDebugFlagsOverride );
        }
        if ( args.hasPhysicsDebugTransparentOverride )
        {
            cRun->SetPhysicsDebugTransparentOverride( args.physicsDebugTransparentOverride );
        }
        if ( args.hasPhysicsDebugAlphaOverride )
        {
            cRun->SetPhysicsDebugAlphaOverride( args.physicsDebugAlphaOverride );
        }
        if ( args.hasPhysicsDebugContactLingerOverride )
        {
            cRun->SetPhysicsDebugContactLingerOverride( args.physicsDebugContactLingerOverride );
        }
#ifdef _DEBUG
        if ( args.physicsRegressionLogOverride[0] != '\0' )
        {
            cRun->SetPhysicsRegressionLogOverride( args.physicsRegressionLogOverride );
        }
        if ( args.physicsCollisionTimeLogOverride[0] != '\0' )
        {
            cRun->SetPhysicsCollisionTimeLogOverride( args.physicsCollisionTimeLogOverride );
        }
        if ( args.physicsDiagnosticsPath[0] != '\0' )
        {
            cRun->SetPhysicsDiagnosticsPath( args.physicsDiagnosticsPath, args.fixedStepForcedByPhysicsDiagnostics );
        }
#endif
        try
        {
            cRun->Initialise();
            if ( args.interactionScriptPath[0] != '\0' )
            {
                cRun->SetInteractionAutomation(
                    args.interactionScriptPath,
                    args.interactionReportPath[0] != '\0' ? args.interactionReportPath : nullptr );
            }
            bool skipExecute = false;
            if ( args.replayLoad )
            {
                if ( !cRun->LoadReplayPresentationArtifact( args.replayLoadPath, true ) )
                {
                    throw std::runtime_error( "failed to load replay v2 presentation artifact" );
                }
            }
#ifdef _DEBUG
            if ( args.replayLoadProbe )
            {
                cRun->VerifyLoadedReplayPresentationProbe( 0.25f );
                skipExecute = true;
            }
            if ( args.replayRestoreFileProbe )
            {
                cRun->VerifyReplaySolverCheckpointFileProbe( args.replayRestoreFileProbePath );
                skipExecute = true;
            }
            if ( args.replayRestoreTargetFileProbe )
            {
                cRun->VerifyReplaySolverTargetFileProbe( args.replayRestoreTargetFileProbePath );
                skipExecute = true;
            }
            if ( args.replayRestoreBranchFileProbe )
            {
                cRun->VerifyReplaySolverBranchFileProbe( args.replayRestoreBranchFileProbePath );
                skipExecute = true;
            }
            if ( args.replayRestoreFailureFileProbe )
            {
                cRun->VerifyReplaySolverFailureFileProbe( args.replayRestoreFailureFileProbePath );
                skipExecute = true;
            }
#endif
            if ( args.dumpAssets )
            {
                cRun->DumpTextureAssets( stdout );
            }
            if ( args.sceneLoadOnly )
            {
                cRun->RunSceneLoadOnly( args.sceneSnapshotOutPath[0] != '\0' ? args.sceneSnapshotOutPath : nullptr );
            }
            else if ( !skipExecute )
            {
                cRun->Execute();
                if ( args.graphicsStress )
                {
                    printf( "[graphics-stress] Execute returned.\n" );
                    fflush( stdout );
                }
            }

            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( "Thanks for using the Skullbonez Core!", "Alert!", MB_OK );
            }
        }
        catch ( const std::exception& e )
        {
            Log().WriteEventf( "fatal_exception message=\"%s\"", e.what() );
            fprintf( stderr, "FATAL: %s\n", e.what() );
            fflush( stderr );
            Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( e.what(), "Alert!", MB_OK );
            }
            return 1;
        }
    } // cRun destroyed here before backend/window cleanup
    return 0;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void CleanupWindow( Window* window, HINSTANCE hInstance )
{
    // Lifetime: disarm callback-fed input queues while the HWND still names
    // the window that WndProc used, before backend/window class teardown.
    if ( window->m_sWindow )
    {
        Input::UnbindCallbackBridge( window->m_sWindow );
    }
    Input::UnbindWindow( *window );
    window->SetResizeRenderBackend( nullptr );
    DestroyGfxBackend();

    if ( window->m_sDevice )
    {
        ReleaseDC( window->m_sWindow, window->m_sDevice );
    }

    if ( window->m_fIsFullScreenMode )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        Input::SetSystemCursorVisible( true );
    }

    UnregisterClass( WINDOW_NAME, hInstance );
    window->Destroy();
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow )
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);

    // Floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    hPrevInstance;
    iCmdShow;

    const CommandLineView commandLine = TokenizeCommandLine( szCmdLine );

#ifdef _DEBUG
    InstallDebugCrashLogger();
    Log().WriteEventf( "process_started command_line=\"%s\"", szCmdLine ? szCmdLine : "" );
    if ( HasOption( commandLine, "--debug-crash-test" ) )
    {
        Log().WriteEventf( "debug_crash_test_requested" );
        volatile int* crashAddress = nullptr;
        *crashAddress = 1;
    }
#endif

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs — without this, MSCTF.dll throws 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;
    if ( HandleGenAtlas( commandLine, atlasExitCode ) )
    {
        return atlasExitCode;
    }

    EngineConfig& cfg = EngineConfig::Instance();

    ParsedArgs args;
    if ( !ParseCommandLine( commandLine, cfg, args ) )
    {
        const char* error = GetCommandLineError();
        fprintf( stderr, "FATAL: %s\n", error );
        MessageBoxA( nullptr, error, "Command line parse failed", MB_OK | MB_ICONERROR );
        CoUninitialize();
        return 1;
    }
    RuntimeAllocation::SetRuntimeAllocationGuardMode( args.allocationGuardMode );
    if ( RuntimeAllocation::RuntimeAllocationGuardEnabled() )
    {
        fprintf( stdout,
                 "[allocation-guard] Enabled mode=%s. Startup, scene, backend, gameplay, replay, capture, and shutdown "
                 "allocations will be summarized at process end.\n",
                 RuntimeAllocation::RuntimeAllocationGuardModeName( args.allocationGuardMode ) );
    }

    int contactAudioSmokeExitCode = 0;
    if ( HandleContactAudioSmoke( args, cfg, contactAudioSmokeExitCode ) )
    {
        CoUninitialize();
        return contactAudioSmokeExitCode;
    }

    int standalonePhysicsExitCode = 0;
    if ( HandlePhysicsStandaloneSmoke( commandLine, standalonePhysicsExitCode ) )
    {
        CoUninitialize();
        return standalonePhysicsExitCode;
    }

    WorkerPool& workerPool = WorkerPool::Instance();
    workerPool.Initialise( cfg.workerThreads );
    if ( args.workerSelfTest )
    {
        const bool workersOk = RunWorkerSystemSelfTest( workerPool, stdout );
        workerPool.Shutdown();
        CoUninitialize();
        return workersOk ? 0 : 1;
    }

    Window* window = Window::Instance();
    window->SetStartupWindowSize( cfg.window.screenX, cfg.window.screenY );
    window->SetProjectionFrustum( cfg.frustumNear, cfg.frustumFar );
    window->CreateAppWindow( hInstance, cfg.window.fullscreen );
    window->m_sDevice = GetDC( window->m_sWindow );

    const RuntimeRenderBackendView renderBackendView = InitRenderBackend( window );
    window->SetResizeRenderBackend( renderBackendView.renderBackend );
    window->HandleScreenResize();

    const int runExitCode = RunApp( window, args, cfg, workerPool, renderBackendView );

    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Shutdown );
        workerPool.Shutdown();
        CleanupWindow( window, hInstance );
    }
    RuntimeAllocation::PrintRuntimeAllocationSummary( stdout );
    int finalExitCode = runExitCode;
    if ( RuntimeAllocation::GetRuntimeAllocationGuardMode() ==
             RuntimeAllocation::RuntimeAllocationGuardMode::Gameplay &&
         RuntimeAllocation::RuntimeAllocationGuardHasGameplayViolations() && finalExitCode == 0 )
    {
        fprintf( stdout, "[allocation-guard] FAIL: gameplay allocation guard detected policy violations.\n" );
        finalExitCode = 9;
    }

    CoUninitialize();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    return finalExitCode;
}
