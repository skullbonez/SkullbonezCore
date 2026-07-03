/*
File: SkullbonezSource/Runtime/Audio/ContactAudioService.cpp
Purpose:
  Implements material-aware contact impact playback through XAudio2.

Mental model:
  The service is a presentation sink. It filters contact events by material
  thresholds and cooldowns, then reuses bounded XAudio2 source voices. The
  decoded sample buffers remain owned here for as long as any voice can read
  them.

Glossary:
  Contact-audio decision: Bounded per-step verdict explaining whether a copied
    contact was submitted, made eligible for flash feedback, or rejected.
  XAudio2 source voice: Backend object that plays one PCM format. Voices are
    reused once their queued buffer drains.
  Wildcard material: A sound-map entry using "*" to match any partner material.
  Impulse range: Tuning span that maps solved normal impulse to gain.
  Impact score: Normal impulse multiplied by pre-solve closing speed; this
    approximates contact work better than solver force alone.
  Contact patch key: Body-pair, feature, and material-pair key used to collapse
    duplicate solver rows without merging different audible patches.
  Body burst budget: Per-burst cap on submitted sounds that mention the same
    dynamic body, used after global ranking to avoid one pile dominating audio.
  Global burst cap: Final Sound-tab-tuned limit on submitted voices per burst
    window after classification and local candidate ranking.
  Rolling/support contact: A body pair that remains touching across physics
    steps; it should stay quiet unless a much stronger impulse arrives.
  Sample library: Decoded sounds loaded for in-game auditioning even when only
    one sample is assigned to the active impact set.

Invariants:
  - SubmitContact() only appends or updates copied events in bounded scratch
    vectors; playback happens in EndPhysicsStep().
  - XAudio2 failures are fail-soft and only affect audio statistics/logging.

Related:
  - SkullbonezSource/Runtime/Audio/ContactAudioService.h
  - SkullbonezData/audio/contact_audio.materials.json
*/
#include "ContactAudioService.h"

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
#include <windows.h>
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
constexpr std::size_t MAX_STEP_CANDIDATES = 512;
constexpr std::size_t MAX_STEP_DECISIONS = 2048;
constexpr std::size_t MAX_COOLDOWN_ENTRIES = 4096;
constexpr uint32_t CONTACT_AUDIO_DEFAULT_BURST_VOICES = 20;
constexpr uint32_t CONTACT_AUDIO_MAX_BURST_VOICES = 40;
constexpr uint32_t CONTACT_AUDIO_MAX_EVENTS_PER_BODY_PER_BURST = 6;
constexpr float CONTACT_AUDIO_REARM_GAP_SECONDS = 0.18f;
constexpr float CONTACT_AUDIO_TERRAIN_REARM_GAP_SECONDS = 0.90f;
constexpr float CONTACT_AUDIO_BURST_GAP_SECONDS = 0.10f;
constexpr float CONTACT_AUDIO_SPIKE_RATIO = 1.65f;
constexpr float CONTACT_AUDIO_SPIKE_DELTA = 1.0f;
constexpr float CONTACT_AUDIO_ROLL_SLIDE_CLOSING_SPEED = 0.35f;
constexpr float CONTACT_AUDIO_ROLL_SLIDE_MIN_SLIP_SPEED = 0.65f;
constexpr float CONTACT_AUDIO_VOICE_STEAL_GAIN_RATIO = 1.20f;
constexpr float CONTACT_AUDIO_VOICE_STEAL_GAIN_DELTA = 0.03f;
constexpr float CONTACT_AUDIO_DEFAULT_MIN_CLOSING_SPEED = 2.0f;
constexpr float CONTACT_AUDIO_DEFAULT_MIN_IMPACT_SCORE = 250.0f;
constexpr float CONTACT_AUDIO_DEFAULT_IMPACT_SCORE_RANGE_SECONDS = 3.0f;
constexpr float CONTACT_AUDIO_LEGACY_CLOSING_SPEED = 2.0f;

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}

float ContactClosingSpeed( const ContactAudioEvent& event )
{
    return event.hasMotionData ? (std::max)( 0.0f, event.normalClosingSpeed ) : CONTACT_AUDIO_LEGACY_CLOSING_SPEED;
}

float ContactImpactScore( const ContactAudioEvent& event )
{
    return event.normalImpulse * ContactClosingSpeed( event );
}

