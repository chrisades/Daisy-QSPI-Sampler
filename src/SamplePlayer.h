#pragma once
#include <cstdint>
#include <cstddef>

class SamplePlayer
{
  public:
    SamplePlayer();

    // Initialize the sample bank
    void Init(uint32_t sample_base, float hardwareSampleRate);

    bool IsLoaded() const;

    float Process();

    uint32_t GetNumClips() const;
    uint32_t GetSampleRate() const;

    // Name a clip was packed with (from wav2bin.py), by index.
    // Use this to enumerate all loaded clips: 
    // for(i = 0; i < GetNumClips(); i++) GetClipName(i).
    const char* GetClipName(uint32_t clip) const;

    // Get the length of a clip in samples.
    uint32_t GetClipLength(const char* name) const;

    // Get the start/end sample indices of a clip.
    uint32_t GetClipStart(const char* name) const;
    uint32_t GetClipEnd(const char* name) const;

    // Start a clip from its current position.
    void Resume(const char* name);

    // Play a clip from the beginning.
    void Replay(const char* name);

    // Stop a clip.
    void Pause(const char* name);

    // Stop all clips.
    void PauseAll();

    // Is a clip currently playing?
    bool IsPlaying(const char* name) const;

    void SetLoop(const char* name, bool loop);
    bool IsLooping(const char* name) const;

    // 1.0 = normal speed
    // 0.5 = half speed
    // 2.0 = double speed
    void SetSpeed(const char* name, float speed);
    float GetSpeed(const char* name) const;

    // Sets clip level from 0.0 to 1.0
    void SetLevel(const char* name, float level);
    float GetLevel(const char* name) const;

    // Position from 0.0 to 1.0.
    float GetPosition(const char* name) const;

  private:

    struct BankHeader
    {
        uint32_t numClips;
        uint32_t sampleRate;
    };

    static constexpr uint32_t NAME_FIELD_LEN = 24;

    struct ClipEntry
    {
        uint32_t start;
        uint32_t end;
        char     name[NAME_FIELD_LEN];
    };


    static constexpr uint32_t MAX_CLIPS = 128;

    struct Voice
    {
        float position;
        float speed;
        float gain;
        float level;
        bool playing;
        bool loop;
        bool restarting; 
    };

    const BankHeader* header_;
    const ClipEntry*  clips_;
    const int16_t*    pcm_;

    uint32_t sampleBase_;

    float rateRatio_;

    Voice voices_[MAX_CLIPS];
 
    bool loaded_;
 
    bool IsValidClip(uint32_t clip) const;

    // Linear search over clips_[].name. Returns UINT32_MAX if not found,
    // not loaded, or name is nullptr
    uint32_t FindClip(const char* name) const;

    bool ValidateBank() const;
 
    float GetPlaybackIncrement(uint32_t clip) const;
};
