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
  EngineServices: Borrowed view over shared process services such as assets,
    textures, cameras, and the native window.
  WindowService: Borrowed view of the native window service exposed to runtime
    slices that need dimensions, title, or resize behavior.
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
class CameraCollection;
class WorldEnvironment;
} // namespace Environment

namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsEngine;
}

namespace Assets
{
class AssetSystem;
}

namespace Textures
{
class TextureCollection;
}

namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry

namespace Basics
{
class CaptureController;
class DiagnosticsController;
class RuntimeCommandQueue;
class SceneController;
class SimulationController;
class RuntimeInputContext;
class Window;
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
    Physics::PhysicsEngine* physics = nullptr;          // Physics-owned runtime snapshot stores
    GameObjects::GameModelCollection* models = nullptr; // Runtime model and solver-visible state
};

struct EngineServices
{
    // Lifetime: these pointers borrow Run-owned services after Run::Initialise
    // wires legacy singleton-backed resources into the composition root.
    Assets::AssetSystem* assets = nullptr;
    Textures::TextureCollection* textures = nullptr;
    Environment::CameraCollection* cameras = nullptr;
    struct WindowService
    {
        Window* window = nullptr;                       // Native window owner borrowed from RunSubsystemState.
    } window;
    Geometry::Terrain* terrain = nullptr;
    Geometry::SkyBox* skyBox = nullptr;
};

class EngineContext
{
  public:
    void Bind( const EngineContextBindings& bindings );
    bool IsBound() const;

    EngineServices Services();

  private:
    EngineContextBindings m_bindings;
};
} // namespace Basics
} // namespace SkullbonezCore
