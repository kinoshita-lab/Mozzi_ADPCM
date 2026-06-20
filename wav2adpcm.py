#!/usr/bin/env python3
"""
Convert a 16-bit mono WAV file to IMA ADPCM format
for use with the ADPCMSample class on Arduino/Mozzi.

IMA ADPCM encodes each 16-bit sample as a 4-bit nibble,
achieving 4:1 compression ratio (16bit -> 4bit per sample).

Usage:
    python wav2adpcm.py <input.wav> [table_name] [--initial-step 0]

Output is written to stdout. Redirect to a file to save:
    python wav2adpcm.py input.wav MY_SOUND > MySound.h
"""

import wave
import struct
import sys
import os
import argparse


# IMA ADPCM step size table (89 entries)
IMA_STEP_TABLE = [
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
]

# IMA ADPCM index adjustment table (16 entries, indexed by nibble value)
IMA_INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def clamp(value, min_val, max_val):
    """Clamp value to [min_val, max_val] range."""
    return max(min(value, max_val), min_val)


def adpcm_encode_sample(diff, step):
    """
    Encode a single sample difference as a 4-bit IMA ADPCM nibble.

    Args:
        diff: The difference between actual sample and predictor
        step: Current step size from the step table

    Returns:
        4-bit nibble value (0-15)
    """
    sign_bit = 1 if diff < 0 else 0
    diff_abs = abs(diff)

    code = 0
    delta = step

    if diff_abs >= delta:
        code |= 4
        diff_abs -= delta
    delta = step >> 1
    if diff_abs >= delta:
        code |= 2
        diff_abs -= delta
    delta = step >> 2
    if diff_abs >= delta:
        code |= 1

    return (sign_bit << 3) | code


def adpcm_decode_nibble(nibble, predictor, step_index):
    """
    Decode a single 4-bit nibble and update predictor/step_index.

    Args:
        nibble: 4-bit IMA ADPCM value (0-15)
        predictor: Current predictor value (int16)
        step_index: Current step index (0-88)

    Returns:
        (new_predictor, new_step_index)
    """
    step = IMA_STEP_TABLE[step_index]

    code = nibble & 0x07
    sign = nibble & 0x08

    diff = 0
    if code & 4:
        diff += step
    if code & 2:
        diff += step >> 1
    if code & 1:
        diff += step >> 2
    diff += step >> 3  # 1/8 step correction

    if sign:
        diff = -diff

    predictor = clamp(predictor + diff, -32768, 32767)

    # Update step index
    step_index += IMA_INDEX_TABLE[nibble]
    step_index = clamp(step_index, 0, 88)

    return predictor, step_index


def adpcm_encode(samples, initial_step_index=0):
    """
    Encode 16-bit PCM samples as IMA ADPCM.

    Args:
        samples: List of 16-bit signed integer samples
        initial_step_index: Initial step table index (default: 0)

    Returns:
        initial_value: First sample value (int16)
        initial_step_index: Initial step table index
        nibbles: List of 4-bit nibble values (0-15)
    """
    initial_value = samples[0]
    predictor = initial_value
    step_index = initial_step_index

    nibbles = []
    for i in range(1, len(samples)):
        diff = samples[i] - predictor
        step = IMA_STEP_TABLE[step_index]
        nibble = adpcm_encode_sample(diff, step)
        nibbles.append(nibble)
        predictor, step_index = adpcm_decode_nibble(nibble, predictor, step_index)

    return initial_value, initial_step_index, nibbles


def adpcm_decode(initial_value, initial_step_index, nibbles, num_samples):
    """
    Decode IMA ADPCM nibbles back to 16-bit PCM samples.

    Args:
        initial_value: First sample value
        initial_step_index: Initial step table index
        nibbles: List of 4-bit nibble values
        num_samples: Total number of samples (including initial)

    Returns:
        List of reconstructed 16-bit sample values
    """
    reconstructed = [initial_value]
    predictor = initial_value
    step_index = initial_step_index

    for nibble in nibbles:
        predictor, step_index = adpcm_decode_nibble(nibble, predictor, step_index)
        reconstructed.append(predictor)

    return reconstructed[:num_samples]


