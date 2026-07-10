/*
File: SkullbonezSource/Runtime/Audio/ContactAudioService.h
Purpose:
  Plays material-aware physics contact impact and rolling sounds.

Mental model:
  Physics produces deterministic contact facts. This service consumes copied
  facts after a physics step, applies presentation-only threshold/cooldown
  policy, and submits sounds to XAudio2. Audio failure must never change
  simulation or validation behavior.

Glossary:
  Contact-audio decision: Presentation-side verdict explaining whether a copied
    contact became a sound, flash feedback, or a specific rejection.
  Contact-audio kind: Perceptual class such as impact, heavy_landing, support,
    settle, roll_slide, or propagated_impulse.
  Simple linear mode: Optional body-motion path that ignores solver contact rows
    and emits from mass-scaled linear velocity changes.
  Contact material: Gameplay/audio material token such as metal, stone, or wood.
  Cooldown key: Stable contact-patch key that prevents persistent contact rows
    from replaying the same impact every fixed tick.
  Patch candidate: One reduced contact patch kept after duplicate solver facts
    for the same body/material/feature key have been merged.
  Impact band: Light, medium, or heavy impulse tier that can select different
    gain/pitch/sample tuning inside one material sound set.
  Pre-solve closing speed: Contact normal velocity before the solver applies
    warm-start or corrective impulses; this is the impact-motion gate for thuds.
  Impact score: Solved normal impulse multiplied by pre-solve closing speed,
    used to keep force-transfer rows quieter than real contact work.
  Rolling lane: Low-gain roll/slide playback with its own level, distance, and
    burst budget so persistent motion does not use the thud falloff.
  Sound set: Material-pair tuning plus one or more decoded sample buffers.
  Sample library: Decoded candidate sounds that the Sound tab can preview and
    assign to a sound set at runtime.
  Submitted contact: A contact event that passed threshold/cooldown/distance
    policy and actually submitted a voice to XAudio2.
  Rolling/support contact: A body pair that remains touching across physics
    steps and should not be treated as a new impact each cooldown window.

Invariants:
  - No audio decision writes back into physics state.
  - Listener and contact data are copied before playback; the audio backend
    never dereferences GameModel, Camera, or PhysicsWorld objects.

Related:
  - SkullbonezSource/Runtime/Audio/ContactAudioService.cpp
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
struct ContactAudioEvent
{
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    uint32_t materialA = 0;
    uint32_t materialB = 0;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    float normalImpulse = 0.0f;
    float normalClosingSpeed = 0.0f;
    float tangentSlipSpeed = 0.0f;
    float linearEnergy = 0.0f;
    float linearDeltaSpeed = 0.0f;
    float linearSpeedBefore = 0.0f;
    float linearSpeedAfter = 0.0f;
    bool isTerrain = false;
    bool hasMotionData = false;
    bool simpleLinear = false;
};

struct ContactAudioStats
{
    uint32_t eventsSeen = 0;
    uint32_t patchCandidates = 0;
    uint32_t mergedCandidates = 0;
    uint32_t candidateOverflows = 0;
    uint32_t burstWindowSkippedCandidates = 0;
    uint32_t budgetRejectedCandidates = 0;
    uint32_t rejectedByThreshold = 0;
    uint32_t rejectedByCooldown = 0;
    uint32_t submittedVoices = 0;
    uint32_t droppedVoices = 0;
    uint32_t rollingCandidates = 0;
    uint32_t rollingSubmittedVoices = 0;
};

struct ContactAudioDecision
{
    ContactAudioEvent event;
    uint64_t pairKey = 0;
    const char* reason = "";       // String literal or borrowed map/sample text for immediate frame use.
    const char* kind = "";         // String literal classification used by SkullScope summaries.
    const char* soundSetName = ""; // Borrowed from the loaded material map.
    const char* bandName = "";     // Borrowed from the loaded material map.
    const char* samplePath = "";   // Borrowed from decoded sample storage.
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float distance = 0.0f;
    float maxDistance = 0.0f;
    float distanceGain = 0.0f;
    float impactGain = 0.0f;
    float motionGain = 0.0f;
    float impactScore = 0.0f;
    float gain = 0.0f;
    float contactAgeSeconds = 0.0f;
    float rearmGapSeconds = 0.0f;
    float previousStrongestImpulse = 0.0f;
    uint32_t maxVoices = 0;
    int sampleIndex = -1;
    bool ongoingContact = false;
    bool impulseSpike = false;
    bool submitted = false;
    bool flashEligible = false;
};

constexpr int CONTACT_AUDIO_TUNING_MAX_BANDS = 4;

// Live-tuning parameters are presentation-only controls. The UI exposes
// cooldowns in milliseconds, while the service keeps the authored runtime values
// in seconds next to the decoded sound-set data.
enum class ContactAudioSetParam
{
    MinImpulse,
    ImpulseRange,
    CooldownMs,
    OverrideCooldownMs,
    MaxDistance,
    BaseGain,
    PitchMin,
    PitchMax,
    MaxVoices
};

enum class ContactAudioBandParam
{
    MinImpulse,
    ImpulseRange,
    BaseGain,
    PitchMin,
    PitchMax
};

struct ContactAudioBandTuning
{
    const char* name = "";         // Borrowed from the loaded material map until it is reloaded or shutdown.
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t sampleCount = 0;
};

struct ContactAudioSetTuning
{
    const char* name = "";         // Borrowed from the loaded material map until it is reloaded or shutdown.
    uint32_t materialA = 0;
    uint32_t materialB = 0;
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float cooldownMs = 0.0f;
    float overrideCooldownMs = 0.0f;
    float maxDistance = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t maxVoices = 0;
    uint32_t sampleCount = 0;
    uint32_t bandCount = 0;
    ContactAudioBandTuning bands[CONTACT_AUDIO_TUNING_MAX_BANDS];
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
    void SetMasterGain( float gain );
    float MasterGain() const;
    void SetMaxDistanceScale( float scale );
    float MaxDistanceScale() const;
    void SetMinClosingSpeed( float speed );
    float MinClosingSpeed() const;
    void SetMinImpactScore( float score );
    float MinImpactScore() const;
    void SetImpactScoreRangeSeconds( float seconds );
    float ImpactScoreRangeSeconds() const;
    void SetSimpleModeEnabled( bool enabled );
    bool SimpleModeEnabled() const;
    void SetSimpleMinLinearEnergy( float energy );
    float SimpleMinLinearEnergy() const;
    void SetSimpleMinLinearDeltaSpeed( float speed );
    float SimpleMinLinearDeltaSpeed() const;
    void SetSimpleLinearEnergyRange( float energy );
    float SimpleLinearEnergyRange() const;
    // Caps ranked contact sounds submitted in each 100 ms burst window.
    void SetBurstVoicesPerWindow( uint32_t voices );
    uint32_t BurstVoicesPerWindow() const;
    // Rolling sounds have their own close-range gain and voice budget so they
    // can be enabled without widening the impact/thud mix.
    void SetRollingLevelDb( float levelDb );
    float RollingLevelDb() const;
    void SetRollingMaxDistance( float distance );
    float RollingMaxDistance() const;
    void SetRollingMinSlipSpeed( float speed );
    float RollingMinSlipSpeed() const;
    void SetRollingVoicesPerWindow( uint32_t voices );
    uint32_t RollingVoicesPerWindow() const;
    int SoundSetCount() const;
    int SoundSampleCount() const;
    // Sample paths are borrowed from decoded audio buffers and are valid until
    // the contact-audio map is reloaded or the service shuts down.
    const char* SoundSamplePath( int sampleIndex ) const;
    // Copies one material sound set into UI-friendly units. String pointers in
    // the snapshot are borrowed for immediate frame use; callers must not cache
    // them across audio-map reloads or Shutdown().
    bool GetSoundSetTuning( int setIndex, ContactAudioSetTuning& out ) const;
    // Applies a bounded live tuning edit to one material set. Returns false for
    // stale frame indices, unknown parameters, or an unloaded audio map.
    bool SetSoundSetParam( int setIndex, ContactAudioSetParam param, float value );
    // Applies a bounded live tuning edit to one impact band inside a material
    // set. Band min-impulse edits may reorder bands for runtime selection.
    bool SetSoundBandParam( int setIndex, int bandIndex, ContactAudioBandParam param, float value );
    bool SetSoundSetSample( int setIndex, int sampleIndex );
    bool PreviewSoundSample( int sampleIndex, float gain );

    void BeginPhysicsStep( float deltaSeconds, const Math::Vector::Vector3& listenerPosition );
    void BeginSimpleLinearStep( int bodyCount );
    void SubmitContact( const ContactAudioEvent& event );
    void SubmitLinearMotion( int bodyIndex,
                             uint32_t materialId,
                             const Math::Vector::Vector3& position,
                             const Math::Vector::Vector3& linearVelocity,
                             float mass );
    void EndPhysicsStep();
    void ResetSimpleLinearHistory();
    int SubmittedContactCount() const;
    bool GetSubmittedContact( int index, ContactAudioEvent& out ) const;
    // Returns bounded per-step verdicts for visual feedback and SkullScope. The
    // records are valid until the next BeginPhysicsStep() or Shutdown().
    int DecisionCount() const;
    bool GetDecision( int index, ContactAudioDecision& out ) const;
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
