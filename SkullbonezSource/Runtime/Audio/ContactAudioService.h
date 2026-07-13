/*
File: SkullbonezSource/Runtime/Audio/ContactAudioService.h
Purpose:
  Plays one material-aware impact thud when a physics contact carries enough
  collision energy.

Summary:
  Physics produces deterministic contact facts (solved impulse plus the
  pre-solve closing speed). After each fixed step this service applies one
  emit rule to those copied facts and submits short thud samples to XAudio2.
  Audio failure must never change simulation or validation behavior.

Concept — the whole model in one paragraph:
  A collision you can hear is two bodies that were APPROACHING each other and
  then stopped approaching. The solver tells us both halves of that story:
  `closingSpeed` is how fast the bodies were approaching just before the
  solver acted (m/s), and `normalImpulse` is how hard the solver had to push
  to stop them (kg*m/s). Their product, halved, is the kinetic energy the
  contact absorbed, in joules. Loud hit = lots of joules. Rolling, resting,
  and force transfer through a stack all have a closing speed of ~zero, so
  they carry ~zero impact energy and stay silent without any special cases.

Glossary:
  Impact energy: 0.5 * normalImpulse * closingSpeed, in joules. The single
    "how big was this hit" number used for the threshold and the volume.
  Closing speed: Contact-normal approach speed measured before the solver
    applies impulses. Rolling/resting contacts measure ~0 here even though
    the solver still pushes on them every step (that push is support force,
    not a hit).
  Pair cooldown: Short per-body-pair window that stops one physical bounce
    (which the solver can resolve across several fixed steps and manifold
    points) from machine-gunning the same thud.
  Sound set: Material-pair recipe from contact_audio.materials.json — sample
    list, base gain, pitch range, and max audible distance.
  Sample library: Decoded candidate sounds the Sound tab can preview and
    assign to a sound set at runtime.
  Decision: Bounded per-step verdict record explaining why a contact did or
    did not become a thud; consumed by SkullScope logging and body flashes.

Invariants:
  - No audio decision writes back into physics state.
  - Listener and contact data are copied before playback; the audio backend
    never dereferences Camera, PhysicsWorld, or solver storage.
  - All step scratch storage is reserved at startup (runtime allocation
    policy: no growth during steady gameplay).

Related:
  - SkullbonezSource/Runtime/Audio/ContactAudioService.cpp
  - Agentic/Plans/TODO/contact-audio-simplification.md
*/
#pragma once

#include <cstdint>

