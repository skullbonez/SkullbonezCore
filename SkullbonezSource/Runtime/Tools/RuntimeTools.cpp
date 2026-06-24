/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
Purpose:
  Provides the runtime tool state ownership boundary.
*/
#include "RuntimeTools.h"

namespace SkullbonezCore::Basics
{
RunRayCastTestState& RuntimeTools::RayCastTest()
{
    return m_rayCastTest;
}

const RunRayCastTestState& RuntimeTools::RayCastTest() const
{
    return m_rayCastTest;
}

LauncherLaser& RuntimeTools::Laser()
{
    return m_laser;
}

const LauncherLaser& RuntimeTools::Laser() const
{
    return m_laser;
}

RunMousePickupState& RuntimeTools::MousePickup()
{
    return m_mousePickup;
}

const RunMousePickupState& RuntimeTools::MousePickup() const
{
    return m_mousePickup;
}

RunEditorPlacementState& RuntimeTools::Editor()
{
    return m_editor;
}

const RunEditorPlacementState& RuntimeTools::Editor() const
{
    return m_editor;
}

RunEditorTracer& RuntimeTools::EditorTracer()
{
    return m_editorTracer;
}

const RunEditorTracer& RuntimeTools::EditorTracer() const
{
    return m_editorTracer;
}
} // namespace SkullbonezCore::Basics
