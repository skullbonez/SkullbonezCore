/*
File: SkullbonezSource/Runtime/App/StartupInputApplication.h
Purpose:
  Sequences native input admission before renderer startup.

Summary:
  App treats raw-mouse registration and renderer creation as one startup
  transaction. A failure reports its exact owner, releases every earlier
  process resource in fixed order, and never calls a later startup stage.

Invariants:
  - Renderer startup is unreachable after raw-input registration failure.
  - Worker, development-tool, Window, and COM cleanup each run exactly once on
    either startup failure and never run on success.

Related:
  - SkullbonezSource/Runtime/App/Init.cpp
  - SkullbonezSource/Runtime/Input/Input.h
*/
#pragma once

#include "../../Core/SbResult.h"

#include <utility>

namespace SkullbonezCore::Runtime
{
template <typename FailureReporter, typename WorkerShutdown, typename DevelopmentToolShutdown,
          typename WindowCleanup, typename ComUninitialize, typename RendererStart>
SkullbonezCore::Core::SbResult StartRendererAfterRawMouseRegistration(
    const SkullbonezCore::Core::SbResult& rawMouseResult, FailureReporter&& reportFailure,
    WorkerShutdown&& shutdownWorkers, DevelopmentToolShutdown&& shutdownDevelopmentTools,
    WindowCleanup&& cleanupWindow, ComUninitialize&& uninitializeCom, RendererStart&& startRenderer )
{
    const auto finishFailure = [&]( const SkullbonezCore::Core::SbResult& failure, const char* title )
    {
        reportFailure( failure, title );
        shutdownWorkers();
        shutdownDevelopmentTools();
        cleanupWindow();
        uninitializeCom();
        return failure;
    };

    if ( !rawMouseResult.Ok() )
    {
        return finishFailure( rawMouseResult, "SkullbonezCore Input Startup Failed" );
    }

    const SkullbonezCore::Core::SbResult rendererResult = startRenderer();

    if ( !rendererResult.Ok() )
    {
        return finishFailure( rendererResult, "SkullbonezCore Renderer Startup Failed" );
    }

    return SkullbonezCore::Core::SbResult::Success();
}
} // namespace SkullbonezCore::Runtime
