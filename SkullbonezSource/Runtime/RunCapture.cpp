/*
File: SkullbonezSource/Runtime/RunCapture.cpp
Purpose:
  Handles runtime screenshot and capture requests.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Run delegates capture encoding to CaptureController so screenshot policy
    stays separate from backend readback details.
  - Capture requests use the active renderer only after the runtime backend has
    been initialized.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#include "CaptureSystem.h"
#include "RunInternal.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

void Run::SaveScreenshot( const char* path )
{
    CaptureController::SaveBackbufferBmp( Gfx(), path );
}
