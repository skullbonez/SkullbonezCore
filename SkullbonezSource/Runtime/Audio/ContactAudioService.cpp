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
  XAudio2 source voice: Backend object that plays one PCM format. Voices are
  reused once their queued buffer drains.
  Wildcard material: A sound-map entry using "*" to match any partner material.
  Impulse range: Tuning span that maps solved normal impulse to gain.

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
constexpr std::size_t MAX_STEP_CANDIDATES = 64;
constexpr std::size_t MAX_COOLDOWN_ENTRIES = 512;

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
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
        uint32_t maxVoices = 8;
        std::vector<int> soundIndices;
        std::vector<SoundBand> bands;
    };

    struct StepCandidate
    {
        uint64_t key = 0;
        ContactAudioEvent event;
    };

    struct CooldownEntry
    {
        uint64_t key = 0;
        float nextTimeSeconds = 0.0f;
        float strongestRecentImpulse = 0.0f;
    };

    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    std::vector<DecodedSound> sounds;
    std::vector<SoundSet> sets;
    std::vector<StepCandidate> stepCandidates;
    std::vector<CooldownEntry> cooldowns;
    Vector3 listenerPosition = Math::Vector::ZERO_VECTOR;
    ContactAudioStats stats;
    float timeSeconds = 0.0f;
    float masterGain = 1.0f;
    float maxDistanceScale = 1.0f;
    uint32_t sampleCursor = 0;
    bool initialized = false;
    bool enabled = true;

    Impl()
    {
        sounds.reserve( 32 );
        sets.reserve( 16 );
        stepCandidates.reserve( MAX_STEP_CANDIDATES );
        cooldowns.reserve( MAX_COOLDOWN_ENTRIES );
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
        sound.voices.reserve( 8 );

        sounds.push_back( std::move( sound ) );
        return static_cast<int>( sounds.size() - 1 );
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
            return nullptr;
        }
        cooldowns.push_back( CooldownEntry{} );
        cooldowns.back().key = key;
        return &cooldowns.back();
    }

    void AddCandidate( const ContactAudioEvent& event )
    {
        ++stats.eventsSeen;
        const uint64_t key = PairKey( event );
        for ( StepCandidate& candidate : stepCandidates )
        {
            if ( candidate.key == key )
            {
                if ( event.normalImpulse > candidate.event.normalImpulse )
                {
                    candidate.event = event;
                }
                return;
            }
        }
        if ( stepCandidates.size() < MAX_STEP_CANDIDATES )
        {
            stepCandidates.push_back( StepCandidate{ key, event } );
        }
    }

    void PlayCandidate( const StepCandidate& candidate )
    {
        const ContactAudioEvent& event = candidate.event;
        const SoundSet* set = ResolveSet( event.materialA, event.materialB );
        if ( !set )
        {
            ++stats.rejectedByThreshold;
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
        if ( event.normalImpulse < minImpulse || soundIndices.empty() )
        {
            ++stats.rejectedByThreshold;
            return;
        }

        CooldownEntry* cooldown = FindCooldown( candidate.key );
        if ( cooldown && timeSeconds < cooldown->nextTimeSeconds &&
             event.normalImpulse < cooldown->strongestRecentImpulse * 1.45f )
        {
            ++stats.rejectedByCooldown;
            return;
        }

        const float distance = ContactAudioDistance( event.point, listenerPosition );
        const float maxDistance = (std::max)( 1.0f, set->maxDistance * maxDistanceScale );
        if ( distance >= maxDistance )
        {
            ++stats.rejectedByThreshold;
            return;
        }

        const float distanceT = Clamp01( 1.0f - distance / maxDistance );
        const float distanceGain = distanceT * distanceT;
        const float impactGain = Clamp01( ( event.normalImpulse - minImpulse ) / impulseRange );
        const float gain = Clamp01( masterGain * baseGain * distanceGain * impactGain );
        if ( gain <= 0.001f )
        {
            ++stats.rejectedByThreshold;
            return;
        }

        if ( !initialized )
        {
            ++stats.droppedVoices;
            return;
        }

        // Concept: bands are presentation tiers only. Selecting a different
        // sample/gain curve for a heavier impulse never feeds back into the
        // solver, replay state, or body stores.
        const uint32_t sampleOrdinal = sampleCursor++ % static_cast<uint32_t>( soundIndices.size() );
        DecodedSound& sound =
            sounds[static_cast<std::size_t>( soundIndices[static_cast<std::size_t>( sampleOrdinal )] )];
        IXAudio2SourceVoice* voice = nullptr;
        for ( VoiceSlot& slot : sound.voices )
        {
            XAUDIO2_VOICE_STATE state = {};
            slot.voice->GetState( &state, XAUDIO2_VOICE_NOSAMPLESPLAYED );
            if ( state.BuffersQueued == 0 )
            {
                voice = slot.voice;
                break;
            }
        }
        if ( !voice && sound.voices.size() < set->maxVoices )
        {
            IXAudio2SourceVoice* newVoice = nullptr;
            if ( SUCCEEDED( xaudio->CreateSourceVoice( &newVoice, &sound.format ) ) )
            {
                sound.voices.push_back( VoiceSlot{ newVoice } );
                voice = newVoice;
            }
        }
        if ( !voice )
        {
            ++stats.droppedVoices;
            return;
        }

        const float pitchT = pitchMax > pitchMin ? static_cast<float>( sampleCursor % 997u ) / 996.0f : 0.0f;
        const float pitch = pitchMin + ( pitchMax - pitchMin ) * pitchT;
        XAUDIO2_BUFFER buffer = {};
        buffer.AudioBytes = static_cast<UINT32>( sound.samples.size() * sizeof( short ) );
        buffer.pAudioData = reinterpret_cast<const BYTE*>( sound.samples.data() );
        buffer.Flags = XAUDIO2_END_OF_STREAM;

        voice->Stop( 0 );
        voice->FlushSourceBuffers();
        voice->SetVolume( gain );
        voice->SetFrequencyRatio( std::clamp( pitch, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO ) );
        if ( SUCCEEDED( voice->SubmitSourceBuffer( &buffer ) ) && SUCCEEDED( voice->Start( 0 ) ) )
        {
            ++stats.submittedVoices;
            if ( cooldown )
            {
                const float cooldownSeconds = event.normalImpulse > cooldown->strongestRecentImpulse * 1.45f
                                                  ? set->overrideCooldownSeconds
                                                  : set->cooldownSeconds;
                cooldown->nextTimeSeconds = timeSeconds + cooldownSeconds;
                cooldown->strongestRecentImpulse = event.normalImpulse;
            }
        }
        else
        {
            voice->FlushSourceBuffers();
            ++stats.droppedVoices;
        }
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


int ContactAudioService::SoundSetCount() const
{
    return static_cast<int>( m_impl->sets.size() );
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


void ContactAudioService::BeginPhysicsStep( float deltaSeconds, const Vector3& listenerPosition )
{
    m_impl->timeSeconds += (std::max)( 0.0f, deltaSeconds );
    m_impl->listenerPosition = listenerPosition;
    m_impl->stepCandidates.clear();
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
        return;
    }
    for ( const Impl::StepCandidate& candidate : m_impl->stepCandidates )
    {
        m_impl->PlayCandidate( candidate );
    }
    m_impl->stepCandidates.clear();
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
    event.isTerrain = true;
    SubmitContact( event );
    EndPhysicsStep();
    return Stats().submittedVoices > 0;
}


const ContactAudioStats& ContactAudioService::Stats() const
{
    return m_impl->stats;
}


void ContactAudioService::ResetFrameStats()
{
    m_impl->stats = ContactAudioStats{};
}
} // namespace Audio
} // namespace Runtime
} // namespace SkullbonezCore
