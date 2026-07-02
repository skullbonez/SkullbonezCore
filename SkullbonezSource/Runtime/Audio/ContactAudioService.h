/*
File: SkullbonezSource/Runtime/Audio/ContactAudioService.h
Purpose:
  Plays material-aware physics contact impact sounds.

Mental model:
  Physics produces deterministic contact facts. This service consumes copied
  facts after a physics step, applies presentation-only threshold/cooldown
  policy, and submits sounds to XAudio2. Audio failure must never change
  simulation or validation behavior.

Glossary:
  Contact material: Gameplay/audio material token such as metal, stone, or wood.
  Cooldown key: Stable body-pair key that prevents persistent contact rows from
    replaying the same impact every fixed tick.
  Impact band: Light, medium, or heavy impulse tier that can select different
    gain/pitch/sample tuning inside one material sound set.
  Sound set: Material-pair tuning plus one or more decoded sample buffers.

Invariants:
  - No audio decision writes back into physics state.
  - Listener and contact data are copied before playback; the audio backend
    never dereferences GameModel, Camera, or PhysicsWorld objects.

Related:
  - SkullbonezSource/Runtime/Audio/ContactAudioService.cpp
  - Agentic/Plans/contact-impact-audio-plan.md
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
    bool isTerrain = false;
};

struct ContactAudioStats
{
    uint32_t eventsSeen = 0;
    uint32_t rejectedByThreshold = 0;
    uint32_t rejectedByCooldown = 0;
    uint32_t submittedVoices = 0;
    uint32_t droppedVoices = 0;
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
    const char* name = ""; // Borrowed from the loaded material map until it is reloaded or shutdown.
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t sampleCount = 0;
};

struct ContactAudioSetTuning
{
    const char* name = ""; // Borrowed from the loaded material map until it is reloaded or shutdown.
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
    int SoundSetCount() const;
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

    void BeginPhysicsStep( float deltaSeconds, const Math::Vector::Vector3& listenerPosition );
    void SubmitContact( const ContactAudioEvent& event );
    void EndPhysicsStep();
    bool PlaySmokeImpact( uint32_t materialId, float normalImpulse );

    const ContactAudioStats& Stats() const;
    void ResetFrameStats();

  private:
    struct Impl;
    Impl* m_impl;
};
} // namespace Audio
} // namespace Runtime
} // namespace SkullbonezCore
