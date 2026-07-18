/*
File: SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h
Purpose:
  Defines value-only authored validation requirements produced during scene load.

Summary:
  Scene population resolves named contact and broadphase requirements to compact
  rows without borrowing the process-lifetime validation harness. The harness
  adopts the completed configuration after the load transaction returns.

Glossary:
  Gate configuration: Cold-load value rows later observed by validation.
  Required contact: Named body pair that must touch before automation completes.
  Broadphase span: Inclusive authored X-cell range that must become active.

Invariants:
  - Configuration growth occurs only in the SceneLoad allocation phase.
  - Body indices identify the newly populated scene and are never reused across
    configurations without replacing the complete value.
  - Moving a configuration into validation performs no scene lookup or callback.

Related:
  - SkullbonezSource/Runtime/RuntimeValidationHarness.h
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
*/
#pragma once

#include <cstddef>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneAutomationGateTracker;

struct SceneRequiredContactGate
{
    char nameA[64] = {};
    char nameB[64] = {};
    int bodyA = -1;
    int bodyB = -1;
    bool touched = false;
};

struct SceneRequiredBroadphaseXCellsGate
{
    int minCellX = 0;
    int maxCellX = 0;
    int cellY = 0;
    int cellZ = 0;
    int lastActiveCellCount = 0;
    int lastObservedMinX = 0;
    int lastObservedMaxX = 0;
    int lastMissingCellX = -1;
    bool hasObservedXRange = false;
    bool activated = false;
};

// Concept: scene loading resolves authored requirements into a value record.
// Validation adopts that record only after the scene owner completes the load,
// so population code cannot retain or mutate the process-lifetime harness.
struct SceneAutomationGateConfiguration
{
    void Reset();
    void ReserveRequiredContacts( std::size_t count );
    void AppendRequiredContact( const char* nameA, const char* nameB, int bodyA, int bodyB );
    void ReserveRequiredBroadphaseXCells( std::size_t count );
    void AppendRequiredBroadphaseXCells( int minCellX, int maxCellX, int cellY, int cellZ );

  private:
    friend class SceneAutomationGateTracker;
    std::vector<SceneRequiredContactGate> m_requiredContacts;
    std::vector<SceneRequiredBroadphaseXCellsGate> m_requiredBroadphaseXCells;
};
} // namespace Runtime
} // namespace SkullbonezCore