#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Runtime
{
namespace Audio
{
// One contact fact copied out of the physics debug-contact export. bodyB < 0
// means static terrain. Everything here is a value copy; the service never
// holds pointers into physics storage.
struct ContactAudioEvent
{
    int bodyA = -1;
    int bodyB = -1;
    uint32_t materialA = 0;
    uint32_t materialB = 0;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    float normalImpulse = 0.0f; // kg*m/s; solver's accumulated normal impulse
    float closingSpeed = 0.0f;  // m/s; pre-solve approach speed along the normal
};

// Per-step and lifetime counters. Every submitted contact fact lands in
// exactly one bucket, so the numbers reconcile: eventsSeen collapses into
// pairCandidates, and every candidate is either rejected once or submitted.
struct ContactAudioStats
{
    uint32_t eventsSeen = 0;          // Raw contact facts submitted this step.
    uint32_t pairCandidates = 0;      // Facts left after same-pair merging.
    uint32_t rejectedByMotion = 0;    // Closing speed under the floor (rolling/resting/support).
    uint32_t rejectedByEnergy = 0;    // Impact energy under the user threshold.
    uint32_t rejectedByCooldown = 0;  // Same pair thudded within the cooldown window.
    uint32_t rejectedByDistance = 0;  // Farther from the listener than the set allows.
    uint32_t submittedVoices = 0;     // Thuds actually handed to XAudio2.
    uint32_t droppedVoices = 0;       // Passed every gate but no voice was free.
};

// Why: "why didn't I hear that?" needs evidence, not vibes. One decision row
// per considered contact records the verdict and the numbers behind it for
// SkullScope logging and the emitted-body flash.
struct ContactAudioDecision
{
    ContactAudioEvent event;
    const char* reason = "";       // String literal; stable for the whole run.
    const char* soundSetName = ""; // Borrowed from the loaded material map.
    const char* samplePath = "";   // Borrowed from decoded sample storage.
    float impactEnergy = 0.0f;     // Joules; see header Concept paragraph.
    float minImpactEnergy = 0.0f;  // Threshold in force when judged.
    float distance = 0.0f;         // Listener distance in world units.
    float maxDistance = 0.0f;      // Audible range of the resolved sound set.
    float gain = 0.0f;             // Final submitted volume (0 when rejected).
    bool submitted = false;
};

// Read-only snapshot of one material sound set for the Sound tab picker.
// Lifetime: string pointers are borrowed from the loaded material map and are
// valid until the map is reloaded or the service shuts down.
struct ContactAudioSetTuning
{
    const char* name = "";
    uint32_t materialA = 0;
    uint32_t materialB = 0;
    float maxDistance = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t sampleCount = 0;
};

class ContactAudioService
{
  public:
    ContactAudioService();
    ~ContactAudioService();

    ContactAudioService( const ContactAudioService& ) = delete;
    ContactAudioService& operator=( const ContactAudioService& ) = delete;

    bool Initialize();
    void Shutdown();
    bool LoadContactAudioMap( const char* path );

    void SetEnabled( bool enabled );
    bool IsEnabled() const;
    bool IsAvailable() const;

    // The two user-facing knobs. Master gain is a plain volume multiplier;
    // min impact energy is the "big enough to hear" threshold in joules.
    void SetMasterGain( float gain );
    float MasterGain() const;
    void SetMinImpactEnergy( float energy );
    float MinImpactEnergy() const;

    // Sample library browsing for the Sound tab. Paths are borrowed from
    // decoded audio buffers and stay valid until the contact-audio map is
    // reloaded or the service shuts down.
    int SoundSetCount() const;
    int SoundSampleCount() const;
    const char* SoundSamplePath( int sampleIndex ) const;
    bool GetSoundSetTuning( int setIndex, ContactAudioSetTuning& out ) const;
    // Makes the chosen library sample the only sample of one material set.
    bool SetSoundSetSample( int setIndex, int sampleIndex );
    bool PreviewSoundSample( int sampleIndex, float gain );

    // Step protocol: Begin copies the listener and clears step scratch,
    // SubmitContact copies facts, EndPhysicsStep judges and plays them.
    void BeginPhysicsStep( float deltaSeconds, const Math::Vector::Vector3& listenerPosition );
    void SubmitContact( const ContactAudioEvent& event );
    void EndPhysicsStep();
    // Scene loads reassign body indices, so pair cooldown history from the
    // previous scene must not leak onto unrelated new bodies.
    void ResetSceneState();

    // Contacts that actually played this step; Run flashes their bodies.
    int SubmittedContactCount() const;
    bool GetSubmittedContact( int index, ContactAudioEvent& out ) const;
    // Bounded per-step verdicts for SkullScope. Records are valid until the
    // next BeginPhysicsStep() or Shutdown().
    int DecisionCount() const;
    bool GetDecision( int index, ContactAudioDecision& out ) const;

    // Headless CLI smoke: fabricates one energetic contact and reports
    // whether a voice was submitted.
    bool PlaySmokeImpact( uint32_t materialId, float normalImpulse );

    const ContactAudioStats& Stats() const;
    const ContactAudioStats& StepStats() const;
    void ResetFrameStats();

  private:
    struct Impl;
    Impl* m_impl;
};
} // namespace Audio
} // namespace Runtime
} // namespace SkullbonezCore