def pack_nibbles(nibbles):
    """
    Pack a list of 4-bit nibbles into bytes.

    Packing order: byte = (nibble[i] << 4) | nibble[i+1]
    - Even index nibble goes to upper 4 bits
    - Odd index nibble goes to lower 4 bits

    If the number of nibbles is odd, the last nibble is packed
    into the upper 4 bits of the last byte with 0 in the lower 4 bits.

    Args:
        nibbles: List of 4-bit nibble values (0-15)

    Returns:
        List of byte values (0-255)
    """
    packed = []
    i = 0
    while i < len(nibbles):
        if i + 1 < len(nibbles):
            byte_val = (nibbles[i] << 4) | nibbles[i + 1]
        else:
            # Odd number of nibbles: last nibble in upper 4 bits
            byte_val = nibbles[i] << 4
        packed.append(byte_val)
        i += 2
    return packed


def compute_stats(samples, initial_value, initial_step_index, nibbles):
    """
    Compute reconstruction error statistics.

    Returns:
        dict with max_error, rms_error, original_size, adpcm_size, compression_ratio
    """
    reconstructed = adpcm_decode(
        initial_value, initial_step_index, nibbles, len(samples)
    )

    errors = [abs(samples[i] - reconstructed[i]) for i in range(len(samples))]
    max_error = max(errors)
    rms_error = (
        (sum(e**2 for e in errors) / len(samples)) ** 0.5 if len(samples) > 0 else 0
    )

    nframes = len(samples)
    original_size = nframes * 2  # 16-bit per sample

    # ADPCM size: initial value (2 bytes) + initial step (1 byte) + packed nibbles
    num_packed_bytes = (len(nibbles) + 1) // 2
    adpcm_size = 2 + 1 + num_packed_bytes

    compression_ratio = (
        (1 - adpcm_size / original_size) * 100 if original_size > 0 else 0
    )

    return {
        "max_error": max_error,
        "rms_error": rms_error,
        "original_size": original_size,
        "adpcm_size": adpcm_size,
        "compression_ratio": compression_ratio,
    }


def write_reconstructed_wav(recon_path, framerate, reconstructed):
    """Write reconstructed samples as a 16-bit mono WAV file."""
    with wave.open(recon_path, "wb") as wo:
        wo.setnchannels(1)
        wo.setsampwidth(2)
        wo.setframerate(framerate)
        raw_data = struct.pack(f"<{len(reconstructed)}h", *reconstructed)
        wo.writeframes(raw_data)


