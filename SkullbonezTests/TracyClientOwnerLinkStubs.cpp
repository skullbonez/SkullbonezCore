// The CPU test executable links Profile Rendering objects but does not start a
// Tracy client. These no-op owner-zone definitions preserve that boundary.
#include "../SkullbonezSource/Core/TracyClientOwner.h"

namespace SkullbonezCore::Core::DevelopmentTools
{
uint32_t TracyClientOwner::RegisterOwnerZone( const char* /*fullPath*/, uint32_t /*hash*/ ) noexcept
{
    return 0u;
}

TracyZoneToken TracyClientOwner::BeginOwnerZone( uint32_t /*sourceLocationHandle*/ ) noexcept
{
    return {};
}

void TracyClientOwner::EndOwnerZone( TracyZoneToken /*token*/ ) noexcept
{
}
} // namespace SkullbonezCore::Core::DevelopmentTools
