#include "math.h"
#include "tone.h"
#include <stdbool.h>
#include <string.h>
#include "fsl_debug_console.h"

#define MAX_CHANNELS (8)
#define MAX_RATE (48000)
#define BYTES_PER_SAMPLE (4)

#define SAMPLE_LENGTH_MS (8)

#define MAX_SAMPLE_SIZE (MAX_CHANNELS * (MAX_RATE) / 1000 * SAMPLE_LENGTH_MS)
#define MAX_SAMPLE_SIZE_BYTES (MAX_SAMPLE_SIZE * BYTES_PER_SAMPLE)

#define NUM_BUFFERS (2)

static int32_t sample_buffer[MAX_SAMPLE_SIZE * NUM_BUFFERS] = {0};
static uint16_t current_frame_size = 0;
static uint8_t current_buffer = 0;
static uint32_t current_sample_rate = 48000;

// For giggles, just allow a max rate of 48 kHz and a max tone frequency of 16 kHz.  Nothing here is
// bomb proof.
static int32_t wave[48*16] = {0};
static uint16_t wave_position = 0;
static uint16_t wave_size = 0;

static bool increasing = true;

void initialize_wave(uint32_t sample_rate, uint16_t tone_frequency, double amplitude, data_length_t bitness)
{
    current_buffer = 0;
    wave_size =  (uint16_t)(sample_rate / tone_frequency);
    current_sample_rate = sample_rate;

    // 4 bytes per sample
    memset(wave, 0, 48*16*4);

    // 4 bytes per sample
    memset(sample_buffer, 0, MAX_SAMPLE_SIZE * NUM_BUFFERS * 4);

    // Frame size is in 32-bit chunks
    current_frame_size = sample_rate / 1000 * SAMPLE_LENGTH_MS * MAX_CHANNELS;
    PRINTF("Current frame size set to %d\r\n", current_frame_size);

    PRINTF("WAVE:\r\n-------------\r\n");
    for (int i = 0; i < wave_size; i++)
    {
        wave[i] = (int32_t)(amplitude * sin(2 * M_PI * i / sample_rate * tone_frequency));
        PRINTF("%d. 0x%08X\r\n", i, wave[i]);

    }
}

int32_t *populate_buffer(uint16_t *num_bytes_xfer)
{
    int32_t *data = &sample_buffer[MAX_SAMPLE_SIZE * current_buffer];

    *num_bytes_xfer = current_frame_size;

    current_buffer = ++current_buffer >= NUM_BUFFERS ? 0 : current_buffer;
    // PRINTF("Populating current buffer %d \r\n", current_buffer);

    /* Going to weave in sine data on the first two channels (L-R of
     * headphone jack).  So, take the correct buffer, line it up and fill
     * it.
     */
    for (uint16_t i = 0, next_step = 0; i*8 < current_frame_size; i++)
    {
        next_step = i * 8;
        *(data + next_step++) = wave[wave_position];
        *(data + next_step) = wave[wave_position];
        if (increasing)
        {
            if (++wave_position >= wave_size)
            {
                wave_position = wave_position - 2 ;
                increasing = false;
            }
        }
        else
        {
            if (wave_position == 0)
            {
                increasing = true;
                ++wave_position;
            }
            else
            {
                --wave_position;
            }
        }
        if (current_frame_size != *num_bytes_xfer)
        {
            PRINTF("Something went bad...");
        }
    }
    return data;

}

void print_buffer_stats()
{
    uint8_t buffer = current_buffer > 0 ? current_buffer-1 : NUM_BUFFERS-1;
    PRINTF("Current buffer: %d\r\nSampling rate: %d Hz\r\nFrame size: %d\r\n", buffer, current_sample_rate, current_frame_size);

    if (current_frame_size != 0)
    {
        int32_t *data = &sample_buffer[MAX_SAMPLE_SIZE * buffer];

        PRINTF("Current Frame:\r\n------------------\r\n");
        for (uint16_t i = 0; i < current_frame_size; i++)
        {
            PRINTF("%d ", *(data +i));
            if (!(i % 5))
            {
                PRINTF("\r\n");
            }

        }
    }
}

