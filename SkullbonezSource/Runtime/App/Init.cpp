/*
File: SkullbonezSource/Runtime/App/Init.cpp
Purpose:
  Owns process-level startup sequencing after focused startup owners resolve
  command-line, launch, probe, and crash-reporting concerns.

Summary:
  Init.cpp establishes the Windows process environment, delegates early-exit
  startup work, constructs the window and DX12 backend, and starts the run loop.

Glossary:
  COM (Component Object Model): Windows interface lifetime model used by DX12
  and platform APIs through reference-counted objects.
  Startup owner: A focused helper unit that parses options, resolves launch
    policy, runs an early probe, or installs crash diagnostics before Run exists.
  Manual profiler lifetime: Development-build Tracy ownership explicitly
    bracketed around every engine thread instead of static initialization.
  Lane R result: Recoverable CLI/startup failure that returns a process exit
    code with owner/message diagnostics instead of using a fatal exception.

Invariants:
  - Startup owners finish option resolution before Run owns subsystems, keeping
    validation launches deterministic from their command line.
  - Early-exit smoke modes must return before worker, window, renderer, or Run
    startup if their evidence claims subsystem isolation.
  - Startup-selected Tracy begins before the initial WorkerPool. An interactive
    Standard start recreates that pool before simulation resumes, and Tracy
    still stops after all workers join while platform logging remains alive.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/TracyClientOwner.h"
#include "../Input/Input.h"
#include "Run.h"
#include "../Startup/StartupCommandLine.h"
#include "../Startup/StartupCrashLogging.h"
#include "../Startup/StartupLaunchResolution.h"
#include "../Startup/StartupProbeHarnesses.h"
#include "Window.h"
#include "../../Core/WindowConstants.h"
#include <cstdio>
#include <cstring>
#include <memory>
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
    const char* safeOwner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Startup";
    const char* safeMessage = result.error.message[0] != '\0' ? result.error.message
                                                              : "Startup failed without details.";

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

// ---------------------------------------------------------------------------
// Render backend
// ---------------------------------------------------------------------------

SkullbonezCore::Core::SbResult InitRenderBackend( Window* window,
                                                  RuntimeRenderBackendView& renderBackendView,
                                                  std::unique_ptr<RenderBackendDX12>& outBackend )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
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
    // render code keeps concrete device/frame/graph/resource owners in
    // RuntimeRenderBackendView and must release every borrow before shutdown
    // resets the backend.
    renderBackendView.renderDevice = &renderBackend->RenderDevice();
    renderBackendView.renderFrame = &renderBackend->Frame();
    renderBackendView.renderGraph = &renderBackend->GraphTransients();
    renderBackendView.renderResources = &renderBackend->ResourceBuilder();
    renderBackendView.renderTextures = &renderBackend->Textures();
    renderBackendView.renderGeometry = &renderBackend->Geometry();
    renderBackendView.renderDiagnostics = &renderBackend->Diagnostics();
    renderBackendView.backbufferCapture = &renderBackend->BackbufferCapture();
    renderBackendView.raytracing = &renderBackend->Raytracing();
    renderBackendView.shaderDevelopment = &renderBackend->ShaderDevelopment();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    renderBackendView.developmentUiRenderer = &renderBackend->DevelopmentUiRenderer();
#endif
    outBackend = std::move( backend );
    return SkullbonezCore::Core::SbResult::Success();
}

int RunApp( Window* window,
            ParsedArgs& args,
            SkullbonezCore::Core::EngineConfig& cfg,
            WorkerPool& workerPool,
            SkullbonezCore::Core::Profiler* profiler,
            RuntimeRenderBackendView renderBackendView,
            SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner )
{
    // Lifetime: Run releases all render-owned resources before its borrowed
    // DX12 backend and Win32 window are torn down by the process owner.
    {
        std::unique_ptr<Run> cRun = std::make_unique<Run>( *window,
                                                           std::move( args.sceneList ),
                                                           cfg,
                                                           workerPool,
                                                           profiler,
                                                           renderBackendView,
                                                           tracyClientOwner );

        const RunStartupOverrides startupOverrides = BuildRunStartupOverrides( args );
        auto reportRunResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeOwner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner
                                                                                        : "Runtime";

            const char* safeMessage = result.error.message[0] != '\0' ? result.error.message
                                                                      : "recoverable runtime operation failed";

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
            const char* safeMessage = result.error.message[0] != '\0' ? result.error.message
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
            const SkullbonezCore::Core::SbResult sceneLoadOnlyResult = cRun->RunSceneLoadOnly( args.sceneSnapshotOutPath[0] != '\0' ? args.sceneSnapshotOutPath : nullptr );

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
        SkullbonezCore::Hardware::Input::UnbindCallbackBridge( windowHandle );
    }

    SkullbonezCore::Hardware::Input::UnbindWindow( *window );
    window->SetResizeRenderFrameOwner( nullptr );
    renderBackend.reset();

    window->ReleaseDeviceContext();

    if ( window->IsFullScreenMode() )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        SkullbonezCore::Hardware::Input::SetSystemCursorVisible( true );
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
    // WinRT/COM activation occurs; without this, MSCTF.dll reports 0x800401F0 during
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
        SkullbonezCore::Core::Log().WriteEventf( "startup_failure owner=\"Startup/CommandLine\" message=\"%s\"",
                                                 error );

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
        return 1;
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
    if ( HandlePhysicsStandaloneSmoke( commandLine, standalonePhysicsExitCode ) )
    {
        CoUninitialize();
        return standalonePhysicsExitCode;
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
        return workersOk ? 0 : 1;
    }

    Window windowOwner;
    Window* window = &windowOwner;
    window->SetStartupWindowSize( cfg.window.screenX, cfg.window.screenY );
    window->SetProjectionFrustum( cfg.camera.frustumNear, cfg.camera.frustumFar );
    const SkullbonezCore::Core::SbResult windowResult = window->CreateAppWindow( hInstance,
                                                                                 cfg.window.fullscreen,
                                                                                 !args.automationWindowHidden );

    if ( !windowResult.ok )
    {
        ReportStartupFailure( windowResult, "SkullbonezCore Startup Failed" );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CoUninitialize();
        return 1;
    }

    window->AcquireDeviceContext();

    RuntimeRenderBackendView renderBackendView;
    std::unique_ptr<RenderBackendDX12> renderBackend;
    const SkullbonezCore::Core::SbResult renderBackendResult = InitRenderBackend( window,
                                                                                  renderBackendView,
                                                                                  renderBackend );

    if ( !renderBackendResult.ok )
    {
        ReportStartupFailure( renderBackendResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }

    window->SetResizeRenderFrameOwner( renderBackendView.renderFrame );
    const SkullbonezCore::Core::SbResult initialResizeResult = window->HandleScreenResize();
    if ( !initialResizeResult.ok )
    {
        ReportStartupFailure( initialResizeResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        tracyClientOwner.Shutdown();
#endif
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }

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

    const int runExitCode = RunApp( window, args, cfg, workerPool, profiler, renderBackendView, tracyClient );

    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Shutdown );
        workerPool.Shutdown();
#if defined( TRACY_ENABLE )
        // Lifetime: no engine worker can publish another marker after this
        // point, while logging and COM/platform teardown are still available.
        tracyClientOwner.Shutdown();
#endif
        CleanupWindow( window, hInstance, renderBackend );
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

    return finalExitCode;
}
