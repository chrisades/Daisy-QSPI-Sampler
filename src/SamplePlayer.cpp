#include "SamplePlayer.h"
#include "daisysp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

using namespace daisysp;

SamplePlayer::SamplePlayer()
    : header_(nullptr),
      clips_(nullptr),
      pcm_(nullptr),
      sampleBase_(0),
      loaded_(false)
{
    for(uint32_t i = 0; i < MAX_CLIPS; i++)
    {
        voices_[i].position   = 0.0f;
        voices_[i].speed      = 1.0f;
        voices_[i].gain       = 0.0f;
        voices_[i].level      = 1.0f;
        voices_[i].playing    = false;
        voices_[i].loop       = false;
        voices_[i].restarting = false;
    }

    loaded_ = ValidateBank();
}

bool SamplePlayer::IsLoaded() const
{
    return loaded_;
}
 
bool SamplePlayer::ValidateBank() const
{
    if(!header_)
        return false;
 
    // Catches blank/erased flash and other corrupt clip counts.
    if(header_->numClips == 0 || header_->numClips > MAX_CLIPS)
        return false;
 
    // Catches a garbage header.
    if(header_->sampleRate < 1000 || header_->sampleRate > 192000)
        return false;
 
    // Catches a corrupt or truncated clip table.
    for(uint32_t i = 0; i < header_->numClips; i++)
    {
        if(clips_[i].end <= clips_[i].start)
            return false;
    }
 
    return true;
}

void SamplePlayer::Init(uint32_t sampleBase, float hardwareSampleRate)
{
    sampleBase_ = sampleBase;

    header_ = reinterpret_cast<const BankHeader*>(
        sampleBase_
    );

    clips_ = reinterpret_cast<const ClipEntry*>(
        sampleBase_
        + sizeof(BankHeader)
    );

    pcm_ = reinterpret_cast<const int16_t*>(
        sampleBase_
        + sizeof(BankHeader)
        + header_->numClips * sizeof(ClipEntry)
    );

    // rateRatio_ = (hardwareSampleRate > 0.0f)
    //     ? static_cast<float>(header_->sampleRate) / hardwareSampleRate
    //     : 1.0f;

    for(uint32_t i = 0; i < MAX_CLIPS; i++)
    {
        voices_[i].position   = 0.0f;
        voices_[i].speed      = 1.0f;
        voices_[i].gain       = 0.0f;
        voices_[i].level      = 1.0f;
        voices_[i].playing    = false;
        voices_[i].loop       = false;
        voices_[i].restarting = false;
    }

    loaded_ = ValidateBank();
}

uint32_t SamplePlayer::GetNumClips() const
{
    if(!header_)
        return 0;

    return header_->numClips;
}


uint32_t SamplePlayer::GetSampleRate() const
{
    if(!header_)
        return 0;

    return header_->sampleRate;
}


const char* SamplePlayer::GetClipName(uint32_t clip) const
{
    if(!IsValidClip(clip))
        return nullptr;

    return clips_[clip].name;
}


uint32_t SamplePlayer::GetClipLength(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 0;

    return clips_[clip].end - clips_[clip].start;
}


uint32_t SamplePlayer::GetClipStart(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 0;

    return clips_[clip].start;
}


uint32_t SamplePlayer::GetClipEnd(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 0;

    return clips_[clip].end;
}

void SamplePlayer::Resume(const char* name)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    if(clips_[clip].end <= clips_[clip].start)
        return;

    voices_[clip].playing = true;
}

void SamplePlayer::Replay(const char* name)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    if(clips_[clip].end <= clips_[clip].start)
        return;

    Voice& voice = voices_[clip];

    if(voice.playing)
    {
        voice.restarting = true;
    }
    else
    {
        voice.position    = static_cast<float>(clips_[clip].start);
        voice.gain         = 0.0f;
        voice.restarting  = false;
    }

    voice.playing = true;
}


void SamplePlayer::Pause(const char* name)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    voices_[clip].playing = false;
}


void SamplePlayer::PauseAll()
{
    const uint32_t numClips = GetNumClips();

    for(uint32_t i = 0;
        i < numClips && i < MAX_CLIPS;
        i++)
    {
        voices_[i].playing = false;
    }
}


