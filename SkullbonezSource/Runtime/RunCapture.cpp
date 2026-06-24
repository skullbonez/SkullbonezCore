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


void Run::LogPerfMemory( const char* checkpoint )
{
    m_diagnosticsRuntime.Diagnostics().LogPerfMemory( sPerfPass + 1, checkpoint );
}
