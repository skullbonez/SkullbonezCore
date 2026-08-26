/*
File: SkullbonezSource/Runtime/App/Init.cpp
Purpose:
  Owns process-level startup sequencing after focused startup owners resolve
  command-line, launch, probe, and crash-reporting concerns.

Summary:
  Establishes the Windows process environment, delegates early-exit
  startup work, constructs the window and DX12 backend, and starts the run loop.

Glossary:
  Startup owner: A focused helper unit that parses options, resolves launch
    policy, runs an early probe, or installs crash diagnostics before Run exists.
  Manual profiler lifetime: Development-build Tracy ownership explicitly
    bracketed around every engine thread instead of static initialization.

Invariants:
  - Startup owners finish option resolution before Run owns subsystems, keeping
    validation launches deterministic from their command line.
  - Early-exit smoke modes must return before worker, window, renderer, or Run
    startup if their evidence claims subsystem isolation.
  - Startup-selected Tracy begins before the initial WorkerPool. An interactive
    Standard start recreates that pool before simulation resumes, and Tracy
    still stops after all workers join while platform logging remains alive.
  - Every normal WinMain return reports the diagnostic store's active and
    session high-water counts while the App-owned store is still alive.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/TracyClientOwner.h"
#include "../Input/Input.h"
#include "ReplayPredictionRetainedGeometry.h"
#include "Run.h"
#include "StartupInputApplication.h"
#include "../Startup/StartupCommandLine.h"
#include "../Startup/StartupCrashLogging.h"
#include "../Startup/StartupLaunchResolution.h"
#include "../Startup/StartupProbeHarnesses.h"
#include "../Startup/Window.h"
#include "../../Core/WindowConstants.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <objbase.h>


using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::Startup;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Threading;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;


namespace
{

void ReportStartupFailure( const SkullbonezCore::Core::SbResult& result, const char* title )
{
    const char* safeOwner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner() : "Startup";
    const char* safeMessage = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage() : "Startup failed without details.";

    char dialogMessage[1024] = {};

    sprintf_s( dialogMessage, sizeof( dialogMessage ), "%s\n\n%s", safeOwner, safeMessage );

    // Recoverable error: startup cannot rely on the game window or an attached terminal to
    // expose failures. Persist the diagnostic and block on a native error dialog
    // so a normal Explorer/IDE launch can never look like a silent clean exit.
    SkullbonezCore::Core::Log().WriteEventf( "startup_failure owner=\"%s\" message=\"%s\"", safeOwner, safeMessage );
    fprintf( stderr, "FATAL[%s]: %s\n", safeOwner, safeMessage );
    fflush( stderr );
    SkullbonezCore::Core::Log().FlushAll();
    MessageBoxA( nullptr, dialogMessage, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND );
}


// Console


// GUI apps have no console by default; attach to the parent terminal so
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


// Render backend


SkullbonezCore::Core::SbResult InitRenderBackend( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, Window* window,
                                                  std::unique_ptr<RenderBackendDX12>& outBackend )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
    auto backend = std::make_unique<RenderBackendDX12>( diagnostics );
    RenderBackendDX12* renderBackend = backend.get();
    const SkullbonezCore::Core::SbResult
        renderInitResult = renderBackend->Init( window->NativeWindowHandle(), window->NativeDeviceContext(),
                                                window->ClientWidth(), window->ClientHeight(),
                                                ReplayOverlay::PREDICTION_RETAINED_RIBBON_SHADER_BASE_NAME );

    if ( !renderInitResult.Ok() )
    {
        // Recoverable error: render backend startup probes the host graphics environment.
        // Failures are reported at process bootstrap before any runtime borrows
        // are published.
        return renderInitResult;
    }

    // Invariant: Prediction owns the logical record layout and configures the
    // generic retained-geometry lane while BackendInit still owns all cold
    // allocation. Frame code receives the geometry owner only after this succeeds.
    if ( !renderBackend->Geometry().ConfigureRetainedGeometryCapacity( ReplayOverlay::PredictionRetainedGeometryCapacity() ) )
    {
        return diagnostics.Failure( "Runtime/Prediction",
                                    "Retained geometry capacity exceeds the renderer's cold safety maximum" );
    }

    // Lifetime: process bootstrap owns the concrete backend. Run binds named
    // owners from it synchronously and releases every borrow before shutdown.
    outBackend = std::move( backend );
    return SkullbonezCore::Core::SbResult::Success();
}

int RunApp( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, Window* window, ParsedArgs& args,
            SkullbonezCore::Core::EngineConfig& cfg, WorkerPool& workerPool, SkullbonezCore::Core::Profiler* profiler,
            RenderBackendDX12& renderBackend, SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner )
{
    // Lifetime: Run releases all render-owned resources before its borrowed
    // DX12 backend and Win32 window are torn down by the process owner.
    {
        std::unique_ptr<Run> cRun = std::make_unique<Run>( diagnostics, *window, std::move( args.sceneList ), cfg,
                                                           workerPool, profiler, renderBackend.BackbufferCapture(),
                                                           tracyClientOwner );

        const RunStartupOverrides startupOverrides = BuildRunStartupOverrides( args );
        auto reportRunResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeOwner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner() : "Runtime";

            const char* safeMessage = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage()
                                                                       : "recoverable runtime operation failed";

            SkullbonezCore::Core::Log().WriteEventf( "recoverable_failure owner=\"%s\" message=\"%s\"", safeOwner,
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
            const char* safeMessage = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage()
                                                                       : "interaction automation failed";

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

        const SkullbonezCore::Core::SbResult bindResult = cRun->BindRenderBackend( renderBackend );

        if ( !bindResult.Ok() )
        {
            return reportRunResult( bindResult );
        }

        const SkullbonezCore::Core::SbResult startupResult = cRun->ApplyStartupOverrides( startupOverrides );

        if ( !startupResult.Ok() )
        {
            return reportInteractionAutomationResult( startupResult );
        }

        cRun->Initialise();

        if ( !cRun->LastSceneLoadResult().Ok() )
        {
            const SkullbonezCore::Core::SbResult startupExit =
                cRun->FinalizeInteractionAutomationReport( cRun->LastSceneLoadResult() );
            return reportRunResult( startupExit );
        }

        if ( args.sceneLoadOnly )
        {
            const SkullbonezCore::Core::SbResult sceneLoadOnlyResult = cRun->RunSceneLoadOnly( args.sceneSnapshotOutPath[0] != '\0' ? args.sceneSnapshotOutPath : nullptr );

            if ( !sceneLoadOnlyResult.Ok() )
            {
                return reportRunResult( sceneLoadOnlyResult );
            }
        }
        else
        {
            const SkullbonezCore::Core::SbResult executeResult = cRun->Execute();

            if ( !executeResult.Ok() )
            {
                if ( executeResult.ErrorOwner() && strcmp( executeResult.ErrorOwner(), "InteractionAutomation" ) == 0 )
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


// Cleanup


void CleanupWindow( Window* window, HINSTANCE instance, std::unique_ptr<RenderBackendDX12>& renderBackend )
{
    // Lifetime: disarm callback-fed input queues while the HWND still names
    // the window that WndProc used, before backend/window class teardown.
    const HWND windowHandle = window->NativeWindowHandle();

    if ( windowHandle )
    {
        SkullbonezCore::Hardware::Input::UnbindCallbackBridge( windowHandle );
        SkullbonezCore::Hardware::Input::UnbindNativeWindow( windowHandle );
    }

    renderBackend.reset();

    window->ReleaseDeviceContext();

    if ( window->IsFullScreenMode() )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        SkullbonezCore::Hardware::Input::SetSystemCursorVisible( true );
    }

    if ( !window->DestroyAppWindow() )
    {
        std::fprintf( stderr, "[startup] DestroyWindow failed during cleanup. win32_error=%lu\n",
                      static_cast<unsigned long>( GetLastError() ) );
    }

    UnregisterClass( WINDOW_NAME, instance );
}


int ReportDiagnosticStoreSession( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int exitCode ) noexcept
{
    const std::uint32_t activeEntries = diagnostics.ActiveEntryCount();
    const std::uint32_t sessionHighWater = diagnostics.SessionHighWater();

    // Invariant: both counter reads release the store lock before either sink
    // runs. Shutdown reporting must never call logging while holding the
    // diagnostic publication lock.
    fprintf( stdout, "[diagnostics] active_entries=%u session_high_water=%u capacity=%zu\n", activeEntries, sessionHighWater,
             SkullbonezCore::Core::SbDiagnosticStore::CAPACITY );

    SkullbonezCore::Core::Log().WriteEventf( "diagnostic_store_session active_entries=%u session_high_water=%u capacity=%zu",
                                             activeEntries, sessionHighWater,
                                             SkullbonezCore::Core::SbDiagnosticStore::CAPACITY );

    SkullbonezCore::Core::Log().FlushAll();
    return exitCode;
}

} // anonymous namespace


// Entry point


int WINAPI WinMain( HINSTANCE instance, HINSTANCE previousInstance, PSTR commandLineText, int showCommand )
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);

    // Floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    previousInstance;
    showCommand;

    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    const CommandLineView commandLine = TokenizeCommandLine( commandLineText );

#ifdef _DEBUG
    InstallDebugCrashLogger();
    SkullbonezCore::Core::Log().WriteEventf( "process_started command_line=\"%s\"", commandLineText ? commandLineText : "" );

    if ( HasOption( commandLine, "--debug-crash-test" ) )
    {
        SkullbonezCore::Core::Log().WriteEventf( "debug_crash_test_requested" );
        volatile int* crashAddress = nullptr;
        *crashAddress = 1;
    }
#endif

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs; without this, MSCTF.dll reports 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;

    if ( HandleGenAtlas( commandLine, atlasExitCode ) )
    {
        return ReportDiagnosticStoreSession( diagnostics, atlasExitCode );
    }

    SkullbonezCore::Core::EngineConfig cfg;

    ParsedArgs args;

    if ( !ParseCommandLine( diagnostics, commandLine, cfg, args ) )
    {
        const char* error = GetCommandLineError();
        SkullbonezCore::Core::Log().WriteEventf( "startup_failure owner=\"Startup/CommandLine\" message=\"%s\"", error );

        fprintf( stderr, "FATAL: %s\n", error );
        fflush( stderr );
        SkullbonezCore::Core::Log().FlushAll();

        // Hazard: validation owns no interactive desktop. A modal parse-error
        // dialog would hide the already-reported failure behind an infinite
        // wait, so hidden automation receives the same diagnostic and exits.
        if ( !HasOption( commandLine, "--automation-hidden-window" ) )
        {
            MessageBoxA( nullptr, error, "Command line parse failed", MB_OK | MB_ICONERROR | MB_SETFOREGROUND );
        }

        CoUninitialize();
        return ReportDiagnosticStoreSession( diagnostics, 1 );
    }

    CoreAllocation::SetRuntimeAllocationGuardMode( args.allocationGuardMode );

    if ( CoreAllocation::RuntimeAllocationGuardEnabled() )
    {
        fprintf( stdout,
                 "[allocation-guard] Enabled mode=%s. Startup, scene, backend, gameplay, replay, capture, and shutdown "
                 "allocations will be summarized at process end.\n",
                 CoreAllocation::RuntimeAllocationGuardModeName( args.allocationGuardMode ) );
    }

    int standalonePhysicsExitCode = 0;

    if ( HandlePhysicsStandaloneSmoke( diagnostics, commandLine, standalonePhysicsExitCode ) )
    {
        CoUninitialize();
        return ReportDiagnosticStoreSession( diagnostics, standalonePhysicsExitCode );
    }

    SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClient = nullptr;
#if defined( TRACY_ENABLE )
    // Lifetime: this owner starts before WorkerPool creates instrumentable
    // threads and is explicitly stopped after their joins on every exit path.
    SkullbonezCore::Core::DevelopmentTools::TracyClientOwner tracyClientOwner;
    tracyClientOwner.Start();
    tracyClient = &tracyClientOwner;
#endif

    // Lifetime: declaration order keeps the Debug lock graph alive until after
    // WorkerPool joins and destroys every mutex borrow.
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    workerPool.Initialise( cfg.runtimeCapacity.workerThreads );

    if ( args.workerSelfTest )
    {
        const bool workersOk = RunWorkerSystemSelfTest( workerPool, stdout );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CoUninitialize();
        return ReportDiagnosticStoreSession( diagnostics, workersOk ? 0 : 1 );
    }

    Window windowOwner( diagnostics );
    Window* window = &windowOwner;
    window->SetStartupWindowSize( cfg.window.screenX, cfg.window.screenY );
    window->SetProjectionFrustum( cfg.camera.frustumNear, cfg.camera.frustumFar );
    const SkullbonezCore::Core::SbResult windowResult = window->CreateAppWindow( instance, cfg.window.fullscreen,
                                                                                 !args.automationWindowHidden );

    if ( !windowResult.Ok() )
    {
        ReportStartupFailure( windowResult, "SkullbonezCore Startup Failed" );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CoUninitialize();
        return ReportDiagnosticStoreSession( diagnostics, 1 );
    }

    const HWND nativeWindow = window->NativeWindowHandle();
    std::unique_ptr<RenderBackendDX12> renderBackend;
    SkullbonezCore::Hardware::Input::BindNativeWindow( nativeWindow );
    SkullbonezCore::Hardware::Input::BindCallbackBridge( nativeWindow );
    const SkullbonezCore::Core::SbResult rawMouseResult =
        SkullbonezCore::Hardware::Input::RegisterRawMouseInput( diagnostics, nativeWindow );
    const auto shutdownDevelopmentTools = [&]()
    {
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
    };
    const SkullbonezCore::Core::SbResult rendererStartupResult =
        SkullbonezCore::Runtime::StartRendererAfterRawMouseRegistration(
            rawMouseResult,
            []( const SkullbonezCore::Core::SbResult& failure, const char* title )
            { ReportStartupFailure( failure, title ); },
            [&]() { workerPool.Shutdown(); }, shutdownDevelopmentTools,
            [&]() { CleanupWindow( window, instance, renderBackend ); }, []() { CoUninitialize(); },
            [&]()
            {
                SkullbonezCore::Hardware::Input::SetSystemCursorVisible( false );
                window->AcquireDeviceContext();
                return InitRenderBackend( diagnostics, window, renderBackend );
            } );

    if ( !rendererStartupResult.Ok() )
    {
        return ReportDiagnosticStoreSession( diagnostics, 1 );
    }

    const SkullbonezCore::Core::SbResult initialResizeResult = renderBackend->Frame().Resize( window->ClientWidth(),
                                                                                              window->ClientHeight() );

    if ( !initialResizeResult.Ok() )
    {
        ReportStartupFailure( initialResizeResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CleanupWindow( window, instance, renderBackend );
        CoUninitialize();
        return ReportDiagnosticStoreSession( diagnostics, 1 );
    }

    window->UpdateProjectionForCurrentClient();

    SkullbonezCore::Core::Profiler* profiler = nullptr;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    // Lifetime: Init owns profiling for the synchronous RunApp call. The
    // profiler's fixed marker rings are intentionally startup-heap-owned so
    // they do not consume WinMain's bounded thread stack. Runtime, render, UI,
    // and physics owners receive only the stable borrow.
    auto profilerOwner = std::make_unique<SkullbonezCore::Core::Profiler>();
    profiler = profilerOwner.get();
#endif
    workerPool.BindProfiler( profiler );

    const int runExitCode = RunApp( diagnostics, window, args, cfg, workerPool, profiler, *renderBackend, tracyClient );

    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Shutdown );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        // Lifetime: no engine worker can publish another marker after this
        // point, while logging and COM/platform teardown are still available.
        tracyClientOwner.Shutdown();
#endif
        CleanupWindow( window, instance, renderBackend );
    }
    CoreAllocation::PrintRuntimeAllocationSummary( stdout );
    int finalExitCode = runExitCode;

    if ( CoreAllocation::GetRuntimeAllocationGuardMode() == CoreAllocation::RuntimeAllocationGuardMode::Gameplay &&
         CoreAllocation::RuntimeAllocationGuardHasGameplayViolations() && finalExitCode == 0 )
    {
        fprintf( stdout, "[allocation-guard] FAIL: gameplay allocation guard detected policy violations.\n" );
        finalExitCode = 9;
    }

    CoUninitialize();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    return ReportDiagnosticStoreSession( diagnostics, finalExitCode );
}