float ContactCandidateRank( const ContactAudioEvent& event )
{
    return event.hasMotionData ? ContactImpactScore( event ) : event.normalImpulse;
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
    try
    {
        input >> out;
        return true;
    }
    catch ( const std::exception& e )
    {
        fprintf( stdout, "[audio] Contact audio map parse failed: %s (%s)\n", path, e.what() );
        return false;
    }
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

uint32_t JsonUintOrDefault( const Json& object, const char* key, uint32_t fallback )
{
    const auto it = object.find( key );
    if ( it == object.end() || !it->is_number_integer() )
    {
        return fallback;
    }
    return static_cast<uint32_t>( (std::max)( 0, it->get<int>() ) );
}

uint64_t PairKey( const ContactAudioEvent& event )
{
    const uint32_t a = static_cast<uint32_t>( (std::max)( event.bodyA, -1 ) + 1 );
    const uint32_t b = static_cast<uint32_t>( (std::max)( event.bodyB, -1 ) + 1 );
    const uint32_t lo = (std::min)( a, b );
    const uint32_t hi = (std::max)( a, b );
    return ( static_cast<uint64_t>( lo ) << 32 ) | static_cast<uint64_t>( hi );
}

uint64_t MixContactAudioKey( uint64_t key, uint64_t value )
{
    key ^= value + 0x9e3779b97f4a7c15ull + ( key << 6 ) + ( key >> 2 );
    return key;
}

uint64_t ContactPatchKey( const ContactAudioEvent& event )
{
    uint64_t key = PairKey( event );
    const uint32_t materialLo = (std::min)( event.materialA, event.materialB );
    const uint32_t materialHi = (std::max)( event.materialA, event.materialB );
    key = MixContactAudioKey( key, event.featureId );
    key = MixContactAudioKey( key, ( static_cast<uint64_t>( materialLo ) << 32 ) | materialHi );
    return key;
}

float ContactAudioDistance( const Vector3& a, const Vector3& b )
{
    const Vector3 d = a - b;
    return sqrtf( d.x * d.x + d.y * d.y + d.z * d.z );
}


float ContactRearmGapSeconds( const ContactAudioEvent& event )
{
    return event.isTerrain ? CONTACT_AUDIO_TERRAIN_REARM_GAP_SECONDS : CONTACT_AUDIO_REARM_GAP_SECONDS;
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

    struct SoundBand
    {
        std::string name;
        float minImpulse = 0.25f;
        float impulseRange = 8.0f;
        float baseGain = 0.55f;
        float pitchMin = 0.94f;
        float pitchMax = 1.06f;
        std::vector<int> soundIndices;
    };

    struct SoundSet
    {
        std::string name;
        uint32_t materialA = CONTACT_AUDIO_DEFAULT;
        uint32_t materialB = CONTACT_AUDIO_WILDCARD;
        float minImpulse = 0.25f;
        float impulseRange = 8.0f;
        float cooldownSeconds = 0.14f;
        float overrideCooldownSeconds = 0.055f;
        float maxDistance = 95.0f;
        float baseGain = 0.55f;
        float pitchMin = 0.94f;
        float pitchMax = 1.06f;
        uint32_t maxVoices = 24;
        std::vector<int> soundIndices;
        std::vector<SoundBand> bands;
    };

    struct StepCandidate
    {
        uint64_t key = 0;
        ContactAudioEvent event;
        float contactAgeSeconds = 0.0f;
        float rearmGapSeconds = 0.0f;
        float previousStrongestImpulse = 0.0f;
        bool ongoingContact = false;
        bool impulseSpike = true;
    };

    struct CooldownEntry
    {
        uint64_t key = 0;
        float nextTimeSeconds = 0.0f;
        float strongestRecentImpulse = 0.0f;
        float lastContactTimeSeconds = -1000.0f;
        bool hasRecentContact = false;
    };

    struct BodySubmissionCount
    {
        int body = -1;
        uint32_t count = 0;
    };

    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    std::vector<DecodedSound> sounds;
    std::vector<SoundSet> sets;
    std::vector<StepCandidate> stepCandidates;
    std::vector<ContactAudioEvent> submittedContacts;
    std::vector<ContactAudioDecision> decisions;
    std::vector<CooldownEntry> cooldowns;
    std::vector<BodySubmissionCount> bodySubmissionCounts;
    Vector3 listenerPosition = Math::Vector::ZERO_VECTOR;
    ContactAudioStats stats;
    ContactAudioStats stepStats;
    float timeSeconds = 0.0f;
    float masterGain = 1.0f;
    float maxDistanceScale = 1.0f;
    float minClosingSpeed = CONTACT_AUDIO_DEFAULT_MIN_CLOSING_SPEED;
    float minImpactScore = CONTACT_AUDIO_DEFAULT_MIN_IMPACT_SCORE;
    float impactScoreRangeSeconds = CONTACT_AUDIO_DEFAULT_IMPACT_SCORE_RANGE_SECONDS;
    uint32_t burstVoicesPerWindow = CONTACT_AUDIO_DEFAULT_BURST_VOICES; // Max submitted sounds per 100 ms burst.
    float nextBurstTimeSeconds = 0.0f;
    uint32_t sampleCursor = 0;
    bool initialized = false;
    bool enabled = true;

    Impl()
    {
        sounds.reserve( 32 );
        sets.reserve( 16 );
        stepCandidates.reserve( MAX_STEP_CANDIDATES );
        submittedContacts.reserve( MAX_STEP_CANDIDATES );
        decisions.reserve( MAX_STEP_DECISIONS );
        cooldowns.reserve( MAX_COOLDOWN_ENTRIES );
        bodySubmissionCounts.reserve( 256 );
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
        bodySubmissionCounts.clear();
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
        sound.voices.reserve( 8 );

        sounds.push_back( std::move( sound ) );
        return static_cast<int>( sounds.size() - 1 );
    }

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
            // Why: large collapses can legitimately exceed the mix cap. Let a
            // stronger new thud replace the quietest active thud, but do not let
            // minor settling churn the voice pool.
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
            set.minImpulse = JsonFloatOrDefault( setJson, "minImpulse", set.minImpulse );
            set.impulseRange = (std::max)( 0.001f, JsonFloatOrDefault( setJson, "impulseRange", set.impulseRange ) );
            set.cooldownSeconds = JsonFloatOrDefault( setJson, "cooldownMs", set.cooldownSeconds * 1000.0f ) * 0.001f;
            set.overrideCooldownSeconds =
                JsonFloatOrDefault( setJson, "overrideCooldownMs", set.overrideCooldownSeconds * 1000.0f ) * 0.001f;
            set.maxDistance = (std::max)( 1.0f, JsonFloatOrDefault( setJson, "maxDistance", set.maxDistance ) );
            set.baseGain = JsonFloatOrDefault( setJson, "baseGain", set.baseGain );
            set.pitchMin = JsonFloatOrDefault( setJson, "pitchMin", set.pitchMin );
            set.pitchMax = JsonFloatOrDefault( setJson, "pitchMax", set.pitchMax );
            set.maxVoices = (std::max)( 1u, JsonUintOrDefault( setJson, "maxVoices", set.maxVoices ) );

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

            const auto bandsIt = setJson.find( "bands" );
            if ( bandsIt != setJson.end() && bandsIt->is_array() )
            {
                for ( const Json& bandJson : *bandsIt )
                {
                    if ( !bandJson.is_object() )
                    {
                        continue;
                    }

                    SoundBand band;
                    band.name = JsonStringOrDefault( bandJson, "name", "impact" );
                    band.minImpulse = JsonFloatOrDefault( bandJson, "minImpulse", set.minImpulse );
                    band.impulseRange =
                        (std::max)( 0.001f, JsonFloatOrDefault( bandJson, "impulseRange", set.impulseRange ) );
                    band.baseGain = JsonFloatOrDefault( bandJson, "baseGain", set.baseGain );
                    band.pitchMin = JsonFloatOrDefault( bandJson, "pitchMin", set.pitchMin );
                    band.pitchMax = JsonFloatOrDefault( bandJson, "pitchMax", set.pitchMax );

                    const auto bandSamplesIt = bandJson.find( "samples" );
                    if ( bandSamplesIt != bandJson.end() && bandSamplesIt->is_array() )
                    {
                        for ( const Json& sample : *bandSamplesIt )
                        {
                            if ( sample.is_string() )
                            {
                                const int index = LoadOggSound( sample.get<std::string>() );
                                if ( index >= 0 )
                                {
                                    band.soundIndices.push_back( index );
                                }
                            }
                        }
                    }
                    set.bands.push_back( std::move( band ) );
                }
                std::sort( set.bands.begin(),
                           set.bands.end(),
                           []( const SoundBand& lhs, const SoundBand& rhs )
                           { return lhs.minImpulse < rhs.minImpulse; } );
            }

            if ( !set.soundIndices.empty() ||
                 std::any_of( set.bands.begin(),
                              set.bands.end(),
                              []( const SoundBand& band ) { return !band.soundIndices.empty(); } ) )
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
        out.minImpulse = set.minImpulse;
        out.impulseRange = set.impulseRange;
        out.cooldownMs = set.cooldownSeconds * 1000.0f;
        out.overrideCooldownMs = set.overrideCooldownSeconds * 1000.0f;
        out.maxDistance = set.maxDistance;
        out.baseGain = set.baseGain;
        out.pitchMin = set.pitchMin;
        out.pitchMax = set.pitchMax;
        out.maxVoices = set.maxVoices;
        out.sampleCount = static_cast<uint32_t>( set.soundIndices.size() );
        out.bandCount = static_cast<uint32_t>(
            (std::min)( set.bands.size(), static_cast<std::size_t>( CONTACT_AUDIO_TUNING_MAX_BANDS ) ) );
        for ( uint32_t i = 0; i < out.bandCount; ++i )
        {
            const SoundBand& band = set.bands[static_cast<std::size_t>( i )];
            ContactAudioBandTuning& dst = out.bands[i];
            dst.name = band.name.c_str();
            dst.minImpulse = band.minImpulse;
            dst.impulseRange = band.impulseRange;
            dst.baseGain = band.baseGain;
            dst.pitchMin = band.pitchMin;
            dst.pitchMax = band.pitchMax;
            dst.sampleCount = static_cast<uint32_t>( band.soundIndices.size() );
        }
        return true;
    }

    bool SetSetParam( int setIndex, ContactAudioSetParam param, float value )
    {
        if ( setIndex < 0 || setIndex >= static_cast<int>( sets.size() ) )
        {
            return false;
        }
        SoundSet& set = sets[static_cast<std::size_t>( setIndex )];
        // Invariant: UI tuning edits only presentation thresholds. Sample lists
        // and material hashes remain owned by the loaded material map.
        switch ( param )
        {
        case ContactAudioSetParam::MinImpulse:
            set.minImpulse = std::clamp( value, 0.0f, 1000.0f );
            return true;
        case ContactAudioSetParam::ImpulseRange:
            set.impulseRange = std::clamp( value, 0.001f, 1000.0f );
            return true;
        case ContactAudioSetParam::CooldownMs:
            set.cooldownSeconds = std::clamp( value, 0.0f, 5000.0f ) * 0.001f;
            return true;
        case ContactAudioSetParam::OverrideCooldownMs:
            set.overrideCooldownSeconds = std::clamp( value, 0.0f, 5000.0f ) * 0.001f;
            return true;
        case ContactAudioSetParam::MaxDistance:
            set.maxDistance = std::clamp( value, 1.0f, 2000.0f );
            return true;
        case ContactAudioSetParam::BaseGain:
            set.baseGain = std::clamp( value, 0.0f, 4.0f );
            return true;
        case ContactAudioSetParam::PitchMin:
            set.pitchMin = (std::min)( ClampPitchRatio( value ), set.pitchMax );
            return true;
        case ContactAudioSetParam::PitchMax:
            set.pitchMax = (std::max)( ClampPitchRatio( value ), set.pitchMin );
            return true;
        case ContactAudioSetParam::MaxVoices:
            set.maxVoices = static_cast<uint32_t>( std::clamp( static_cast<int>( std::round( value ) ), 1, 32 ) );
            return true;
        default:
            return false;
        }
    }

    bool SetBandParam( int setIndex, int bandIndex, ContactAudioBandParam param, float value )
    {
        if ( setIndex < 0 || setIndex >= static_cast<int>( sets.size() ) )
        {
            return false;
        }
        SoundSet& set = sets[static_cast<std::size_t>( setIndex )];
        if ( bandIndex < 0 || bandIndex >= static_cast<int>( set.bands.size() ) )
        {
            return false;
        }
        SoundBand& band = set.bands[static_cast<std::size_t>( bandIndex )];
        switch ( param )
        {
        case ContactAudioBandParam::MinImpulse:
            band.minImpulse = std::clamp( value, 0.0f, 1000.0f );
            std::sort( set.bands.begin(),
                       set.bands.end(),
                       []( const SoundBand& lhs, const SoundBand& rhs ) { return lhs.minImpulse < rhs.minImpulse; } );
            return true;
        case ContactAudioBandParam::ImpulseRange:
            band.impulseRange = std::clamp( value, 0.001f, 1000.0f );
            return true;
        case ContactAudioBandParam::BaseGain:
            band.baseGain = std::clamp( value, 0.0f, 4.0f );
            return true;
        case ContactAudioBandParam::PitchMin:
            band.pitchMin = (std::min)( ClampPitchRatio( value ), band.pitchMax );
            return true;
        case ContactAudioBandParam::PitchMax:
            band.pitchMax = (std::max)( ClampPitchRatio( value ), band.pitchMin );
            return true;
        default:
            return false;
        }
    }

    bool SetSetSample( int setIndex, int sampleIndex )
    {
        if ( setIndex < 0 || setIndex >= static_cast<int>( sets.size() ) || sampleIndex < 0 ||
             sampleIndex >= static_cast<int>( sounds.size() ) )
        {
            return false;
        }
        SoundSet& set = sets[static_cast<std::size_t>( setIndex )];
        set.soundIndices.clear();
        set.soundIndices.push_back( sampleIndex );
        for ( SoundBand& band : set.bands )
        {
            // Why: a chosen audition sample should become the only active
            // impact sound. Empty band sample lists intentionally fall back to
            // the set-level choice while preserving each band's gain curve.
            band.soundIndices.clear();
        }
        return true;
    }

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

    const SoundBand* ResolveBand( const SoundSet& set, float normalImpulse ) const
    {
        const SoundBand* selected = nullptr;
        for ( const SoundBand& band : set.bands )
        {
            if ( normalImpulse >= band.minImpulse )
            {
                selected = &band;
            }
        }
        return selected;
    }

    ContactAudioDecision BaseDecision( const ContactAudioEvent& event, uint64_t key, const char* reason ) const
    {
        ContactAudioDecision decision;
        decision.event = event;
        decision.pairKey = key;
        decision.reason = reason;
        return decision;
    }

    void CountEventSeen()
    {
        ++stats.eventsSeen;
        ++stepStats.eventsSeen;
    }

    void CountPatchCandidate()
    {
        ++stats.patchCandidates;
        ++stepStats.patchCandidates;
    }

    void CountMergedCandidate()
    {
        ++stats.mergedCandidates;
        ++stepStats.mergedCandidates;
    }

    void CountCandidateOverflow()
    {
        ++stats.candidateOverflows;
        ++stepStats.candidateOverflows;
    }

    void CountBurstWindowSkip( uint32_t count )
    {
        stats.burstWindowSkippedCandidates += count;
        stepStats.burstWindowSkippedCandidates += count;
    }

    void CountBudgetRejection()
    {
        ++stats.budgetRejectedCandidates;
        ++stepStats.budgetRejectedCandidates;
        ++stats.droppedVoices;
        ++stepStats.droppedVoices;
    }

    void CountThresholdRejection()
    {
        ++stats.rejectedByThreshold;
        ++stepStats.rejectedByThreshold;
    }

    void CountCooldownRejection()
    {
        ++stats.rejectedByCooldown;
        ++stepStats.rejectedByCooldown;
    }

    void CountSubmittedVoice()
    {
        ++stats.submittedVoices;
        ++stepStats.submittedVoices;
    }

    void CountDroppedVoice()
    {
        ++stats.droppedVoices;
        ++stepStats.droppedVoices;
    }

    void RecordDecision( const ContactAudioDecision& decision )
    {
        if ( decisions.size() < MAX_STEP_DECISIONS )
        {
            decisions.push_back( decision );
        }
    }

    uint32_t SubmittedCountForBody( int body ) const
    {
        if ( body < 0 )
        {
            return 0;
        }
        for ( const BodySubmissionCount& entry : bodySubmissionCounts )
        {
            if ( entry.body == body )
            {
                return entry.count;
            }
        }
        return 0;
    }

    bool HasBodyBudget( const ContactAudioEvent& event ) const
    {
        if ( SubmittedCountForBody( event.bodyA ) >= CONTACT_AUDIO_MAX_EVENTS_PER_BODY_PER_BURST )
        {
            return false;
        }
        return event.bodyB < 0 || event.bodyB == event.bodyA ||
               SubmittedCountForBody( event.bodyB ) < CONTACT_AUDIO_MAX_EVENTS_PER_BODY_PER_BURST;
    }

    void CountBodySubmission( int body )
    {
        if ( body < 0 )
        {
            return;
        }
        for ( BodySubmissionCount& entry : bodySubmissionCounts )
        {
            if ( entry.body == body )
            {
                ++entry.count;
                return;
            }
        }
        bodySubmissionCounts.push_back( BodySubmissionCount{ body, 1 } );
    }

    void CountBodySubmission( const ContactAudioEvent& event )
    {
        CountBodySubmission( event.bodyA );
        if ( event.bodyB != event.bodyA )
        {
            CountBodySubmission( event.bodyB );
        }
    }

    CooldownEntry* FindCooldown( uint64_t key )
    {
        for ( CooldownEntry& entry : cooldowns )
        {
            if ( entry.key == key )
            {
                return &entry;
            }
        }
        if ( cooldowns.size() >= MAX_COOLDOWN_ENTRIES )
        {
            CooldownEntry* oldest = nullptr;
            for ( CooldownEntry& entry : cooldowns )
            {
                if ( !oldest || entry.lastContactTimeSeconds < oldest->lastContactTimeSeconds )
                {
                    oldest = &entry;
                }
            }
            if ( oldest )
            {
                // Invariant: a full cooldown table must degrade deterministically.
                // Reusing the stalest pair keeps rolling/support contacts tracked
                // in large piles instead of treating them as fresh impacts forever.
                *oldest = CooldownEntry{};
                oldest->key = key;
                return oldest;
            }
            return nullptr;
        }
        cooldowns.push_back( CooldownEntry{} );
        cooldowns.back().key = key;
        return &cooldowns.back();
    }

    void AddCandidate( const ContactAudioEvent& event )
    {
        CountEventSeen();
        const uint64_t key = ContactPatchKey( event );
        for ( StepCandidate& candidate : stepCandidates )
        {
            if ( candidate.key == key )
            {
                CountMergedCandidate();
                if ( decisions.size() < MAX_STEP_CANDIDATES )
                {
                    ContactAudioDecision decision = BaseDecision( event, key, "patch_merged" );
                    decision.previousStrongestImpulse = candidate.event.normalImpulse;
                    RecordDecision( decision );
                }
                if ( ContactCandidateRank( event ) > ContactCandidateRank( candidate.event ) )
                {
                    candidate.event = event;
                }
                return;
            }
        }

        StepCandidate next;
        next.key = key;
        next.event = event;
        next.rearmGapSeconds = ContactRearmGapSeconds( event );
        if ( CooldownEntry* cooldown = FindCooldown( key ) )
        {
            next.contactAgeSeconds = timeSeconds - cooldown->lastContactTimeSeconds;
            next.ongoingContact = cooldown->hasRecentContact && next.contactAgeSeconds <= next.rearmGapSeconds;
            next.previousStrongestImpulse = next.ongoingContact ? cooldown->strongestRecentImpulse : 0.0f;
            next.impulseSpike = next.previousStrongestImpulse <= 0.0f ||
                                ( event.normalImpulse >= next.previousStrongestImpulse * CONTACT_AUDIO_SPIKE_RATIO &&
                                  event.normalImpulse >= next.previousStrongestImpulse + CONTACT_AUDIO_SPIKE_DELTA );

            // Why: contact history must track every observed pair, not only pairs
            // that win the burst selector. Otherwise burst-skipped wall contacts
            // can look "new" later and emit from propagated support impulses.
            cooldown->lastContactTimeSeconds = timeSeconds;
            cooldown->hasRecentContact = true;
            cooldown->strongestRecentImpulse = next.ongoingContact
                                                   ? (std::max)( cooldown->strongestRecentImpulse, event.normalImpulse )
                                                   : event.normalImpulse;
        }
        if ( stepCandidates.size() < MAX_STEP_CANDIDATES )
        {
            stepCandidates.push_back( next );
            CountPatchCandidate();
        }
        else
        {
            CountCandidateOverflow();
            if ( decisions.size() < MAX_STEP_CANDIDATES )
            {
                RecordDecision( BaseDecision( event, key, "patch_queue_full" ) );
            }
        }
    }

    void PlayCandidate( const StepCandidate& candidate )
    {
        const ContactAudioEvent& event = candidate.event;
        const SoundSet* set = ResolveSet( event.materialA, event.materialB );
        if ( !set )
        {
            RecordDecision( BaseDecision( event, candidate.key, "no_sound_set" ) );
            CountThresholdRejection();
            return;
        }
        const SoundBand* band = ResolveBand( *set, event.normalImpulse );
        const float minImpulse = band ? band->minImpulse : set->minImpulse;
        const float impulseRange = band ? band->impulseRange : set->impulseRange;
        const float baseGain = band ? band->baseGain : set->baseGain;
        const float pitchMin = band ? band->pitchMin : set->pitchMin;
        const float pitchMax = band ? band->pitchMax : set->pitchMax;
        const std::vector<int>& soundIndices =
            band && !band->soundIndices.empty() ? band->soundIndices : set->soundIndices;
        ContactAudioDecision decision = BaseDecision( event, candidate.key, "" );
        decision.soundSetName = set->name.c_str();
        decision.bandName = band ? band->name.c_str() : "";
        decision.minImpulse = minImpulse;
        decision.impulseRange = impulseRange;
        decision.maxVoices = set->maxVoices;
        if ( event.normalImpulse < minImpulse )
        {
            decision.reason = "below_min_impulse";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }
        if ( soundIndices.empty() )
        {
            decision.reason = "no_samples";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }

        CooldownEntry* cooldown = FindCooldown( candidate.key );
        const bool ongoingContact = candidate.ongoingContact;
        const bool impulseSpike = candidate.impulseSpike;
        const float contactAge = candidate.contactAgeSeconds;
        const float rearmGapSeconds = candidate.rearmGapSeconds;
        const float previousStrongest = candidate.previousStrongestImpulse;
        decision.contactAgeSeconds = contactAge;
        decision.rearmGapSeconds = rearmGapSeconds;
        decision.previousStrongestImpulse = previousStrongest;
        decision.ongoingContact = ongoingContact;
        decision.impulseSpike = impulseSpike;

        const float closingSpeed = ContactClosingSpeed( event );
        const float impactScore = ContactImpactScore( event );
        const float minImpactScoreForSet = (std::max)( minImpactScore, minImpulse * minClosingSpeed );
        decision.impactScore = impactScore;

        if ( ongoingContact && !event.isTerrain )
        {
            // Why: object/object contacts that were already touching are usually
            // force-transfer/support rows. A propagated spike may be real physics
            // but it is not a new audible contact patch.
            decision.reason = "ongoing_object_contact";
            RecordDecision( decision );
            CountCooldownRejection();
            return;
        }

        if ( event.hasMotionData && closingSpeed <= CONTACT_AUDIO_ROLL_SLIDE_CLOSING_SPEED &&
             event.tangentSlipSpeed >= CONTACT_AUDIO_ROLL_SLIDE_MIN_SLIP_SPEED )
        {
            decision.reason = "roll_or_slide";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }

        if ( ongoingContact && event.isTerrain && !impulseSpike && event.hasMotionData &&
             closingSpeed < minClosingSpeed )
        {
            decision.reason = "settle";
            RecordDecision( decision );
            CountCooldownRejection();
            return;
        }

        if ( cooldown && timeSeconds < cooldown->nextTimeSeconds )
        {
            if ( !ongoingContact || !impulseSpike )
            {
                decision.reason = ongoingContact ? "cooldown_ongoing" : "cooldown";
                RecordDecision( decision );
                CountCooldownRejection();
                return;
            }
        }

        if ( event.hasMotionData && closingSpeed < minClosingSpeed )
        {
            // Why: solver impulse can travel through an already-touching wall. A
            // thud needs contact work, not just constraint force.
            decision.reason = "propagated_impulse";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }
        if ( event.hasMotionData && impactScore < minImpactScoreForSet )
        {
            decision.reason = "below_min_impact_score";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }

        const float distance = ContactAudioDistance( event.point, listenerPosition );
        const float maxDistance = (std::max)( 1.0f, set->maxDistance * maxDistanceScale );
        decision.distance = distance;
        decision.maxDistance = maxDistance;
        if ( distance >= maxDistance )
        {
            decision.reason = "distance";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }

        const float distanceT = Clamp01( 1.0f - distance / maxDistance );
        const float distanceGain = distanceT * distanceT;
        const float impulseGain = Clamp01( ( event.normalImpulse - minImpulse ) / impulseRange );
        const float scoreRange = (std::max)( 0.001f, impulseRange * impactScoreRangeSeconds );
        const float motionGain =
            event.hasMotionData ? Clamp01( ( impactScore - minImpactScoreForSet ) / scoreRange ) : impulseGain;
        const float impactGain = event.hasMotionData ? (std::min)( impulseGain, motionGain ) : impulseGain;
        const float gain = Clamp01( masterGain * baseGain * distanceGain * impactGain );
        decision.distanceGain = distanceGain;
        decision.impactGain = impactGain;
        decision.motionGain = motionGain;
        decision.gain = gain;
        if ( gain <= 0.001f )
        {
            decision.reason = "gain_floor";
            RecordDecision( decision );
            CountThresholdRejection();
            return;
        }

        if ( !initialized )
        {
            decision.reason = "backend_unavailable";
            RecordDecision( decision );
            CountDroppedVoice();
            return;
        }

        // Concept: bands are presentation tiers only. Selecting a different
        // sample/gain curve for a heavier impulse never feeds back into the
        // solver, replay state, or body stores.
        const uint32_t sampleOrdinal = sampleCursor++ % static_cast<uint32_t>( soundIndices.size() );
        const int soundIndex = soundIndices[static_cast<std::size_t>( sampleOrdinal )];
        decision.sampleIndex = soundIndex;
        if ( soundIndex >= 0 && soundIndex < static_cast<int>( sounds.size() ) )
        {
            decision.samplePath = sounds[static_cast<std::size_t>( soundIndex )].path.c_str();
        }

        const float pitchT = pitchMax > pitchMin ? static_cast<float>( sampleCursor % 997u ) / 996.0f : 0.0f;
        const float pitch = pitchMin + ( pitchMax - pitchMin ) * pitchT;
        bool stoleVoice = false;
        if ( SubmitDecodedSound( soundIndex, gain, pitch, set->maxVoices, stoleVoice ) )
        {
            CountSubmittedVoice();
            decision.reason = stoleVoice ? "voice_stolen" : "submitted";
            decision.submitted = true;
            decision.flashEligible = true;
            // Lifetime: Run reads this copied event immediately after
            // EndPhysicsStep(); no GameModel or solver storage is borrowed.
            if ( submittedContacts.size() < MAX_STEP_CANDIDATES )
            {
                submittedContacts.push_back( event );
            }
            if ( cooldown )
            {
                const float cooldownSeconds =
                    ongoingContact && impulseSpike ? set->overrideCooldownSeconds : set->cooldownSeconds;
                cooldown->nextTimeSeconds = timeSeconds + cooldownSeconds;
                cooldown->strongestRecentImpulse = (std::max)( cooldown->strongestRecentImpulse, event.normalImpulse );
                cooldown->lastContactTimeSeconds = timeSeconds;
                cooldown->hasRecentContact = true;
            }
        }
        else
        {
            decision.reason = "voice_cap";
            CountDroppedVoice();
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


void ContactAudioService::SetMaxDistanceScale( float scale )
{
    m_impl->maxDistanceScale = std::clamp( scale, 0.01f, 16.0f );
}


float ContactAudioService::MaxDistanceScale() const
{
    return m_impl->maxDistanceScale;
}


void ContactAudioService::SetMinClosingSpeed( float speed )
{
    m_impl->minClosingSpeed = std::clamp( speed, 0.0f, 20.0f );
}


float ContactAudioService::MinClosingSpeed() const
{
    return m_impl->minClosingSpeed;
}


void ContactAudioService::SetMinImpactScore( float score )
{
    m_impl->minImpactScore = std::clamp( score, 0.0f, 5000.0f );
}


float ContactAudioService::MinImpactScore() const
{
    return m_impl->minImpactScore;
}


void ContactAudioService::SetImpactScoreRangeSeconds( float seconds )
{
    m_impl->impactScoreRangeSeconds = std::clamp( seconds, 0.001f, 10.0f );
}


float ContactAudioService::ImpactScoreRangeSeconds() const
{
    return m_impl->impactScoreRangeSeconds;
}


void ContactAudioService::SetBurstVoicesPerWindow( uint32_t voices )
{
    m_impl->burstVoicesPerWindow = std::clamp( voices, 1u, CONTACT_AUDIO_MAX_BURST_VOICES );
}


uint32_t ContactAudioService::BurstVoicesPerWindow() const
{
    return m_impl->burstVoicesPerWindow;
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
    if ( sampleIndex < 0 || sampleIndex >= static_cast<int>( m_impl->sounds.size() ) )
    {
        return "";
    }
    return m_impl->sounds[static_cast<std::size_t>( sampleIndex )].path.c_str();
}


bool ContactAudioService::GetSoundSetTuning( int setIndex, ContactAudioSetTuning& out ) const
{
    return m_impl->GetSetTuning( setIndex, out );
}


bool ContactAudioService::SetSoundSetParam( int setIndex, ContactAudioSetParam param, float value )
{
    return m_impl->SetSetParam( setIndex, param, value );
}


bool ContactAudioService::SetSoundBandParam( int setIndex, int bandIndex, ContactAudioBandParam param, float value )
{
    return m_impl->SetBandParam( setIndex, bandIndex, param, value );
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
        m_impl->bodySubmissionCounts.clear();
        return;
    }
    if ( m_impl->timeSeconds < m_impl->nextBurstTimeSeconds )
    {
        // Why: piles can generate hundreds of real contact rows per second. The
        // sound model is intentionally a burst selector, not a contact counter.
        m_impl->CountBurstWindowSkip( static_cast<uint32_t>( m_impl->stepCandidates.size() ) );
        m_impl->stepCandidates.clear();
        m_impl->bodySubmissionCounts.clear();
        return;
    }
    m_impl->bodySubmissionCounts.clear();
    const Vector3 listenerPosition = m_impl->listenerPosition;
    std::sort( m_impl->stepCandidates.begin(),
               m_impl->stepCandidates.end(),
               [listenerPosition]( const Impl::StepCandidate& lhs, const Impl::StepCandidate& rhs )
               {
                   const float lhsRank = ContactCandidateRank( lhs.event );
                   const float rhsRank = ContactCandidateRank( rhs.event );
                   if ( fabsf( lhsRank - rhsRank ) > 0.001f )
                   {
                       return lhsRank > rhsRank;
                   }
                   const float lhsDistance = ContactAudioDistance( lhs.event.point, listenerPosition );
                   const float rhsDistance = ContactAudioDistance( rhs.event.point, listenerPosition );
                   if ( fabsf( lhsDistance - rhsDistance ) > 0.001f )
                   {
                       return lhsDistance < rhsDistance;
                   }
                   return lhs.key < rhs.key;
               } );

    uint32_t submittedThisBurst = 0;
    for ( const Impl::StepCandidate& candidate : m_impl->stepCandidates )
    {
        if ( submittedThisBurst >= m_impl->burstVoicesPerWindow )
        {
            ContactAudioDecision decision = m_impl->BaseDecision( candidate.event, candidate.key, "burst_budget" );
            m_impl->RecordDecision( decision );
            m_impl->CountBudgetRejection();
            continue;
        }
        if ( !m_impl->HasBodyBudget( candidate.event ) )
        {
            ContactAudioDecision decision = m_impl->BaseDecision( candidate.event, candidate.key, "body_budget" );
            m_impl->RecordDecision( decision );
            m_impl->CountBudgetRejection();
            continue;
        }
        const std::size_t submittedBefore = m_impl->submittedContacts.size();
        m_impl->PlayCandidate( candidate );
        if ( m_impl->submittedContacts.size() > submittedBefore )
        {
            m_impl->CountBodySubmission( candidate.event );
            ++submittedThisBurst;
        }
    }
    if ( submittedThisBurst > 0 )
    {
        m_impl->nextBurstTimeSeconds = m_impl->timeSeconds + CONTACT_AUDIO_BURST_GAP_SECONDS;
    }
    m_impl->stepCandidates.clear();
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
    event.featureId = 1;
    event.materialA = materialId;
    event.materialB = CONTACT_AUDIO_DEFAULT;
    event.point = Math::Vector::ZERO_VECTOR;
    event.normal = Vector3( 0.0f, 1.0f, 0.0f );
    event.normalImpulse = normalImpulse;
    // Why: the smoke path is synthetic and has no solver velocity row. Give it
    // enough closing speed to exercise the current score gate without lowering
    // gameplay thresholds just to keep the headless test audible.
    const float syntheticImpactScore =
        m_impl->minImpactScore + (std::max)( normalImpulse, 1.0f ) * CONTACT_AUDIO_LEGACY_CLOSING_SPEED * 2.0f;
    event.normalClosingSpeed =
        (std::max)( m_impl->minClosingSpeed, syntheticImpactScore / (std::max)( normalImpulse, 0.001f ) );
    event.isTerrain = true;
    event.hasMotionData = true;
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
