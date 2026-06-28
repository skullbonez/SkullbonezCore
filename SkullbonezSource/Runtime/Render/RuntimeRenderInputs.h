/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h
Purpose:
  Names the borrowed runtime inputs consumed by frame rendering.

Mental model:
  Runtime render code should receive a small view of the systems and state it
  needs for one frame, not the entire Run object. These structs are references
  only; ownership stays with Run until later extraction phases move it.

Glossary:
  Render services: Borrowed references to systems required by render passes.
  Render inputs: One-frame wrapper around the current render services.
  Borrowed pointer: Nullable dependency that remains owned by Run or a scene
  subsystem.

Invariants:
  - RuntimeRenderInputs is rebuilt for the current render call and is not
    stored by render passes.
  - References and pointers here do not transfer ownership.
  - Optional pointers remain nullable to match the current Run-owned subsystem
    lifetime.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Textures
{
class TextureCollection;
}

namespace GameObjects
{
class GameModelCollection;
}

namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment

namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry

namespace Rendering
{
class IRenderCommandContext;
} // namespace Rendering

namespace UI
{
class InGameUI;
}

namespace Basics
{
class Window;

struct RuntimeRenderServices
{
    Textures::TextureCollection& textures;
    GameObjects::GameModelCollection& models;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    Environment::CameraCollection& cameras;
    Window& window;
    UI::InGameUI& ui;
    Geometry::SkyBox* skyBox;
    // Lifetime: this command facet is borrowed from the process-bound backend
    // for exactly this render call; pass code must not store it.
    Rendering::IRenderCommandContext& renderCommands;
    bool renderReady = false;
};

struct RuntimeRenderInputs
{
    RuntimeRenderServices services;
};
} // namespace Basics
} // namespace SkullbonezCore
