/*
File: SkullbonezSource/Runtime/Audio/ContactAudioService.cpp
Purpose:
  Implements the one-rule contact thud player on top of XAudio2.

Summary:
  The service is a presentation sink. Each fixed physics step it copies
  contact facts, merges duplicates per body pair, applies one emit rule
  (real approach motion + enough impact energy + pair cooldown + audible
  distance), and plays the loudest survivors through pooled XAudio2 voices.
  The decoded sample buffers remain owned here for as long as any voice can
  read them.

Concept — how a contact becomes (or fails to become) a thud:
  1. MERGE: the solver reports several rows for one physical touch (manifold
     points, sub-features). All rows for the same unordered body pair merge
     into one candidate, keeping the row with the most impact energy.
  2. MOTION GATE: candidates whose pre-solve closing speed is under a small
     floor are dropped. This is the line that keeps rolling, resting, and
     force-transfer-through-a-stack silent: the solver pushes on those
     contacts every step, but the bodies were not actually approaching.
  3. ENERGY GATE: impact energy (0.5 * impulse * closing speed, joules) must
     beat the user threshold. Two boxes glancing in the air are a few joules;
     a box slamming into terrain is thousands. One number covers both.
  4. COOLDOWN GATE: a real bounce can take the solver 2-3 fixed steps to
     fully absorb, and each step would re-report it. A short per-pair window
     turns that into exactly one thud.
  5. DISTANCE + VOLUME: closer and harder means louder. Volume is
     master * setGain * distanceFalloff * energyGain — nothing else.

Glossary:
  XAudio2 source voice: Backend object that plays one PCM buffer. Voices are
    pooled per decoded sample and reused once their queued buffer drains.
  Voice stealing: When every pooled voice is busy, a clearly louder new thud
    may restart the quietest active one. Big pile collapses sound full
    instead of clipped, while soft hits never churn the pool.
  Wildcard material: A sound-map entry using "*" that matches any partner
    material; specific pairs win over wildcard fallbacks.
  Pair key: Unordered (bodyA, bodyB) key. Deliberately excludes the solver's
    feature id: feature ids change every step while a body rolls or a
    manifold rotates, which is exactly the churn the cooldown must survive.
  Rolling hook: The single marked point below where slip-based rolling audio
    would classify contacts, if it is ever wanted again.

Invariants:
  - SubmitContact() only appends/updates copied events in bounded scratch
    vectors; playback happens in EndPhysicsStep().
  - XAudio2 failures are fail-soft and only affect audio statistics/logging.
  - Scratch and cooldown storage is reserved once in the Impl constructor;
    steady gameplay never grows it (runtime allocation policy).

Related:
  - SkullbonezSource/Runtime/Audio/ContactAudioService.h
  - SkullbonezData/audio/contact_audio.materials.json
  - Agentic/Plans/TODO/contact-audio-simplification.md
*/
#include "ContactAudioService.h"
#include "../../Assets/AssetKeys.h"

#include "../../Core/Common.h"

#include "../../../ThirdPtySource/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include "../../Core/PlatformWin32.h"
#include <xaudio2.h>

#pragma warning( push, 0 )
#pragma warning( disable : 4701 )
#include "stb_vorbis.c"
#pragma warning( pop )

using SkullbonezCore::Math::Vector::Vector3;
using Json = nlohmann::ordered_json;

