/*
File: SkullbonezSource/Runtime/EngineContext.h
Purpose:
  Names the runtime-owned system graph behind the Run facade.

Mental model:
  EngineContext is a bound view over systems owned by Run. It prevents new
  extraction slices from reaching through individual Run members without first
  declaring which runtime boundary they need.

Glossary:
  EngineContext: Bound view over runtime-owned systems.
  Binding: Non-owning pointer to a subsystem owned by Run.
  Runtime boundary: Named subsystem edge used by extraction slices.
  Facade: Small public surface that hides broader runtime ownership.

Invariants:
  - All bindings are borrowed; Run keeps ownership and lifetime.
  - New extracted systems should request context through this declared surface.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RuntimeViewModel.h
*/
#pragma once

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}

namespace GameObjects
{
class GameModelCollection;
}

namespace Basics
{
class CaptureController;
class DiagnosticsController;
class RuntimeCommandQueue;
class SceneController;
class SimulationController;
class RuntimeInputContext;
struct RunCameraState;
struct RunDebugState;
struct RunRuntimeSettings;
struct RunSubsystemState;

struct EngineContextBindings
{
    SceneController* scene = nullptr;                   // Scene queue and scene-run state
    SimulationController* simulation = nullptr;         // Runtime timestep controller
    CaptureController* capture = nullptr;               // Screenshot and capture automation
    DiagnosticsController* diagnostics = nullptr;       // Perf/SkullScope diagnostics state
    RuntimeCommandQueue* commands = nullptr;            // Deferred runtime command intent
    RunSubsystemState* systems = nullptr;               // Window/camera/texture/terrain resources
    RunRuntimeSettings* runtimeSettings = nullptr;      // Live runtime toggles
    RuntimeInputContext* input = nullptr;               // Per-frame semantic input state
    RunCameraState* camera = nullptr;                   // Camera and tracking state
    RunDebugState* debug = nullptr;                     // Debug visualization toggles
    Environment::WorldEnvironment* world = nullptr;     // Fluid/gravity/terrain bounds
    GameObjects::GameModelCollection* models = nullptr; // Runtime model and solver-visible state
};

class EngineContext
{
  public:
    void Bind( const EngineContextBindings& bindings );
    bool IsBound() const;

    const EngineContextBindings& Bindings() const;
    EngineContextBindings& Bindings();

  private:
    EngineContextBindings m_bindings;
};
} // namespace Basics
} // namespace SkullbonezCore
