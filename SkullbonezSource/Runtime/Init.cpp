/*
File: SkullbonezSource/Runtime/Init.cpp
Purpose:
  Bootstraps the Windows process, parses command-line options, and starts the run loop.

Summary:
  Init.cpp bootstraps the Windows process, parses command-line options, and
  starts the run loop. As an implementation unit, keep edits anchored on local
  owner boundaries and call direction and on the glossary/invariants below.

Glossary:
  DX11/OpenGL: Retired runtime renderer choices. The parser names them only to
  explain why old command lines are rejected.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  and platform APIs through reference-counted objects.
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.
  Standalone physics smoke: Early-exit validation mode that exercises public
    physics API construction without runtime/window/renderer ownership.
  Runtime handle smoke: Early-exit validation mode that uses runtime
    SceneController construction but proves returned physics handles stay
    aligned with body, collider, constraint, and render mirrors.
  Lane R result: Recoverable CLI/startup failure that returns a process exit
    code with owner/message diagnostics instead of using a fatal exception.

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
#include "WindowConstants.h"
#include "../Core/Log.h"
#include "Audio/ContactAudioService.h"
#include "Run.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "../Rendering/Text.h"
#include "Window.h"
#include "Input.h"
#include "../Core/Timer.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "RunLaunchOptions.Renderer.h"
#include "Startup/StartupCommandLine.h"
#include "Startup/StartupCrashLogging.h"
#include "Startup/StartupLaunchResolution.h"
#include "Startup/StartupProbeHarnesses.h"
#include "Scene/SceneController.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../World/WorldEnvironment.h"
#include "../Core/PlatformProfiler.h"
#include "../Core/Profiler.h"
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


using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::Startup;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Threading;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


namespace
{

void ReportStartupFailure( const SkullbonezCore::Core::SbResult& result, const char* title )
{
    const char* safeOwner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Startup";
    const char* safeMessage =
        result.error.message[0] != '\0' ? result.error.message : "Startup failed without details.";
    char dialogMessage[1024] = {};
    sprintf_s( dialogMessage, sizeof( dialogMessage ), "%s\n\n%s", safeOwner, safeMessage );

    // Lane R: startup cannot rely on the game window or an attached terminal to
    // expose failures. Persist the diagnostic and block on a native error dialog
    // so a normal Explorer/IDE launch can never look like a silent clean exit.
    SkullbonezCore::Core::Log().WriteEventf( "startup_failure owner=\"%s\" message=\"%s\"", safeOwner, safeMessage );
    fprintf( stderr, "FATAL[%s]: %s\n", safeOwner, safeMessage );
    fflush( stderr );
    SkullbonezCore::Core::Log().FlushAll();
    MessageBoxA( nullptr, dialogMessage, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND );
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

// GUI apps have no console by default â€” attach to the parent terminal so
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


// ---------------------------------------------------------------------------
// --gen-atlas early exit
// SDF font atlas file generation path: exits before GPU context setup.
// True means the flag was present; outExitCode is 0 on success, 1 on failure.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Command-line parsing
// ---------------------------------------------------------------------------


} // anonymous namespace


namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
// Build the ordered list of scene paths from --suite or --scene.
// Falls back to a single empty string (generated demo mode) when neither flag is given.

// --vsync on|off patches the already-loaded startup config.


} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore


namespace
{

// Guards --physics-regression-log against use in non-Debug builds.
// False means startup should abort.


// Guards --physics-collision-time-log against use in non-Debug builds.
// False means startup should abort.


// Guards --physics-diag / --physics-diagnostics against use in non-Debug builds.
// Diagnostics traces are model-facing debug artifacts and are not a Profile/Release dependency.


// Guards the replay scrub SkullScope probe against use in non-Debug builds.

// Guards the replay restore hash SkullScope probe against use in non-Debug builds.

// Guards the replay v2 save probe against use in non-Debug builds.

// Guards the replay v2 load probe against use in non-Debug builds.

// Guards the saved replay checkpoint restore probe against use in non-Debug builds.

// Guards the saved replay checkpoint-plus-event target restore probe against use in non-Debug builds.

// Guards the saved replay checkpoint-plus-event branch-from-file probe against use in non-Debug builds.

// Guards the saved replay expected-failure probe against use without SkullScope diagnostics.


// ParsedArgs owns all command-line option state after this pass.
// Also loads engine.cfg and applies any overrides to the passed startup config.
// False means startup should abort, such as --physics-regression-log in Release.

// ---------------------------------------------------------------------------
// Render backend
// ---------------------------------------------------------------------------

SkullbonezCore::Core::SbResult InitRenderBackend( Window* window,
                                                  RuntimeRenderBackendView& renderBackendView,
                                                  std::unique_ptr<RenderBackendDX12>& outBackend )
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
    auto backend = std::make_unique<RenderBackendDX12>();
    RenderBackendDX12* renderBackend = backend.get();
    const SkullbonezCore::Core::SbResult renderInitResult = renderBackend->Init( window->NativeWindowHandle(),
                                                                                 window->NativeDeviceContext(),
                                                                                 window->ClientWidth(),
                                                                                 window->ClientHeight() );
    if ( !renderInitResult.ok )
    {
        // Lane R: render backend startup probes the host graphics environment.
        // Failures are reported at process bootstrap before any runtime borrows
        // are published into RuntimeRenderBackendView.
        renderBackendView = RuntimeRenderBackendView();
        return renderInitResult;
    }

    // Lifetime: the process bootstrap owns the backend unique_ptr. Runtime
    // render code keeps borrowed capability facets in RuntimeRenderBackendView
    // and must let them die before shutdown resets the owner.
    renderBackendView.deviceLifecycle = renderBackend;
    renderBackendView.renderCommands = renderBackend;
    renderBackendView.renderResources = renderBackend;
    renderBackendView.renderDiagnostics = renderBackend;
    renderBackendView.captureBackend = renderBackend;
    renderBackendView.rayTracingBackend = renderBackend;
    renderBackendView.shaderDevelopment = renderBackend;
    outBackend = std::move( backend );
    return SkullbonezCore::Core::SbResult::Success();
}

// ---------------------------------------------------------------------------
// Main run
// Run is scoped here so its destructor releases render-owned resources
// before the DX12 backend and the Win32 window are torn down.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------

int RunApp( Window* window,
            ParsedArgs& args,
            SkullbonezCore::Core::EngineConfig& cfg,
            WorkerPool& workerPool,
            SkullbonezCore::Core::Profiler* profiler,
            RuntimeRenderBackendView renderBackendView )
{
    {
        std::unique_ptr<Run> cRun =
            std::make_unique<Run>( *window, std::move( args.sceneList ), cfg, workerPool, profiler, renderBackendView );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        struct ProfilerRenderDiagnosticsLifetime
        {
            SkullbonezCore::Core::Profiler* profiler = nullptr;
            ~ProfilerRenderDiagnosticsLifetime()
            {
                if ( profiler )
                {
                    profiler->BindRenderDiagnostics( nullptr );
                }
            }
        };
        // Lifetime: this guard is declared after cRun, so it clears SkullbonezCore::Core::Profiler's
        // renderer-diagnostics borrow before Run's destructor releases
        // backend-owned resources through the still-live DX12 backend.
        ProfilerRenderDiagnosticsLifetime profilerRenderDiagnosticsLifetime{ profiler };
#endif
        const RunStartupOverrides startupOverrides = BuildRunStartupOverrides( args );
        auto reportRunResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeOwner =
                result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Runtime";
            const char* safeMessage =
                result.error.message[0] != '\0' ? result.error.message : "recoverable runtime operation failed";
            SkullbonezCore::Core::Log().WriteEventf( "recoverable_failure owner=\"%s\" message=\"%s\"",
                                                     safeOwner,
                                                     safeMessage );
            fprintf( stderr, "[runtime] Recoverable failure owner=%s reason=\"%s\"\n", safeOwner, safeMessage );
            fflush( stderr );
            SkullbonezCore::Core::Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( safeMessage, "Runtime Failure", MB_OK );
            }
            return 1;
        };
        auto reportInteractionAutomationResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeMessage =
                result.error.message[0] != '\0' ? result.error.message : "interaction automation failed";
            SkullbonezCore::Core::Log().WriteEventf( "interaction_automation_failed message=\"%s\"", safeMessage );
            fprintf( stderr, "[interaction] Automation failed: %s\n", safeMessage );
            fflush( stderr );
            SkullbonezCore::Core::Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( safeMessage, "Interaction Automation Failed", MB_OK );
            }
            return 1;
        };

        const SkullbonezCore::Core::SbResult startupResult = cRun->ApplyStartupOverrides( startupOverrides );
        if ( !startupResult.ok )
        {
            return reportInteractionAutomationResult( startupResult );
        }

        cRun->Initialise();
        if ( !cRun->LastSceneLoadResult().ok )
        {
            return reportRunResult( cRun->LastSceneLoadResult() );
        }
        if ( args.sceneLoadOnly )
        {
            const SkullbonezCore::Core::SbResult sceneLoadOnlyResult =
                cRun->RunSceneLoadOnly( args.sceneSnapshotOutPath[0] != '\0' ? args.sceneSnapshotOutPath : nullptr );
            if ( !sceneLoadOnlyResult.ok )
            {
                return reportRunResult( sceneLoadOnlyResult );
            }
        }
        else
        {
            const SkullbonezCore::Core::SbResult executeResult = cRun->Execute();
            if ( !executeResult.ok )
            {
                if ( executeResult.error.owner && strcmp( executeResult.error.owner, "InteractionAutomation" ) == 0 )
                {
                    return reportInteractionAutomationResult( executeResult );
                }
                return reportRunResult( executeResult );
            }
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
    } // cRun destroyed here before backend/window cleanup
    return 0;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void CleanupWindow( Window* window, HINSTANCE hInstance, std::unique_ptr<RenderBackendDX12>& renderBackend )
{
    // Lifetime: disarm callback-fed input queues while the HWND still names
    // the window that WndProc used, before backend/window class teardown.
    const HWND windowHandle = window->NativeWindowHandle();
    if ( windowHandle )
    {
        Input::UnbindCallbackBridge( windowHandle );
    }
    Input::UnbindWindow( *window );
    window->SetResizeRenderLifecycle( nullptr );
    renderBackend.reset();

    window->ReleaseDeviceContext();

    if ( window->IsFullScreenMode() )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        Input::SetSystemCursorVisible( true );
    }

    UnregisterClass( WINDOW_NAME, hInstance );
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
    SkullbonezCore::Core::Log().WriteEventf( "process_started command_line=\"%s\"", szCmdLine ? szCmdLine : "" );
    if ( HasOption( commandLine, "--debug-crash-test" ) )
    {
        SkullbonezCore::Core::Log().WriteEventf( "debug_crash_test_requested" );
        volatile int* crashAddress = nullptr;
        *crashAddress = 1;
    }
#endif

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs â€” without this, MSCTF.dll throws 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;
    if ( HandleGenAtlas( commandLine, atlasExitCode ) )
    {
        return atlasExitCode;
    }

    SkullbonezCore::Core::EngineConfig cfg;

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

    WorkerPool workerPool;
    workerPool.Initialise( cfg.runtimeCapacity.workerThreads );
    if ( args.workerSelfTest )
    {
        const bool workersOk = RunWorkerSystemSelfTest( workerPool, stdout );
        workerPool.Shutdown();
        CoUninitialize();
        return workersOk ? 0 : 1;
    }

    Window windowOwner;
    Window* window = &windowOwner;
    window->SetStartupWindowSize( cfg.window.screenX, cfg.window.screenY );
    window->SetProjectionFrustum( cfg.camera.frustumNear, cfg.camera.frustumFar );
    const SkullbonezCore::Core::SbResult windowResult =
        window->CreateAppWindow( hInstance, cfg.window.fullscreen, !args.automationWindowHidden );
    if ( !windowResult.ok )
    {
        ReportStartupFailure( windowResult, "SkullbonezCore Startup Failed" );
        workerPool.Shutdown();
        CoUninitialize();
        return 1;
    }
    window->AcquireDeviceContext();

    RuntimeRenderBackendView renderBackendView;
    std::unique_ptr<RenderBackendDX12> renderBackend;
    const SkullbonezCore::Core::SbResult renderBackendResult =
        InitRenderBackend( window, renderBackendView, renderBackend );
    if ( !renderBackendResult.ok )
    {
        ReportStartupFailure( renderBackendResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }
    window->SetResizeRenderLifecycle( renderBackendView.deviceLifecycle );
    const SkullbonezCore::Core::SbResult initialResizeResult = window->HandleScreenResize();
    if ( !initialResizeResult.ok )
    {
        ReportStartupFailure( initialResizeResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }

    SkullbonezCore::Core::Profiler* profiler = nullptr;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    // Why: SkullbonezCore::Core::Profiler remains the sanctioned diagnostics singleton, but runtime
    // owners receive this startup borrow instead of resolving it mid-frame.
    profiler = &SkullbonezCore::Core::Profiler::Instance();
    profiler->BindRenderDiagnostics( renderBackendView.renderDiagnostics );
#endif

    const int runExitCode = RunApp( window, args, cfg, workerPool, profiler, renderBackendView );

    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Shutdown );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        profiler->BindRenderDiagnostics( nullptr );
#endif
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
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