namespace SkullbonezCore
{
namespace Runtime
{
namespace Audio
{
namespace
{
constexpr uint32_t CONTACT_AUDIO_WILDCARD = HashStr( "*" );
constexpr uint32_t CONTACT_AUDIO_DEFAULT = HashStr( "default" );

// Bounded step scratch. 256 body pairs touching with audible energy in one
// 60 Hz step is already a catastrophic pile; beyond that we keep the loudest.
constexpr std::size_t MAX_STEP_CANDIDATES = 256;
constexpr std::size_t MAX_STEP_DECISIONS = 256;
// Cooldown entries track "this pair thudded recently". 512 concurrent noisy
// pairs outlives any current scene; the stalest entry is recycled when full.
constexpr std::size_t MAX_COOLDOWN_PAIRS = 512;

// MOTION GATE floor, m/s. Rolling and resting contacts measure closing
// speeds of a few centimetres per second (solver jitter); genuine hits that
// are worth hearing arrive at walking speed or faster. The energy threshold
// is the real loudness gate — this floor only has to separate "approaching"
// from "already touching".
constexpr float CONTACT_AUDIO_MIN_CLOSING_SPEED = 0.75f;

// COOLDOWN GATE window, seconds. Long enough that one physical bounce
// (resolved by the solver across a few 60 Hz steps) plays once; short enough
// that a genuine re-bounce a couple of tenths later still plays.
constexpr float CONTACT_AUDIO_PAIR_COOLDOWN_SECONDS = 0.15f;

// VOLUME curve: full volume is reached when a hit carries this many times
// the threshold energy. With the default 125 J threshold, ~2000 J (a heavy
// box landing hard) maxes out. The square root applied below makes mid hits
// audibly mid instead of near-silent, because perceived loudness is far from
// linear in energy.
constexpr float CONTACT_AUDIO_FULL_VOLUME_ENERGY_MULTIPLIER = 16.0f;

// Default "big enough to hear" threshold, joules. Tuned live from the Sound
// tab and persisted through engine.cfg (contact_audio_min_impact_energy).
constexpr float CONTACT_AUDIO_DEFAULT_MIN_IMPACT_ENERGY = 125.0f;

// Audible-range multiplier applied to each sound set's authored maxDistance
// at load time. Why 8: the owner's tuned engine.cfg carried
// contact_audio_max_distance_scale = 8 before that slider was removed, so
// the authored ~95-unit JSON ranges actually played out to ~760 world units.
// Baking the factor here preserves that audible range without a knob.
constexpr float CONTACT_AUDIO_DISTANCE_SCALE = 8.0f;

// Voice pool cap per decoded sample. More simultaneous copies of one thud
// sample than this just sums into mush; louder new hits steal instead.
constexpr uint32_t CONTACT_AUDIO_MAX_VOICES_PER_SOUND = 12;

// Voice stealing must be clearly justified: the new thud needs to be both
// 20% and an absolute step louder than the quietest active voice, so soft
// settling noise cannot churn the pool.
constexpr float CONTACT_AUDIO_VOICE_STEAL_GAIN_RATIO = 1.20f;
constexpr float CONTACT_AUDIO_VOICE_STEAL_GAIN_DELTA = 0.03f;

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}

// Impact energy in joules. Why this formula: normalImpulse is the momentum
// the solver removed along the contact normal (kg*m/s) and closingSpeed is
// the speed at which that momentum was arriving (m/s). Energy = 1/2 * p * v,
// exactly like 1/2 * m * v^2 with p = m * v. Support/rolling rows have big
// impulses but ~zero closing speed, so their energy is ~zero — which is the
// entire trick that removes rolling false positives.
float ContactImpactEnergy( const ContactAudioEvent& event )
{
    return 0.5f * (std::max)( 0.0f, event.normalImpulse ) * (std::max)( 0.0f, event.closingSpeed );
}

uint32_t MaterialHashFromToken( const std::string& token )
{
    return token.empty() ? CONTACT_AUDIO_DEFAULT : HashStr( token.c_str() );
}

bool ReadFileJson( const char* path, Json& out )
{
    std::ifstream input( path );
    if ( !input )
    {
        return false;
    }
    out = Json::parse( input, nullptr, false );
    if ( out.is_discarded() )
    {
        fprintf( stdout, "[audio] Contact audio map parse failed: %s\n", path );
        return false;
    }
    return true;
}

std::string JsonStringOrDefault( const Json& object, const char* key, const char* fallback )
{
    const auto it = object.find( key );
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string( fallback );
}

float JsonFloatOrDefault( const Json& object, const char* key, float fallback )
{
    const auto it = object.find( key );
    return it != object.end() && it->is_number() ? it->get<float>() : fallback;
}

// Unordered body-pair key. See "Pair key" in the header comment for why the
// solver feature id is deliberately left out.
uint64_t PairKey( const ContactAudioEvent& event )
{
    const uint32_t a = static_cast<uint32_t>( (std::max)( event.bodyA, -1 ) + 1 );
    const uint32_t b = static_cast<uint32_t>( (std::max)( event.bodyB, -1 ) + 1 );
    const uint32_t lo = (std::min)( a, b );
    const uint32_t hi = (std::max)( a, b );
    return ( static_cast<uint64_t>( lo ) << 32 ) | static_cast<uint64_t>( hi );
}

float ContactAudioDistance( const Vector3& a, const Vector3& b )
{
    const Vector3 d = a - b;
    return sqrtf( d.x * d.x + d.y * d.y + d.z * d.z );
}

float ClampPitchRatio( float value )
{
    return std::clamp( value, 0.25f, 4.0f );
}
} // namespace

struct ContactAudioService::Impl
{
    struct VoiceSlot
    {
        IXAudio2SourceVoice* voice = nullptr;
        float gain = 0.0f;
    };

    struct DecodedSound
    {
        std::string path;
        WAVEFORMATEX format = {};
        std::vector<short> samples;
        std::vector<VoiceSlot> voices;
    };

    struct SoundSet
    {
        std::string name;
        uint32_t materialA = CONTACT_AUDIO_DEFAULT;
        uint32_t materialB = CONTACT_AUDIO_WILDCARD;
        float maxDistance = 95.0f;
        float baseGain = 0.55f;
        float pitchMin = 0.94f;
        float pitchMax = 1.06f;
        std::vector<int> soundIndices;
    };

