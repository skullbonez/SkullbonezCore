#pragma once

#include "../Camera/DemoDirector.h"

namespace SkullbonezCore
{
namespace Runtime
{
bool LoadDemoShotList( const char* path, DemoShotList& outShotList );
bool SaveDemoShotList( const char* path, const DemoShotList& shotList );

} // namespace Runtime
} // namespace SkullbonezCore