def convert_wav_to_adpcm(wav_path, table_name, initial_step_index=0):
    """Convert a WAV file to IMA ADPCM format C header."""
    try:
        with wave.open(wav_path, "rb") as w:
            # Format check
            num_channels = w.getnchannels()
            sampwidth = w.getsampwidth()
            framerate = w.getframerate()
            nframes = w.getnframes()

            if num_channels != 1:
                print(
                    f"Error: Mono wav required. Input has {num_channels} channels.",
                    file=sys.stderr,
                )
                sys.exit(1)

            if sampwidth != 2:
                print(
                    f"Error: 16-bit wav required. Input is {sampwidth * 8}-bit.",
                    file=sys.stderr,
                )
                sys.exit(1)

            print(f"Processing: {wav_path}", file=sys.stderr)
            print(f"Rate: {framerate}Hz, Length: {nframes} samples", file=sys.stderr)
            print(
                f"Initial step index: {initial_step_index}",
                file=sys.stderr,
            )

            # Read all samples
            raw_data = w.readframes(nframes)
            samples = struct.unpack(f"<{nframes}h", raw_data)

        # ADPCM encode
        initial_value, step_index_out, nibbles = adpcm_encode(
            samples, initial_step_index
        )

        # Pack nibbles into bytes
        packed_bytes = pack_nibbles(nibbles)

        # Reconstruct signal for verification
        reconstructed = adpcm_decode(
            initial_value, step_index_out, nibbles, len(samples)
        )

        # Write reconstructed WAV
        base, ext = os.path.splitext(wav_path)
        recon_path = base + "_convert_result" + ext
        write_reconstructed_wav(recon_path, framerate, reconstructed)
        print(f"Reconstructed WAV: {recon_path}", file=sys.stderr)

        # Compute statistics
        stats = compute_stats(samples, initial_value, step_index_out, nibbles)

        # --- Output C++ header ---
        header_str = f"""\
#ifndef {table_name}_H_
#define {table_name}_H_

#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif
#include "MozziGuts.h"

#define {table_name}_NUM_CELLS {nframes}
#define {table_name}_SAMPLERATE {framerate}

// IMA ADPCM encoded: initial value + initial step index + packed nibbles
const int16_t {table_name}_INITIAL PROGMEM = {initial_value};
const uint8_t {table_name}_INITIAL_STEP PROGMEM = {step_index_out};
const uint8_t {table_name}_DATA[] PROGMEM = {{
"""
        print(header_str, end="")

        # Output packed bytes
        for i, byte_val in enumerate(packed_bytes):
            end_char = ","
            if i == len(packed_bytes) - 1:
                end_char = ""  # 最後はカンマなし

            print(f"0x{byte_val:02X}{end_char}", end="")

            # 20個ごとに改行
            if (i + 1) % 20 == 0:
                print()
            else:
                print(" ", end="")

        footer_str = f"""
}};

#endif /* {table_name}_H_ */
"""
        print(footer_str)

        # Print statistics to stderr
        num_nibbles = len(nibbles)
        num_packed_bytes = len(packed_bytes)
        print(f"\n--- IMA ADPCM Encoding Statistics ---", file=sys.stderr)
        print(
            f"Original size:  {stats['original_size']} bytes ({nframes} samples x 2 bytes)",
            file=sys.stderr,
        )
        print(
            f"ADPCM size:     {stats['adpcm_size']} bytes (2 initial + 1 step + {num_packed_bytes} packed bytes, {num_nibbles} nibbles)",
            file=sys.stderr,
        )
        print(
            f"Compression:    {stats['compression_ratio']:.1f}% reduction",
            file=sys.stderr,
        )
        print(f"Max recon error: {stats['max_error']}", file=sys.stderr)
        print(f"RMS recon error: {stats['rms_error']:.2f}", file=sys.stderr)

    except FileNotFoundError:
        print(f"Error: File not found: {wav_path}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert a 16-bit mono WAV file to IMA ADPCM format for Arduino/Mozzi ADPCMSample class.",
        epilog="Output is written to stdout. Redirect to a file to save: python wav2adpcm.py input.wav > output.h",
    )
    parser.add_argument("input_wav", help="Input WAV file (16-bit mono)")
    parser.add_argument(
        "table_name",
        nargs="?",
        help="Table name for C identifiers (default: derived from filename)",
    )
    parser.add_argument(
        "--initial-step",
        type=int,
        default=0,
        help="Initial step table index for IMA ADPCM (default: 0, range: 0-88)",
    )

    args = parser.parse_args()

    input_wav = args.input_wav

    if args.table_name:
        t_name = args.table_name
    else:
        base = os.path.basename(input_wav)
        t_name = os.path.splitext(base)[0].upper().replace(" ", "_")

    # Validate initial_step
    if args.initial_step < 0 or args.initial_step > 88:
        print("Error: --initial-step must be between 0 and 88", file=sys.stderr)
        sys.exit(1)

    convert_wav_to_adpcm(input_wav, t_name, args.initial_step)
