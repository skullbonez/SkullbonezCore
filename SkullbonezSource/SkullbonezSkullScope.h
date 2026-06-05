#pragma once


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;


class SkullScope final
{
  public:
#ifdef _DEBUG
    void SetPath( const char* path );
    void SetRunId( const char* runId );
    void EmitFrame( GameModelCollection& collection, float dt );
#endif

  private:
#ifdef _DEBUG
    void ResetRunState();
    void ResetPenetrationState();

    char m_physicsDiagnosticsPath[256] = {};
    char m_physicsDiagnosticsRunId[32] = {};
    int m_physicsDiagnosticsFrame = 0;
    int m_physicsDiagnosticsEventCounter = 0;
    double m_physicsDiagnosticsTimeSeconds = 0.0;
    double m_physicsDiagnosticsPrevEnergy = 0.0;
    bool m_physicsDiagnosticsHasPrevEnergy = false;
    char m_physicsDiagnosticsPenetrationContact[64] = {};
    int m_physicsDiagnosticsPenetrationFrames = 0;
    int m_physicsDiagnosticsPenetrationGrowthFrames = 0;
    double m_physicsDiagnosticsPenetrationWindowStart = 0.0;
    double m_physicsDiagnosticsPrevPenetration = 0.0;
    bool m_physicsDiagnosticsPenetrationSustainedReported = false;
    bool m_physicsDiagnosticsPenetrationGrowingReported = false;
#endif
};
} // namespace GameObjects
} // namespace SkullbonezCore
