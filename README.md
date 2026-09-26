# Play WAV files on the Daisy Seed without an SD Card



This is an example project showing how to utilize the QSPI memory region on the Daisy Seed to store sounds that can be loaded and played in realtime, no SD Card required.



It does so with two binary files, one for the audio data flashed onto the Seed's QSPI memory, and one for the main program stored in the Seed's default flash region.

<br><br>

## PREREQUISITES



- [Daisy Toolchain](https://docs.daisy.audio/tutorials/cpp-dev-env/) (ARM GCC + make)
- [Python](https://www.python.org/downloads/) 
- **Windows:** use [Git Bash](https://git-scm.com/downloads) to run the commands below

<br><br>

## CONTENT



```
.
├── samples/            - folder containing wav files
├── wav2bin.py          - python script that converts all the samples to one binary file
├── Example.cpp         - main program that loads sound data and plays each clip at randomized speeds and output levels
└── src/
    ├── SamplePlayer.h  - Sampler class used by Example.cpp that allows the play/pause/restarting of multiple samples by name
    └── SamplePlayer.cpp
```

<br><br>

## INSTRUCTIONS FOR RUNNING PROJECT



This repo is meant to live inside the `seed` folder of the [DaisyExamples](https://github.com/daisyaudio/DaisyExamples) repo.

<br>

**Step 0:** Clone Daisy Examples if you haven't already

```shell
$ git clone --recurse-submodules https://github.com/electro-smith/DaisyExamples ~/Desktop/DaisyExamples
```

<br>

**Step 1:** Clone this repository in the proper folder and enter it

```shell
$ cd ~/Desktop/DaisyExamples/seed
$ git clone https://github.com/chrisades/Daisy-QSPI-Sampler.git
$ cd Daisy-QSPI-Sampler
```

<br>

**Step 2:** Load custom samples (or keep the default ones) in the `samples` folder and run the python script

```shell
$ python wav2bin.py samples/
```

<br>

**Step 3:** Flash the Daisy bootloader

```shell
$ make program-boot
```

after success, hit the BOOT button on the seed to maintain boot flash mode so you can drop in the storage binary. You should see an endlessly 'breathing' LED.

<br>

**Step 4:** Load sample bank binary into the QSPI memory region by running

```shell
$ dfu-util -a 0 -s 0x90040000 -D samples.bin -d 0483:df11
```

<br>

**Step 5:** Enter the regular boot mode on the seed by holding BOOT then RESET, and releasing RESET then BOOT, then run

```shell
$ make clean && make
$ make program-dfu
```

You should now hear the uploaded samples being played back at varying levels and speeds. 

<br><br>

## TROUBLESHOOTING



If the sample bank failed validation and didn't load,`Example.cpp` catches this and drops into a loop that blinks the LED and prints diagnostics. You can connect to the Daisy's serial port to read them. Likely causes:

- `samples.bin` was flashed to a different QSPI address than `QSPI_START` in `Example.cpp` 
- The bank has more than 128 clips, or the flash write was interrupted or truncated

<br><br>

## INSTALLING WITH DAISY WEB PROGRAMMER

1. Download the [storage binary file]() and the [program binary file]().

2. Visit [Daisy Seed web programmer](https://flash.daisy.audio/).

3. Go to Bootloader, flash the Daisy bootloader (v5.4 default works fine) and press the BOOT button on the Daisy right after download (you should see a 'breathing' LED)

4. Go to File Upload and upload `samples.bin` (you can ignore the invalid Daisy binary error)

5. Enter the regular boot mode by holding BOOT then RESET, and releasing RESET then BOOT

6. Go to File Upload and upload `Example.bin`

<br><br>

## NOTES FOR MODIFYING

<br>

#### Sample Rate

`wav2bin.py` accepts different wav files of varying sample rates and converts them all to specified rate. So, make sure the rate you enter in **Step 2** is identical to the sample rate you set for your Daisy hardware object. If the rates don't match, clips will still load and play, just at the wrong pitch/speed. The sample rate stored in the bank isn't automatically resampled to the hardware's rate at runtime.

For example, if you ran

```shell
$ python wav2bin.py samples/ --rate 96000
```

then in your initialization be sure to set

```c++
int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(Daisy::SaiHandle::Config::SampleRate::SAI_96KHZ);

    // ...
}
```

`wav2bin.py` has a few other custom options like

- `--normalize {clip,bank,none}` - normalize each clip individually (default), normalize the whole bank by one shared gain, or leave levels untouched
- `--fade-in` / `--fade-out` - fade length in ms applied to each clip (default 1.0 ms each, 0 to disable)
- `--recursive` - also search subfolders of the input folder for `.wav` files
- an output path can be given as a second argument



for the full list run

```shell
$ python wav2bin.py --help
```

<br>

#### Clip Limits

In this example project, each sample bank can have a max of 128 clips, and each clip's name can be a max of 23 characters long. These should be more than enough for typical use with 8MB max storage. 

Names longer than that are truncated, and if a duplicate somehow occurs, the SamplePlayer class will only ever find the first one it sees when calling its member functions.

<br>

#### QSPI

On the Daisy, QSPI memory begins at address `0x90000000` and is 8MB large, ending at `0x90800000`. Despite this, in **Step 4** we flashed the samples.bin onto memory address `0x90040000.` This follows electrosmith's convention of leaving the first 256kB untouched, as explained in the [bootloader details](https://github.com/electro-smith/libDaisy/blob/master/doc/md/_a7_Getting-Started-Daisy-Bootloader.md).

I followed this convention so that the `samples.bin` file could be flashed using the [Daisy web programmer](https://flash.daisy.audio/), if needed. The total ~7.75 MB of storage can hold roughly 80 seconds worth of audio at 48kHz, since `wav2bin.py` encodes the sound bank as 16-bit integers. 

Though if you don't care about being able to have your bin loaded using the webprogrammer, or you really need to use that extra 256kB, then you can choose the storage address in **Step 4** to be `0x90000000`. 

```shell
$ dfu-util -a 0 -s 0x90000000 -D samples.bin -d 0483:df11
```

Just be sure to also alter the memory address that the SamplePlayer reads in `Example.cpp`.

```c++
constexpr uint32_t QSPI_START = 0x90000000; // QSPI binary storage start address
```

For reference, the various memory address locations for the Daisy can be found [here](https://github.com/electro-smith/libDaisy/blob/master/core/STM32H750IB_flash.lds).

<br>

#### SDRAM

In `Example.cpp` the sound bank binary stored in QSPI is copied over onto SDRAM. The purpose for this is that SDRAM has higher performance when it comes to realtime operations. For ease of use, `Example.cpp` allocates the full `0x90040000` to `0x90800000`(~7.75MB) size even though the size of `samples.bin` will probably be smaller than that. If you'd like to not waste the uneeded space you can use the start and end addresses printed at the bottom after running `wav2bin.py`.

For example let's say I run

```shell
$ python wav2bin.py samples/ --address 0x90040000
```

and the bottom output says

```shell
   bank will occupy 0x90040000 - 0x901d0ad2
```

then, in `Example.cpp`, I'll set

```c++
constexpr uint32_t QSPI_START = 0x90040000; // QSPI binary storage start address
constexpr uint32_t QSPI_END =   0x901d0ad2; // QSPI binary storage end address
```

If for whatever reason you want to skip this allocation you should comment it out along with memcpy, and initialize the SamplePlayer object with your QSPI start address

```c++
constexpr uint32_t QSPI_START = 0x90040000;
// constexpr uint32_t QSPI_END =   0x90800000; 
// constexpr size_t   SAMPLE_BANK_SIZE = QSPI_END - QSPI_START;
// uint8_t DSY_SDRAM_BSS SDRAM_BANK[SAMPLE_BANK_SIZE];

// ...

// memcpy(SDRAM_BANK, reinterpret_cast<const void*>(QSPI_START), SAMPLE_BANK_SIZE);
sampler.Init(QSPI_START, sampleRate);
```
<br>

#### Stereo Audio

Due to the small memory size we are working with, the `wav2bin.py` script automatically mixes all stereo files into mono. If you want to replicate a stereo audio image, you'd have to split it up in two seperate mono wav files and place them in your `samples` folder, before running `wav2bin.py`. This split can be done in most DAWs or using [ffmpeg](https://ffmpeg.org/download.html) in the terminal:

```shell
ffmpeg -i clip.wav -map_channel 0.0.0 clip_L.wav -map_channel 0.0.1 clip_R.wav
```

Then you could then initialize two SamplePlayer objects and process each one as it's own channel:

```c++
void AudioCallback(AudioHandle::InputBuffer  in, AudioHandle::OutputBuffer out, size_t size)
{
    for(size_t i = 0; i < size; i++) {

        //trigger a clip replay
        if(trigger) {
            samplerL.Replay("clip_L.wav");
            samplerR.Replay("clip_R.wav");
        }

        float outL = samplerL.Process();
        float outR = samplerR.Process();

        out[0][i] = outL;
        out[1][i] = outR;
    }
}


int main(void)
{
    // ...

    //Initialize left and right samplers
    memcpy(SDRAM_BANK, reinterpret_cast<const void*>(QSPI_START), SAMPLE_BANK_SIZE);
    const uint32_t sdramBankAddr = reinterpret_cast<uint32_t>(SDRAM_BANK);
    samplerL.Init(sdramBankAddr, sampleRate);
    samplerR.Init(sdramBankAddr, sampleRate);

    // ...
}
```

<br><br>

## CREDITS


