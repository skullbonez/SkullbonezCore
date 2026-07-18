/*
File: SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.cpp
Purpose:
  Owns Tracy client startup, shutdown, frame marks, thread names, and status.

Summary:
  The process composition root starts the manually managed on-demand client
  before engine workers exist and shuts it down after joining them. All vendor
  entry points run in the bounded Tracy allocation-owner scope established by
  E2. The client copies thread names, so indexed worker labels may be formatted
  in a fixed stack buffer.

Glossary:
  On-demand client: Tracy mode that records only while an external viewer is
    connected instead of retaining an unbounded pre-connection history.
  Connection snapshot: Direct read of Tracy's existing atomic connection flag;
    it does not probe the network, launch a process, or allocate.

Invariants:
  - Startup and shutdown are idempotent at the engine boundary.
  - Frame marks are ignored until manual startup has completed.
  - The 32-byte worker-name buffer bounds all formatted thread labels.
  - Main/render/replay/prediction/IO share one lane in the current topology;
    the main thread name states that fact rather than inventing fake threads.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.h
  - SkullbonezSource/Runtime/Init.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Core/WorkerPool.cpp
*/
#include "TracyClientOwner.h"

#include "../Allocation/DevelopmentToolAllocation.h"

#include <tracy/Tracy.hpp>

#include <atomic>
#include <cstdio>

namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
std::atomic<bool> g_tracyInitialized{ false };
}

namespace SkullbonezCore::Runtime::DevelopmentTools
{
TracyClientOwner::~TracyClientOwner()
{
    Shutdown();
}

void TracyClientOwner::Start()
{
    if ( m_started )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    tracy::StartupProfiler();
    m_started = tracy::IsProfilerStarted();
    g_tracyInitialized.store( m_started, std::memory_order_release );
    if ( !m_started )
    {
        return;
    }

    // Concept: current runtime topology executes render, replay/prediction
    // coordination, and cold IO on the main thread. A composite label makes
    // that ownership visible without pretending those roles have private lanes.
    tracy::SetThreadName( "Skore Main + Render + Replay + IO" );
    TracySetProgramName( "SkullbonezCore" );
    fprintf( stdout, "[tracy] Manual on-demand client started. viewer=waiting\n" );
    fflush( stdout );
}

void TracyClientOwner::Shutdown() noexcept
{
    if ( !m_started )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    // Lifetime: publish the stopped state before freeing Tracy's process data;
    // UI snapshots and marker seams therefore stop reaching the vendor first.
    g_tracyInitialized.store( false, std::memory_order_release );
    tracy::ShutdownProfiler();
    m_started = false;
    fprintf( stdout, "[tracy] Client stopped after engine worker shutdown.\n" );
    fflush( stdout );
}

TracyClientStatus TracyClientOwner::CopyStatus() noexcept
{
    const bool initialized = g_tracyInitialized.load( std::memory_order_acquire );
    // Why: IsConnected is an atomic state read owned by the client. The editor
    // never scans processes, opens sockets, or constructs a string per frame.
    return { true, initialized, initialized && TracyIsConnected };
}

void TracyClientOwner::MarkSubmittedFrame() noexcept
{
    if ( !g_tracyInitialized.load( std::memory_order_acquire ) )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    FrameMark;
}

void TracyClientOwner::NameWorkerThread( int workerIndex ) noexcept
{
    if ( !g_tracyInitialized.load( std::memory_order_acquire ) )
    {
        return;
    }

    char threadName[32] = {};
    _snprintf_s( threadName, sizeof( threadName ), _TRUNCATE, "Skore Worker %02d", workerIndex );
    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    tracy::SetThreadNameWithHint( threadName, 1 );
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
