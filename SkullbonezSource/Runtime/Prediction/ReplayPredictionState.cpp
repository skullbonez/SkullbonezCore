/*
Purpose:
  Constructs the retained replay-prediction state shared by runtime and CPU tests.

Invariant:
  Constructor reserves every snapshot row needed by steady prediction builds.
*/
#include "../../Physics/PhysicsEngine.h"
#include "ReplayPrediction.h"

namespace SkullbonezCore::Runtime
{
RunReplayPredictionState::RunReplayPredictionState()
{
    simulation.predictionWorld.tornadoSystemConfig.vortices.reserve( Gameplay::TornadoGameplay::MAX_ACTIVE_FORCE_FIELDS );
    simulation.predictionWorld.tornadoCaptureSeconds.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    simulation.predictionWorld.tornadoEjectCooldownSeconds.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}
} // namespace SkullbonezCore::Runtime
