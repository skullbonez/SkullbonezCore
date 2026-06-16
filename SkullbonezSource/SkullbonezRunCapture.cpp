/*
File: SkullbonezSource/SkullbonezRunCapture.cpp
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
#include "SkullbonezCaptureSystem.h"
#include "SkullbonezRunInternal.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

void SkullbonezRun::SaveScreenshot( const char* path )
{
    CaptureSystem::SaveBackbufferBmp( Gfx(), path );
}


void SkullbonezRun::LogPerfMemory( const char* checkpoint )
{
    RuntimeDiagnostics::LogPerfMemory( m_perfLogState, sPerfPass + 1, checkpoint );
}
