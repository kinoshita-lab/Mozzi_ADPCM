# Mozzi ADPCM

This repository provides an IMA ADPCM decoder and encoder for the [Mozzi](https://sensorium.github.io/Mozzi/) audio synthesis library on Arduino.
It achieves a 4:1 compression ratio by encoding 16-bit audio samples into 4-bit nibbles, saving precious flash memory on microcontrollers while maintaining reasonable audio quality.

## Components

*   `wav2adpcm.py`: A Python script that encodes a 16-bit mono WAV file into an IMA ADPCM C++ header file.
*   `ADPCMSample.h`: A C++ template class for Mozzi that decodes and plays the generated ADPCM data.

## Requirements

*   **Encoder:** Python 3. (No external dependencies required). The input WAV file must strictly be **16-bit mono**.
*   **Decoder:** Arduino IDE (>= 1.0) and the Mozzi library.

## Usage

### 1. Encoding Audio

Convert your 16-bit mono WAV file into a C++ header file using the `wav2adpcm.py` script.

```bash
# Basic usage (table name is derived from the filename)
python wav2adpcm.py input.wav > Output.h

# Specify a custom table name for the C++ identifiers
python wav2adpcm.py input.wav MY_SOUND_TABLE > Output.h

# Set an initial step index (IMA ADPCM step index 0-88, default: 0).
# A non-zero value can improve tracking for signals with large initial amplitude.
python wav2adpcm.py input.wav MY_SOUND_TABLE --initial-step 16 > Output.h
```

The output is written to `stdout`, so redirect it to a file (e.g., `> Output.h`). Encoding statistics (compression ratio, max/RMS reconstruction error, etc.) are printed to `stderr`. A reconstructed WAV file (`<input_basename>_convert_result.wav`) is also generated next to the input file for quality verification.

### 2. Playing Audio in Mozzi

Include the generated header and `ADPCMSample.h` in your Arduino sketch. Use it similarly to the standard Mozzi `Sample` class.

```cpp
#include <MozziGuts.h>
#include "ADPCMSample.h"
#include "Output.h" // Your generated header

// Instantiate the ADPCM sample player.
// MY_SOUND_TABLE_NUM_CELLS and AUDIO_RATE are required template parameters.
ADPCMSample<MY_SOUND_TABLE_NUM_CELLS, AUDIO_RATE> sample(
    &MY_SOUND_TABLE_INITIAL,
    &MY_SOUND_TABLE_INITIAL_STEP,
    MY_SOUND_TABLE_DATA
);

void setup() {
    startMozzi();
    sample.setFreq((float) MY_SOUND_TABLE_SAMPLERATE / MY_SOUND_TABLE_NUM_CELLS);
    sample.start();
}

void updateControl() {
    // You can re-trigger or change frequency here
}

int updateAudio() {
    return (int) sample.next();
}

void loop() {
    audioHook();
}
```

### API Reference

The `ADPCMSample` class provides the following key methods:

| Method | Description |
|--------|-------------|
| `start()` | Start playback from the current start position. |
| `next()` | Advance by one step and return the decoded sample. Call this in `updateAudio()`. |
| `setFreq(float)` | Set the playback frequency (pitch/speed). |
| `setLoopingOn()` / `setLoopingOff()` | Enable or disable looping playback. |
| `isPlaying()` | Returns `true` while playback is in progress. |
| `setTable(...)` | Switch the data table pointers at runtime. |
| `setStart()` / `setEnd()` | Set the playback range. |
| `rangeWholeSample()` | Reset the playback range to the whole sample. |
| `atIndex(index)` | Get the sample at a specific index. **Warning:** O(n) — avoid calling in audio callbacks. |

