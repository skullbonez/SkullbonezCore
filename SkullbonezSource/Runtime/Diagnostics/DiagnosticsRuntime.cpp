/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
Purpose:
  Provides the runtime diagnostics ownership boundary.
*/
#include "DiagnosticsRuntime.h"

namespace SkullbonezCore
{
namespace Basics
{
CaptureController& DiagnosticsRuntime::Capture()
{
    return m_capture;
}


const CaptureController& DiagnosticsRuntime::Capture() const
{
    return m_capture;
}


DiagnosticsController& DiagnosticsRuntime::Diagnostics()
{
    return m_diagnostics;
}


const DiagnosticsController& DiagnosticsRuntime::Diagnostics() const
{
    return m_diagnostics;
}
} // namespace Basics
} // namespace SkullbonezCore
