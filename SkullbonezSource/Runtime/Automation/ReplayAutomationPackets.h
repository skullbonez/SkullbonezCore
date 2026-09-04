/*
File: SkullbonezSource/Runtime/Automation/ReplayAutomationPackets.h
Purpose:
  Defines bounded replay-presentation evidence copied into Automation reports.

Summary:
  App publishes these scalar probe values after presentation submission.
  Automation may serialize them but cannot reach the App-owned presentation
  state that produced them.

Invariants:
  - Values describe one sampled presentation key and contain no owner pointer.
  - Readiness cannot regress while target and source-frame identity are stable.

Related:
  - SkullbonezSource/Runtime/Automation/ReplayAutomationView.h
  - SkullbonezSource/Runtime/App/ReplayPredictionPresentation.h
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Runtime
{
struct ReplayTrajectorySubmissionProbeStats
{
    bool hasSubmission = false;
    bool stableWindowReady = false;
    bool noReserveGrowth = true;
    int observedFrameCount = 0;
    int stableFrameCount = 0;
    int stableWindowTargetFrameCount = 120;
    int firstFrame = -1;
    int lastFrame = -1;
    uint64_t stableHash = 0;
    uint64_t geometryBytes = 0;
    uint64_t vertexBytes = 0;
    uint32_t vertexCount = 0;
    uint32_t segmentCount = 0;
    uint64_t reserveGrowthEventsAtStart = 0;
    uint64_t reserveGrowthEventsAtEnd = 0;
    uint64_t presentationTargetId = 0;
    uint64_t presentationSourceFrame = 0;
    uint32_t presentationTopologyVersion = 0;
    uint32_t futureTreeReadinessDropCount = 0;
    bool presentationKeyValid = false;
    bool futureTreeReadySeen = false;
    bool futureTreeReadyLastFrame = false;
};
} // namespace SkullbonezCore::Runtime
