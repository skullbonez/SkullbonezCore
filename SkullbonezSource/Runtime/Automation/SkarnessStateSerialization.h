#pragma once

#if defined( SKULLBONEZ_SKARNESS )

#include "SkarnessProtocol.h"

#include <array>
#include <cstdint>
#include <string>

namespace SkullbonezCore::Runtime
{
struct ReplayAutomationView;

struct SkarnessSerializedStateTopic
{
    std::string payload;
    uint64_t ownerVersion = 0;
    uint64_t appendCursor = 0;
    uint64_t evictCursor = 0;
};

using SkarnessSerializedStateTopics =
    std::array<SkarnessSerializedStateTopic, SKARNESS_STATE_TOPICS.size()>;

// Lifetime: ReplayAutomationView borrows owner storage only for this call. The
// returned strings are detached before App may mutate Replay again.
void BuildSkarnessStateTopics( const SkarnessFrameState& state, const ReplayAutomationView& replay,
                               SkarnessStateDetail detail, SkarnessSerializedStateTopics& outTopics );
} // namespace SkullbonezCore::Runtime

#endif