    struct StepCandidate
    {
        uint64_t key = 0;
        ContactAudioEvent event;
    };

    struct CooldownEntry
    {
        uint64_t key = 0;
        float lastThudTimeSeconds = -1000.0f;
    };

    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    std::vector<DecodedSound> sounds;
    std::vector<SoundSet> sets;
    std::vector<StepCandidate> stepCandidates;
    std::vector<ContactAudioEvent> submittedContacts;
    std::vector<ContactAudioDecision> decisions;
    std::vector<CooldownEntry> cooldowns;
    Vector3 listenerPosition = Math::Vector::ZERO_VECTOR;
    ContactAudioStats stats;
    ContactAudioStats stepStats;
    float timeSeconds = 0.0f;
    float masterGain = 1.0f;
    float minImpactEnergy = CONTACT_AUDIO_DEFAULT_MIN_IMPACT_ENERGY;
    uint32_t sampleCursor = 0;
    bool initialized = false;
    bool enabled = true;

    Impl()
    {
        // Runtime allocation policy: every runtime-growable container is
        // reserved here, before steady gameplay, and the step logic never
        // pushes past these capacities.
        sounds.reserve( 64 );
        sets.reserve( 16 );
        stepCandidates.reserve( MAX_STEP_CANDIDATES );
        submittedContacts.reserve( MAX_STEP_CANDIDATES );
        decisions.reserve( MAX_STEP_DECISIONS );
        cooldowns.reserve( MAX_COOLDOWN_PAIRS );
    }

