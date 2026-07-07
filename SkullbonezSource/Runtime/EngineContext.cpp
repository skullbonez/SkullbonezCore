/*
File: SkullbonezSource/Runtime/EngineContext.cpp
Purpose:
  Implements runtime system graph binding.

Mental model:
  The context is intentionally lightweight: Run owns the actual systems and
  binds their addresses once construction has completed.

Glossary:
  EngineContext: Bound view over runtime-owned systems.
  Binding: Non-owning pointer to a subsystem owned by Run.
  Runtime boundary: Named subsystem edge used by extraction slices.

Invariants:
  - EngineContext must not take ownership of bound systems.
  - IsBound() is conservative so incomplete bindings fail closed.
  - Debug builds assert if callers bind or dereference a partial context.

Related:
  - SkullbonezSource/Runtime/EngineContext.h
  - SkullbonezSource/Runtime/Run.h
*/
#include "EngineContext.h"

#include "RunState.h"

#include <cassert>

namespace SkullbonezCore
{
namespace Basics
{
void EngineContext::Bind( const EngineContextBindings& bindings )
{
    m_bindings = bindings;
    assert( IsBound() && "EngineContext requires every borrowed runtime binding" );
}


bool EngineContext::IsBound() const
{
    return m_bindings.scene && m_bindings.simulation && m_bindings.capture && m_bindings.diagnostics &&
           m_bindings.commands && m_bindings.systems && m_bindings.runtimeSettings && m_bindings.input &&
           m_bindings.camera && m_bindings.debug && m_bindings.world && m_bindings.physics && m_bindings.models;
}


EngineServices EngineContext::Services()
{
    // Invariant: service export still requires the full bound graph, but the
    // broad binding packet is no longer exposed to feature code.
    assert( IsBound() && "EngineServices requested before full Bind()" );
    RunSubsystemState& systems = *m_bindings.systems;
    EngineServices services{ &systems.assets,
                             systems.textures,
                             systems.cameras,
                             EngineServices::WindowService{ systems.window },
                             systems.terrain.get(),
                             systems.skyBox };
    // Invariant: service borrowers should fail before using half-initialized
    // process services. Run binds the broad context in its constructor, then
    // fills these legacy service bridges during Initialise().
    assert( services.assets && services.textures && services.cameras && services.window.window && services.terrain &&
            services.skyBox && "EngineServices requested before Run::Initialise completed" );
    return services;
}
} // namespace Basics
} // namespace SkullbonezCore
