/*
File: SkullbonezSource/Runtime/Replay/ReplayExporter.h
Purpose:
  Serializes retained replay samples into a replay artifact file.
*/
#pragma once

#include "ReplayRecorder.h"

namespace SkullbonezCore
{
namespace Basics
{
class ReplayExporter
{
  public:
    static bool Save( const ReplayRecorder& recorder, const char* path );
    static bool Save( const ReplaySolverRecorder& recorder, const char* path );
};
} // namespace Basics
} // namespace SkullbonezCore
