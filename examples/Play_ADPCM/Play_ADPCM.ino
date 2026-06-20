/*
  Play_ADPCM
  Demonstrates playing an IMA ADPCM compressed audio sample with Mozzi.

  This example uses the ADPCMSample class to decode and play audio stored
  in IMA ADPCM format (4:1 compression). The sample data is contained in
  the accompanying Zunda_Sample.h header file.

  To use your own audio:
    1. Prepare a 16-bit mono WAV file.
    2. Run: python wav2adpcm.py input.wav MY_AUDIO > my_audio.h
    3. Include the generated file and update the macro prefixes.

  Circuit:
    Audio output on digital audio pin (see Mozzi documentation for your board).
    For standard Arduino, audio output is typically on pin 9 (via PWM).

  This example code is in the public domain.
*/

#include <MozziGuts.h>
#include "ADPCMSample.h"
#include "Zunda_Sample.h"

// Create the ADPCMSample instance.
// Template parameters:
//   NUM_TABLE_CELLS = ZUNDA_32768HZ_NUM_CELLS (number of samples in the table)
//   UPDATE_RATE     = AUDIO_RATE (Mozzi's audio update rate, defined in Mozzi)
ADPCMSample<ZUNDA_32768HZ_NUM_CELLS, AUDIO_RATE> sample;

void setup() {
  startMozzi();

  // Point the sample to the encoded ADPCM data
  sample.setTable(&ZUNDA_32768HZ_INITIAL, &ZUNDA_32768HZ_INITIAL_STEP, ZUNDA_32768HZ_DATA);

  // Set playback frequency to the original sample rate for natural speed
  sample.setFreq((int)ZUNDA_32768HZ_SAMPLERATE);

  // Loop the sample continuously
  sample.setLoopingOn();

  // Start playback from the beginning
  sample.start();
}

void updateControl() {
  // Nothing to control in this simple example.
}

AudioOutput_t updateAudio() {
  // ADPCMSample returns a 16-bit value; convert to Mozzi output format
  return MonoOutput::fromNBit(16, sample.next());
}
