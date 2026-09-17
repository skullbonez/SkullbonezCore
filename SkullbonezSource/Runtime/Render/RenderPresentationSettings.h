/*
File: SkullbonezSource/Runtime/Render/RenderPresentationSettings.h
Purpose:
  Defines renderer-owned live presentation policy that survives backend rebuilds.

Summary:
  RuntimeRenderer owns these values alongside its render passes. Scene loading,
  UI, and stress tools may edit them at explicit cold/frame boundaries, while
  physics keeps its own state in its domain owner.

Glossary:
  Pipeline sync: Diagnostic mode that forces CPU/GPU synchronization before a
    frame is rendered.

Invariants:
  - These values never change deterministic physics state.
  - Vsync is retained while the backend is absent and applied when it is live.
  - No physics, input, or scene-lifecycle state belongs here.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/App/OperatorCommandApplication.cpp
*/
#pragma once

#include "../../Core/Config.h"

namespace SkullbonezCore
{
namespace Runtime
{
// Process-wide preview policy. Scene loads and saves retain their authored
// values; disabling the override reveals those values without a restore cache.
class SplitFutureLookOverride
{
  public:
    void Toggle( bool authoredSplitFutureEnabled )
    {
        const bool enabled = m_mode == Mode::Enabled || ( m_mode == Mode::Authored && authoredSplitFutureEnabled );
        m_mode = enabled ? Mode::Disabled : Mode::Enabled;
    }

    SkullbonezCore::Core::CinematicRenderConfig Resolve( SkullbonezCore::Core::CinematicRenderConfig cinematic, bool authoredRendering ) const
    {
        cinematic.enabled = authoredRendering;
        if ( m_mode == Mode::Enabled )
        {
            cinematic.enabled = true;
            cinematic.skyAtmosphereEnabled = true;
            cinematic.bloomEnabled = true;
            cinematic.skyMode = 22;
            cinematic.terrainMode = 16;
            cinematic.objectStyle = 14;
            cinematic.waterMode = 5;
        }
        else if ( m_mode == Mode::Disabled && cinematic.objectStyle == 14 )
        {
            // Split Future already authors this look: its first F7 press must
            // visibly turn it off, rather than reapply the same preset.
            cinematic.enabled = false;
        }
        return cinematic;
    }

  private:
    enum class Mode
    {
        Authored,
        Enabled,
        Disabled
    };
    Mode m_mode = Mode::Authored;
};

struct RenderPresentationSettings
{
    bool vsyncEnabled = true;
    bool pipelineSyncEnabled = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
