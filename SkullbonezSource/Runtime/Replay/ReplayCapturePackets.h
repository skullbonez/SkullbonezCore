/*
File: ReplayCapturePackets.h
Purpose:
  Publishes scalar replay recorder statistics without exposing capture storage or mutation APIs.

Summary:
  Capture owners report bounded-ring counts and hashes through these value
  packets. Runtime startup, shutdown, overlay, and validation consumers cannot
  recover recorder authority from them.

Glossary:
  Sample capacity: Maximum retained samples in one bounded replay ring.
  Event capacity: Maximum retained owner events in the bounded event ring.

Invariants:
  - Statistics contain no sample pointers or mutable storage.
  - Capacities and counters describe one synchronous owner snapshot.

Related:
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
struct ReplayRecorderStats
{
    bool enabled = false;
    uint64_t totalFramesCaptured = 0;
    uint64_t totalFramesEvicted = 0;
    ReplayFrameIndex nextFrameIndex = 0;
    std::size_t sampleCapacity = 0;
    std::size_t sampleCount = 0;
    std::size_t checkpointCapacity = 0;
    std::size_t checkpointCount = 0;
    uint64_t latestStateHash = 0;
};

struct ReplayEventRecorderStats
{
    bool enabled = false;
    uint64_t totalEventsCaptured = 0;
    uint64_t totalEventsEvicted = 0;
    uint32_t nextSequence = 0;
    std::size_t eventCapacity = 0;
    std::size_t eventCount = 0;
};
} // namespace SkullbonezCore::Runtime
