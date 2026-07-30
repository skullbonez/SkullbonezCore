/*
File: SkullbonezTests/TestResultLoadFixtures.h
Purpose:
  Keeps successful parser fixtures on the production result-returning APIs.

Summary:
  Tests construct the output value first and then check the same Lane-R load
  operation used by runtime owners. No test-only fail-fast production wrapper
  is needed to obtain a value.

Glossary:
  Lane R: Recoverable result path that returns diagnostics without terminating
    the process or publishing a partially parsed value.

Invariants:
  - A false return leaves the caller-owned output available for diagnostics.
  - Helpers retain neither diagnostic stores nor parsed values.

Related:
  - SkullbonezSource/Physics/ConvexHullShape.h
  - SkullbonezSource/Scene/AuthoredScene.h
*/
#pragma once

#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"

namespace SkullbonezTests::ResultLoadFixtures
{
inline bool TryLoadConvexHull( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path,
                               SkullbonezCore::Math::CollisionDetection::ConvexHullShape& output )
{
    return SkullbonezCore::Math::CollisionDetection::ConvexHullShape::TryLoadFromFile( diagnostics, path, output ).Ok();
}


inline bool TryLoadAuthoredScene( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path,
                                  SkullbonezCore::Runtime::AuthoredScene& output )
{
    return SkullbonezCore::Runtime::AuthoredScene::TryLoadFromFile( diagnostics, path, output ).Ok();
}
} // namespace SkullbonezTests::ResultLoadFixtures
