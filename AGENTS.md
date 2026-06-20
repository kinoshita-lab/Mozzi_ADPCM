# OpenCode Agents Instructions

This repository provides an IMA ADPCM decoder (`ADPCMSample.h`) and encoder (`wav2adpcm.py`) for the Mozzi audio synthesis library on Arduino.

## Core Workflows
- **Generate ADPCM Headers:** 
  Use the Python script to convert 16-bit mono WAV files into C headers suitable for Mozzi. Redirect stdout to save the file:
  `python wav2adpcm.py input.wav [table_name] [--initial-step N] > Output.h`
  (`--initial-step` sets the IMA ADPCM step index 0-88, default 0)
- **Audio Constraints:** The input to `wav2adpcm.py` must strictly be **16-bit mono WAV**.

## Architecture & Codebase
- `ADPCMSample.h`: A C++ template class extending Mozzi capabilities to play ADPCM compressed audio. It uses `PROGMEM` to efficiently store data in Flash memory.
- `wav2adpcm.py`: Python encoder that reads a WAV file, compresses it at a 4:1 ratio, and outputs a C header with the packed data array. No external dependencies are required (uses standard library).

## Workflow Conventions
- **No Build System:** There is currently no automated build system (e.g., Make, CMake, PlatformIO) or test suite configured in this repository.
- **Arduino Constraints:** C++ code must remain compatible with standard Arduino IDE compilation and the Mozzi library (Arduino >= 100).