bool SamplePlayer::IsPlaying(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return false;

    return voices_[clip].playing;
}


void SamplePlayer::SetLoop(const char* name, bool loop)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    voices_[clip].loop = loop;
}


bool SamplePlayer::IsLooping(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return false;

    return voices_[clip].loop;
}

void SamplePlayer::SetSpeed(const char* name, float speed)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    voices_[clip].speed = std::max(0.001f, speed);
}

float SamplePlayer::GetSpeed(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 1.0f;

    return voices_[clip].speed;
}

void SamplePlayer::SetLevel(const char* name, float level)
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return;

    voices_[clip].level = std::clamp(level, 0.0f, 1.0f);
}

float SamplePlayer::GetLevel(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 1.0f;

    return voices_[clip].level;
}

float SamplePlayer::GetPosition(const char* name) const
{
    uint32_t clip = FindClip(name);

    if(!IsValidClip(clip))
        return 0.0f;

    const float start = static_cast<float>(clips_[clip].start);

    const float end = static_cast<float>(clips_[clip].end);

    const float length = end - start;

    if(length <= 0.0f)
        return 0.0f;

    return std::clamp((voices_[clip].position - start) / length, 0.0f, 1.0f);
}

float SamplePlayer::GetPlaybackIncrement(uint32_t clip) const
{
    if(!IsValidClip(clip))
        return 0.0f;

    //return voices_[clip].speed * rateRatio_;
    return voices_[clip].speed;
}

float SamplePlayer::Process()
{
    if(!header_ || !clips_ || !pcm_)
        return 0.0f;

    float output = 0.0f;

    const uint32_t numClips = std::min(header_->numClips, MAX_CLIPS);

    // Below this, a voice's fade is considered finished.
    constexpr float GAIN_FLOOR = 0.0005f;

    for(uint32_t clip = 0; clip < numClips; clip++) {
        Voice& voice = voices_[clip];

        if(!voice.playing && voice.gain < GAIN_FLOOR)
            continue;

        const uint32_t start = clips_[clip].start;
        const uint32_t end   = clips_[clip].end;

        if(end <= start)
        {
            voice.playing    = false;
            voice.gain       = 0.0f;
            voice.restarting = false;
            continue;
        }

        if(voice.playing && !voice.restarting &&
           voice.position >= static_cast<float>(end - 1))
        {
            if(voice.loop)
            {
                voice.position = static_cast<float>(start);
            }
            else
            {
                voice.position = static_cast<float>(end - 1);
                voice.playing = false;
            }
        }

        const uint32_t index = static_cast<uint32_t>(voice.position);
        const float fraction = voice.position - static_cast<float>(index);
        const float sample_a = pcm_[index];
        const float sample_b = pcm_[index + 1];
        const float sample = (sample_a + (sample_b - sample_a) * fraction) / 32768.0f;

        const bool silent = voice.restarting || !voice.playing;
        fonepole(voice.gain, silent ? 0.0f : 1.0f, 0.01f);

        output += sample * voice.level * voice.gain;

        if(voice.restarting)
        {
            if(voice.gain < GAIN_FLOOR)
            {
                voice.restarting = false;
                if(voice.playing)
                {
                    voice.position = static_cast<float>(start);
                }
            }
        }
        else if(voice.playing)
        {
            voice.position += GetPlaybackIncrement(clip);
        }
    }

    return output;
}

bool SamplePlayer::IsValidClip(uint32_t clip) const
{
    if(!header_)
        return false;

    if(clip >= header_->numClips)
        return false;

    if(clip >= MAX_CLIPS)
        return false;

    return true;
}

uint32_t SamplePlayer::FindClip(const char* name) const
{
    if(!loaded_ || !clips_ || !name)
        return UINT32_MAX;

    const uint32_t numClips = std::min(header_->numClips, MAX_CLIPS);

    for(uint32_t i = 0; i < numClips; i++)
    {
        if(strncmp(clips_[i].name, name, NAME_FIELD_LEN) == 0)
            return i;
    }

    return UINT32_MAX;
}
