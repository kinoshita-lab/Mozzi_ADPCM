#ifndef ADPCMSAMPLE_H_
#define ADPCMSAMPLE_H_

#include "MozziHeadersOnly.h"
#include "mozzi_fixmath.h"
#include "mozzi_pgmspace.h"

#define ADPCM_F_BITS 16
#define ADPCM_F_BITS_AS_MULTIPLIER 65536

// IMA ADPCM step size table (89 entries)
// NOTE: uint16_t required because max value is 32767 (> 255)
const uint16_t ima_step_table[] PROGMEM = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};

// IMA ADPCM index adjustment table (16 entries, indexed by nibble value)
const int8_t ima_index_table[] PROGMEM = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8};

template <unsigned int NUM_TABLE_CELLS, unsigned int UPDATE_RATE>
class ADPCMSample
{
public:
    /**
     * Construct an ADPCMSample with initial value, initial step index,
     * and packed nibble data pointers.
     *
     * @param INITIAL_VALUE   Pointer to the initial sample value (int16_t)
     * @param INITIAL_STEP    Pointer to the initial step table index (uint8_t)
     * @param DATA            Pointer to the packed nibble data array (uint8_t)
     */
    ADPCMSample(const int16_t* INITIAL_VALUE, const uint8_t* INITIAL_STEP, const uint8_t* DATA)
        : initial(INITIAL_VALUE), initial_step(INITIAL_STEP), data(DATA),
          endpos_fractional((uint64_t)NUM_TABLE_CELLS << ADPCM_F_BITS)
    {
        setLoopingOff();
    }

    ADPCMSample()
        : initial(nullptr), initial_step(nullptr), data(nullptr),
          endpos_fractional((uint64_t)NUM_TABLE_CELLS << ADPCM_F_BITS)
    {
        setLoopingOff();
    }

    /**
     * Set the data table pointers.
     *
     * @param INITIAL_VALUE   Pointer to the initial sample value (int16_t)
     * @param INITIAL_STEP    Pointer to the initial step table index (uint8_t)
     * @param DATA            Pointer to the packed nibble data array (uint8_t)
     */
    inline void setTable(const int16_t* INITIAL_VALUE, const uint8_t* INITIAL_STEP, const uint8_t* DATA)
    {
        initial      = INITIAL_VALUE;
        initial_step = INITIAL_STEP;
        data         = DATA;
    }

    inline void setStart(unsigned int startpos)
    {
        startpos_fractional = (uint64_t)startpos << ADPCM_F_BITS;
    }

    /**
     * Start playback from the current start position.
     *
     * Resets the ADPCM decoder and pre-decodes up to the start position
     * so that the first next() call in the audio callback is fast (O(1)).
     */
    inline void start()
    {
        phase_fractional = startpos_fractional;
        resetDecoder();
        // Pre-decode to start position so first next() call is O(1).
        unsigned int start_idx = startpos_fractional >> ADPCM_F_BITS;
        decodeForwardTo(start_idx);
    }

    inline void start(unsigned int startpos)
    {
        setStart(startpos);
        start();
    }

    inline void setEnd(unsigned int end)
    {
        endpos_fractional = (uint64_t)end << ADPCM_F_BITS;
    }

    inline void rangeWholeSample()
    {
        startpos_fractional = 0;
        endpos_fractional   = (uint64_t)NUM_TABLE_CELLS << ADPCM_F_BITS;
    }

    inline void setLoopingOn()
    {
        looping = true;
    }

    inline void setLoopingOff()
    {
        looping = false;
    }

    /**
     * Get the next sample value from the ADPCM-encoded waveform.
     *
     * Under normal forward playback, this is O(1) since the decoder
     * only needs to advance by 0 or 1 steps per call. However, when
     * looping occurs or the phase moves backward, the decoder must
     * reset and re-decode from the beginning, which is O(n).
     *
     * @return The decoded 16-bit sample value, or 0 if playback has ended
     */
    inline int16_t next()
    {
        if (phase_fractional > endpos_fractional) {
            if (looping) {
                phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
                // Reset decoder and re-decode to new position after loop
                resetDecoder();
                unsigned int new_target = phase_fractional >> ADPCM_F_BITS;
                decodeForwardTo(new_target);
            } else {
                return 0;
            }
        }

        unsigned int target_index = phase_fractional >> ADPCM_F_BITS;

        // If phase moved backward, reset and re-decode from beginning
        if (target_index < decoder_index) {
            resetDecoder();
        }

        // Decode forward to current position (usually 0 or 1 steps)
        decodeForwardTo(target_index);

        int16_t out = current_decoded;

        incrementPhase();
        return out;
    }

    inline boolean isPlaying()
    {
        return phase_fractional < endpos_fractional;
    }

    inline void setFreq(int frequency)
    {
        phase_increment_fractional = ((((uint64_t)NUM_TABLE_CELLS << ADJUST_FOR_NUM_TABLE_CELLS) * frequency) / UPDATE_RATE) << (ADPCM_F_BITS - ADJUST_FOR_NUM_TABLE_CELLS);
    }

