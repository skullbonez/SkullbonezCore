/*
File: SkullbonezSource/Runtime/RunCapture.cpp
Purpose:
  Handles runtime screenshot and capture requests.

Mental model:
  Run owns the user/runtime command, CaptureController owns image encoding, and
  the render backend owns the actual swap-chain readback. This file is the thin
  handoff point between those responsibilities.

Glossary:
  Capture backend: Narrow renderer capability that can report capture support
  and read pixels from the active back buffer.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Run delegates capture encoding to CaptureController so screenshot policy
    stays separate from backend readback details.
  - Capture requests use the active renderer only after the runtime backend has
    been initialized.
  - Screenshot callers receive only the capture/readback surface, not the full
    render device surface.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#include "CaptureSystem.h"
#include "RunInternal.h"
#include "../Rendering/IRenderCaptureBackend.h"

#include <cstdio>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

void Run::SaveScreenshot( const char* path )
{
    // Why: screenshot save only needs readback capability, so keep the call on
    // the narrow capture facade instead of handing CaptureController Gfx().
    CaptureController::SaveBackbufferBmp( SkullbonezCore::Rendering::GfxCapture(), path );
    printf( "[capture] Screenshot taken: %s\n", path );
    fflush( stdout );
}