    bool InitializeBackend()
    {
        if ( initialized )
        {
            return true;
        }

        HRESULT hr = XAudio2Create( &xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
        if ( FAILED( hr ) )
        {
            fprintf( stdout, "[audio] XAudio2Create failed: 0x%08lx. Contact audio disabled.\n", hr );
            return false;
        }

        hr = xaudio->CreateMasteringVoice( &masterVoice );
        if ( FAILED( hr ) )
        {
            fprintf( stdout, "[audio] CreateMasteringVoice failed: 0x%08lx. Contact audio disabled.\n", hr );
            xaudio->Release();
            xaudio = nullptr;
            return false;
        }

        initialized = true;
        return true;
    }

    void ShutdownBackend()
    {
        for ( DecodedSound& sound : sounds )
        {
            for ( VoiceSlot& slot : sound.voices )
            {
                if ( slot.voice )
                {
                    slot.voice->DestroyVoice();
                    slot.voice = nullptr;
                }
            }
        }
        sounds.clear();
        sets.clear();
        stepCandidates.clear();
        submittedContacts.clear();
        decisions.clear();
        cooldowns.clear();
        if ( masterVoice )
        {
            masterVoice->DestroyVoice();
            masterVoice = nullptr;
        }
        if ( xaudio )
        {
            xaudio->Release();
            xaudio = nullptr;
        }
        initialized = false;
    }

    int LoadOggSound( const std::string& path )
    {
        for ( int i = 0; i < static_cast<int>( sounds.size() ); ++i )
        {
            if ( sounds[static_cast<std::size_t>( i )].path == path )
            {
                return i;
            }
        }

        int channels = 0;
        int sampleRate = 0;
        short* output = nullptr;
        const int samplesPerChannel = stb_vorbis_decode_filename( path.c_str(), &channels, &sampleRate, &output );
        if ( samplesPerChannel <= 0 || !output || channels <= 0 || sampleRate <= 0 )
        {
            fprintf( stdout, "[audio] Failed to decode contact sound: %s\n", path.c_str() );
            if ( output )
            {
                free( output );
            }
            return -1;
        }

        DecodedSound sound;
        sound.path = path;
        sound.samples.assign( output, output + static_cast<std::size_t>( samplesPerChannel * channels ) );
        free( output );

        sound.format.wFormatTag = WAVE_FORMAT_PCM;
        sound.format.nChannels = static_cast<WORD>( channels );
        sound.format.nSamplesPerSec = static_cast<DWORD>( sampleRate );
        sound.format.wBitsPerSample = 16;
        sound.format.nBlockAlign = static_cast<WORD>( channels * sizeof( short ) );
        sound.format.nAvgBytesPerSec = sound.format.nSamplesPerSec * sound.format.nBlockAlign;
        sound.voices.reserve( CONTACT_AUDIO_MAX_VOICES_PER_SOUND );

        sounds.push_back( std::move( sound ) );
        return static_cast<int>( sounds.size() - 1 );
    }

    const char* SamplePath( int soundIndex ) const
    {
        if ( soundIndex < 0 || soundIndex >= static_cast<int>( sounds.size() ) )
        {
            return "";
        }
        return sounds[static_cast<std::size_t>( soundIndex )].path.c_str();
    }

    // Plays one decoded sample at the given volume and pitch through the
    // sample's voice pool. Lifetime: the XAUDIO2_BUFFER points into
    // DecodedSound::samples, which outlives every voice (ShutdownBackend
    // destroys voices before releasing sample storage).
    bool SubmitDecodedSound( int soundIndex, float gain, float pitch, uint32_t maxVoices, bool& outStoleVoice )
    {
        outStoleVoice = false;
        if ( !initialized || soundIndex < 0 || soundIndex >= static_cast<int>( sounds.size() ) )
        {
            return false;
        }

        DecodedSound& sound = sounds[static_cast<std::size_t>( soundIndex )];
        IXAudio2SourceVoice* voice = nullptr;
        VoiceSlot* selectedSlot = nullptr;
        VoiceSlot* weakestActiveSlot = nullptr;
        float weakestActiveGain = ( std::numeric_limits<float>::max )();
        for ( VoiceSlot& slot : sound.voices )
        {
            XAUDIO2_VOICE_STATE state = {};
            slot.voice->GetState( &state, XAUDIO2_VOICE_NOSAMPLESPLAYED );
            if ( state.BuffersQueued == 0 )
            {
                selectedSlot = &slot;
                voice = slot.voice;
                slot.gain = 0.0f;
                break;
            }
            if ( slot.gain < weakestActiveGain )
            {
                weakestActiveGain = slot.gain;
                weakestActiveSlot = &slot;
            }
        }
        if ( !voice && sound.voices.size() < maxVoices )
        {
            IXAudio2SourceVoice* newVoice = nullptr;
            if ( SUCCEEDED( xaudio->CreateSourceVoice( &newVoice, &sound.format ) ) )
            {
                sound.voices.push_back( VoiceSlot{ newVoice } );
                selectedSlot = &sound.voices.back();
                voice = newVoice;
            }
        }
        if ( !voice && weakestActiveSlot )
        {
            // Why: large collapses can legitimately exceed the pool. Let a
            // clearly stronger new thud replace the quietest active thud, but
            // never let minor settling churn the voice pool.
            if ( gain >= weakestActiveGain * CONTACT_AUDIO_VOICE_STEAL_GAIN_RATIO &&
                 gain >= weakestActiveGain + CONTACT_AUDIO_VOICE_STEAL_GAIN_DELTA )
            {
                selectedSlot = weakestActiveSlot;
                voice = weakestActiveSlot->voice;
                outStoleVoice = true;
            }
        }
        if ( !voice )
        {
            return false;
        }

        XAUDIO2_BUFFER buffer = {};
        buffer.AudioBytes = static_cast<UINT32>( sound.samples.size() * sizeof( short ) );
        buffer.pAudioData = reinterpret_cast<const BYTE*>( sound.samples.data() );
        buffer.Flags = XAUDIO2_END_OF_STREAM;

        voice->Stop( 0 );
        voice->FlushSourceBuffers();
        voice->SetVolume( std::clamp( gain, 0.0f, 4.0f ) );
        voice->SetFrequencyRatio( std::clamp( pitch, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO ) );
        if ( SUCCEEDED( voice->SubmitSourceBuffer( &buffer ) ) && SUCCEEDED( voice->Start( 0 ) ) )
        {
            if ( selectedSlot )
            {
                selectedSlot->gain = gain;
            }
            return true;
        }
        voice->FlushSourceBuffers();
        if ( selectedSlot )
        {
            selectedSlot->gain = 0.0f;
        }
        return false;
    }

    // Loads the authored material map. The loader reads only the fields the
    // simplified model uses (name, materials, samples, maxDistance, baseGain,
    // pitchMin/pitchMax) and ignores historical tuning fields (bands, impulse
    // thresholds, cooldowns, voice caps) so existing JSON keeps loading
    // unchanged.
    bool LoadMap( const char* path )
    {
        Json root;
        if ( !ReadFileJson( path, root ) )
        {
            fprintf( stdout, "[audio] Contact audio map missing: %s\n", path );
            return false;
        }
        const auto setsIt = root.find( "sets" );
        if ( setsIt == root.end() || !setsIt->is_array() )
        {
            fprintf( stdout, "[audio] Contact audio map has no sets array: %s\n", path );
            return false;
        }

        sets.clear();
        // The library list decodes every candidate thud up front so the Sound
        // tab can audition and assign them without touching disk mid-game.
        const auto librarySamplesIt = root.find( "librarySamples" );
        if ( librarySamplesIt != root.end() && librarySamplesIt->is_array() )
        {
            for ( const Json& sample : *librarySamplesIt )
            {
                if ( sample.is_string() )
                {
                    LoadOggSound( sample.get<std::string>() );
                }
            }
        }
        for ( const Json& setJson : *setsIt )
        {
            if ( !setJson.is_object() )
            {
                continue;
            }

            SoundSet set;
            set.name = JsonStringOrDefault( setJson, "name", "impact.default" );
            const auto materialsIt = setJson.find( "materials" );
            if ( materialsIt != setJson.end() && materialsIt->is_array() && materialsIt->size() >= 2 )
            {
                if ( ( *materialsIt )[0].is_string() )
                {
                    set.materialA = MaterialHashFromToken( ( *materialsIt )[0].get<std::string>() );
                }
                if ( ( *materialsIt )[1].is_string() )
                {
                    set.materialB = MaterialHashFromToken( ( *materialsIt )[1].get<std::string>() );
                }
            }
            set.maxDistance = (std::max)( 1.0f, JsonFloatOrDefault( setJson, "maxDistance", set.maxDistance ) ) *
                              CONTACT_AUDIO_DISTANCE_SCALE;
            set.baseGain = JsonFloatOrDefault( setJson, "baseGain", set.baseGain );
            set.pitchMin = JsonFloatOrDefault( setJson, "pitchMin", set.pitchMin );
            set.pitchMax = JsonFloatOrDefault( setJson, "pitchMax", set.pitchMax );

            const auto samplesIt = setJson.find( "samples" );
            if ( samplesIt != setJson.end() && samplesIt->is_array() )
            {
                for ( const Json& sample : *samplesIt )
                {
                    if ( sample.is_string() )
                    {
                        const int index = LoadOggSound( sample.get<std::string>() );
                        if ( index >= 0 )
                        {
                            set.soundIndices.push_back( index );
                        }
                    }
                }
            }

            if ( !set.soundIndices.empty() )
            {
                sets.push_back( std::move( set ) );
            }
        }

        fprintf( stdout,
                 "[audio] Contact audio loaded %zu sound set(s), %zu sample(s).\n",
                 sets.size(),
                 sounds.size() );
        return !sets.empty();
    }

    bool GetSetTuning( int setIndex, ContactAudioSetTuning& out ) const
    {
        if ( setIndex < 0 || setIndex >= static_cast<int>( sets.size() ) )
        {
            return false;
        }

        const SoundSet& set = sets[static_cast<std::size_t>( setIndex )];
        out = ContactAudioSetTuning{};
        out.name = set.name.c_str();
        out.materialA = set.materialA;
        out.materialB = set.materialB;
        out.maxDistance = set.maxDistance;
        out.baseGain = set.baseGain;
        out.pitchMin = set.pitchMin;
        out.pitchMax = set.pitchMax;
        out.sampleCount = static_cast<uint32_t>( set.soundIndices.size() );
        return true;
    }

    bool SetSetSample( int setIndex, int sampleIndex )
    {
        if ( setIndex < 0 || setIndex >= static_cast<int>( sets.size() ) || sampleIndex < 0 ||
             sampleIndex >= static_cast<int>( sounds.size() ) )
        {
            return false;
        }
        // Why: a chosen audition sample becomes the only active thud for the
        // set. The material map on disk is untouched; this is a live edit.
        SoundSet& set = sets[static_cast<std::size_t>( setIndex )];
        set.soundIndices.clear();
        set.soundIndices.push_back( sampleIndex );
        return true;
    }

    // Picks the sound set for a material pair: an exact pair match wins, then
    // the most specific wildcard entry (metal/* beats */*).
    const SoundSet* ResolveSet( uint32_t materialA, uint32_t materialB ) const
    {
        const SoundSet* fallback = nullptr;
        int fallbackScore = -1;
        for ( const SoundSet& set : sets )
        {
            const bool exact = ( set.materialA == materialA && set.materialB == materialB ) ||
                               ( set.materialA == materialB && set.materialB == materialA );
            if ( exact )
            {
                return &set;
            }
            const bool wildcard = set.materialA == CONTACT_AUDIO_WILDCARD || set.materialB == CONTACT_AUDIO_WILDCARD;
            const bool matchesA =
                set.materialA == CONTACT_AUDIO_WILDCARD || set.materialA == materialA || set.materialA == materialB;
            const bool matchesB =
                set.materialB == CONTACT_AUDIO_WILDCARD || set.materialB == materialA || set.materialB == materialB;
            if ( wildcard && matchesA && matchesB )
            {
                // Why: default/* is intentionally broad, but a material-specific
                // wildcard such as metal/* must win for terrain/default contacts.
                const auto scoreMaterial = []( uint32_t material ) -> int
                {
                    if ( material == CONTACT_AUDIO_WILDCARD )
                    {
                        return 0;
                    }
                    return material == CONTACT_AUDIO_DEFAULT ? 1 : 2;
                };
                const int score = scoreMaterial( set.materialA ) + scoreMaterial( set.materialB );
                if ( score > fallbackScore )
                {
                    fallback = &set;
                    fallbackScore = score;
                }
            }
        }
        return fallback;
    }

    void RecordDecision( const ContactAudioDecision& decision )
    {
        if ( decisions.size() < MAX_STEP_DECISIONS )
        {
            decisions.push_back( decision );
        }
    }

    // Finds or creates the cooldown row for a pair. Invariant: a full table
    // degrades deterministically by recycling the stalest pair instead of
    // allocating, so a huge pile cannot grow memory or drop tracking wholesale.
    CooldownEntry* FindCooldown( uint64_t key )
    {
        CooldownEntry* stalest = nullptr;
        for ( CooldownEntry& entry : cooldowns )
        {
            if ( entry.key == key )
            {
                return &entry;
            }
            if ( !stalest || entry.lastThudTimeSeconds < stalest->lastThudTimeSeconds )
            {
                stalest = &entry;
            }
        }
        if ( cooldowns.size() < MAX_COOLDOWN_PAIRS )
        {
            cooldowns.push_back( CooldownEntry{} );
            cooldowns.back().key = key;
            return &cooldowns.back();
        }
        if ( stalest )
        {
            *stalest = CooldownEntry{};
            stalest->key = key;
        }
        return stalest;
    }

    // MERGE stage. The solver reports one row per manifold point, so a flat
    // box landing produces four rows for one audible thud. Keep only the most
    // energetic row per body pair.
    void AddCandidate( const ContactAudioEvent& event )
    {
        ++stats.eventsSeen;
        ++stepStats.eventsSeen;
        const uint64_t key = PairKey( event );
        for ( StepCandidate& candidate : stepCandidates )
        {
            if ( candidate.key == key )
            {
                if ( ContactImpactEnergy( event ) > ContactImpactEnergy( candidate.event ) )
                {
                    candidate.event = event;
                }
                return;
            }
        }
        if ( stepCandidates.size() < MAX_STEP_CANDIDATES )
        {
            stepCandidates.push_back( StepCandidate{ key, event } );
            ++stats.pairCandidates;
            ++stepStats.pairCandidates;
        }
        // A full candidate table silently keeps the pairs it already has;
        // candidates are judged loudest-first, so the loss is the quietest
        // corner of an already deafening step.
    }

    // Gates 2-5 for one merged candidate. Every exit records a decision so
    // SkullScope can answer "why was this silent" with data.
    void PlayCandidate( const StepCandidate& candidate )
    {
        const ContactAudioEvent& event = candidate.event;
        ContactAudioDecision decision;
        decision.event = event;
        decision.impactEnergy = ContactImpactEnergy( event );
        decision.minImpactEnergy = minImpactEnergy;

        const SoundSet* set = ResolveSet( event.materialA, event.materialB );
        if ( !set || set->soundIndices.empty() )
        {
            // No recipe for this material pair: nothing could ever play, so
            // count it with the energy/threshold rejections for the stats line.
            decision.reason = "no_sound_set";
            RecordDecision( decision );
            ++stats.rejectedByEnergy;
            ++stepStats.rejectedByEnergy;
            return;
        }
        decision.soundSetName = set->name.c_str();

        // MOTION GATE. Rolling hook: if rolling audio is ever wanted, this is
        // the branch where low-closing-speed contacts with real tangential
        // slip would be classified and routed to a quiet rolling lane. Today
        // they are intentionally silent.
        if ( event.closingSpeed < CONTACT_AUDIO_MIN_CLOSING_SPEED )
        {
            decision.reason = "no_approach_motion";
            RecordDecision( decision );
            ++stats.rejectedByMotion;
            ++stepStats.rejectedByMotion;
            return;
        }

        // ENERGY GATE — the user's "big enough to hear" slider.
        if ( decision.impactEnergy < minImpactEnergy )
        {
            decision.reason = "below_min_energy";
            RecordDecision( decision );
            ++stats.rejectedByEnergy;
            ++stepStats.rejectedByEnergy;
            return;
        }

        // COOLDOWN GATE — one thud per pair per bounce.
        CooldownEntry* cooldown = FindCooldown( candidate.key );
        if ( cooldown && timeSeconds - cooldown->lastThudTimeSeconds < CONTACT_AUDIO_PAIR_COOLDOWN_SECONDS )
        {
            decision.reason = "pair_cooldown";
            RecordDecision( decision );
            ++stats.rejectedByCooldown;
            ++stepStats.rejectedByCooldown;
            return;
        }

        // DISTANCE GATE and falloff. (1 - d/max)^2 fades smoothly to zero at
        // the set's authored audible range instead of cutting off abruptly.
        const float distance = ContactAudioDistance( event.point, listenerPosition );
        decision.distance = distance;
        decision.maxDistance = set->maxDistance;
        if ( distance >= set->maxDistance )
        {
            decision.reason = "too_far";
            RecordDecision( decision );
            ++stats.rejectedByDistance;
            ++stepStats.rejectedByDistance;
            return;
        }
        const float distanceT = Clamp01( 1.0f - distance / set->maxDistance );
        const float distanceGain = distanceT * distanceT;

        // VOLUME. energyGain runs 0..1 from the threshold up to
        // FULL_VOLUME_ENERGY_MULTIPLIER times the threshold. The square root
        // is a perceptual bend: hearing is roughly logarithmic, so linear
        // energy would make everything but the biggest slam nearly silent.
        const float fullVolumeEnergy = minImpactEnergy * CONTACT_AUDIO_FULL_VOLUME_ENERGY_MULTIPLIER;
        const float energyRange = (std::max)( fullVolumeEnergy - minImpactEnergy, 1.0f );
        const float energyGain = sqrtf( Clamp01( ( decision.impactEnergy - minImpactEnergy ) / energyRange ) );
        const float gain = Clamp01( masterGain * set->baseGain * distanceGain * energyGain );
        decision.gain = gain;
        if ( gain <= 0.001f )
        {
            decision.reason = "gain_floor";
            RecordDecision( decision );
            ++stats.rejectedByEnergy;
            ++stepStats.rejectedByEnergy;
            return;
        }

        if ( !initialized )
        {
            decision.reason = "backend_unavailable";
            RecordDecision( decision );
            ++stats.droppedVoices;
            ++stepStats.droppedVoices;
            return;
        }

        // Sample rotation avoids the machine-gun effect of one identical
        // waveform repeating; the small pitch spread does the same job for
        // single-sample sets. Neither affects physics or determinism —
        // presentation only.
        const uint32_t sampleOrdinal = sampleCursor++ % static_cast<uint32_t>( set->soundIndices.size() );
        const int soundIndex = set->soundIndices[static_cast<std::size_t>( sampleOrdinal )];
        decision.samplePath = SamplePath( soundIndex );
        const float pitchT = set->pitchMax > set->pitchMin ? static_cast<float>( sampleCursor % 997u ) / 996.0f : 0.0f;
        const float pitch = ClampPitchRatio( set->pitchMin + ( set->pitchMax - set->pitchMin ) * pitchT );

        bool stoleVoice = false;
        if ( SubmitDecodedSound( soundIndex, gain, pitch, CONTACT_AUDIO_MAX_VOICES_PER_SOUND, stoleVoice ) )
        {
            decision.reason = stoleVoice ? "voice_stolen" : "submitted";
            decision.submitted = true;
            ++stats.submittedVoices;
            ++stepStats.submittedVoices;
            // Lifetime: Run reads these copied events immediately after
            // EndPhysicsStep() to flash the emitting bodies; no solver storage
            // is borrowed.
            if ( submittedContacts.size() < MAX_STEP_CANDIDATES )
            {
                submittedContacts.push_back( event );
            }
            if ( cooldown )
            {
                cooldown->lastThudTimeSeconds = timeSeconds;
            }
        }
        else
        {
            decision.reason = "voice_pool_full";
            ++stats.droppedVoices;
            ++stepStats.droppedVoices;
        }
        RecordDecision( decision );
    }
};

ContactAudioService::ContactAudioService() : m_impl( new Impl() )
{
}


ContactAudioService::~ContactAudioService()
{
    Shutdown();
    delete m_impl;
    m_impl = nullptr;
}


bool ContactAudioService::Initialize()
{
    return m_impl->InitializeBackend();
}


void ContactAudioService::Shutdown()
{
    if ( m_impl )
    {
        m_impl->ShutdownBackend();
    }
}


bool ContactAudioService::LoadContactAudioMap( const char* path )
{
    return m_impl->LoadMap( path );
}


void ContactAudioService::SetEnabled( bool enabled )
{
    m_impl->enabled = enabled;
}


bool ContactAudioService::IsEnabled() const
{
    return m_impl->enabled;
}


bool ContactAudioService::IsAvailable() const
{
    return m_impl->initialized && !m_impl->sets.empty();
}


void ContactAudioService::SetMasterGain( float gain )
{
    m_impl->masterGain = std::clamp( gain, 0.0f, 4.0f );
}


float ContactAudioService::MasterGain() const
{
    return m_impl->masterGain;
}


void ContactAudioService::SetMinImpactEnergy( float energy )
{
    m_impl->minImpactEnergy = std::clamp( energy, 1.0f, 100000.0f );
}


float ContactAudioService::MinImpactEnergy() const
{
    return m_impl->minImpactEnergy;
}


int ContactAudioService::SoundSetCount() const
{
    return static_cast<int>( m_impl->sets.size() );
}


int ContactAudioService::SoundSampleCount() const
{
    return static_cast<int>( m_impl->sounds.size() );
}


const char* ContactAudioService::SoundSamplePath( int sampleIndex ) const
{
    return m_impl->SamplePath( sampleIndex );
}


bool ContactAudioService::GetSoundSetTuning( int setIndex, ContactAudioSetTuning& out ) const
{
    return m_impl->GetSetTuning( setIndex, out );
}


bool ContactAudioService::SetSoundSetSample( int setIndex, int sampleIndex )
{
    return m_impl->SetSetSample( setIndex, sampleIndex );
}


bool ContactAudioService::PreviewSoundSample( int sampleIndex, float gain )
{
    bool stoleVoice = false;
    return m_impl->SubmitDecodedSound( sampleIndex, gain, 1.0f, 4, stoleVoice );
}


void ContactAudioService::BeginPhysicsStep( float deltaSeconds, const Vector3& listenerPosition )
{
    m_impl->timeSeconds += (std::max)( 0.0f, deltaSeconds );
    m_impl->listenerPosition = listenerPosition;
    m_impl->stepCandidates.clear();
    m_impl->submittedContacts.clear();
    m_impl->decisions.clear();
    m_impl->stepStats = ContactAudioStats{};
}


void ContactAudioService::SubmitContact( const ContactAudioEvent& event )
{
    if ( m_impl->enabled )
    {
        m_impl->AddCandidate( event );
    }
}


void ContactAudioService::EndPhysicsStep()
{
    if ( !m_impl->enabled )
    {
        m_impl->stepCandidates.clear();
        m_impl->submittedContacts.clear();
        m_impl->decisions.clear();
        return;
    }

    // Judge loudest-first so the voice pool goes to the biggest hits when a
    // wall of boxes lands in a single step. Ties break on the pair key so
    // the order is deterministic for identical inputs.
    std::sort( m_impl->stepCandidates.begin(),
               m_impl->stepCandidates.end(),
               []( const Impl::StepCandidate& lhs, const Impl::StepCandidate& rhs )
               {
                   const float lhsEnergy = ContactImpactEnergy( lhs.event );
                   const float rhsEnergy = ContactImpactEnergy( rhs.event );
                   if ( lhsEnergy != rhsEnergy )
                   {
                       return lhsEnergy > rhsEnergy;
                   }
                   return lhs.key < rhs.key;
               } );

    for ( const Impl::StepCandidate& candidate : m_impl->stepCandidates )
    {
        m_impl->PlayCandidate( candidate );
    }
    m_impl->stepCandidates.clear();
}


void ContactAudioService::ResetSceneState()
{
    // Why: cooldown keys are (bodyA, bodyB) index pairs, and indices are
    // scene-local. Clearing on scene load stops a thud fired just before the
    // load from muting an unrelated pair that reuses the same indices.
    m_impl->cooldowns.clear();
    m_impl->stepCandidates.clear();
    m_impl->submittedContacts.clear();
    m_impl->decisions.clear();
}


int ContactAudioService::SubmittedContactCount() const
{
    return static_cast<int>( m_impl->submittedContacts.size() );
}


bool ContactAudioService::GetSubmittedContact( int index, ContactAudioEvent& out ) const
{
    if ( index < 0 || index >= static_cast<int>( m_impl->submittedContacts.size() ) )
    {
        return false;
    }

    out = m_impl->submittedContacts[static_cast<std::size_t>( index )];
    return true;
}


int ContactAudioService::DecisionCount() const
{
    return static_cast<int>( m_impl->decisions.size() );
}


bool ContactAudioService::GetDecision( int index, ContactAudioDecision& out ) const
{
    if ( index < 0 || index >= static_cast<int>( m_impl->decisions.size() ) )
    {
        return false;
    }
    out = m_impl->decisions[static_cast<std::size_t>( index )];
    return true;
}


bool ContactAudioService::PlaySmokeImpact( uint32_t materialId, float normalImpulse )
{
    if ( !m_impl->enabled || !IsAvailable() )
    {
        return false;
    }

    ResetFrameStats();
    BeginPhysicsStep( 0.016f, Math::Vector::ZERO_VECTOR );
    ContactAudioEvent event;
    event.bodyA = 0;
    event.bodyB = -1;
    event.materialA = materialId;
    event.materialB = CONTACT_AUDIO_DEFAULT;
    event.point = Math::Vector::ZERO_VECTOR;
    event.normalImpulse = (std::max)( normalImpulse, 1.0f );
    // Why: the smoke path is synthetic and has no solver velocity row. Give
    // it enough closing speed that the fabricated impact energy clears the
    // configured threshold with headroom, so the headless test stays audible
    // without loosening any gameplay gate.
    event.closingSpeed = (std::max)( CONTACT_AUDIO_MIN_CLOSING_SPEED * 2.0f,
                                     4.0f * m_impl->minImpactEnergy / event.normalImpulse );
    SubmitContact( event );
    EndPhysicsStep();
    return Stats().submittedVoices > 0;
}


const ContactAudioStats& ContactAudioService::Stats() const
{
    return m_impl->stats;
}


const ContactAudioStats& ContactAudioService::StepStats() const
{
    return m_impl->stepStats;
}


void ContactAudioService::ResetFrameStats()
{
    m_impl->stats = ContactAudioStats{};
    m_impl->stepStats = ContactAudioStats{};
}
} // namespace Audio
} // namespace Runtime
} // namespace SkullbonezCore
