// --- Includes ---
#include "SkullbonezCaptureSystem.h"
#include "SkullbonezRunInternal.h"

// --- Usings ---
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
    if ( !m_perfLogState.perfLogFile )
    {
        return;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof( pmc );
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    {
        double mb = static_cast<double>( pmc.WorkingSetSize ) / ( 1024.0 * 1024.0 );
        fprintf( m_perfLogState.perfLogFile, "# MEM %s pass=%d working_set_mb=%.2f\n", checkpoint, sPerfPass + 1, mb );
        ++m_perfLogState.perfLogWritesSinceFlush;
        if ( m_perfLogState.isPerfLogFlushEnabled ||
             ( m_perfLogState.perfLogFlushInterval > 0 && m_perfLogState.perfLogWritesSinceFlush >= m_perfLogState.perfLogFlushInterval ) )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
    }
}