    inline void setFreq(float frequency)
    {
        phase_increment_fractional = (uint64_t)((((float)NUM_TABLE_CELLS * frequency) / UPDATE_RATE) * ADPCM_F_BITS_AS_MULTIPLIER);
    }

    inline void setFreq_Q24n8(Q24n8 frequency)
    {
        phase_increment_fractional = (((((uint64_t)NUM_TABLE_CELLS << ADJUST_FOR_NUM_TABLE_CELLS) >> 3) * frequency) / (UPDATE_RATE >> 6)) << (ADPCM_F_BITS - ADJUST_FOR_NUM_TABLE_CELLS - (8 - 3 + 6));
    }

    /**
     * Get the sample value at a specific index.
     *
     * WARNING: This is O(n) because ADPCM requires sequential decoding
     * from the beginning. Avoid calling this in audio callbacks.
     *
     * @param index  Sample index (0-based)
     * @return The decoded 16-bit sample value at the given index
     */
    inline int16_t atIndex(unsigned int index)
    {
        int16_t val      = FLASH_OR_RAM_READ<const int16_t>(initial);
        uint8_t step_idx = FLASH_OR_RAM_READ<const uint8_t>(initial_step);
        for (unsigned int i = 0; i < index && i < NUM_TABLE_CELLS - 1; i++) {
            // Read nibble from packed data
            uint8_t byte_val = FLASH_OR_RAM_READ<const uint8_t>(data + (i / 2));
            uint8_t nibble;
            if (i & 1) {
                nibble = byte_val & 0x0F;
            } else {
                nibble = (byte_val >> 4) & 0x0F;
            }

            uint16_t step = pgm_read_word(&ima_step_table[step_idx]);
            uint8_t code  = nibble & 0x07;
            int32_t diff  = 0;
            if (code & 4)
                diff += step;
            if (code & 2)
                diff += step >> 1;
            if (code & 1)
                diff += step >> 2;
            diff += step >> 3;

            if (nibble & 0x08)
                diff = -diff;

            val      = clamp16(val + diff);
            step_idx = clamp_step_index(step_idx + pgm_read_byte(&ima_index_table[nibble]));
        }
        return val;
    }

    inline uint64_t phaseIncFromFreq(unsigned int frequency)
    {
        return (((uint64_t)frequency * NUM_TABLE_CELLS) / UPDATE_RATE) << ADPCM_F_BITS;
    }

    inline void setPhaseInc(uint64_t phaseinc_fractional)
    {
        phase_increment_fractional = phaseinc_fractional;
    }

private:
    static const uint8_t ADJUST_FOR_NUM_TABLE_CELLS = (NUM_TABLE_CELLS < 2048) ? 8 : 0;

    static inline int16_t clamp16(int32_t val)
    {
        if (val > 32767)
            return 32767;
        if (val < -32768)
            return -32768;
        return (int16_t)val;
    }

    static inline uint8_t clamp_step_index(int16_t idx)
    {
        if (idx < 0)
            return 0;
        if (idx > 88)
            return 88;
        return (uint8_t)idx;
    }

    inline void resetDecoder()
    {
        decoder_index      = 0;
        current_decoded    = FLASH_OR_RAM_READ<const int16_t>(initial);
        decoder_step_index = FLASH_OR_RAM_READ<const uint8_t>(initial_step);
    }

    inline void decodeOneSample()
    {
        // Read nibble from packed byte data
        // nibble index = decoder_index (0-based, counting from first delta)
        uint8_t byte_val = FLASH_OR_RAM_READ<const uint8_t>(data + (decoder_index / 2));
        uint8_t nibble;
        if (decoder_index & 1) {
            nibble = byte_val & 0x0F; // Odd index: lower 4 bits
        } else {
            nibble = (byte_val >> 4) & 0x0F; // Even index: upper 4 bits
        }

        // Get step size from table
        uint16_t step = pgm_read_word(&ima_step_table[decoder_step_index]);

        // Decode nibble to difference
        uint8_t code = nibble & 0x07;
        int32_t diff = 0;
        if (code & 4)
            diff += step;
        if (code & 2)
            diff += step >> 1;
        if (code & 1)
            diff += step >> 2;
        diff += step >> 3; // 1/8 step correction

        if (nibble & 0x08)
            diff = -diff;

        // Update predictor
        current_decoded = clamp16(current_decoded + diff);

        // Update step index
        int8_t index_adj   = pgm_read_byte(&ima_index_table[nibble]);
        decoder_step_index = clamp_step_index(decoder_step_index + index_adj);
    }

    inline void decodeForwardTo(unsigned int target_index)
    {
        while (decoder_index < target_index && decoder_index < NUM_TABLE_CELLS - 1) {
            decodeOneSample();
            decoder_index++;
        }
    }

    inline void incrementPhase()
    {
        phase_fractional += phase_increment_fractional;
    }

    volatile uint64_t phase_fractional;
    volatile uint64_t phase_increment_fractional;
    const int16_t* initial;
    const uint8_t* initial_step;
    const uint8_t* data;
    bool looping;
    uint64_t startpos_fractional, endpos_fractional;

    // ADPCM decoder state
    unsigned int decoder_index;
    int16_t current_decoded;
    uint8_t decoder_step_index;
};

#endif /* ADPCMSAMPLE_H_ */