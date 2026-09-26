#include "daisy_seed.h"
#include "daisysp.h"
#include "src/SamplePlayer.h"

using namespace daisy;
using namespace daisysp;

// Make space in SDRAM to copy sample bank from QSPI
// since SDRAM has higher performance
constexpr uint32_t QSPI_START = 0x90040000; // QSPI binary storage start address
constexpr uint32_t QSPI_END =   0x90800000; // QSPI binary storage end address
constexpr size_t   SAMPLE_BANK_SIZE = QSPI_END - QSPI_START;
uint8_t DSY_SDRAM_BSS SDRAM_BANK[SAMPLE_BANK_SIZE];

DaisySeed hw;
SamplePlayer sampler;

Metro metronome;
size_t clip = 0;

void AudioCallback(AudioHandle::InputBuffer  in, AudioHandle::OutputBuffer out, size_t size)
{
    const size_t numClips = sampler.GetNumClips();

    for(size_t i = 0; i < size; i++) {

        //Once per metronome tick, play the next clip
        if(metronome.Process() && numClips > 0) {

            const char *clipName = sampler.GetClipName(clip);

            float randSpeed = daisy::Random::GetFloat(0.3f, 2.0f);

            sampler.SetLevel(clipName, 0.35f); // set the level of the clip
            sampler.SetSpeed(clipName, randSpeed); // set the playback speed of the clip
            sampler.Replay(clipName); // trigger clip replay
            clip = (clip + 1) % numClips; // choose next clip
        }

        float s = sampler.Process();

        out[0][i] = s;
        out[1][i] = s;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_96KHZ);
    hw.SetAudioBlockSize(4);
    float sampleRate = hw.AudioSampleRate();

    memcpy(SDRAM_BANK, reinterpret_cast<const void*>(QSPI_START), SAMPLE_BANK_SIZE); // Copy over sample bank from QSPI to SDRAM
    sampler.Init(reinterpret_cast<uint32_t>(SDRAM_BANK), sampleRate); // Initialize sampler with SDRAM bank address and sampleRate

    if(!sampler.IsLoaded()) { //Blink LED if samples did not load properly or are corrupted
        hw.StartLog();
        while(1) {
            hw.SetLed(true);
            System::Delay(500);
            hw.SetLed(false);
            System::Delay(500);
            hw.PrintLine("numClips: %lu", sampler.GetNumClips());
            hw.PrintLine("samplRate: %lu", sampler.GetSampleRate());
            for(uint32_t i = 0; i < 7; i++) {
                hw.PrintLine("clip %lu: start=%lu end=%lu", i,
                 sampler.GetClipStart(sampler.GetClipName(i)),
                 sampler.GetClipEnd(sampler.GetClipName(i)));
                 hw.PrintLine("%s", sampler.GetClipName(i));
            }
        }
    }

    metronome.Init(2.0f, sampleRate); // 2.0hz metronome to trigger samples

    hw.StartAudio(AudioCallback);

    for(;;){}
}